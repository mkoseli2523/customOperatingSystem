// memory.c - Memory management
//

#ifndef TRACE
#ifdef MEMORY_TRACE
#define TRACE
#endif
#endif

#ifndef DEBUG
#ifdef MEMORY_DEBUG
#define DEBUG
#endif
#endif

#include "config.h"

#include "memory.h"
#include "console.h"
#include "halt.h"
#include "heap.h"
#include "csr.h"
#include "string.h"
#include "error.h"
#include "thread.h"
#include "process.h"

#include <stdint.h>

// EXPORTED VARIABLE DEFINITIONS
//

char memory_initialized = 0;
uintptr_t main_mtag;

// IMPORTED VARIABLE DECLARATIONS
//

// The following are provided by the linker (kernel.ld)

extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// INTERNAL TYPE DEFINITIONS
//

union linked_page {
    union linked_page * next;
    char padding[PAGE_SIZE];
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN2(vma) (((vma) >> (9+9+12)) & 0x1FF)
#define VPN1(vma) (((vma) >> (9+12)) & 0x1FF)
#define VPN0(vma) (((vma) >> 12) & 0x1FF)
#define MIN(a,b) (((a)<(b))?(a):(b))

// INTERNAL FUNCTION DECLARATIONS
//
struct pte * walk_pt(struct pte* root, uintptr_t vma, int create);

static inline int wellformed_vma(uintptr_t vma);
static inline int wellformed_vptr(const void * vp);
static inline int aligned_addr(uintptr_t vma, size_t blksz);
static inline int aligned_ptr(const void * p, size_t blksz);
static inline int aligned_size(size_t size, size_t blksz);

static inline uintptr_t active_space_mtag(void);
static inline struct pte * mtag_to_root(uintptr_t mtag);
struct pte * active_space_root(void);

static inline void * pagenum_to_pageptr(uintptr_t n);
static inline uintptr_t pageptr_to_pagenum(const void * p);

static inline void * round_up_ptr(void * p, size_t blksz);
static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz);
static inline size_t round_up_size(size_t n, size_t blksz);
static inline void * round_down_ptr(void * p, size_t blksz);
static inline size_t round_down_size(size_t n, size_t blksz);
static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz);

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag);
static inline struct pte null_pte(void);

static inline void sfence_vma(void);

// INTERNAL GLOBAL VARIABLES
//

static union linked_page * free_list;

static struct pte main_pt2[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt1_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));
static struct pte main_pt0_0x80000[PTE_CNT]
    __attribute__ ((section(".bss.pagetable"), aligned(4096)));

// EXPORTED VARIABLE DEFINITIONS
//

// EXPORTED FUNCTION DEFINITIONS
// 


/**
 * Initializes the memory subsystem, including page tables, heap allocator, 
 * and free page pool.
 * 
 * Sets up the direct memory mapping for the kernel, configures the heap and 
 * page allocator, and enables paging. Also ensures the kernel image fits within 
 * a 2MB megapage and establishes the free list for available memory pages.
 */
