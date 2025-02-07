/**
 * syscall.c - kernel-level syscalls for user programs
 */

#include "../user/scnum.h"
#include "../user/syscall.h"
#include "console.h"
#include "memory.h"
#include "process.h"
#include "fs.h"
#include "trap.h"
#include "device.h"
#include "io.h"
#include "timer.h"
#include "heap.h"
#include "kfs.h"

/**
 * sysexit - Exits the current process
 * 
 * This system call terminates the currently running process. 
 * Should not return as process is terminated upon successful
 * execution.
 * 
 * @param void      does not take in any parameters
 * 
 * @return          does not return under normal circumstances, as process is terminated
 */
static int sysexit(void){
    process_exit(); // Terminate current process
    return 0; // should never be reached
}

/**
 * sysmsgout - prints a message to the user terminal. 
 * 
 * This msystem call outputs a string to a user-facing I/O interface.
 * The mesage must be a null terminated string, and the pointer must 
 * refer to a valid, user-accessible memory region
 * 
 * @param msg       pointer to a null-terminated string to be printed
 * 
 * @return          returns 0 on success, or -1 if pointer is invalid
 */
static int sysmsgout(const char *msg){
    int result;

    trace("%s(msg=%p)", __func__, msg);

    // Validate the pointer to ensure it's valid user memory and null-terminated
    result = memory_validate_vstr(msg, PTE_U);
    if (result != 0) {
        return result;
    } 
    
    // Print message along w thread info
    kprintf("Thread <%s:%d> says: %s\n", thread_name(running_thread()), running_thread(), msg);

    return 0;
}

/**
 * sysdevopen - opens a device and associates with a file descriptor.
 * 
 * This system call opens a specified device by name and associates 
 * it with a file descriptor in the fd table. The device is identified by
 * its name and instance number, which is provided by the user program
 * 
 * @param fd        file descriptor to associate with the device.
 * @param name      Null-terminated string representing device name
 * @param instno    Instance number of the device to open.
 * 
 * @return          0 on success, -EMFILE(-10) if fd is out of range,
 *                  -EINVAL(-1) if device name is invalid or memory validation fails,
 *                  negative value from 'device_open' if device can't be opened
 */
static int sysdevopen(int fd, const char *name, int instno){
    if (fd < 0 || fd >= PROCESS_IOMAX){
        return -EMFILE; //FD out of range
    }
    // Attempt to open device name string 
    if (memory_validate_vstr(name, PTE_U) != 0){
        return -EINVAL; //invalid file name
    } 

    struct io_intf *dev_io = NULL;
    // Attempt to open the device 
    int result = device_open(&dev_io, name, instno);
    if(result < 0){
        return result; // return error code from device open 
    }

    current_process()->iotab[fd] = dev_io; // store the interface in the FD table
    
    return result;
}

/**
 * sysfsopen - opens a file in the fs and associates it with a fd
 * 
 * This syscall opens a file identified by its name an associates it with 
 * a file descriptor in the fd table. File is accessed through file system.
 * 
 * @param fd        File descriptor to associate with the opened file
 * @param name      Null-terminated string representing file name
 * 
 * @return          0 on success, -EMFILE if fd is out of range, 
 *                  -EINVAL if file name pointer is invalid or fails memory validation.
 *                  Negative value from fs_open if file cannot be opened
 */
static int sysfsopen(int fd, const char *name){
    if (fd < 0 || fd >= PROCESS_IOMAX){
        return -EMFILE; // Invalid file name
    }
    if(memory_validate_vstr(name, PTE_U | PTE_A) != 0){
        return -EINVAL; // Invalid file name
    }

    struct io_intf *fs_io = NULL;
    int result = fs_open(name, &fs_io);
    if(result < 0){
        return result;
    }

    current_process()->iotab[fd] = fs_io;

    return result;
}

/**
 * sysclose - close a file or device associated w a fd
 *
 * This function validates the file descriptor, retrieves the associated 
 * resource from the FD table, and calls the resource's "close" operation.
 * File descriptor is then cleared from the FD table to make room for reuse.
 *
 *  @param fd           The file descriptor to close
 * 
 *  @return             Returns 0 on success, -EBADFD if file descriptor is invalid or not in use
 * 
 */
