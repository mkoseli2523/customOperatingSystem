// elf.c - ELF Executable loader implementation
// This file contains the function for loading and validating ELF (Executable and Linkable Format)
// files. 'elf_load' reads an ELF file from an I/O interface and loads its executable segments into memory.
// The function also verifies the ELF magic number, architecture and endianness.
// 
// Functions:
//      int elf_load(struct io_intf *io, void (**entryptr)(struct io_intf *io))
//
// Dependencies:
//      Requires "elf.h" for ELF structure definitions and "io.h" for the I/O interface used
//      to read the ELF file.
//
#include "elf.h"
#include "io.h"
#include "console.h"
#include "config.h"
#include "memory.h"
#include "process.h"
#include <stdint.h>
#include <string.h> 

// functions needed by elf_laod

static inline uintptr_t round_down_addr(uintptr_t addr, size_t blksz) {
    return (addr / blksz * blksz);
}

static inline size_t round_up_size(size_t n, size_t blksz) {
    return (n + blksz-1) / blksz * blksz;
}

/** 
 * elf_load - Load an ELF executable from an I/O interface.
 * 
 * @io: pointer to an I/O interface that allows reading the ELF file.
 * @entryptr: A pointer to a function pointer, which is set to the entry point of the 
 *            the ELF file if loading is successful.
 * 
 * This function reads the ELF header, validates its magic number, type, and endianness,
 * and then loads each program segment marked with `PT_LOAD` into memory at its specified
 * virtual address (`p_vaddr`). It also zeroes out any remaining space if the memory size
 * (`p_memsz`) is larger than the file size (`p_filesz`). If all validation and loading
 * steps are successful, `entryptr` is set to the ELF file's entry point.
 * 
 * Returns:
 *      0 on success
 *     -1 if the ELF header could not be read
 *     -2 if the ELF magic number is invalid
 *     -3 if the ELF type or machine is unsupported
 *     -4 if seeking to the program header fails
 *     -5 if reading the program header fails
 *     -6 if a segment is out of bounds
 *     -7 if seeking to a segment offset fails
 *     -8 if loading a segment fails
 *     -9 if the ELF file is not little-endian
 *     -10 if there's a memory allocation or stack failure
 *     -11 if segment overlaps with the stack
 */
int elf_load(struct io_intf *io, void (**entryptr)(void)){
    Elf64_Ehdr elf_header;

    // 1. Read and validate ELF Header
    if(ioread_full(io, &elf_header, sizeof(Elf64_Ehdr)) != sizeof(Elf64_Ehdr)){
        return -1; // Failed to read ELF Header
    }

    if (!ELF_MAGIC_OK(elf_header)){
        return -2; // Invalid ELF magic number
    }

    // 2. Verify ELF type and architecture for 64-bit RISC-V
    if (elf_header.e_type != 2 || elf_header.e_machine != RV64_MACHINE){
        return -3; // Unsupported ELF type or machine
    }

    if(elf_header.e_ident[5] != ELFDATA2LSB){
        return -9; // NOT Little-Endian
    }

    // 3. Parse and load each program header
    for (uint16_t i = 0; i < elf_header.e_phnum; i++) {
        Elf64_Phdr phdr;
        
        // Seek to the next program header using ioseek
        if (ioseek(io, elf_header.e_phoff + i * elf_header.e_phentsize) != 0) {
            return -4; // Failed to seek to program header
        }
        
        // Read the program header
        if (ioread_full(io, &phdr, sizeof(Elf64_Phdr)) != sizeof(Elf64_Phdr)) {
            return -5; // Failed to read program header
        }

        if ((phdr.p_vaddr + phdr.p_memsz) > USER_STACK_VMA) {
            return -11; // Segment overlaps with the stack
        }
        
        if (phdr.p_type == PT_LOAD) {
            // Check if segment is within the allowed memory range
            if (phdr.p_vaddr < USER_START_VMA || (phdr.p_vaddr + phdr.p_memsz) > USER_END_VMA) {
                return -6; // Segment is out of bounds
            }

            // Align virtual address and memory size
            uintptr_t aligned_vaddr = round_down_addr(phdr.p_vaddr, PAGE_SIZE);
            size_t aligned_memsz = round_up_size(phdr.p_memsz, PAGE_SIZE);

            // Convert program header flags (p_flags) to PTE Flags
            uint8_t rwxug_flags = 0;
            if (phdr.p_flags & PF_R) rwxug_flags |= PTE_R;
            if (phdr.p_flags & PF_W) rwxug_flags |= PTE_W;
            if (phdr.p_flags & PF_X) rwxug_flags |= PTE_X;
            rwxug_flags |= PTE_U; // User-accessible by default

            // Map memory for the segment
            void *mapped_range = memory_alloc_and_map_range(aligned_vaddr, aligned_memsz, rwxug_flags | PTE_W);
            if(!mapped_range) {
                return -10;
            }
            
            // Load the segment into memory at p_vaddr
            if (ioseek(io, phdr.p_offset) != 0) {
                return -7; // Failed to seek to segment offset
            }
            
            if (ioread_full(io, (void *)phdr.p_paddr, phdr.p_filesz) != phdr.p_filesz) {
                return -8; // Failed to load segment
            }

            // Zero out remaining memory if p_memsz > p_filesz
            if (phdr.p_memsz > phdr.p_filesz) {
                memset((void *)(phdr.p_vaddr + phdr.p_filesz), 0, phdr.p_memsz - phdr.p_filesz);
            }

            // Set range flags for the segment 
            memory_set_range_flags((const void *)aligned_vaddr, aligned_memsz, rwxug_flags);
        }
    }


    // 4. Set the entry point function pointer
    *entryptr = (void (*)(void))elf_header.e_entry;

    console_printf("current tid: %u\n", current_process()->tid);
    console_printf("ELF loaded successfully. Entryptr: %p \n", (void*)*entryptr);

    return 0; // Success
}

