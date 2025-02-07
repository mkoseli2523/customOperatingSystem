// excp.c - Exception handlers
//

#include "trap.h"
#include "csr.h"
#include "halt.h"
#include "memory.h"
#include "signals.h"

#include <stddef.h>

// EXPORTED FUNCTION DECLARATIONS
//

// Called to handle an exception in S mode and U mode, respectively

extern void smode_excp_handler(unsigned int code, struct trap_frame * tfr);
extern void umode_excp_handler(unsigned int code, struct trap_frame * tfr);

// INTERNAL FUNCTION DECLARATIONS
//

static void __attribute__ ((noreturn)) default_excp_handler (
    unsigned int code, const struct trap_frame * tfr);

// IMPORTED FUNCTION DECLARATIONS
//

extern void syscall_handler(struct trap_frame * tfr); // syscall.c

// INTERNAL GLOBAL VARIABLES
//

static const char * const excp_names[] = {
	[RISCV_SCAUSE_INSTR_ADDR_MISALIGNED] = "Misaligned instruction address",
	[RISCV_SCAUSE_INSTR_ACCESS_FAULT] = "Instruction access fault",
	[RISCV_SCAUSE_ILLEGAL_INSTR] = "Illegal instruction",
	[RISCV_SCAUSE_BREAKPOINT] = "Breakpoint",
	[RISCV_SCAUSE_LOAD_ADDR_MISALIGNED] = "Misaligned load address",
	[RISCV_SCAUSE_LOAD_ACCESS_FAULT] = "Load access fault",
	[RISCV_SCAUSE_STORE_ADDR_MISALIGNED] = "Misaligned store address",
	[RISCV_SCAUSE_STORE_ACCESS_FAULT] = "Store access fault",
    [RISCV_SCAUSE_ECALL_FROM_UMODE] = "Environment call from U mode",
    [RISCV_SCAUSE_ECALL_FROM_SMODE] = "Environment call from S mode",
    [RISCV_SCAUSE_INSTR_PAGE_FAULT] = "Instruction page fault",
    [RISCV_SCAUSE_LOAD_PAGE_FAULT] = "Load page fault",
    [RISCV_SCAUSE_STORE_PAGE_FAULT] = "Store page fault"
};

// EXPORTED FUNCTION DEFINITIONS
//

void smode_excp_handler(unsigned int code, struct trap_frame * tfr) {
	default_excp_handler(code, tfr);
}
/**
 * umode_excp_handler - handles exceptions while running in user mode. 
 * 
 * This funciton classifies the exception based on its code and invokes thhe
 * appropriate handler for the specific exception. 
 * 
 * @param: code The exception code indicating the type of exception.
 *               Values are defined in the RISC-V specification:
 *               - 12: Instruction page fault
 *               - 13: Load page fault
 *               - 15: Store/AMO page fault
 *               - 8: System call
 * 
 * @param tfr   Pointer to the trap frame structure, which contains the 
 *              saved registers and state information at the time of 
 *              the exception.
 * 
 * @returns     This funciton returns nothing.
 */
void umode_excp_handler(unsigned int code, struct trap_frame * tfr) {
    switch (code) {
    case RISCV_SCAUSE_INSTR_PAGE_FAULT: // instruction page fault
    case RISCV_SCAUSE_LOAD_PAGE_FAULT: // load page fault
    case RISCV_SCAUSE_STORE_PAGE_FAULT: // store/amo page fault
        console_printf("page fault in supervisor mode\n");
        memory_handle_page_fault((void *)csrr_stval());
        break;
    case RISCV_SCAUSE_ECALL_FROM_UMODE:
        syscall_handler(tfr); // Pass trap frame to syscall handler
        break;
    default:
        default_excp_handler(code, tfr);
        break;
    }

    signal_deliver();
}

void default_excp_handler (
    unsigned int code, const struct trap_frame * tfr)
{
    const char * name = NULL;

    if (0 <= code && code < sizeof(excp_names)/sizeof(excp_names[0]))
		name = excp_names[code];
	
	if (name == NULL)
		kprintf("Exception %d at %p\n", code, (void*)tfr->sepc);
	else
		kprintf("%s at %p\n", name, (void*)tfr->sepc);
	
    panic(NULL);
}
