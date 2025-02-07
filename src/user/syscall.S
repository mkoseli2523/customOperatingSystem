#include "scnum.h"

        .text

        .global _exit
        .type   _exit, @function
_exit:
        li      a7, SYSCALL_EXIT
        ecall
        ret

        .global _msgout
        .type   _msgout, @function
_msgout:
        li      a7, SYSCALL_MSGOUT
        ecall
        ret

        .global _devopen
        .type   _devopen, @function
_devopen:
        li      a7, SYSCALL_DEVOPEN
        ecall
        ret

        .global _fsopen
        .type   _fsopen, @function
_fsopen:
        li      a7, SYSCALL_FSOPEN
        ecall
        ret

        .global _close
        .type   _close, @function
_close:
        li      a7, SYSCALL_CLOSE
        ecall
        ret

        .global _read
        .type   _read, @function
_read:
        li      a7, SYSCALL_READ
        ecall
        ret

        .global _write
        .type   _write, @function
_write:
        li      a7, SYSCALL_WRITE
        ecall
        ret

        .global _ioctl
        .type   _ioctl, @function
_ioctl:
        li      a7, SYSCALL_IOCTL
        ecall
        ret

        .global _exec
        .type   _exec, @function
_exec:
        li      a7, SYSCALL_EXEC
        ecall
        ret

        .global _fork
        .type   _fork, @function
_fork:
        li      a7, SYSCALL_FORK
        ecall
        ret

        .global _wait
        .type   _wait, @function
_wait:
        li      a7, SYSCALL_WAIT
        ecall
        ret

        .global _usleep
        .type   _usleep, @function
_usleep:
        li      a7, SYSCALL_USLEEP
        ecall
        ret

#         .global _kill
#         .type   _kill, @function
# _kill:
#         li      a7, SYSCALL_KILL
#         ecall
#         ret

        .global _getnumprogs
        .type   _getnumprogs, @function
_getnumprogs:
        li      a7, SYSCALL_NUMPROGS
        ecall
        ret

        .global _getprognames
        .type   _getprognames, @function
_getprognames:
        li      a7, SYSCALL_PROGNAMES
        ecall
        ret

        .global _getprocs
        .type   _getprocs, @function
_getprocs:
        li      a7, SYSCALL_PROCS
        ecall
        ret

        .global _signal
        .type   _signal, @function
_signal:
        li      a7, SYSCALL_SIGNAL
        ecall
        ret

        .end