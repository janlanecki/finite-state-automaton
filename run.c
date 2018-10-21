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
#include <sys/prctl.h>
#include "helper.h"

automaton_t AUT;
pid_t VALIDATOR_PID;
pid_t MAIN_RUN_PID;


void graceful_exit(const char *msg) {
  fprintf(stderr, "RUN: %s (%d; %s)\n", msg, errno, strerror(errno));
  exit(1);
}

void read_automaton_and_word(int read_desc, size_t size, char *word) {
  char err_msg[40];
  bool error = false;
  if (read(read_desc, (automaton_t *) &AUT, sizeof(automaton_t)) <= 0) { // automaton_s?
    strcpy(err_msg, "Error in read(automaton)");
    error = true;
  }
  if (read(read_desc, word, size) <= 0) {
    strcpy(err_msg, "Error in read(word)");
    error = true;
  }
  if (error) {
    close(read_desc);
    graceful_exit(err_msg);
  }
  word[size] = '\0';
}

void define_sigchld() {
  struct sigaction sigchld_action;
  sigset_t mask;
  sigemptyset(&mask);
  sigchld_action.sa_handler = SIG_DFL;
  sigchld_action.sa_mask = mask;
  sigchld_action.sa_flags = SA_NOCLDWAIT;
  sigaction(SIGCHLD, &sigchld_action, NULL);
}

void failed_fork_handler(int *pipe_desc) {
  char err_msg[40] = "Error in fork";
  if (close(pipe_desc[1]) == -1)
    strcpy(err_msg, "Error in close(pipe_desc[1])");
  if (close(pipe_desc[0]) == -1)
    strcpy(err_msg, "Error in close(pipe_desc[0])");
  graceful_exit(err_msg);
}

bool check(unsigned int pos, short int state, size_t size, char *word) {
  if (pos == size)
    return AUT.acc[state];
  if (AUT.trans_no[state][word[pos] - FIRST_LETTER] == 0)
    return state < AUT.uni_no;
  if (AUT.trans_no[state][word[pos] - FIRST_LETTER] == 1)
    return check(pos + 1, AUT.trans[state][word[pos] - FIRST_LETTER][0], size, word);
    
  int pipe_desc[2];
  if (pipe(pipe_desc) == -1)
    fprintf(stderr, "Error in pipe");
  for (short int i = 0; i < AUT.trans_no[state][word[pos] - FIRST_LETTER]; ++i) {
    switch (fork()) {
      case -1:
        failed_fork_handler(pipe_desc);
      case 0: 
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) { /// rodzic usunięty przed prctl
          exit(0);
        }
        close(pipe_desc[0]);
                                  
        bool result = check(pos + 1, AUT.trans[state][word[pos] - FIRST_LETTER][i],
                            size, word);
                            
        if (write(pipe_desc[1], &result, sizeof(bool)) != sizeof(bool)) {
          graceful_exit("Error in write");
        }
        close(pipe_desc[1]);
        break;
    }
  }
  close(pipe_desc[1]);
  for (short int i = 0; i < AUT.trans_no[state][word[pos] - FIRST_LETTER]; ++i) {
    int wstatus;
    wait(&wstatus);
    if (pos == 0 && WEXITSTATUS(wstatus) == -1) {
      graceful_exit("Error in run");
    }
    bool accepted;
    if (read(pipe_desc[0], &accepted, sizeof(bool)) <= 0) {
      close(pipe_desc[0]);
      graceful_exit("Error in read");
    }
    close(pipe_desc[0]);
      
    if (state < AUT.uni_no) {
      if (!accepted)
        return false;
    }
    else if (accepted) {
      return true;
    }
  }
  return state < AUT.uni_no;
}

void send_result(bool result) { // tu error -> send signal osobno od graceful
  mqd_t out_desc = mq_open(VALIDATOR_Q_NAME, O_WRONLY);
  if (out_desc == (mqd_t) -1) {
    graceful_exit("Error in mq_open");
  }
  fprintf(stderr, "Run %d opened validator queue\n", getpid()); //
  char msg[2];
  sprintf(msg, "%d", result);
  if (mq_send(out_desc, msg, 2, QUERY_PRIORITY) == -1) {
    graceful_exit("Error in mq_send");
  }
  if (mq_close(out_desc)) {
    graceful_exit("Error in close");
  }
}

int main(int argc, char *argv[]) {
  prctl(PR_SET_PDEATHSIG, SIGTERM);
  if (getppid() == 1) { /// rodzic usunięty przed prctl
    exit(0);
  }
  //read_desc_str, itoa(getpid()), itoa(pid), itoa(size)
  int read_desc = atoi(argv[1]);
  VALIDATOR_PID = (pid_t) atoi(argv[2]);
  pid_t tester_pid = (pid_t) atoi(argv[3]);
  size_t size = (size_t) atol(argv[4]);
  char err_msg[30], word[size + 1]; /// word + '\0'
  
  define_sigchld();
  read_automaton_and_word(read_desc, size, word);
  
  bool result = check(0, AUT.bgn, size, word);
  send_result(result);

  exit(0);
}