static int sysclose(int fd){
    struct process *proc = current_process();

    if (fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] == NULL) {
        return -EBADFD; // invalid file descriptor
    }

    // proc->iotab[fd]->ops->close(proc->iotab[fd]); // Close the associated resource
    ioclose(proc->iotab[fd]);
    proc->iotab[fd] = NULL;                      // Remove the file descriptor from the table
    return 0;
}

/**
 * sysread - Reads data from a file descriptor.
 *
 * Validates the file descriptor and buffer, then reads up to `bufsz` bytes 
 * into `buf`.
 *
 * @param fd    File descriptor to read from.
 * @param buf   Buffer to store the data.
 * @param bufsz Maximum number of bytes to read.
 *
 * @return      Number of bytes read, or a negative error code.
 */
static long sysread(int fd, void *buf, size_t bufsz){
    struct process *proc = current_process();

    if (fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] == NULL){
        return -EBADFD; // invalid file descriptor
    }

    if (memory_validate_vptr_len(buf, bufsz, PTE_W | PTE_U) != 0){
        return -EINVAL; // invalid buf pointers
    }

    return proc->iotab[fd]->ops->read(proc->iotab[fd], buf, bufsz);
}


/**
 * syswrite - Writes data to a file descriptor.
 *
 * Validates the file descriptor and buffer, then writes up to `len` bytes 
 * from `buf`.
 *
 * @param fd    File descriptor to write to.
 * @param buf   Buffer containing the data to write.
 * @param len   Number of bytes to write.
 *
 * @return      Number of bytes written, or a negative error code.
 */
static long syswrite(int fd, const void *buf, size_t len){
    struct process *proc = current_process();

    if (fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] == NULL){
        return -EBADFD; // invalid file descriptor
    }

    if(memory_validate_vptr_len(buf, len, PTE_R | PTE_U) != 0){
        return -EINVAL; // invalid buf pointer
    }

    return proc->iotab[fd]->ops->write(proc->iotab[fd], buf, len);
}


/**
 * sysioctl - Sends a control command to a device or file.
 *
 * Validates the file descriptor and argument, then performs the requested
 * control operation.
 *
 * @param fd    File descriptor to operate on.
 * @param cmd   Command to execute.
 * @param arg   Argument for the command.
 *
 * @return      0 on success, or a negative error code.
 */
static int sysioctl(int fd, int cmd, void *arg){
    struct process *proc = current_process();

    if (fd < 0 || fd >= PROCESS_IOMAX || proc->iotab[fd] == NULL){
        return -EBADFD;
    }

    if (!arg) {     // check if arg is a valid pointer
        return -EINVAL;
    }

    switch (cmd) {
        case IOCTL_GETLEN:
        case IOCTL_GETPOS:
        case IOCTL_GETBLKSZ:
            // These commands only require read access to `arg`
            if (arg && (memory_validate_vptr_len(arg, sizeof(uint64_t), PTE_W | PTE_U) != 0)) {
                return -EINVAL; // Invalid or inaccessible `arg` pointer
            }
            break;

        case IOCTL_SETPOS:
            // This command requires both read and write access to `arg`
            if (arg && memory_validate_vptr_len(arg, sizeof(uint64_t), PTE_R | PTE_W | PTE_U) != 0) {
                return -EINVAL; // Invalid or inaccessible `arg` pointer
            }
            break;

        default:
            return -ENOTSUP; // Unsupported command
    }

    return proc->iotab[fd]->ops->ctl(proc->iotab[fd], cmd, arg);

}


/**
 * sysexec - Executes a program from a file descriptor.
 *
 * Validates the file descriptor and ensures it points to an executable. 
 * If valid, starts execution.
 *
 * @param fd    File descriptor pointing to the executable.
 *
 * @return      0 on success, or a negative error code.
 */
