# Custom Operating System

A learning-focused OS project providing basic process management, signals, and an interactive shell on RISC-V QEMU.

## Features
- Thread scheduler
- Device drivers 
- Interrupt and exception handling
- Signals
- Syscalls
- Preemptive multitasking
- Forking
- Ref counting
- Virtual memory
- Shell

## Build
1. Install `RISCV gnu toolchain` 
2. Clone the repository

## Usage
- In order to compile and run user programs, first see the `user` folder. I have included some example user programs.
- See the `Makefile` in `user` folder, and possibly add methods to compile own user programs
- Use the `Makefile` in `util` in order to compile the user programs into a .raw format
- Move the .raw file generated into kernel and use `make run-kernel` in order to launch the kernel in QEMU

## Project Structure
- The "heart" of the project is within the `kern` folder, which includes all the files for the actual kernel to run

## Future
- Whenever I get a break from classes I try to add a new future or two. I have various functions in the future I'd like to implement

    - **Advanced Signals**: Add more comprehensive signal handling (e.g., SIGUSR1, SIGUSR2) with user-defined handlers.
    - **IPC Mechanisms**: Implement pipes or a shared memory approach for inter-process communication.
    - **Networking**: (Long-term goal) Add a basic TCP/IP stack for remote connections.
    - **Doom Port**: Ambitious goal to eventually run Doom—requires graphics, audio, and extensive user mode support.