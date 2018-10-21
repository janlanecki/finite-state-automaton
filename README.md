# Finite-state Automaton

## Three complementary programs `Tester`, `Validator`, and `Run` simulating specified automatons.
#### Validator reads in a configuration of an automaton and starts working as a server that receives words to validate from multiple Testers and starts a Run program for each word. For each Tester program Validator keeps count of the words it received, sent to Runs and how many of them were accepted by the automaton. It sends this information to a Tester upon request.
#### Tester reads new words from the standard input and sends them for validation until EOF is read.
#### Run actually performs the automaton simulation and returns if the word is accepted or not to the Validator.

#### Features:
* two processes in Tester exchange information by fifo message queue
* fifo message queue in Validator for messages from Tester and Run programs
* new process in Validator for each Run instance
* Run forks as the underlying automaton splits into alternative runs over the world, children send results to the first Run process