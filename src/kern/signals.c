// signals.c

#include "process.h"



/**
 * this function is used to deliver a signal to a process
 * 
 * called on way back to returning from a syscall
 */
void signal_deliver() {
    struct process * p = current_process();
    unsigned int unmasked = p->pending_signals & ~p->blocked_signals;
    if (!unmasked) return;

    for (int sig = 1; sig < NSIG; sig++) {
        unsigned int mask = (1 << sig);
        if (unmasked & mask) {
            p->pending_signals &= ~mask;
            signal_handle(sig);
            break;
        }
    }
}



/**
 * signal handler, this function can either be used to handle a given signal 
 * or set the handler for a given signal
 * 
 * this function retrieves the signal handler for a given signal within a given process
 * if handler is NULL, ie = 0, default action
 * if handler is negative, ignore
 * else it's a custom handler
 * 
 * @param   sig     type of signal to deliver
 */

void signal_handle(int sig) {
    struct process * p = current_process();
    void (*handler)(int) = p->signal_handler[sig];
    if (handler == NULL) {
        // default action
        if (sig == SIGTERM) {
            process_exit();
        }

        // ---------------------------
        // handle other signals
        // ---------------------------
    } else if ((uintptr_t)handler == (uintptr_t)-1) {
        // ignore
    } else {
        // set the handler to custom handler
        // setup_signal_handler(sig, handler);
    }
}