static int sysexec(int fd){ //assume process exec handles cleanup of fd table
    struct process* curr_process = current_process();

    // Validate file descriptor
    if (fd < 0 || fd >= PROCESS_IOMAX || curr_process->iotab[fd] == NULL) {
        return -EBADFD; // Invalid or unused file descriptor
    }

    struct io_intf* arg = curr_process->iotab[fd]; // get the argument into process_exec before clearing
    curr_process->iotab[fd] = NULL;

    // Execute the program
    return process_exec(arg);
}


/**
 * @brief Waits for a specific thread or any thread to finish execution.
 *
 * @param tid The ID of the thread to wait for, or 0 to wait for any thread.
 * @return 0 on success, or an error code if the operation fails.
 */
static int syswait(int tid){
    trace("%s(%d)", __func__, tid);

    // if no tid specified, wait for any thread to exist, otherwise wait for specified one
    if(tid == 0){
        return thread_join_any();
    } else {
        return thread_join(tid);
    }
}



/**
 * @brief Puts the calling thread to sleep for a specified amount of time in microseconds.
 *
 * @param us The duration of the sleep in microseconds. Must be greater than 0.
 * @return 0 on success, or -EINVAL if the input is invalid.
 */
static int sysusleep(unsigned long us){
    struct alarm al;

    // ensure time to sleep for is non zero
    if (us == 0){
        return -EINVAL;
    }

    // Initialize alarm
    alarm_init(&al, "sysusleep");

    // Convert microseconds to timer ticks
    uint64_t ticks = (us * TIMER_FREQ) / 1000000;

    // Sleep for the specified number of ticks
    alarm_sleep(&al, ticks);
    return 0;
}


/**
 * @brief Creates a new child process by forking the current process's state.
 *
 * @param tfr A pointer to the trap frame representing the current process's state.
 * @return The ID of the newly created child process to the parent, or -1 if the operation fails.
 */
static int sysfork(const struct trap_frame *tfr){
    //make a child process
    struct process *child_proc;
    for(int i = 0; i < 16; i++){ //16 is NPROC, the number of processes
        if(proctab[i] == NULL){
            child_proc = kmalloc(sizeof(struct process));
            proctab[i] = child_proc;
            break;
        }
    }
    // ensure child proc is not null and is properly allocated
    if(!child_proc){
        return -1;
    }
    // find the index into proctab array that child proc lives and set that as the tid for child
    struct process *current_proc = current_process();
    for(int i = 0; i < 16; i++){
        if(proctab[i] == child_proc){
            child_proc->id = i;
            break;
        }
    }
    child_proc->tid = -1; // Will be set by thread_fork_to_user
    child_proc->mtag = 0; // Will be set by memory_space_clone in thread_fork_to_user

    // copy over iotab array to child, incrementing refcnt if io_intf exists
    for(int j = 0; j < PROCESS_IOMAX; j++){
        if (current_proc->iotab[j]) 
            ioref(current_proc->iotab[j]);
            
        child_proc->iotab[j] = current_proc->iotab[j];
    }

    // call thread fork to user to finish forking
    int result = thread_fork_to_user(child_proc, tfr);

    // if it fails, set the memory assigned to child proc to 0 and decrement the refcnt
    if(result<0){
        memset(child_proc, 0, sizeof(struct process));
        proctab[child_proc->id] = NULL;

        //decrement refcnt
        for(int j = 0; j < PROCESS_IOMAX; j++){
            if (current_proc->iotab[j]) 
                ioclose(current_proc->iotab[j]);
        }
        return result;
    }
    
    //return child proc id to parent
    return child_proc->id;

}



/**
 * this 
 */

static int syssignal (int pid, int sig) {
    struct process *p = find_process_by_pid(pid);

    console_printf("syssignal called ");

    if (!p) return -EINVAL;

    console_printf("process id: %d\n", p->id);

    // check if its a catchable signal
    if (sig == SIGKILL) {
        process_terminate(pid);
        return;
    }

    uint64_t mask = 1 << sig;

    p->pending_signals |= mask;

    return 0;
}



