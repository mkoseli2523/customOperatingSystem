// process.h - User process
//

#ifndef _PROCESS_H_
#define _PROCESS_H_

// NPROC is the maximum number of processes

#ifndef NPROC
#define NPROC 16
#endif

#ifndef PROCESS_IOMAX
#define PROCESS_IOMAX 16
#endif

#include "config.h"
#include "io.h"
#include "thread.h"
#include "memory.h"
#include "elf.h"
#include "console.h"
#include "halt.h"
#include "signals.h"
#include <stdint.h>
#include <string.h>

// EXPORTED TYPE DEFINITIONS
//

struct process {
    int id; // process id of this process
    int tid; // thread id of associated thread
    uintptr_t mtag; // memory space identifier
    struct io_intf * iotab[PROCESS_IOMAX];

    // -------------------------
    // SIGNAL-RELATED FIELDS
    // -------------------------
    uint64_t pending_signals;
    uint64_t blocked_signals;
    void (*signal_handler[NSIG])(int);
};

// EXPORTED VARIABLES DECLARATIONS
//

extern char procmgr_initialized;
extern struct process * proctab[];

// EXPORTED FUNCTION DECLARATIONS
//

extern void procmgr_init(void);
extern int process_exec(struct io_intf * exeio);

extern void __attribute__ ((noreturn)) process_exit(void);

extern void process_terminate(int pid);

static inline struct process * current_process(void);
static inline int current_pid(void);

extern struct process * find_process_by_pid(int pid);

// these functions are defined in thrams.s
extern void __attribute__ ((noreturn)) _thread_finish_jump (
        struct thread_stack_anchor * stack_anchor,
        uintptr_t usp, uintptr_t upc, ...);

// INLINE FUNCTION DEFINITIONS
// 

static inline struct process * current_process(void) {
    return thread_process(running_thread());
}

static inline int current_pid(void) {
    return thread_process(running_thread())->id;
}

#endif // _PROCESS_H_
