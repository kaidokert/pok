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
}

/* Stack size definitions */
_stack_size = 0x400;  /* 1KB stack */

SECTIONS
{
  /* Vector table goes to flash */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
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
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >SRAM AT >FLASH
  
  _sidata = LOADADDR(.data);

  /* Uninitialized data (zero-initialized) */
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
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

  /* Heap (optional, after BSS) */
  _end = .;
  PROVIDE(end = .);
  
  /* Remove debugging sections */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
}