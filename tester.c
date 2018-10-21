#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/prctl.h>
#include "helper.h"

#define CHAR_ACC 'A'
#define CHAR_NOT_ACC 'N'
#define PARENT_END_SIG SIGRTMAX
#define BONUS_QUERY_SIZE 60

mqd_t OUT_DESC;
const char* OUT_NAME;
mqd_t IN_DESC;
char IN_NAME[TESTER_NUM_LENGTH + 2]; /// '/' + tester_number + '\0'
int sent = 0, received = 0, accepted = 0;

void graceful_exit(const char *msg) {
  fprintf(stderr, "TESTER: %s (%d; %s)\n", msg, errno, strerror(errno));
  exit(1);
}

void graceful_exit_one_close(mqd_t desc, const char* q_name, const char* msg) {
  char err_msg[30];
  strcpy(err_msg, msg);
  if (mq_close(desc))
    strcpy(err_msg, "Error in mq_close");
  if (mq_unlink(q_name))
    graceful_exit("Error in mq_unlink");
  graceful_exit(msg);
}

void close_queues() { 
  char err_msg[30];
  bool error = false;
  if (mq_close(IN_DESC)) {
    strcpy(err_msg, "Error in mq_close");
    error = true;
  }
  if (!error && mq_unlink(IN_NAME)) {
    strcpy(err_msg, "Error in mq_unlink");
    error = true;
  }
  if (!error && mq_close(OUT_DESC)) {
    strcpy(err_msg, "Error in mq_close");
    error = true;
  }
  if (!error && mq_unlink(OUT_NAME)) {
    strcpy(err_msg, "Error in mq_unlink");
    error = true;
  }
  if (error)
    graceful_exit(err_msg);
}
  
void sig_handler() { // int sig w argumencie bo handler?
  close_queues();
  graceful_exit("Error in sender");
}
  
void define_signals() {
  sigset_t mask;
  sigemptyset(&mask);
  struct sigaction parent_sig_action;
  parent_sig_action.sa_handler = (*sig_handler);
  parent_sig_action.sa_mask = mask;
  parent_sig_action.sa_flags = SA_NOCLDWAIT;
  sigaction(PARENT_END_SIG, &parent_sig_action, NULL);  
  
  struct sigaction sigchld_action;
  sigchld_action.sa_handler = SIG_DFL;
  sigchld_action.sa_mask = mask;
  sigchld_action.sa_flags = SA_NOCLDWAIT;
  sigaction(SIGCHLD, &sigchld_action, NULL);
}
  
void failed_fork_handler(int *pipe_desc) {
  char err_msg[30] = "Error in fork";
  if (close(pipe_desc[1]) == -1)
    strcpy(err_msg, "Error in close(pipe_desc[1])");
  if (close(pipe_desc[0]) == -1)
    strcpy(err_msg, "Error in close(pipe_desc[0])");
  close_queues(0);
  graceful_exit(err_msg);
}

void send_queries(int write_desc) {
  /** reading from stdin and sending to validator */
  char *buf = NULL;
  size_t size = 0;
  ssize_t length;
  while ((length = getline(&buf, &size, stdin)) != EOF) {
    if (buf[0] == '!') {
      char msg[BONUS_QUERY_SIZE];
      sprintf(msg, "%c %d %s", END_MSG, getppid(), IN_NAME);
      if (mq_send(OUT_DESC, msg, BONUS_QUERY_SIZE, QUERY_PRIORITY) == -1) {
        kill(getppid(), PARENT_END_SIG);
        free(buf);
        graceful_exit("Error in mq_send");//error = true; error_msg = "Error in mq_send";
      }
      break;
    }
    else {
      /// wysyła komunikat postaci: 'nie_koniec numer_testera rozmiar_słowa słowo'
      if (length > 0 && buf[length - 1] == '\n') {
        buf[--length] = '\0';
      }
      char msg[BONUS_QUERY_SIZE + length];
      sprintf(msg, "%c %d %s %ld %s", NON_END_MSG, getpid(), IN_NAME, length, buf);
      if (mq_send(OUT_DESC, msg, BONUS_QUERY_SIZE + length, QUERY_PRIORITY) == -1) {
        kill(getppid(), PARENT_END_SIG);
        free(buf);
        graceful_exit("Error in mq_send");
      }
      sent++;
      
    }
  }
  if (length == EOF && buf[0] != '!') {
    char msg[BONUS_QUERY_SIZE];
    sprintf(msg, "%c %d %s", EOF_MSG, getppid(), IN_NAME);
    if (mq_send(OUT_DESC, msg, BONUS_QUERY_SIZE, QUERY_PRIORITY) == -1) {
      kill(getppid(), PARENT_END_SIG);
      free(buf);
      graceful_exit("Error in mq_send");
    }
  }
  free(buf);
}

