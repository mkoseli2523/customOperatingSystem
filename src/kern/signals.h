// signals.h

#ifndef _SIGNALS_H_
#define _SIGNALS_H_

#define NSIG        10   // # of signals defined, 1 larger the largest signal number defined

// define names of signals
// each signal is assigned a positive integer 
#define SIGTERM     1
#define SIGKILL     2
#define SIGINT      3
#define SIGALRM     4
#define SIGSTOP     5
#define SIGCONT     6
#define SIGPIPE     7
#define SIGUSR1     8
#define SIGUSR2     9

// define error codes 
#define ERRSIGINVAL     -1

// extern function declerations
extern void signal_deliver();
extern void signal_handle(int sig);

#endif
