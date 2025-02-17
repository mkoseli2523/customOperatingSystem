// scnum.h - System call numbers
//

#ifndef _SCNUM_H_
#define _SCNUM_H_

#define SYSCALL_EXIT    0
#define SYSCALL_MSGOUT  1

#define SYSCALL_DEVOPEN 10
#define SYSCALL_FSOPEN  11

#define SYSCALL_CLOSE   20
#define SYSCALL_READ    21
#define SYSCALL_WRITE   22
#define SYSCALL_IOCTL   23

#define SYSCALL_EXEC    30
#define SYSCALL_FORK    31

#define SYSCALL_USLEEP  40
#define SYSCALL_WAIT    41

// #define SYSCALL_KILL    42
#define SYSCALL_PROGNAMES   43
#define SYSCALL_NUMPROGS    44
#define SYSCALL_PROCS       45
#define SYSCALL_SIGNAL      46


#endif // _SCNUM_H_
