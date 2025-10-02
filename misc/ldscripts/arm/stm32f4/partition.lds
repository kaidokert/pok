OUTPUT_FORMAT("elf32-littlearm");
OUTPUT_ARCH("arm")
ENTRY(__pok_partition_start)

/* Memory layout for W^X security:
 * Each partition has 16KB total (0x4000 bytes)
 * Split into:
 *   - Code region (RX): 8KB at base address (actual code is ~5KB, padded for alignment)
 *   - Data region (RW): 8KB at base + 8KB (allows 1 thread with 8KB stack)
 * 8KB data region ensures proper MPU alignment (8KB boundary at offset 0x2000)
 */

MEMORY
{
	CODE (rx)  : ORIGIN = DEFINED(__PARTITION_BASE_ADDR) ? __PARTITION_BASE_ADDR : 0x0,
	             LENGTH = 8K
	DATA (rw)  : ORIGIN = DEFINED(__PARTITION_BASE_ADDR) ? __PARTITION_BASE_ADDR + 8K : 8K,
	             LENGTH = 8K
}

SECTIONS
{
	__partition_begin = ORIGIN(CODE);
	__code_start = ORIGIN(CODE);

	/* Read-only code and constants (RX permissions) */
	.text : ALIGN(4)
	{
		*(.text)
		*(.text.*)
		. = ALIGN(4);
	} > CODE

	.rodata : ALIGN(4)
	{
		*(.rodata)
		*(.rodata.*)
		. = ALIGN(4);
	} > CODE

	__code_end = .;
	__code_size = __code_end - __code_start;

	__data_start = ORIGIN(DATA);

	/* Read-write data (RW permissions, no execute) */
	.data : ALIGN(4)
	{
		*(.data)
		*(.data.*)
		. = ALIGN(4);
	} > DATA

	.bss : ALIGN(4)
	{
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(4);
	} > DATA

	__data_end = .;
	__data_size = __data_end - __data_start;
	__partition_end = .;

	/DISCARD/ : {
		*(.note.gnu.property);
		*(.note.gnu.build-id);
		*(.comment);
		*(.ARM.exidx*);
		*(.ARM.extab*);
	}
}
