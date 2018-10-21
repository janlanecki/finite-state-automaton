#ifndef AUTOMATA_HELPER_H
#define AUTOMATA_HELPER_H

#include <stdatomic.h>
#include <stdbool.h>
// #include <mqueue.h>

#define MAX_LEN 1000
#define MAX_VALIDATOR_MSG_LEN 1060
#define BONUS_QUERY_SIZE 60
#define RESULT_PRIORITY 2
#define QUERY_PRIORITY 3
#define END_PRIORITY 4
#define TESTER_NUM_LENGTH 20
#define TESTER_COUNTER_INIT (10000000000000000000ULL) /// 10^19
#define FIRST_LETTER ((int) 'a')
#define MAX_STATES 100 /// should be lesser than max short int
#define MAX_LETTERS 26

typedef enum code_e {END_MSG, NON_END_MSG, EOF_MSG, RESULT_MSG} code; /// up to 255 codes

typedef struct automaton_s {
  short int ltr_no; ///< rozmiar alfabetu: alfabet to zbiór {a,...,x}, gdzie 'x'-'a' = A-1;
  short int sts_no; ///< liczba stanów: stany to zbiór {0,...,Q-1};
  short int uni_no; ///< liczba stanów uniwersalnych: {0, .., U-1}
  short int acc_no; ///< liczba stanów akceptujących
  short int bgn; ///< stan początkowy
  bool acc[MAX_STATES]; ///< tablica stanów akceptujących
  short int trans_no[MAX_STATES][MAX_LETTERS]; ///< liczba stanów na które przechodzi
  short int trans[MAX_STATES][MAX_LETTERS][MAX_STATES]; ///< stanu i litery w tablicę stanów
} automaton_t;

extern const char* VALIDATOR_Q_NAME;
extern atomic_ullong TESTER_COUNTER;

#endif /* AUTOMATA_HELPER_H */
