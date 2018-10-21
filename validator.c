#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include "helper.h"

#define NON_TRANSITION_LINES 3
#define EXEC_ARGUMENT_SIZE 50

typedef struct tester_s {
  char q_name[TESTER_NUM_LENGTH + 2]; /// '/' + TESTER_NUM_LENGTH + '\0'
  pid_t pid;
  int acc;
  int rcd;
  struct tester_s *next;
} tester_t;

// typedef struct automaton_s {
  // short int ltr_no; ///< rozmiar alfabetu: alfabet to zbiór {a,...,x}, gdzie 'x'-'a' = A-1;
  // short int sts_no; ///< liczba stanów: stany to zbiór {0,...,Q-1};
  // short int uni_no; ///< liczba stanów uniwersalnych: {0, .., U-1}
  // short int acc_no; ///< liczba stanów akceptujących
  // short int bgn; ///< stan początkowy
  // bool acc[MAX_STATES]; ///< tablica stanów akceptujących
  // bool trans[MAX_STATES][MAX_LETTERS][MAX_STATES];///< stanu i litery w tablicę stanów
// } automaton_t;

tester_t *LIST = NULL, *LIST_END = NULL;
automaton_t AUT;
mqd_t IN_DESC;
char *buf;
int received = 0, sent = 0, accepted = 0, runs = 0;


void graceful_exit(const char *msg) {
  fprintf(stderr, "VALIDATOR: %s (%d; %s)\n", msg, errno, strerror(errno));
  exit(1);
}

void graceful_exit_one_close(mqd_t desc, const char* q_name, const char* msg) {
  char err_msg[30];
  strcpy(err_msg, msg);
  if (mq_close(desc))
    strcpy(err_msg, "Error in mq_close");
  if (mq_unlink(q_name))
    graceful_exit("Error in mq_unlink");
  graceful_exit(err_msg);
}

void graceful_exit_two_closes(mqd_t desc1, const char* q_name1,
                              mqd_t desc2, const char* q_name2, const char* msg) {
  char err_msg[30];
  strcpy(err_msg, msg);
  if (mq_close(desc1))
    strcpy(err_msg, "Error in mq_close");
  if (mq_unlink(q_name1))
    graceful_exit_one_close(desc2, q_name2, "Error in mq_unlink");
  graceful_exit_one_close(desc2, q_name2, err_msg);
}

bool list_empty() {
  return LIST == NULL;
}

void list_add(tester_t *t) {
  if (list_empty()) {
    LIST = malloc(sizeof(tester_t));
    LIST_END = LIST;
  }
  else {
    LIST_END->next = malloc(sizeof(tester_t));
    LIST_END = LIST_END->next;
  }
  *LIST_END = (tester_t) { .pid = t->pid, .acc = t->acc, .rcd = t->rcd, .next = NULL };
  strcpy(LIST_END->q_name, t->q_name);
}

tester_t* list_find(pid_t pid) {
  tester_t *ptr = LIST;
  while (ptr != NULL) {
    if (ptr->pid == pid)
      return ptr;
    ptr = ptr->next;
  }
  return NULL;
}

bool list_delete(pid_t pid) {
  tester_t *ptr = LIST, *prev = NULL;
  while (ptr != NULL) {
    if (ptr->pid == pid) {
      if (ptr == LIST_END) 
        LIST_END = prev;
      if (prev != NULL)
        prev->next = ptr->next;
      else
        LIST = ptr->next;
      free(ptr);
      return true;
    }
    prev = ptr;
    ptr = ptr->next;
  }
  return false;
}

