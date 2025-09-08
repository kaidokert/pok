/*
 * STM32F4 Linker Script for POK ARM Cortex-M port
 * Memory layout for STM32F407VG (1MB Flash, 128KB SRAM)
 */

ENTRY(Reset_Handler)

/* Memory layout */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
  SRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
  CCMSRAM (rw) : ORIGIN = 0x10000000, LENGTH = 64K  /* Core Coupled Memory for DMA-free data */
}

/* Provide memory region bounds for startup code */
PROVIDE(__SRAM_START__ = ORIGIN(SRAM));
PROVIDE(__SRAM_END__ = ORIGIN(SRAM) + LENGTH(SRAM));
PROVIDE(__SRAM_SIZE__ = LENGTH(SRAM));
PROVIDE(__FLASH_START__ = ORIGIN(FLASH));
PROVIDE(__FLASH_END__ = ORIGIN(FLASH) + LENGTH(FLASH));

/* Stack size definitions */
_stack_size = 0x1000;  /* 4KB stack - sufficient for ISR nesting, printf, context frames */

/* MSP Placement Note:
 * Stack is now correctly placed at top of SRAM for optimal memory layout.
 * This prevents heap/stack collisions by providing maximum separation. */

SECTIONS
{
  /* Vector table goes to flash - MUST start at exact Flash base address 0x08000000
   * for proper CPU boot (initial SP and reset vector fetch) */
  .isr_vector :
  {
    . = ORIGIN(FLASH);  /* Force vector table to exact Flash start */
    KEEP(*(.isr_vector))
    . = ALIGN(0x200);   /* Align after vector table for next section */
  } >FLASH

  /* Program code and constants */
  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  /* ARM exception unwinding */
  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  /* Initialized data (copied from flash to RAM at startup) */
  .data :
  {
    . = ALIGN(8);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(8);
    _edata = .;
  } >SRAM AT >FLASH

  _sidata = LOADADDR(.data);

  /* Uninitialized data (zero-initialized) */
  .bss :
  {
    . = ALIGN(8);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
    _ebss = .;

    /* FIXED: Heap base should be after BSS in SRAM, not in CCMSRAM
     * This ensures heap is DMA-accessible and in contiguous SRAM */
    _end = .;
    PROVIDE(end = .);
  } >SRAM

  /* Stack section placed at top of SRAM to avoid heap/stack collisions */
  .stack (NOLOAD) :
  {
    /* Absorb .stack section from startup.S to prevent orphan */
    KEEP(*(.stack))

    /* Stack top at end of SRAM */
    _estack = ORIGIN(SRAM) + LENGTH(SRAM);
    PROVIDE(__StackTop = _estack);
    PROVIDE(_stack_top = _estack);

    /* Reserve stack_size bytes growing down from SRAM end */
    . = ALIGN(8);
    . = _estack - _stack_size;  /* Move location counter to reserve space */
    /* Note: No additional alignment after reservation to avoid false assertion triggers */
    PROVIDE(__StackLimit = .);
    PROVIDE(_stack_bottom = .);
  } >SRAM

  /* Verify stack placement is correct */
  ASSERT(_estack == (ORIGIN(SRAM) + LENGTH(SRAM)), "Stack must end at top of SRAM")
  ASSERT((_estack - _stack_bottom) >= _stack_size, "Stack size exceeds available SRAM")

  /* CCM SRAM section - fast access memory not accessible by DMA
   * Note: CCMSRAM should NOT contain heap as it's not DMA-accessible
   * and would cause issues with memory allocators expecting contiguous SRAM */
  .ccmsram (NOLOAD) :
  {
    . = ALIGN(8);
    _sccmsram = .;
    *(.ccmsram)
    *(.ccmsram*)
    . = ALIGN(8);
    _eccmsram = .;
  } >CCMSRAM

  /* Remove specific debugging sections instead of entire libraries */
  /DISCARD/ :
  {
    *(.comment)
    *(.debug*)
    *(.note*)
    *(.eh_frame*)
  }

  /* Note: Stack overflow protection should be implemented in runtime code
   * since linker ASSERT syntax varies between toolchains */
}
