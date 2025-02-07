//           elf.h - ELF executable loader
//          

#ifndef _ELF_H_
#define _ELF_H_

#include "io.h"

//           Relevant defines
#define EI_NIDENT   16
#define ELFMAG0     0x7f
#define ELFMAG1     'E'
#define ELFMAG2     'L'
#define ELFMAG3     'F'
#define PT_LOAD     1   // Program header type for loadable segments
#define PF_X        0x1 // Execute permission
#define PF_W        0x2 // Write permission
#define PF_R        0x4 // Read permission


#define RV64_MACHINE 243
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

// Validate that file is in ELF format
#define ELF_MAGIC_OK(ehdr) ((ehdr).e_ident[0] == ELFMAG0 && \
                            (ehdr).e_ident[1] == ELFMAG1 && \
                            (ehdr).e_ident[2] == ELFMAG2 && \
                            (ehdr).e_ident[3] == ELFMAG3)

#define LOAD_START  USER_START_VMA
#define LOAD_END    USER_END_VMA

// Elf Header Structure for 64 bit ELF
typedef struct elf64_hdr
{
    unsigned char   e_ident[EI_NIDENT];     // ELF identification bytes
    uint16_t        e_type;                 // Object file type
    uint16_t        e_machine;              // Architecture
    uint32_t        e_version;              // ELF Version
    uint64_t        e_entry;                // Entry point address
    uint64_t        e_phoff;                // Program header table offset
    uint64_t        e_shoff;                // Section header table offset
    uint32_t        e_flags;                // Processor specific flags
    uint16_t        e_ehsize;               // ELF header size
    uint16_t        e_phentsize;            // Program Header entry size
    uint16_t        e_phnum;
    uint16_t        e_shentsize;
    uint16_t        e_shnum;
    uint16_t        e_shstrndx;
} Elf64_Ehdr;

// Program Header Strucutre for 64 bit ELF
typedef struct elf64_phdr{
    uint32_t        p_type;
    uint32_t        p_flags;
    uint64_t        p_offset;
    uint64_t        p_vaddr;
    uint64_t        p_paddr;
    uint64_t        p_filesz;
    uint64_t        p_memsz;
    uint64_t        p_align;
} Elf64_Phdr;

//           arg1: io interface from which to load the elf arg2: pointer to void
//           (*entry)(struct io_intf *io), which is a function pointer elf_load fills in
//           w/ the address of the entry point

//           int elf_load(struct io_intf *io, void (**entry)(struct io_intf *io)) Loads an
//           executable ELF file into memory and returns the entry point. The /io/
//           argument is the I/O interface, typically a file, from which the image is to
//           be loaded. The /entryptr/ argument is a pointer to an function pointer that
//           will be filled in with the entry point of the ELF file.
//           Return 0 on success or a negative error code on error.

int elf_load(struct io_intf *io, void (**entryptr)(void));

//           _ELF_H_
#endif