void send_to_tester(char *q_name, char *msg, size_t size) {
  mqd_t out_desc = mq_open(q_name, O_WRONLY);
  if(out_desc == (mqd_t) -1) {
    graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME, "Error in mq_open(tester q)");
  }
  fprintf(stderr, "Validator opened tester %s queue\n", q_name); //
  
  if (mq_send(out_desc, msg, size, QUERY_PRIORITY) == -1) {
    graceful_exit_two_closes(IN_DESC, VALIDATOR_Q_NAME, 
                             out_desc, q_name, "Error in mq_send(result)");
  }
  fprintf(stderr, "Validator sent result to tester %s queue\n", q_name); //
  
  if (mq_close(out_desc))
    graceful_exit("Error in close");
  
  if (msg[0] == RESULT_MSG)
    sent++;
}

void close_validator_queue() {
  if (mq_close(IN_DESC))
    graceful_exit("Error in close");
  if (mq_unlink(VALIDATOR_Q_NAME))
    graceful_exit("Error in unlink");
}

void send_end_delete_testers_maybe_print(bool print) {
  char msg[2] = {END_MSG, '\0'};
  size_t size = 2;
  if (print && LIST->rcd > 0)
    fprintf(stdout, "PID: %d\n", (int) LIST->pid);
  while (LIST != NULL) {
    send_to_tester(LIST->q_name, msg, size);
    if (print)
      fprintf(stdout, "Rcd: %d\nAcc: %d\n", LIST->rcd, LIST->acc);
    list_delete(LIST->pid);
  }
}

/// zakończenie przy wywalonym runie
void exit_sequence_w_error(char *err_msg) {
  //pętla wysyłająca wszysktim tsterom z listy END_MSG
  send_end_delete_testers_maybe_print(false);
  // zamknięcie kolejki walidatroa
  close_validator_queue();
  // exit(1)
  graceful_exit(err_msg);
}

void process_result(char *);

void drain_results() {
  // receive, run-- lub sent to tester
  char msg[MAX_VALIDATOR_MSG_LEN];
  while (runs > 0) {
    if (mq_receive(IN_DESC, msg, MAX_VALIDATOR_MSG_LEN, NULL) == -1)
      exit_sequence_w_error("Error in receive");
      
    if (buf[0] == RESULT_MSG) {
      process_result(buf);
    }
    else {
      tester_t t = {0};
      code temp;
      sscanf(buf, "%c %d %s", (char *) &temp, &t.pid, t.q_name);
      if (list_find(t.pid) != NULL)
        list_add(&t);
    }
  }
}

void exit_sequence() { /// dostałem wykrzyknik
  // wyciągam wyniki z kolejki tak długo aż zmienna runs się wyzeruje
  // dodaję nowe testery z komunikatów do listy
  // gdy trafiam na wynik, posyłam dalej
  drain_results();
  fprintf(stdout, "Rcd: %d\nSnt: %d\nAcc: %d\n", received, sent, accepted);
  // po pętli, wszystkim testerom z listy wysyłam END przy okazji wypisuję staty i ich usuwam
  send_end_delete_testers_maybe_print(true);
  // zamknięcie kolejki walidatora
  close_validator_queue();
}

void sigchld_handler() {
  int wstatus;
  wait(&wstatus);
  if (WEXITSTATUS(wstatus) != 0) {
    exit_sequence_w_error("Error in run");
  }
}

void redefine_signals() {
  struct sigaction sigchld_action;
  sigset_t mask;
  sigemptyset(&mask);
  sigchld_action.sa_handler = *sigchld_handler; // tu handler robiący wait($wstatus) i sprawdzający exit
  sigchld_action.sa_mask = mask;
  sigchld_action.sa_flags = SA_RESTART | SA_NOCLDWAIT | SA_NODEFER;
  sigaction(SIGCHLD, &sigchld_action, NULL);
}

