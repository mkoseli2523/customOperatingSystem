// lock.h - A sleep lock
//

#ifdef LOCK_TRACE
#define TRACE
#endif

#ifdef LOCK_DEBUG
#define DEBUG
#endif

#ifndef _LOCK_H_
#define _LOCK_H_

#include "thread.h"
#include "halt.h"
#include "console.h"
#include "intr.h"

struct lock {
    struct condition cond;
    int tid; // thread holding lock or -1
};

static inline void lock_init(struct lock * lk, const char * name);
static inline void lock_acquire(struct lock * lk);
static inline void lock_release(struct lock * lk);

// INLINE FUNCTION DEFINITIONS
//

static inline void lock_init(struct lock * lk, const char * name) {
    trace("%s(<%s:%p>", __func__, name, lk);
    condition_init(&lk->cond, name);
    lk->tid = -1;
}

/**
 * lock_acquire - Acquires a lock, ensuring mutual exclusion.
 *
 * This function attempts to acquire the given lock. If the lock is already held,
 * the calling thread is put to sleep until the lock becomes available.
 *
 * @param lk: Pointer to the lock to acquire.
 */

static inline void lock_acquire(struct lock * lk) {
    trace("%s(<%s:%p>)", __func__, lk->cond.name, lk);

    while(1){
        int intr_state = intr_disable();

        if(lk->tid == -1) {
            lk->tid = running_thread();
            intr_restore(intr_state);
            debug("Thread <%s:%d> acquired lock <%s:%p>", 
                thread_name(running_thread()), running_thread(),
                lk->cond.name, lk);
            return;
        }

        // Wait if lock is held
        condition_wait(&lk->cond);
    }
}

static inline void lock_release(struct lock * lk) {
    trace("%s(<%s:%p>", __func__, lk->cond.name, lk);

    assert (lk->tid == running_thread());
    
    lk->tid = -1;
    condition_broadcast(&lk->cond);

    debug("Thread <%s:%d> released lock <%s:%p>",
        thread_name(running_thread()), running_thread(),
        lk->cond.name, lk);
}

#endif // _LOCK_H_