void memory_init(void) {
    const void * const text_start = _kimg_text_start;
    const void * const text_end = _kimg_text_end;
    const void * const rodata_start = _kimg_rodata_start;
    const void * const rodata_end = _kimg_rodata_end;
    const void * const data_start = _kimg_data_start;
    union linked_page * page;
    void * heap_start;
    void * heap_end;
    size_t page_cnt;
    uintptr_t pma;
    const void * pp;

    trace("%s()", __func__);

    assert (RAM_START == _kimg_start);

    kprintf("           RAM: [%p,%p): %zu MB\n",
        RAM_START, RAM_END, RAM_SIZE / 1024 / 1024);
    kprintf("  Kernel image: [%p,%p)\n", _kimg_start, _kimg_end);

    // Kernel must fit inside 2MB megapage (one level 1 PTE)
    
    if (MEGA_SIZE < _kimg_end - _kimg_start)
        panic("Kernel too large");

    // Initialize main page table with the following direct mapping:
    // 
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // RAM_START to _kimg_end:           RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB
    
    // Identity mapping of two gigabytes (as two gigapage mappings)
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void*)pma, PTE_R | PTE_W | PTE_G);
    
    // Third gigarange has a second-level page table
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First physical megarange of RAM is mapped as individual pages with
    // permissions based on kernel image region.

    main_pt1_0x80000[VPN1(RAM_START_PMA)] =
        ptab_pte(main_pt0_0x80000, PTE_G);

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] =
            leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Enable paging. This part always makes me nervous.

    main_mtag =  // Sv39
        ((uintptr_t)RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
        pageptr_to_pagenum(main_pt2);
    
    csrw_satp(main_mtag);
    sfence_vma();

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end;
    heap_end = round_up_ptr(heap_start, PAGE_SIZE);
    if (heap_end - heap_start < HEAP_INIT_MIN) {
        heap_end += round_up_size (
            HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end)
        panic("Not enough memory");
    
    // Initialize heap memory manager

    heap_init(heap_start, heap_end);

    kprintf("Heap allocator: [%p,%p): %zu KB free\n",
        heap_start, heap_end, (heap_end - heap_start) / 1024);

    free_list = heap_end; // heap_end is page aligned
    page_cnt = (RAM_END - heap_end) / PAGE_SIZE;

    kprintf("Page allocator: [%p,%p): %lu pages free\n",
        free_list, RAM_END, page_cnt);

    // Put free pages on the free page list
    // TODO: FIXME implement this (must work with your implementation of
    // memory_alloc_page and memory_free_page).
    
    // put free pages on the free pages list by going through the entire space avaliable,
    // assigning linked pages and updating the head of the list - free_list
    // this needs the heap_end and RAN_END to be page alligned which heap_end for sure is 
    // uintptr_t aligned_ram_end = round_down_addr((uintptr_t)RAM_END, PAGE_SIZE);
    for(pp = heap_end; pp<RAM_END; pp+=PAGE_SIZE){
        page = (union linked_page *)pp;
        page->next = free_list;
        free_list = page;
    }


    // Allow supervisor to access user memory. We could be more precise by only
    // enabling it when we are accessing user memory, and disable it at other
    // times to catch bugs.

    csrs_sstatus(RISCV_SSTATUS_SUM);

    memory_initialized = 1;
}



/**
 * Allocates a zeroed memory page from the free list.
 * 
 * @return Pointer to the allocated memory page, or NULL on failure.
 */
void *memory_alloc_page(void) {
    union linked_page *page;

    // Ensure the free list exists
    if (free_list == NULL) {
        panic("no free pages in free_list: memory_alloc_page");
        return NULL;
    }

    // Remove the first page from the free list
    page = free_list;
    free_list = free_list->next;

    // Zero out the page
    memset((void *)page, 0, PAGE_SIZE);

    // Return the address of direct-mapped page
    return (void *)page;
}



/**
 * Frees a memory page and returns it to the free list.
 * 
 * @param pp Input pointer to the memory page to be freed. Must be page-aligned and non-NULL.
 *           We clear the page and then add it to the back of the free_list.
 *           
 */

void memory_free_page(void * pp){
    union linked_page *page;

    // Ensure the input page is valid and page-aligned
    if ((uintptr_t)pp % PAGE_SIZE != 0 || pp == NULL) {
        panic("Invalid page address provided in memory_free_page");
        return;
    }

    // Zero out the page to prevent stale data
    memset(pp, 0, PAGE_SIZE);

    // Cast the page pointer to the linked_page structure
    page = (union linked_page *)pp;

    // Add the page back to the free list
    page->next = free_list;
    free_list = page;
}



/**
 * Sets the access flags for a specific memory page.
 * 
 * @param vp         Pointer to the virtual address of the memory page. 
 * @param rwxug_flags Access flags for the page. The flags are masked to ensure only valid bits are set.
 * 
 * Ensures the virtual address is page-aligned and the corresponding page table entry (PTE) exists and is valid.
 * Updates the PTE with the specified flags and flushes the TLB to reflect the changes.
 */

void memory_set_page_flags(const void *vp, uint8_t rwxug_flags) {
    struct pte *pte;

    // Ensure the virtual pointer is page-aligned
    if ((uintptr_t)vp % PAGE_SIZE != 0) {
        kprintf("vp not page alligned in memory_set_page_flags");
        return;
    }

    // Walk the page table to get the PTE for the virtual address
    pte = walk_pt(active_space_root(), (uintptr_t)vp, 0); // argument create is 0: we do not want to
                                                     // create missing tables
    // checks for a valid pte
    if (pte == NULL || !(pte->flags & PTE_V)) {
        kprintf("pte is null or not valid in memory_set_page_flags");
        return;
    }

    // Update the PTE with the new flags
    pte->flags &= ~PTE_FLAGS_MASK;
    pte->flags |= rwxug_flags;

    // Flush the TLB to ensure the changes are visible
    sfence_vma();
}



/**
 * memory_space_reclaim - reclaims memory for the current process's memory space.
 * 
 * This function switches to the main memory space, flushes the TLB, and frees
 * user-space pages and non-global page table entries from the current memory space.
 * 
 * @param: This function does not take in any parameters
 * 
 * @returns: This function does not return.
 */

void memory_space_reclaim(void) {
    // retrieve the current satp value (ie the old mem space)
    uintptr_t old_satp = active_space_mtag();

    // extract the root page table pointer
    struct pte* old_root_pa = mtag_to_root(old_satp);

    // switch to the main mem space
    csrw_satp(main_mtag);

    // flush the tlb
    sfence_vma();

    // reclaim the old memory space's page tables and pages
    for (uintptr_t vaddr = USER_START_VMA; vaddr < USER_END_VMA; vaddr += PAGE_SIZE) {
        // walk the pt
        struct pte* pte = walk_pt(old_root_pa, vaddr, 0);
        
        // check if pte is valid
        if (!pte) continue;

        // check if valid flag is set
        if (!(pte->flags & PTE_V)) continue; 

        // skip global mappings
        if (pte->flags & PTE_G) continue;

        // free the physical page if its a leaf pte
        if (pte->flags & (PTE_R | PTE_W | PTE_X)) {
            // free the phyical page
            uintptr_t pa = (uintptr_t)(pte->ppn) << 12;
            memory_free_page((void *)pa);

            // invalidate the pte
            memset(pte, 0, sizeof(struct pte));
        }
    }

    // can't free the old root pt itself
    // since it might contain global or shared mappings
}



/**
 * allocates and maps a range of virtual addresses with provided flags
 * 
 * this function allocates physical pages and maps them to the specified virtual memory range.
 * it uses memory_alloc_and_map_page function to allocate and map individual pages
 * 
 * @param vma           starting vma to map
 * @param size          size of the memory range to allocate and map
 * @param rwxug_flags   flags for the page table entry
 * 
 * @return              returns a pointer to the beginning of the mapped virtual memory address
 *                      returns NULL if allocation or mapping fails
 */

void * memory_alloc_and_map_range (uintptr_t vma, size_t size, uint_fast8_t rwxug_flags) {
    uintptr_t start_vma = vma;
    uintptr_t end_vma = start_vma + size;
    size_t page_size = PAGE_SIZE;

    // allign start and end addresses
    round_down_addr(start_vma, page_size);
    round_up_addr(end_vma, page_size);

    size_t num_pages = (end_vma - start_vma) / page_size;

    for (size_t pages_mapped = 0; pages_mapped < num_pages; pages_mapped++) {
        uintptr_t current_vma = start_vma + pages_mapped * page_size;

        void* result = memory_alloc_and_map_page(current_vma, rwxug_flags);

        if (!result) {
            // allocation or mapping failed 
            // unroll each allocated page
            for (int i = 0; i < pages_mapped; i++) {
                uintptr_t rollback_vma = start_vma + i * page_size;

                // unmap the page and free the physical memory
                memory_free_page((void *)rollback_vma);
            }

            kprintf("something went wrong when allocating a page, rolling back each allocated page\n");
            return NULL;
        }
    }

    return (void *)start_vma;
}



/**
 * modifies flags of all ptes within the specified virtual memory range
 * 
 * this function iterates through each page with the starting address and the size of pages
 * to be allocated, then calls memory_set_page_flags function to modify the flags of a given 
 * page
 * 
 * @param vp            starting virtual address of the range
 * @param size          size of the range in bytes
 * @param rwxug_flags   new flags
 */

void memory_set_range_flags (const void * vp, size_t size, uint_fast8_t rwxug_flags) {
    uintptr_t start_addr = (uintptr_t) vp;
    uintptr_t end_addr = start_addr + size;
    size_t page_size = PAGE_SIZE;

    // make sure the start and end addresses are page aligned
    round_down_addr(start_addr, page_size);
    round_up_addr(end_addr, page_size);

    // calculate the num of pages
    size_t num_pages = (end_addr - start_addr) / page_size;

    // iterate over each page in the range
    for (size_t current_page = 0; current_page < num_pages; current_page++) {
        // grab the current address and set the flag
        uintptr_t current_addr = start_addr + current_page * page_size;
        memory_set_page_flags((void *)current_addr, rwxug_flags);       
    }
}



/**
 * unmaps and frees all user space pages
 * 
 * this function retrieves the root page table and traverses the page table hierarchy
 * to unmap and free all pages that have the user flag set
 */

void memory_unmap_and_free_user(void) {
    // retrieve the current satp value (ie the old mem space)
    uintptr_t old_satp = active_space_mtag();

    // extract the root page table pointer
    struct pte* root_pt = mtag_to_root(old_satp);

    // iterate over the user virtual address range and unmap user pages
    for (uintptr_t vma = USER_START_VMA; vma < USER_END_VMA; vma += PAGE_SIZE) {
        // walk to the pte
        struct pte* pte = walk_pt(root_pt, vma, 0);

        // check if pte is valid
        if (!pte || (!(pte->flags & PTE_V))) continue; 

        // check if user flag is set
        if (!(pte->flags & PTE_U)) continue;

        // if leaf, unmap and free the pp
        if (pte->flags & (PTE_R | PTE_W | PTE_X)) {
            // clear the mem flags
            pte->flags = 0;

            // leaf page, unmap and free
            uintptr_t pa = (uintptr_t)(pte->ppn) << 12;

            memory_free_page((void *)pa);
        }
    }

    // flush the tlb
    sfence_vma();
}



/**
 * Allocates a physical memory page and maps it to a virtual address
 * 
 * This function allocates a single physical memory page and establishes a 
 * mapping between the specified virtual memory address and the allocated physical page.
 * The mapping is created in the current virtual memory space with the specified access permissions.
 * 
 * @param vma           virtual memory address to be mapped. Must be page aligned and well-formed for the current paging scheme.
 * @param rwxug_flags   A combination of the access permission flags for the mapping:
 *                      - PTE_R: Readable
 *                      - PTE_W: Writable
 *                      - PTE_E: Executable
 *                      - PTE_U: User-Accessible 
 *                      - PTE_G: Global
 * 
 * @return - A pointer to the virtual memory address if successful.
 *         - NULL if the allocation or mapping fails
 */

void *memory_alloc_and_map_page(uintptr_t vma, uint_fast8_t rwxug_flags){
    // Ensure virtual address is well-formed and page-aligned
    if(!wellformed_vma(vma) || !aligned_addr(vma, PAGE_SIZE)){
        return NULL;
    }

    // Allocate physical page
    void *physical_page = memory_alloc_page();
    if(!physical_page) {
        panic("Out of physical memory!");
        return NULL;
    }

    // Traverse or create page tables for the virtual address
    struct pte *pte = walk_pt(active_space_root(), vma, 1);
    if (!pte) {
        memory_free_page(physical_page);
        return NULL;
    }

    // Set up the leaf PTE to point to the allocated physical page
    *pte = leaf_pte(physical_page, rwxug_flags);

    // Flush TLB to ensure new mapping is recognized
    sfence_vma();
    
    // Return mapped virtual address
    return (void *) vma;
}



/**
 * handles page fault for the given virtual address
 * 
 * @param vptr  pointer to the faulting virtual address. must be aligned with page size and 
 *              within the user region
 */

void memory_handle_page_fault(const void * vptr){
    uintptr_t va = (uintptr_t) vptr;
    struct pte * root_pt, * pa_pte, * new_pp;
    
    console_printf("handling page fault at virtual address: 0x%lx\n", va);

    // check if the virtual address is within the user mem space
    if (va < USER_START_VMA || va >= USER_END_VMA) {
        console_printf("memory_handle_page_fault: 0x%lx is outside user space\n", va);
        panic("page fault in invalid address space");
    }

    // ensure va is page aligned
    va = round_down_addr(va, PAGE_SIZE);
    
    if (!aligned_addr(va, PAGE_SIZE)) {
        console_printf("memory_handle_page_fault: 0x%lx is not page-aligned\n", va);
        panic("page fault at non-aligned address");
    }

    // get the root page table for the active memory space and get to the pte for the va
    root_pt = active_space_root();
    pa_pte = walk_pt(root_pt, va, 1);

    if (pa_pte == NULL) {
        console_printf("memory_handle_page_fault: pte not found for address 0x%lx\n", va);
        panic("Page fault: PTE not found");
    }

    // allocate new pp
    new_pp = (struct pte *) memory_alloc_and_map_page(va, PTE_R | PTE_W | PTE_U);

    if (new_pp == NULL) {
        console_printf("memory_handle_page_fault: failed to allocate physical page for address 0x%lx\n", va);
        panic("Page fault: Memory allocation failed");
    }

    // flush tlb
    sfence_vma();

    console_printf("memory_handle_page_fault: successfully handled page fault at address 0x%lx\n", va);
}



/**
 * validates a virtual memory range and checks for required permission
 * 
 * @param vp            pointer to the start of the virtual memory range
 * @param len           length of the range in bytes
 * @param rwxug_flags   flags for the given range
 * 
 * @return              returns 0 if the range is valid and has the required flags
 *                      else returns a negative value
 */

int memory_validate_vptr_len (const void * vp, size_t len, uint_fast8_t rwxug_flags){
    // Validate the ptr and len are well-formed
    if (!wellformed_vma((uintptr_t)vp) || len == 0){
        return -1;
    }

    uintptr_t start_vma = (uintptr_t)vp;
    uintptr_t end_vma = start_vma + len;

    // make sure the start and end addresses are page aligned
    round_down_addr(start_vma, PAGE_SIZE);
    round_up_addr(end_vma, PAGE_SIZE);

    // Traverse all pages within the range [start_vma, end_vma)
    for(uintptr_t current_vma = start_vma; current_vma < end_vma; current_vma += PAGE_SIZE){
        // Get the page table entry for the current virtual address
        struct pte *pte = walk_pt(active_space_root(), current_vma, 0);
        if (!pte || !(pte->flags & PTE_V)){
            return -1; // Page is not mapped
        }

        // Check if the page has the required flags
        if ((pte->flags & rwxug_flags) != rwxug_flags){
            return -1; // Required flags are not present 
        }
    }

    return 0; // All pages in the range are valid and have the required flags
}



/**
 * validates virtual memory string is well formed and accesible
 * 
 * @param vs        pointer to the start of the new virtual memory string
 * @param ug_flags  flags specifying required permissions for the string
 * 
 * @return          returns 0 if the string is valid, has the required flags
 *                  else returns a negative value
 */

int memory_validate_vstr (const char * vs, uint_fast8_t ug_flags){
    if (!wellformed_vma((uintptr_t)vs)){
        return -1;
    }

    uintptr_t current_vma = (uintptr_t)vs;
    
    while (1) {
        // Get PTE for the current virtual address
        struct pte *pte = walk_pt(active_space_root(), current_vma, 0);
        if(!pte || !(pte->flags & PTE_V)){
            return -1; // Page is not mapped
        }

        // Check if page has required user and readable flags 
        if((pte->flags & ug_flags) != ug_flags){
            return -1; // Required flags are not present
        }

        // Access the current character
        const char *current_char = (const char *)current_vma;
        if (*current_char == '\0'){
            return 0; // Found null terminator, string is valid
        }

        current_vma++;
        
        if(current_vma % PAGE_SIZE == 0){
            // Moving to next page, repeat checks for new page
            continue;
        }
    }    

    return -1; // Should never reach here.
}

/**
 * this function clones the memory space of the parent into the child
 * 
 * This function duplicates the current process's memory space, returning a new mtag
 * representing the child's address space. It performs a shallow copy of the kernel mappings, and a 
 * deep copy of user-space pages by allocating new pages and copying their contents
 * 
 * @param asid      address space identifier for the child's address space unused for this MP
 * 
 * @return          returns the mtag of the newly cloned memory space
 */
uintptr_t memory_space_clone(uint_fast16_t asid){
    // get parent mtag 
    uintptr_t parent_mtag = current_process()->mtag;

    // extract parent root pt 
    struct pte* parent_root_pt = mtag_to_root(parent_mtag);

    // allocate new root page 
    struct pte *new_root = memory_alloc_page();
    if (!new_root) 
        return 0; // Allocation failure

    struct pte *child_root = new_root;

    memset(new_root, 0, PAGE_SIZE); // Zero out the new root table

    for (int i = 0; i < PTE_CNT; i++) {
        if (main_pt2[i].flags & PTE_G) 
            child_root[i] = main_pt2[i];
    }

    // clone the page table entries
    for (uintptr_t vma = USER_START_VMA; vma < USER_END_VMA; vma += PAGE_SIZE) {
        struct pte *parent_pte = walk_pt(parent_root_pt, vma, 0);
        if (!parent_pte || !(parent_pte->flags & PTE_V)) {
            continue; // Skip unmapped pages
        }

        void *parent_phys_page = pagenum_to_pageptr(parent_pte->ppn);

        // walk to the same vma in the child root
        struct pte *child_pte = walk_pt(child_root, vma, 1);

        // allocate the page that the child pte points to 
        void* child_pt = memory_alloc_page();

        // set the ppn of the child pte to the newly allocated page
        child_pte->ppn = pageptr_to_pagenum(child_pt);

        // copy the flags over from paren pte
        child_pte->flags |= parent_pte->flags;

        void *child_phys_page = pagenum_to_pageptr(child_pte->ppn);

        // clone the contents of the parent vma to child vma
        memcpy(child_phys_page, parent_phys_page, PAGE_SIZE);
    }

    // construct new mtag with given asid 
    uintptr_t new_mtag = ((uintptr_t) RISCV_SATP_MODE_Sv39 << RISCV_SATP_MODE_shift) |
                         ((uintptr_t) asid << RISCV_SATP_ASID_shift) |
                         pageptr_to_pagenum(child_root);

    return new_mtag;
}


// helper function

/**
 * walks the page table to find or create the pte corresponding to a virtual address
 * 
 * this function traverses the page table hierarchy starting from the root and locates
 * the pte that maps the 4kb page containing the given vma. If create is non-zero the 
 * function will allocate a new page table(s) as needed to complete the walk down to the 
 * leaf level (level 0). This function does not map mega or giga pages. 
 * 
 * @param root      pointer to the root page table
 * @param vma       virtual memory address for which the PTE is sought
 * @param create    if non-zero, indicates that missing pts should be created
 * 
 * @return          returns a pointer to the pte containing vma
 *                  if the pte can't be found or created returns NULL
 */

struct pte * walk_pt(struct pte* root, uintptr_t vma, int create) {
    struct pte* pt = root;

    // virtual page number bits
    uint64_t vpn[3];
    vpn[0] = VPN0(vma);
    vpn[1] = VPN1(vma);
    vpn[2] = VPN2(vma);

    // walk down the page table starting from the highest level (ie level 2)
    for (int level = 2; level > 0; level--) {
        // check if the entry is valid
        if (pt != NULL && pt[vpn[level]].flags & PTE_V) {
            // grab the page table entry of the next level
            struct pte* pte = (struct pte*)((uint64_t)pt[vpn[level]].ppn << PAGE_ORDER);

            // if pte has flags r=0, w=0, and x=0, pte refers to next level
            if (pt[vpn[level]].flags & (PTE_R | PTE_W | PTE_X)) {
                // leaf pte encountered at a non-leaf level, return
                return NULL;
            } else {
                // pte is valid pointing to the next level
                // make pt point to pte
                pt = pte;
            }
        } else if (create) {
            // entry isn't valid create the entry
            // allocate a new page table
            struct pte* new_pt = (struct pte*)memory_alloc_page(); // should panic if no pages available

            console_printf("new pt address: 0x%x\n", new_pt);

            pt[vpn[level]].ppn = (uint64_t)new_pt >> PAGE_ORDER;
            pt[vpn[level]].flags = PTE_V;
            
            // set up the pte to point to the new page table
            pt = new_pt;
        } else {
            // entry isn't valid return NULL
            return NULL;
        }
    }

    // at level 0, return the pte corresponding to vpn[0]
    return &pt[vpn[0]];
}


// INTERNAL FUNCTION DEFINITIONS
//

static inline int wellformed_vma(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits+1));
}