void init_automaton() {
  int lines;
  char *buf = NULL;
  size_t size = 0;
  memset(AUT.acc, 0, MAX_STATES * sizeof(bool)); // AUT.acc = {0}; 
  memset(AUT.trans, -1, MAX_STATES * MAX_STATES * MAX_LETTERS * sizeof(short int)); // AUT.trans = {-1};
  memset(AUT.trans_no, 0, MAX_STATES * MAX_LETTERS * sizeof(short int)); // AUT.trans_no = {0};
  
  getline(&buf, &size, stdin);
  sscanf(buf, "%d %hd %hd %hd %hd", &lines, &AUT.ltr_no, &AUT.sts_no,
                                    &AUT.uni_no, &AUT.acc_no);
  
  getline(&buf, &size, stdin);
  sscanf(buf, "%hd", &AUT.bgn);
  
  getline(&buf, &size, stdin);
  char *token = strtok(buf, " \n");
  int idx = 0;
  while (token != NULL) {
    AUT.acc[atoi(token)] = true; 
    token = strtok(NULL, " \n");
  }
  
  for (int i = 0; i < lines - NON_TRANSITION_LINES; ++i) {
    idx = 0;
    getline(&buf, &size, stdin);
    char *token;
    short int from = atoi(strtok(buf, " "));
    char letter = strtok(NULL, " ")[0];
    token = strtok(NULL, " \n");
    while (token != NULL) {
      AUT.trans[from][letter - FIRST_LETTER][idx++] = atoi(token);
      token = strtok(NULL, " \n");
    }
    AUT.trans_no[from][letter - FIRST_LETTER] = idx;
  }
  
  free(buf);
}

void print_beginning_of_automaton(automaton_t a) {
  printf("%hd %hd %hd %hd\n", a.ltr_no, a.sts_no, a.uni_no, a.acc_no);
  printf("%hd\n", a.bgn);
  for (int i = 0; i < 10; ++i)
    printf("%d ", a.acc[i]);
  printf("\n");
  for (int so = 0; so < 2; ++so) {
    printf("stan %d: ", so);
    for (int l = 0; l < 2; ++l) {
      printf("%c: w stany ", l + FIRST_LETTER);
      for (int si = 0; si < 2; ++si) {
        printf("%d ", a.trans[so][l][si]);
      }
    }
    printf("\n");
  }
}

void open_validator_queue() {
  struct mq_attr validator_q_attr;
  validator_q_attr.mq_maxmsg = 10;
  validator_q_attr.mq_msgsize = MAX_VALIDATOR_MSG_LEN; 
  
  IN_DESC = mq_open(VALIDATOR_Q_NAME, O_CREAT | O_RDWR, 00700, &validator_q_attr);
  if (IN_DESC == (mqd_t) -1)
    graceful_exit("Error in mq_open");
  fprintf(stderr, "Validator opened validator queue\n"); //
  
  // chyba nieprzydatne /** checking if sufficient message size */
  if (mq_getattr(IN_DESC, &validator_q_attr) == -1)
    graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME, "Error in mq_getattr");
  if (validator_q_attr.mq_msgsize < MAX_VALIDATOR_MSG_LEN)
    graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME, "Error because of "
                                                        "insufficient queue size");
}

void failed_fork_handler(int *pipe_desc) {
  char err_msg[30] = "Error in fork";
  if (close(pipe_desc[1]) == -1)
    strcpy(err_msg, "Error in close(pipe_desc[1])");
  if (close(pipe_desc[0]) == -1)
    strcpy(err_msg, "Error in close(pipe_desc[0])");
  graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME, err_msg);
}

void spawn_run(int read_desc, pid_t tester_pid, size_t size) {
  char args[EXEC_ARGUMENT_SIZE];
  sprintf(args, "%d %d %d %ld", read_desc, getppid(), tester_pid, size); //itoa?
  execl("./run.c", "run", args, NULL);
  /// execl error handling
  if (close(read_desc) == -1)
    graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME,
                            "Error in close(pipe_desc[0])");
  graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME, "Error in execl");
}

