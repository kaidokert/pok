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

/**
 * \file    arch/arm/stm32f4/startup.S
 * \author  POK team
 * \brief   STM32F4 startup code and vector table
 */

.syntax unified
.cpu cortex-m4
.thumb

/* Stack top (end of RAM) */
.equ    Stack_Size, 0x1000
.section ".stack"
stack_mem:
.space  Stack_Size
.equ    __StackTop, stack_mem + Stack_Size

/* Vector table */
.section ".isr_vector", "a"
.align 2
.globl vector_table
vector_table:
    .word   __StackTop                  /* Initial Stack Pointer */
    .word   Reset_Handler               /* Reset Handler */
    .word   NMI_Handler                 /* NMI Handler */
    .word   HardFault_Handler           /* Hard Fault Handler */
    .word   MemManage_Handler           /* Memory Management Fault Handler */
    .word   BusFault_Handler            /* Bus Fault Handler */
    .word   UsageFault_Handler          /* Usage Fault Handler */
    .word   0                           /* Reserved */
    .word   0                           /* Reserved */
    .word   0                           /* Reserved */
    .word   0                           /* Reserved */
    .word   SVC_Handler                 /* SVCall Handler */
    .word   DebugMon_Handler            /* Debug Monitor Handler */
    .word   0                           /* Reserved */
    .word   PendSV_Handler              /* PendSV Handler */
    .word   SysTick_Handler             /* SysTick Handler */

    /* External interrupts (STM32F4 specific) */
    .rept   82
    .word   Default_Handler             /* External interrupt handlers */
    .endr

.text
.thumb
.align 2

/* Reset handler */
.globl Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    /* Set stack pointer */
    ldr r0, =__StackTop
    mov sp, r0

    /* Initialize data section */
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
    bl  copy_data

    /* Initialize BSS section */
    ldr r0, =_sbss
    ldr r1, =_ebss
    bl  clear_bss

    /* Call main POK kernel initialization */
    bl  pok_boot

    /* Should never reach here */
hang:
    b   hang

/* Copy data section from flash to RAM */
copy_data:
    cmp r0, r1            /* Compare start and end */
    bge copy_data_done    /* If start >= end, skip */
copy_data_loop:
    ldr r3, [r2]          /* Load from flash */
    str r3, [r0]          /* Store to RAM */
    adds r0, r0, #4       /* Increment RAM pointer */
    adds r2, r2, #4       /* Increment flash pointer */
    cmp r0, r1            /* Compare with end address */
    blt copy_data_loop    /* Continue if not at end */
copy_data_done:
    bx lr

/* Clear BSS section */
clear_bss:
    movs r2, #0           /* Clear value */
    cmp r0, r1            /* Compare start and end */
    bge clear_bss_done    /* If start >= end, skip */
clear_bss_loop:
    str r2, [r0]          /* Clear word */
    adds r0, r0, #4       /* Increment pointer */
    cmp r0, r1            /* Compare with end address */
    blt clear_bss_loop    /* Continue if not at end */
clear_bss_done:
    bx lr

/* Default handler for unused interrupts */
.globl Default_Handler
.type Default_Handler, %function
Default_Handler:
    b Default_Handler

/* Weak aliases for exception handlers */
.weak NMI_Handler
.thumb_set NMI_Handler, Default_Handler

.weak HardFault_Handler
.thumb_set HardFault_Handler, Default_Handler

.weak MemManage_Handler
.thumb_set MemManage_Handler, Default_Handler

.weak BusFault_Handler
.thumb_set BusFault_Handler, Default_Handler

.weak UsageFault_Handler
.thumb_set UsageFault_Handler, Default_Handler

.weak SVC_Handler
.thumb_set SVC_Handler, Default_Handler

.weak DebugMon_Handler
.thumb_set DebugMon_Handler, Default_Handler

.weak PendSV_Handler
.thumb_set PendSV_Handler, Default_Handler

.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler

.end
