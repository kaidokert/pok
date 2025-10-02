/*
 *                               POK header
 *
 * The following file is a part of the POK project. Any modification should
 * be made according to the POK licence. You CANNOT use this file or a part
 * of a file for your own project.
 *
 * For more information on the POK licence, please see our LICENCE FILE
 *
 * Please follow the coding guidelines described in doc/CODING_GUIDELINES
 *
 *                                      Copyright (c) 2007-2025 POK team
 */

#ifndef ELF_H_
#define ELF_H_

typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef uint32_t Elf32_Off;
typedef uint32_t Elf32_Addr;

#define EI_NIDENT (16)

typedef struct {
  unsigned char e_ident[EI_NIDENT]; /* Magic number and other info */
  Elf32_Half e_type;                /* Object file type */
  Elf32_Half e_machine;             /* Architecture */
  Elf32_Word e_version;             /* Object file version */
  Elf32_Addr e_entry;               /* Entry point virtual address */
  Elf32_Off e_phoff;                /* Program header table file offset */
  Elf32_Off e_shoff;                /* Section header table file offset */
  Elf32_Word e_flags;               /* Processor-specific flags */
  Elf32_Half e_ehsize;              /* ELF header size in bytes */
  Elf32_Half e_phentsize;           /* Program header table entry size */
  Elf32_Half e_phnum;               /* Program header table entry count */
  Elf32_Half e_shentsize;           /* Section header table entry size */
  Elf32_Half e_shnum;               /* Section header table entry count */
  Elf32_Half e_shstrndx;            /* Section header string table index */
} Elf32_Ehdr;

/* Program segment header.  */

typedef struct {
  Elf32_Word p_type;   /* Segment type */
  Elf32_Off p_offset;  /* Segment file offset */
  Elf32_Addr p_vaddr;  /* Segment virtual address */
  Elf32_Addr p_paddr;  /* Segment physical address */
  Elf32_Word p_filesz; /* Segment size in file */
  Elf32_Word p_memsz;  /* Segment size in memory */
  Elf32_Word p_flags;  /* Segment flags */
  Elf32_Word p_align;  /* Segment alignment */
} Elf32_Phdr;

#define PT_LOAD 1 /* Loadable program segment */

/* Section header */
typedef struct {
  Elf32_Word sh_name;      /* Section name (string tbl index) */
  Elf32_Word sh_type;      /* Section type */
  Elf32_Word sh_flags;     /* Section flags */
  Elf32_Addr sh_addr;      /* Section virtual addr at execution */
  Elf32_Off sh_offset;     /* Section file offset */
  Elf32_Word sh_size;      /* Section size in bytes */
  Elf32_Word sh_link;      /* Link to another section */
  Elf32_Word sh_info;      /* Additional section information */
  Elf32_Word sh_addralign; /* Section alignment */
  Elf32_Word sh_entsize;   /* Entry size if section holds table */
} Elf32_Shdr;

/* Relocation entry with addend (RELA) */
typedef struct {
  Elf32_Addr r_offset; /* Address */
  Elf32_Word r_info;   /* Relocation type and symbol index */
  int32_t r_addend;    /* Addend */
} Elf32_Rela;

/* Relocation entry without addend (REL) */
typedef struct {
  Elf32_Addr r_offset; /* Address */
  Elf32_Word r_info;   /* Relocation type and symbol index */
} Elf32_Rel;

/* Section header types */
#define SHT_NULL 0     /* Section header table entry unused */
#define SHT_PROGBITS 1 /* Program data */
#define SHT_SYMTAB 2   /* Symbol table */
#define SHT_STRTAB 3   /* String table */
#define SHT_RELA 4     /* Relocation entries with addends */
#define SHT_REL 9      /* Relocation entries, no addends */

/* ARM relocation types */
#define R_ARM_NONE 0        /* No reloc */
#define R_ARM_PC24 1        /* Deprecated ARM instruction */
#define R_ARM_ABS32 2       /* Direct 32 bit */
#define R_ARM_REL32 3       /* PC relative 32 bit */
#define R_ARM_PC13 4        /* Obsolete */
#define R_ARM_ABS16 5       /* Direct 16 bit */
#define R_ARM_ABS12 6       /* Direct 12 bit */
#define R_ARM_THM_ABS5 7    /* Direct & 0x7C (LDR, STR) */
#define R_ARM_ABS8 8        /* Direct 8 bit */
#define R_ARM_SBREL32 9     /* ?? */
#define R_ARM_THM_PC22 10   /* ARM Thumb BL */
#define R_ARM_THM_PC8 11    /* ARM Thumb B */
#define R_ARM_AMP_VCALL9 12 /* Obsolete */
#define R_ARM_SWI24 13      /* Obsolete */
#define R_ARM_THM_SWI8 14   /* Obsolete */
#define R_ARM_XPC25 15      /* Obsolete */
#define R_ARM_THM_XPC22 16  /* Obsolete */
#define R_ARM_COPY 20       /* Copy symbol at runtime */
#define R_ARM_GLOB_DAT 21   /* Create GOT entry */
#define R_ARM_JUMP_SLOT 22  /* Create PLT entry */
#define R_ARM_RELATIVE 23   /* Adjust by program base */
#define R_ARM_GOTOFF 24     /* 32 bit offset to GOT */
#define R_ARM_GOTPC 25      /* 32 bit PC relative offset to GOT */
#define R_ARM_GOT32 26      /* 32 bit GOT entry */
#define R_ARM_PLT32 27      /* 32 bit PLT address */

/* How to extract and insert information held in the r_info field */
#define ELF32_R_SYM(val) ((val) >> 8)
#define ELF32_R_TYPE(val) ((val) & 0xff)
#define ELF32_R_INFO(sym, type) (((sym) << 8) + ((type) & 0xff))

#endif /* !ELF_H_ */
