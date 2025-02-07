// plic.c - RISC-V PLIC
//

#include "plic.h"
#include "console.h"

#include <stdint.h>

// COMPILE-TIME CONFIGURATION
//

// *** Note to student: you MUST use PLIC_IOBASE for all address calculations,
// as this will be used for testing!

#ifndef PLIC_IOBASE
#define PLIC_IOBASE 0x0C000000
#endif

#define PLIC_SRCCNT 0x400
#define PLIC_CTXCNT 1

// INTERNAL FUNCTION DECLARATIONS
//

// *** Note to student: the following MUST be declared extern. Do not change these
// function delcarations!

extern void plic_set_source_priority(uint32_t srcno, uint32_t level);
extern int plic_source_pending(uint32_t srcno);
extern void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_set_context_threshold(uint32_t ctxno, uint32_t level);
extern uint32_t plic_claim_context_interrupt(uint32_t ctxno);
extern void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno);

// Currently supports only single-hart operation. The low-level PLIC functions
// already understand contexts, so we only need to modify the high-level
// functions (plic_init, plic_claim, plic_complete).

// EXPORTED FUNCTION DEFINITIONS
// 

void plic_init(void) {
    int i;

    // Disable all sources by setting priority to 0, enable all sources for
    // context 0 (M mode on hart 0).

    for (i = 0; i < PLIC_SRCCNT; i++) {
        plic_set_source_priority(i, 0);
        plic_enable_source_for_context(1, i); // changed context no
    }
}

extern void plic_enable_irq(int irqno, int prio) {
    trace("%s(irqno=%d,prio=%d)", __func__, irqno, prio);
    plic_set_source_priority(irqno, prio);
}

extern void plic_disable_irq(int irqno) {
    if (0 < irqno)
        plic_set_source_priority(irqno, 0);
    else
        debug("plic_disable_irq called with irqno = %d", irqno);
}

extern int plic_claim_irq(void) {
    // Hardwired context 0 (M mode on hart 0)
    trace("%s()", __func__);
    return plic_claim_context_interrupt(1); // changed context no
}

extern void plic_close_irq(int irqno) {
    // Hardwired context 0 (M mode on hart 0)
    trace("%s(irqno=%d)", __func__, irqno);
    plic_complete_context_interrupt(1, irqno); // changed context no
}

// INTERNAL FUNCTION DEFINITIONS
//

/***********************************************************************
 * void plic_set_source_priority(uint32_t srcno, uint32_t level)
 * 
 * This function sets the priority level of a specific interrupt source 
 * by writing to its corresponding priority register.
 * 
 * Arguments:
 *  - srcno: The interrupt source number whose priority is to be set.
 *  - level: The priority level to assign to the interrupt source.
 * 
 * Returns: 
 * void (none)
 * 
 * Effects:
 *  - The priority of the specified interrupt source is updated.
***********************************************************************/

void plic_set_source_priority(uint32_t srcno, uint32_t level) {
    // Calculate the address of where the interrupt source priority is stored
    volatile uint32_t* priority_reg = (volatile uint32_t*)(PLIC_IOBASE + srcno * sizeof(uint32_t));
    
    // Write new priority level into memory location
    *priority_reg = level;
}

/***********************************************************************
* int plic_source_pending(uint32_t srcno)
* 
* This function checks whether a specific interrupt source is pending
* by examining the corresponding pending register.
* 
* Arguments: 
* - srcno: The interrupt source number to check.
* 
* Returns: 
* - 1 if the interrupt source is pending, 0 otherwise.
*
* Effects: 
* - The function reads from the pending register and returns whether
*   the specified interrupt source is pending.
***********************************************************************/

int plic_source_pending(uint32_t srcno) {
    // Calculate the address of the word (4 bytes) in the pending array that holds the pending bit for the source
    volatile uint32_t* pending_reg = (volatile uint32_t*)(PLIC_IOBASE + 0x1000 + (srcno / 32) * sizeof(uint32_t));

    // Check specific bit in the word that corresponds to the interrupt source
    return (*pending_reg & (1 << (srcno % 32))) != 0;
}