bool receive_results(int read_desc, pid_t child_pid) {
  char buf[MAX_VALIDATOR_MSG_LEN] = {0};
  while (true) {
    if (mq_receive(read_desc, buf, MAX_VALIDATOR_MSG_LEN, NULL) == -1)
      return false;
    if (buf[0] == END_MSG)
      return true;
      
    received++;
    char word[MAX_LEN];
    bool accepted_or_not;
    sscanf("%s %d", word, &accepted_or_not);
    if (accepted_or_not)
      ++accepted;
    printf("%s %c\n", word, accepted_or_not ? CHAR_ACC : CHAR_NOT_ACC);
  }
  return true;
}

int main() {
  unsigned long long tester_number = atomic_fetch_add(&TESTER_COUNTER, 1);
  struct mq_attr results_q_attr;
  results_q_attr.mq_maxmsg = 10;
  results_q_attr.mq_msgsize = MAX_LEN + 2; /// word + ' ' + A|N + '\0'
  OUT_NAME = VALIDATOR_Q_NAME;
  
  if (tester_number < TESTER_COUNTER_INIT)
    graceful_exit("Wrong tester number given"); 
  fprintf(stderr, "Tester %lld started\n", tester_number); //
  
  sprintf(IN_NAME, "/%lld", tester_number);
  define_signals();
  
  /** opening queue for queries sent to validator */
  OUT_DESC = mq_open(VALIDATOR_Q_NAME, O_WRONLY);
  if (OUT_DESC == (mqd_t) -1)
    graceful_exit("Error in mq_open");
  fprintf(stderr, "Tester %lld opened validator queue\n", tester_number); //
  
  /** opening queue for results sent from validator */
  IN_DESC = mq_open(IN_NAME, O_CREAT | O_RDONLY, 00700, &results_q_attr);
  if(IN_DESC == (mqd_t) -1) {
    graceful_exit_one_close(OUT_DESC, OUT_NAME, "Error in mq_open");
  }
  fprintf(stderr, "Tester %lld opened results queue\n", tester_number); //
  
  pid_t child_pid;
  int pipe_desc[2];
  char pipe_read_desc_str[10];
  if (pipe(pipe_desc) == -1)
    graceful_exit("Error in pipe");
  switch (child_pid = fork()) {
    case -1:
      failed_fork_handler(pipe_desc);
      break;
    case 0:
      prctl(PR_SET_PDEATHSIG, SIGTERM);
      if (getppid() == 1) { /// rodzic usunięty przed prctl
        exit(0);
      }
      if (close(pipe_desc[0]) == -1)
        fprintf(stderr, "Error in close(pipe_desc[0])\n");
      send_queries(pipe_desc[1]);
      break;
  }
  if (close(pipe_desc[1]) == -1)
    fprintf(stderr, "Error in close(pipe_desc[1])\n");
  bool no_error = receive_results(pipe_desc[0], child_pid);
  if (!no_error) {
    close_queues(OUT_DESC, OUT_NAME, IN_DESC, IN_NAME);
    close(pipe_desc[0]);
    graceful_exit("Error in receiver");
  }
 
  close_queues(OUT_DESC, OUT_NAME, IN_DESC, IN_NAME);

  if (read(pipe_desc[0], &sent, sizeof(unsigned long long)) <= 0) {
    close(pipe_desc[0]);
    graceful_exit("Error in read");
  }
  
  printf("Snt: %d\nRcd: %d\nAcc: %d\n", sent, received, accepted);
  
  exit(0);
}
