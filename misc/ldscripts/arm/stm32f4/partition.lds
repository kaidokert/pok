OUTPUT_FORMAT("elf32-littlearm");
OUTPUT_ARCH("arm")
ENTRY(__pok_partition_start)

/* Memory layout for W^X security with dead code elimination:
 * Each partition has 32KB total (0x8000 bytes)
 * Split into:
 *   - Code region (RX): 16KB at base address (sufficient after dead code elimination)
 *   - Data region (RW): 16KB at base + 16KB (for .data, .bss, thread stacks)
 * Regions aligned to MPU requirements (16KB boundary at offset 0x4000)
 *
 * IMPORTANT: These sizes must match POK_PARTITION_CODE_SIZE and
 * POK_PARTITION_DATA_SIZE defined in kernel/include/core/partition.h
 */

MEMORY
{
	CODE (rx)  : ORIGIN = DEFINED(__PARTITION_BASE_ADDR) ? __PARTITION_BASE_ADDR : 0x0,
	             LENGTH = 16K  /* POK_PARTITION_CODE_SIZE */
	DATA (rw)  : ORIGIN = DEFINED(__PARTITION_BASE_ADDR) ? __PARTITION_BASE_ADDR + 16K : 16K,
	             LENGTH = 16K  /* POK_PARTITION_DATA_SIZE */
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