/***********************************************************************
* void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno)
* 
* This function enables an interrupt source for a specific context, 
* allowing the interrupt to be triggered when it is pending.
* 
* Arguments: 
* - ctxno: The context number (e.g., hart or mode) to enable the interrupt for.
* - srcno: The interrupt source number to enable for the context.
* 
* Returns: 
* - void
*
* Effects: 
* - Enables the specified interrupt source for the given context.
***********************************************************************/
void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno) {
    // Calculate the address of the enable register for the given context and source
    volatile uint32_t* enable_reg = (volatile uint32_t*)(PLIC_IOBASE + 0x2000 + ctxno * 0x80 + (srcno / 32) * sizeof(uint32_t));

    // Set the bit to enable the interrupt source for this context
    *enable_reg |= (1 << (srcno % 32));
}

/***********************************************************************
* void plic_disable_source_for_context (uint32_t ctxno, uint32_t srcid)
* 
* This function disables an interrupt source for a specific context,
* preventing the interrupt from being triggered for that context.
* 
* Arguments: 
* - ctxno: The context number (e.g., hart or mode) to disable the interrupt for.
* - srcid: The interrupt source number to disable for the context.
* 
* Returns: 
* - void
*
* Effects: 
* - Disables the specified interrupt source for the given context.
***********************************************************************/

void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcid) {
    // Calculate the address of the enable register for the given context and source 
    volatile uint32_t* enable_reg = (volatile uint32_t*)(PLIC_IOBASE + 0x2000 + ctxno * 0x80 + (srcid / 32) * sizeof(uint32_t));
    
    // Clear the bit to disable interrupt source for this context
    *enable_reg &= ~(1 << (srcid % 32));
}

/***********************************************************************
* void plic_set_context_threshold (uint32_t ctxno, uint32_t level)
* 
* This function sets the interrupt priority threshold for a specific 
* context. Interrupts with a priority lower than the threshold will 
* not be serviced.
* 
* Arguments: 
* - ctxno: The context number (e.g., hart or mode) to set the threshold for.
* - level: The priority threshold level to set for the context.
* 
* Returns: void
*
* Effects: 
* - The priority threshold for the specified context is updated.
***********************************************************************/
void plic_set_context_threshold(uint32_t ctxno, uint32_t level) {
    // Calculate the address of the threshold register for the given context
    volatile uint32_t* threshold_reg = (volatile uint32_t*)((uintptr_t)PLIC_IOBASE + 0x200000 + ctxno * 0x1000);
    
    // Set the priority threshold for the context
    *threshold_reg = level;
}

/***********************************************************************
* uint32_t plic_claim_context_interrupt (uint32_t ctxno)
* 
* This function claims the highest-priority pending interrupt for a 
* specific context and returns the interrupt source number. The interrupt
* is acknowledged so that the handler can process it.
* 
* Arguments: 
* - ctxno: The context number (e.g., hart or mode) claiming the interrupt.
* 
* Returns: 
* - The interrupt source number of the pending interrupt with the highest priority.
*
* Effects: 
* - A pending interrupt is claimed for processing.
***********************************************************************/
uint32_t plic_claim_context_interrupt(uint32_t ctxno) {
    // Calculate the address of the claim register for the given context
    volatile uint32_t* claim_reg = (volatile uint32_t*)((uintptr_t)PLIC_IOBASE + 0x200004 + ctxno * 0x1000);

    // Return the interrupt source number of the pending interrupt w/ highest priority
    return *claim_reg;
}

/***********************************************************************
* void plic_complete_context_interrupt (uint32_t ctxno, uint32_t srcno)
* 
* This function signals that the interrupt has been handled by writing
* the interrupt source number back to the claim/complete register.
* 
* Arguments: 
* - ctxno: The context number (e.g., hart or mode) that processed the interrupt.
* - srcno: The interrupt source number that has been handled.
* 
* Returns: void
*
* Effects: 
* - The specified interrupt source is marked as complete for the given context.
***********************************************************************/

void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno) {
    // Calculate the address of the complete register for the given context
    volatile uint32_t* complete_reg = (volatile uint32_t*)((uintptr_t)PLIC_IOBASE + 0x200004 + ctxno * 0x1000);

    // Write the source number back to the register to signal the interrupt has been handled
    *complete_reg = srcno;
}