/**
 * this function returns the currently running processes
 * 
 * @param pids      points to the place to copy the process ids to
 * @param names     points to the place to copy the process names to
 * 
 * @return          returns 0 on success, else returns a negative value
 */
static int sysrunningprocs(int * pids, char * names) {
    // iterate over the process table 
    // copy the pid of each running process into pids
    // copy the name of the thread into names field
    int num_written;

    for (int i = 0; i < NPROC; i++) {
        if (proctab[i]) {
            pids[i] = proctab[i]->id;

            char * name = get_thread_name(proctab[i]->tid);

            if (name == NULL) {
                continue;
            }

            names[FS_NAMELEN - 1] = '\0';

            strncpy(names, name, FS_NAMELEN);
            
            num_written++;
            names += FS_NAMELEN;
        } 
    } 

    return num_written;
}



/**
 * this function returns the number of user programs loaded into the kernel
 * 
 * this function simply checks the num_inodes field of the bootblock, useful for 
 * listing the runnable user programs in shell
 * 
 * @param arg       points to integer to return the value with
 * 
 * @return          returns 0 on success else a negative value
 */

static int sysnumprograms (void * arg) {
    if (!arg)   return -EINVAL;

    *(int*) arg = boot_block.num_inodes;
    return 0;
}



/**
 * this function returns the name of the user programs loaded into the kernel
 * 
 * this function goes through the num of entries in the boot block and copies
 * the file_name field into the arg pointer
 * 
 * @param arg       points to the place to copy the file names to
 * 
 * @return          returns 0 on success else a negative value
 */

static int sysprognames (void * arg) {
    if (!arg) return -EINVAL;

    for (int i = 0; i < boot_block.num_inodes; i++) {
        strncpy(arg, boot_block.dir_entries[i].file_name, FS_NAMELEN);
        arg += FS_NAMELEN;  // advance buffer pointer
    }

    return 0;
} 



/**
 * syscall - Dispatches the appropriate system call.
 *
 * @param tfr   Trap frame containing syscall information and arguments.
 * @return      The result of the system call or an error code.
 */
int64_t syscall(struct trap_frame * tfr){
    const uint64_t * const a = tfr->x + TFR_A0;
    switch(a[7]){
        case SYSCALL_EXIT:
            return sysexit();

        case SYSCALL_MSGOUT:
            return sysmsgout((const char*) a[0]);
            
        case SYSCALL_DEVOPEN:
            return sysdevopen(a[0], (const char*)a[1], a[2]);
            
        case SYSCALL_FSOPEN:
            return sysfsopen(a[0], (const char*)a[1]);
            
        case SYSCALL_CLOSE:
            return sysclose(a[0]);
            
        case SYSCALL_READ:
            return sysread(a[0], (void *)a[1], (size_t)a[2]);
            
        case SYSCALL_WRITE:
            return syswrite(a[0], (const void *)a[1], (size_t)a[2]);
            
        case SYSCALL_IOCTL:
            return sysioctl(a[0], a[1], (void *)a[2]);
            
        case SYSCALL_EXEC:
            return sysexec(a[0]);
            
        case SYSCALL_WAIT:
            return syswait(a[0]);
            
        case SYSCALL_USLEEP:
            return sysusleep(a[0]);
            
        case SYSCALL_FORK:
            return sysfork((const struct trap_frame *)tfr);

        case SYSCALL_SIGNAL:
            return syssignal(a[0], a[1]);

        case SYSCALL_NUMPROGS:
            return sysnumprograms((void *)a[0]);
        
        case SYSCALL_PROGNAMES:
            return sysprognames((void *)a[0]);

        case SYSCALL_PROCS:
            return sysrunningprocs((int *)a[0], (char *)a[1]);

        default:
            return -EINVAL; // Invalid syscall
            break;
    } 
}

/**
 * syscall_handler - Handles system calls.
 *
 * Determines the system call from the trap frame and dispatches it to
 * the appropriate handler.
 *
 * @param tfr   Trap frame containing syscall information.
 */
void syscall_handler(struct trap_frame * tfr){
    tfr->sepc += 4;
    tfr->x[TFR_A0] = syscall(tfr);
}