static inline int wellformed_vptr(const void * vp) {
    return wellformed_vma((uintptr_t)vp);
}

static inline int aligned_addr(uintptr_t vma, size_t blksz) {
    return ((vma % blksz) == 0);
}

static inline int aligned_ptr(const void * p, size_t blksz) {
    return (aligned_addr((uintptr_t)p, blksz));
}

static inline int aligned_size(size_t size, size_t blksz) {
    return ((size % blksz) == 0);
}

static inline uintptr_t active_space_mtag(void) {
    return csrr_satp();
}

uintptr_t active_memory_space(void) {
    return active_space_mtag();
}

uintptr_t memory_space_switch(uintptr_t mtag) {
    uintptr_t old_mtag = csrrw_satp(mtag);

    sfence_vma();

    return old_mtag;
}

static inline struct pte * mtag_to_root(uintptr_t mtag) {
    return (struct pte *)((mtag << 20) >> 8);
}


struct pte * active_space_root(void) {
    return mtag_to_root(active_space_mtag());
}

static inline void * pagenum_to_pageptr(uintptr_t n) {
    return (void*)(n << PAGE_ORDER);
}

static inline uintptr_t pageptr_to_pagenum(const void * p) {
    return (uintptr_t)p >> PAGE_ORDER;
}

static inline void * round_up_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)(p + blksz-1) / blksz * blksz);
}

static inline uintptr_t round_up_addr(uintptr_t addr, size_t blksz) {
    return ((addr + blksz-1) / blksz * blksz);
}

static inline size_t round_up_size(size_t n, size_t blksz) {
    return (n + blksz-1) / blksz * blksz;
}

static inline void * round_down_ptr(void * p, size_t blksz) {
    return (void*)((uintptr_t)p / blksz * blksz);
}

static inline size_t round_down_size(size_t n, size_t blksz) {
    return n / blksz * blksz;
}

static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz) {
    return (addr / blksz * blksz);
}

static inline struct pte leaf_pte (
    const void * pptr, uint_fast8_t rwxug_flags)
{
    return (struct pte) {
        .flags = rwxug_flags | PTE_A | PTE_D | PTE_V,
        .ppn = pageptr_to_pagenum(pptr)
    };
}

static inline struct pte ptab_pte (
    const struct pte * ptab, uint_fast8_t g_flag)
{
    return (struct pte) {
        .flags = g_flag | PTE_V,
        .ppn = pageptr_to_pagenum(ptab)
    };
}

static inline struct pte null_pte(void) {
    return (struct pte) { };
}

static inline void sfence_vma(void) {
    asm inline ("sfence.vma" ::: "memory");
}