void process_word(pid_t tester_pid, size_t size, const char *word) {
  // open pipe
  // child: fork, exec ze słowem, rozmiarem, pidem
  // definicja handlera błędu od run
  int pipe_desc[2];
  if (pipe(pipe_desc) == -1)
    graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME, "Error in pipe");
  
  runs++;
  
  switch (fork()) {
    case -1:
      failed_fork_handler(pipe_desc);
      break;
    case 0: 
      if (close(pipe_desc[1]) == -1)
        graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME,
                                "Error in close(pipe_desc[1])");
      spawn_run(pipe_desc[0], tester_pid, size);
      break;
    default: // czy sizeof(AUT) == sizeof((char *) AUT)
      if (close(pipe_desc[0]) == -1)
        graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME,
                                "Error in close(pipe_desc[0])");
      bool error = false; char *err_msg;
      if (write(pipe_desc[1], (char *) &AUT, sizeof(automaton_t)) != sizeof(automaton_t)) {
        err_msg = "Error in write(automaton)";
        error = true;
      }
      // czy sizeof(word) == size
      ssize_t length = (ssize_t) size;
      if (!error && write(pipe_desc[1], word, size) != length) {
        err_msg = "Error in write(word)";
        error = true; 
      }
      if (error) {
        if (close(pipe_desc[1]) == -1)
          graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME,
                                  "Error in close(pipe_desc[0])");
        graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME, err_msg);
      }
  }
}

void process_query(char *buf) {
  tester_t t = {0};
  code c;
  size_t size;
  int offset;
  sscanf(buf, "%c%n %d%n %s%n %lu%n", (char *) &c, &offset, &t.pid, &offset, t.q_name, 
                                     &offset, &size, &offset);
  char word[size];
  sscanf(buf + offset, " %s", word); // offset + 1, bo spacja? chyba nie
  tester_t *old = list_find(t.pid);
  t.rcd = 1;
  if (old == NULL)
    list_add(&t);
  else
    old->rcd++;
  received++;
  process_word(t.pid, size, word);
}

void process_result(char *buf) {
  // kod pid czy_akcept słowo
  code c;
  pid_t tester_pid;
  bool accept = false;
  size_t size;
  char word[MAX_LEN];
  runs--;
  
  sscanf(buf, "%c %d %d %lu %s", (char *) &c, (pid_t *) &tester_pid, (int *) &accepted, &size, word);
  fprintf(stderr, "Validator ~390: code = %d\n", c); //
  tester_t *requester = list_find(tester_pid);
  if (accept) {
    requester->acc++;
    accepted++;
  }
  
  char msg[size + 3];
  sprintf(msg, "%s %d", word, accept);
  send_to_tester(requester->q_name, msg, size + 3);
}

void process_eof(char *buf) {
  char end_msg[2] = {END_MSG, '\0'};
  char q_name[TESTER_NUM_LENGTH + 2];
  code temp;
  pid_t pid;
  
  sscanf(buf, "%c %d %s", (char *) &temp, &pid, q_name);
  list_delete(pid); // USUWANIE TESTERA BŁĄD
  
  send_to_tester(q_name, end_msg, 2);
}

void work() {
  char buf[MAX_VALIDATOR_MSG_LEN];
  bool end = false;
  while (!end) {
    // read query, from run -> upd stats, send to tstr,
    if (mq_receive(IN_DESC, buf, MAX_VALIDATOR_MSG_LEN, NULL) == -1) {
      graceful_exit_one_close(IN_DESC, VALIDATOR_Q_NAME, "Error in receive");
    }
    switch (buf[0]) {
      case END_MSG: {// przetworz wiadomosc, dodaj testera do listy
        tester_t t = {0};
        code temp;
        sscanf(buf, "%c %d %s", (char *) &temp, &t.pid, t.q_name);
        if (list_find(t.pid) != NULL)
          list_add(&t);
        exit_sequence();
        end = true;
        break;
      }
      case RESULT_MSG:
        process_result(buf);
        break;
      case NON_END_MSG:
        process_query(buf);
        break;
      case EOF_MSG:
        process_eof(buf);
        break;
    }
  }
}

int main() {
  redefine_signals();
  open_validator_queue();
  init_automaton();
  work();

  exit(0);
}
