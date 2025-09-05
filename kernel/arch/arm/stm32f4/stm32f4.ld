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

/* Stack size definitions */
_stack_size = 0x1000;  /* 4KB stack - sufficient for ISR nesting, printf, context frames */

SECTIONS
{
  /* Vector table goes to flash - must be 0x200 (512) byte aligned for proper VTOR operation */
  .isr_vector :
  {
    . = ALIGN(0x200);
    KEEP(*(.isr_vector))
    . = ALIGN(8);
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
  } >SRAM

  /* Stack (grows downward from top of SRAM) */
  .stack :
  {
    . = ALIGN(8);
    . = . + _stack_size;
    . = ALIGN(8);
    _estack = .;
  } >SRAM

  /* CCM SRAM section - fast access memory not accessible by DMA */
  .ccmsram (NOLOAD) :
  {
    . = ALIGN(8);
    _sccmsram = .;
    *(.ccmsram)
    *(.ccmsram*)
    . = ALIGN(8);
    _eccmsram = .;
  } >CCMSRAM

  /* Heap (optional, after BSS) */
  _end = .;
  PROVIDE(end = .);

  /* Remove specific debugging sections instead of entire libraries */
  /DISCARD/ :
  {
    *(.comment)
    *(.debug*)
    *(.note*)
    *(.eh_frame*)
  }
}
