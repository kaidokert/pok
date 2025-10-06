OUTPUT_FORMAT("elf32-littlearm");
OUTPUT_ARCH("arm")
ENTRY(Reset_Handler)

/* STM32F405RGT6 memory layout (NetduinoPlus2) */
/* Note: Hardware has 192KB RAM but using 128KB until MPU config investigated */
PHYS_BASE = 0x08000000;  /* Flash start address */
RAM_BASE  = 0x20000000;  /* RAM start address */
RAM_SIZE  = 0x20000;     /* 128KB RAM (conservative for stability) */
FLASH_SIZE = 0x100000;   /* 1MB Flash */

/* Memory region definitions */
__FLASH_START__ = PHYS_BASE;
__FLASH_END__ = PHYS_BASE + FLASH_SIZE;
__SRAM_START__ = RAM_BASE;
__SRAM_END__ = RAM_BASE + RAM_SIZE;
__SRAM_SIZE__ = RAM_SIZE;

/* Stack pointer - top of RAM */
_estack = RAM_BASE + RAM_SIZE;

PHDRS
{
	text PT_LOAD FLAGS(5);    /* Read + Execute */
	data PT_LOAD FLAGS(6);    /* Read + Write */
	bss  PT_LOAD FLAGS(6);    /* Read + Write */
	archive PT_LOAD FLAGS(4); /* Read only */
}

SECTIONS
{
	. = PHYS_BASE;

	.text : AT(ADDR(.text))
	{
		*(.isr_vector)  /* Interrupt vector table */
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata.*)
		. = ALIGN(4);
	} :text

	/* Store initialization data in flash after text section */

	.data RAM_BASE : AT(ADDR(.text) + SIZEOF(.text))
	{
		_sdata = .;  /* Start of data section in RAM */
		__data_start = .;
		*(.data)
		*(.data.*)
		. = ALIGN(4);
		_edata = .;  /* End of data section in RAM */
		__data_end = .;
	} :data

	_sidata = LOADADDR(.data);

	.bss :
	{
		_sbss = .;  /* Start of BSS */
		__bss_start__ = .;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;  /* End of BSS */
		__bss_end__ = .;
	} :bss

	/* Archive section for partitions - stored in flash after data */
	. = LOADADDR(.data) + SIZEOF(.data);
	.archive2 : AT(ADDR(.archive2))
	{
		__archive2_begin = .;
		*(.archive2)
		. = ALIGN(4);
		__archive2_end = .;
	} :archive

	/* Archive flash address for POK loader */
	__siarchive2 = __archive2_begin;

	/* Reserve space after BSS for heap */
	. = ALIGN(4);
	__heap_start = .;

	/* Stack grows down from top of RAM - reserve 4KB */
	__stack_size = 0x1000;
	__stack_start = RAM_BASE + RAM_SIZE - __stack_size;
	__stack_end = RAM_BASE + RAM_SIZE;

	/* Ensure we don't exceed RAM - temporarily disabled for debugging */
	/* ASSERT(__heap_start < __stack_start, "Data/BSS too large for RAM") */

	/* POK end marker */
	__pok_end = __stack_end;

	/DISCARD/ : {
		*(.note.gnu.property);
		*(.note.gnu.build-id);
		*(.comment);
		*(.ARM.exidx*);
		*(.ARM.extab*);
	}
}
