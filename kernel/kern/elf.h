/*
 * kernel/kern/elf.h — ELF64 structure definitions for UNHOX
 *
 * Minimal subset needed to load statically-linked ELF64 executables.
 * Only PT_LOAD segments are processed; dynamic linking is not supported.
 *
 * Reference: System V ABI — ELF Specification, AMD64 supplement.
 */

#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/* ELF magic number */
#define ELF_MAGIC       0x464C457F  /* "\x7FELF" as little-endian uint32 */

/* e_ident indices */
#define EI_CLASS        4
#define EI_DATA         5
#define EI_VERSION      6

/* EI_CLASS values */
#define ELFCLASS64      2

/* EI_DATA values */
#define ELFDATA2LSB     1           /* little-endian */

/* e_type values */
#define ET_EXEC         2           /* executable file */

/* e_machine values */
#define EM_X86_64       62

/* p_type values */
#define PT_NULL         0
#define PT_LOAD         1
#define PT_NOTE         4
#define PT_PHDR         6

/* p_flags values */
#define PF_X            0x1         /* executable */
#define PF_W            0x2         /* writable */
#define PF_R            0x4         /* readable */

/*
 * Elf64_Ehdr — ELF64 file header (64 bytes)
 */
typedef struct {
    uint8_t     e_ident[16];        /* magic + class + data + version + pad */
    uint16_t    e_type;             /* object file type */
    uint16_t    e_machine;          /* architecture */
    uint32_t    e_version;          /* ELF version */
    uint64_t    e_entry;            /* entry point virtual address */
    uint64_t    e_phoff;            /* program header table offset */
    uint64_t    e_shoff;            /* section header table offset */
    uint32_t    e_flags;            /* processor-specific flags */
    uint16_t    e_ehsize;           /* ELF header size */
    uint16_t    e_phentsize;        /* program header entry size */
    uint16_t    e_phnum;            /* number of program header entries */
    uint16_t    e_shentsize;        /* section header entry size */
    uint16_t    e_shnum;            /* number of section header entries */
    uint16_t    e_shstrndx;         /* section name string table index */
} Elf64_Ehdr;

/*
 * Elf64_Phdr — ELF64 program header (56 bytes)
 */
typedef struct {
    uint32_t    p_type;             /* segment type */
    uint32_t    p_flags;            /* segment flags */
    uint64_t    p_offset;           /* offset in file */
    uint64_t    p_vaddr;            /* virtual address in memory */
    uint64_t    p_paddr;            /* physical address (usually ignored) */
    uint64_t    p_filesz;           /* size of segment in file */
    uint64_t    p_memsz;            /* size of segment in memory */
    uint64_t    p_align;            /* alignment */
} Elf64_Phdr;

#endif /* ELF_H */
