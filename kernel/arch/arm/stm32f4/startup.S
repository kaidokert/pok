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

/* Stack top provided by linker script - no duplicate allocation */
.extern _estack  /* Use linker-provided stack top symbol */

/* Vector table - must be power-of-two aligned for VTOR
 * STM32F4 requires minimum 128-byte alignment, relaxed from 512-byte for compatibility */
.section .isr_vector, "ax", %progbits
.balign 128
.globl vector_table
.type vector_table, %object
.global __isr_vector_start
.global __isr_vector_end
__isr_vector_start:
vector_table:
    .word   _estack
    .word   Reset_Handler
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
    .word   WWDG_IRQHandler                 /* Window WatchDog */
    .word   PVD_IRQHandler                  /* PVD through EXTI Line detection */
    .word   TAMP_STAMP_IRQHandler           /* Tamper and TimeStamps through the EXTI line */
    .word   RTC_WKUP_IRQHandler             /* RTC Wakeup through the EXTI line */
    .word   FLASH_IRQHandler                /* FLASH */
    .word   RCC_IRQHandler                  /* RCC */
    .word   EXTI0_IRQHandler                /* EXTI Line0 */
    .word   EXTI1_IRQHandler                /* EXTI Line1 */
    .word   EXTI2_IRQHandler                /* EXTI Line2 */
    .word   EXTI3_IRQHandler                /* EXTI Line3 */
    .word   EXTI4_IRQHandler                /* EXTI Line4 */
    .word   DMA1_Stream0_IRQHandler         /* DMA1 Stream 0 */
    .word   DMA1_Stream1_IRQHandler         /* DMA1 Stream 1 */
    .word   DMA1_Stream2_IRQHandler         /* DMA1 Stream 2 */
    .word   DMA1_Stream3_IRQHandler         /* DMA1 Stream 3 */
    .word   DMA1_Stream4_IRQHandler         /* DMA1 Stream 4 */
    .word   DMA1_Stream5_IRQHandler         /* DMA1 Stream 5 */
    .word   DMA1_Stream6_IRQHandler         /* DMA1 Stream 6 */
    .word   ADC_IRQHandler                  /* ADC1, ADC2 and ADC3s */
    .word   CAN1_TX_IRQHandler              /* CAN1 TX */
    .word   CAN1_RX0_IRQHandler             /* CAN1 RX0 */
    .word   CAN1_RX1_IRQHandler             /* CAN1 RX1 */
    .word   CAN1_SCE_IRQHandler             /* CAN1 SCE */
    .word   EXTI9_5_IRQHandler              /* External Line[9:5]s */
    .word   TIM1_BRK_TIM9_IRQHandler        /* TIM1 Break and TIM9 */
    .word   TIM1_UP_TIM10_IRQHandler        /* TIM1 Update and TIM10 */
    .word   TIM1_TRG_COM_TIM11_IRQHandler   /* TIM1 Trigger and Commutation and TIM11 */
    .word   TIM1_CC_IRQHandler              /* TIM1 Capture Compare */
    .word   TIM2_IRQHandler                 /* TIM2 */
    .word   TIM3_IRQHandler                 /* TIM3 */
    .word   TIM4_IRQHandler                 /* TIM4 */
    .word   I2C1_EV_IRQHandler              /* I2C1 Event */
    .word   I2C1_ER_IRQHandler              /* I2C1 Error */
    .word   I2C2_EV_IRQHandler              /* I2C2 Event */
    .word   I2C2_ER_IRQHandler              /* I2C2 Error */
    .word   SPI1_IRQHandler                 /* SPI1 */
    .word   SPI2_IRQHandler                 /* SPI2 */
    .word   USART1_IRQHandler               /* USART1 */
    .word   USART2_IRQHandler               /* USART2 */
    .word   USART3_IRQHandler               /* USART3 */
    .word   EXTI15_10_IRQHandler            /* External Line[15:10]s */
    .word   RTC_Alarm_IRQHandler            /* RTC Alarm (A and B) through EXTI Line */
    .word   OTG_FS_WKUP_IRQHandler          /* USB OTG FS Wakeup through EXTI line */
    .word   TIM8_BRK_TIM12_IRQHandler       /* TIM8 Break and TIM12 */
    .word   TIM8_UP_TIM13_IRQHandler        /* TIM8 Update and TIM13 */
    .word   TIM8_TRG_COM_TIM14_IRQHandler   /* TIM8 Trigger and Commutation and TIM14 */
    .word   TIM8_CC_IRQHandler              /* TIM8 Capture Compare */
    .word   DMA1_Stream7_IRQHandler         /* DMA1 Stream7 */
    .word   FSMC_IRQHandler                 /* FSMC */
    .word   SDIO_IRQHandler                 /* SDIO */
    .word   TIM5_IRQHandler                 /* TIM5 */
    .word   SPI3_IRQHandler                 /* SPI3 */
    .word   UART4_IRQHandler                /* UART4 */
    .word   UART5_IRQHandler                /* UART5 */
    .word   TIM6_DAC_IRQHandler             /* TIM6 and DAC1&2 underrun errors */
    .word   TIM7_IRQHandler                 /* TIM7 */
    .word   DMA2_Stream0_IRQHandler         /* DMA2 Stream 0 */
    .word   DMA2_Stream1_IRQHandler         /* DMA2 Stream 1 */
    .word   DMA2_Stream2_IRQHandler         /* DMA2 Stream 2 */
    .word   DMA2_Stream3_IRQHandler         /* DMA2 Stream 3 */
    .word   DMA2_Stream4_IRQHandler         /* DMA2 Stream 4 */
    .word   ETH_IRQHandler                  /* Ethernet */
    .word   ETH_WKUP_IRQHandler             /* Ethernet Wakeup through EXTI line */
    .word   CAN2_TX_IRQHandler              /* CAN2 TX */
    .word   CAN2_RX0_IRQHandler             /* CAN2 RX0 */
    .word   CAN2_RX1_IRQHandler             /* CAN2 RX1 */
    .word   CAN2_SCE_IRQHandler             /* CAN2 SCE */
    .word   OTG_FS_IRQHandler               /* USB OTG FS */
    .word   DMA2_Stream5_IRQHandler         /* DMA2 Stream 5 */
    .word   DMA2_Stream6_IRQHandler         /* DMA2 Stream 6 */
    .word   DMA2_Stream7_IRQHandler         /* DMA2 Stream 7 */
    .word   USART6_IRQHandler               /* USART6 */
    .word   I2C3_EV_IRQHandler              /* I2C3 event */
    .word   I2C3_ER_IRQHandler              /* I2C3 error */
    .word   OTG_HS_EP1_OUT_IRQHandler       /* USB OTG HS End Point 1 Out */
    .word   OTG_HS_EP1_IN_IRQHandler        /* USB OTG HS End Point 1 In */
    .word   OTG_HS_WKUP_IRQHandler          /* USB OTG HS Wakeup through EXTI */
    .word   OTG_HS_IRQHandler               /* USB OTG HS */
    .word   DCMI_IRQHandler                 /* DCMI */
    .word   CRYP_IRQHandler                 /* CRYP crypto */
    .word   HASH_RNG_IRQHandler             /* Hash and Rng */
    .word   FPU_IRQHandler                  /* FPU */
    .word   UART7_IRQHandler                /* UART7 */
    .word   UART8_IRQHandler                /* UART8 */
    .word   SPI4_IRQHandler                 /* SPI4 */
    .word   SPI5_IRQHandler                 /* SPI5 */
    .word   SPI6_IRQHandler                 /* SPI6 */
    .word   SAI1_IRQHandler                 /* SAI1 */
    .word   LTDC_IRQHandler                 /* LTDC */
    .word   LTDC_ER_IRQHandler              /* LTDC error */
    .word   DMA2D_IRQHandler
__isr_vector_end:
.size vector_table, __isr_vector_end - vector_table

/* Compile-time verification: ensure vector table size matches expected count
 * STM32F4 has 16 system vectors + 91 external IRQs = 107 total vectors
 * Each vector is 4 bytes, so total size should be 428 bytes */
.if (__isr_vector_end - __isr_vector_start) != (107 * 4)
.error "Vector table size mismatch: expected 107 vectors (428 bytes)"
.endif

.text
.thumb
.align 2

/* Reset handler */
.globl Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    /* Disable interrupts during initialization */
    cpsid i

    /* Set stack pointer with validation */
    ldr r0, =_estack
    /* Validate stack pointer is properly aligned (8-byte alignment) */
    and r1, r0, #7
    cmp r1, #0
    bne stack_error           /* Stack not 8-byte aligned */
    /* Validate stack is in valid memory range (basic check) */
    ldr r1, =__SRAM_START__   /* SRAM base from linker */
    cmp r0, r1
    blo stack_error           /* Stack below SRAM */
    ldr r1, =__SRAM_END__     /* SRAM end from linker */
    cmp r0, r1
    bhi stack_error           /* Stack above SRAM */
    mov sp, r0

    /* Enhanced vector table setup with validation */
    ldr r0, =vector_table
    /* Validate vector table alignment (must be power-of-2, >= table size)
     * STM32F4 requires minimum 128-byte alignment */
    mov r1, #127             /* 0x7F = 127 */
    and r1, r0, r1           /* Check 128-byte alignment */
    cmp r1, #0
    bne vector_error         /* Vector table not 128-byte aligned */

    /* Validate vector table is in a valid memory region (Flash or SRAM) */
    ldr r1, =__FLASH_START__
    ldr r2, =__FLASH_END__
    cmp r0, r1
    blo 1f                    /* below flash -> check sram */
    cmp r0, r2
    blo 2f                    /* in flash */
1:  ldr r1, =__SRAM_START__
    ldr r2, =__SRAM_END__
    cmp r0, r1
    blo vector_error
    cmp r0, r2
    bhs vector_error
2:

    /* Set VTOR with memory barriers */
    ldr r1, =0xE000ED08      /* SCB->VTOR */
    str r0, [r1]
    dsb                      /* Data Synchronization Barrier */
    isb                      /* Instruction Synchronization Barrier */

    /* Verify VTOR was set correctly */
    ldr r2, [r1]
    cmp r0, r2
    bne vector_error         /* VTOR not set correctly */

    /* Initialize data section with bounds checking */
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
    /* Validate data section pointers */
    cmp r0, r1
    bhi data_error           /* sdata > edata (invalid) */
    push {r3-r5, lr}         /* Preserve registers clobbered by copy_data_safe */
    bl  copy_data_safe
    pop {r3-r5, lr}

    /* Initialize BSS section with bounds checking */
    ldr r0, =_sbss
    ldr r1, =_ebss
    /* Validate BSS section pointers */
    cmp r0, r1
    bhi bss_error            /* sbss > ebss (invalid) */
    bl  clear_bss_safe

    /* Re-enable interrupts before calling kernel */
    cpsie i

    /* Call main POK kernel initialization */
    bl  pok_boot

    /* Should never reach here */
hang:
    b   hang

/* Error handlers for initialization failures */
stack_error:
    /* Stack initialization failed - use basic infinite loop */
    b   stack_error

vector_error:
    /* Vector table setup failed - halt system */
    b   vector_error

data_error:
    /* Data section invalid - halt system */
    b   data_error

bss_error:
    /* BSS section invalid - halt system */
    b   bss_error

/* Safe copy data section from flash to RAM with bounds checking */
copy_data_safe:
    /* Validate memory regions before copying */
    cmp r0, r1            /* Compare start and end */
    bge copy_data_done    /* If start >= end, skip */

    /* Check for reasonable size limit (prevent runaway) */
    sub r3, r1, r0        /* Calculate size */
    ldr r4, =__SRAM_SIZE__ /* Use SRAM size from linker script */
    cmp r3, r4
    bhi data_error        /* Data section too large */

    /* Validate RAM destination is in SRAM */
    ldr r4, =__SRAM_START__ /* SRAM base from linker script */
    cmp r0, r4
    blo data_error        /* Below SRAM */
    ldr r4, =__SRAM_END__   /* SRAM end from linker script */
    cmp r1, r4
    bhi data_error        /* Above SRAM */

    /* Validate Flash source is in Flash */
    ldr r4, =__FLASH_START__ /* Flash base from linker script */
    cmp r2, r4
    blo data_error        /* Below Flash */
    /* Check both Flash source start and end to prevent overread */
    add r5, r2, r3        /* r5 = Flash source end (start + size) */
    ldr r4, =__FLASH_END__   /* Flash end from linker script */
    cmp r2, r4
    bhi data_error        /* Flash source start above Flash end */
    cmp r5, r4
    bhi data_error        /* Flash source end would exceed Flash end */

copy_data_loop:
    /* Calculate remaining bytes */
    subs r3, r1, r0       /* r3 = bytes remaining */
    cmp r3, #4            /* Can we copy a full word? */
    blt copy_tail_bytes   /* Less than 4 bytes, copy byte-wise */

    /* Copy word */
    ldr r4, [r2]          /* Load word from flash */
    str r4, [r0]          /* Store word to RAM */
    adds r0, r0, #4       /* Increment RAM pointer */
    adds r2, r2, #4       /* Increment flash pointer */
    b copy_data_loop      /* Continue loop */

copy_tail_bytes:
    /* Handle remaining 1-3 bytes */
    cmp r0, r1            /* Check if we're done */
    bge copy_data_done    /* Yes, we're done */

    ldrb r4, [r2]         /* Load byte from flash */
    strb r4, [r0]         /* Store byte to RAM */
    adds r0, r0, #1       /* Increment RAM pointer */
    adds r2, r2, #1       /* Increment flash pointer */
    b copy_tail_bytes     /* Continue byte copy */

copy_data_done:
    bx lr

/* Safe clear BSS section with bounds checking */
clear_bss_safe:
    movs r2, #0
    cmp r0, r1
    bge clear_bss_done
    /* Check for reasonable size limit (prevent runaway) */
    sub r3, r1, r0        /* Calculate size */
    ldr r4, =__SRAM_SIZE__ /* SRAM size limit for BSS section */
    cmp r3, r4
    bhi bss_error         /* BSS section too large */

    /* Validate RAM destination is in SRAM */
    ldr r4, =__SRAM_START__ /* SRAM base from linker script */
    cmp r0, r4
    blo bss_error         /* Below SRAM */
    ldr r4, =__SRAM_END__   /* SRAM end from linker script */
    cmp r1, r4
    bhi bss_error         /* Above SRAM */

    /* align start to 4-byte boundary */
    movs r3, #3
    ands r3, r0, r3
    beq 1f
0:
    strb r2, [r0]
    adds r0, r0, #1
    ands r3, r0, #3
    bne 0b
1:
    /* word clear */
    sub r3, r1, r0
    ands r3, r3, #3
    subs r4, r1, r3       /* r4 = word end */
2:
    cmp r0, r4
    bge 3f
    str r2, [r0]
    adds r0, r0, #4
    b 2b
3:
    /* tail bytes */
    cmp r0, r1
    bge clear_bss_done
4:
    strb r2, [r0]
    adds r0, r0, #1
    cmp r0, r1
    blt 4b
clear_bss_done:
    bx lr

/* Legacy functions kept for compatibility */
copy_data:
    b copy_data_safe

clear_bss:
    b clear_bss_safe

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

/* External interrupt handlers - weak symbols can be overridden */
.weak WWDG_IRQHandler
.thumb_set WWDG_IRQHandler, Default_Handler
.weak PVD_IRQHandler
.thumb_set PVD_IRQHandler, Default_Handler
.weak TAMP_STAMP_IRQHandler
.thumb_set TAMP_STAMP_IRQHandler, Default_Handler
.weak RTC_WKUP_IRQHandler
.thumb_set RTC_WKUP_IRQHandler, Default_Handler
.weak FLASH_IRQHandler
.thumb_set FLASH_IRQHandler, Default_Handler
.weak RCC_IRQHandler
.thumb_set RCC_IRQHandler, Default_Handler
.weak EXTI0_IRQHandler
.thumb_set EXTI0_IRQHandler, Default_Handler
.weak EXTI1_IRQHandler
.thumb_set EXTI1_IRQHandler, Default_Handler
.weak EXTI2_IRQHandler
.thumb_set EXTI2_IRQHandler, Default_Handler
.weak EXTI3_IRQHandler
.thumb_set EXTI3_IRQHandler, Default_Handler
.weak EXTI4_IRQHandler
.thumb_set EXTI4_IRQHandler, Default_Handler
.weak DMA1_Stream0_IRQHandler
.thumb_set DMA1_Stream0_IRQHandler, Default_Handler
.weak DMA1_Stream1_IRQHandler
.thumb_set DMA1_Stream1_IRQHandler, Default_Handler
.weak DMA1_Stream2_IRQHandler
.thumb_set DMA1_Stream2_IRQHandler, Default_Handler
.weak DMA1_Stream3_IRQHandler
.thumb_set DMA1_Stream3_IRQHandler, Default_Handler
.weak DMA1_Stream4_IRQHandler
.thumb_set DMA1_Stream4_IRQHandler, Default_Handler
.weak DMA1_Stream5_IRQHandler
.thumb_set DMA1_Stream5_IRQHandler, Default_Handler
.weak DMA1_Stream6_IRQHandler
.thumb_set DMA1_Stream6_IRQHandler, Default_Handler
.weak ADC_IRQHandler
.thumb_set ADC_IRQHandler, Default_Handler
.weak CAN1_TX_IRQHandler
.thumb_set CAN1_TX_IRQHandler, Default_Handler
.weak CAN1_RX0_IRQHandler
.thumb_set CAN1_RX0_IRQHandler, Default_Handler
.weak CAN1_RX1_IRQHandler
.thumb_set CAN1_RX1_IRQHandler, Default_Handler
.weak CAN1_SCE_IRQHandler
.thumb_set CAN1_SCE_IRQHandler, Default_Handler
.weak EXTI9_5_IRQHandler
.thumb_set EXTI9_5_IRQHandler, Default_Handler
.weak TIM1_BRK_TIM9_IRQHandler
.thumb_set TIM1_BRK_TIM9_IRQHandler, Default_Handler
.weak TIM1_UP_TIM10_IRQHandler
.thumb_set TIM1_UP_TIM10_IRQHandler, Default_Handler
.weak TIM1_TRG_COM_TIM11_IRQHandler
.thumb_set TIM1_TRG_COM_TIM11_IRQHandler, Default_Handler
.weak TIM1_CC_IRQHandler
.thumb_set TIM1_CC_IRQHandler, Default_Handler
.weak TIM2_IRQHandler
.thumb_set TIM2_IRQHandler, Default_Handler
.weak TIM3_IRQHandler
.thumb_set TIM3_IRQHandler, Default_Handler
.weak TIM4_IRQHandler
.thumb_set TIM4_IRQHandler, Default_Handler
.weak I2C1_EV_IRQHandler
.thumb_set I2C1_EV_IRQHandler, Default_Handler
.weak I2C1_ER_IRQHandler
.thumb_set I2C1_ER_IRQHandler, Default_Handler
.weak I2C2_EV_IRQHandler
.thumb_set I2C2_EV_IRQHandler, Default_Handler
.weak I2C2_ER_IRQHandler
.thumb_set I2C2_ER_IRQHandler, Default_Handler
.weak SPI1_IRQHandler
.thumb_set SPI1_IRQHandler, Default_Handler
.weak SPI2_IRQHandler
.thumb_set SPI2_IRQHandler, Default_Handler
.weak USART1_IRQHandler
.thumb_set USART1_IRQHandler, Default_Handler
.weak USART2_IRQHandler
.thumb_set USART2_IRQHandler, Default_Handler
.weak USART3_IRQHandler
.thumb_set USART3_IRQHandler, Default_Handler
.weak EXTI15_10_IRQHandler
.thumb_set EXTI15_10_IRQHandler, Default_Handler
.weak RTC_Alarm_IRQHandler
.thumb_set RTC_Alarm_IRQHandler, Default_Handler
.weak OTG_FS_WKUP_IRQHandler
.thumb_set OTG_FS_WKUP_IRQHandler, Default_Handler
.weak TIM8_BRK_TIM12_IRQHandler
.thumb_set TIM8_BRK_TIM12_IRQHandler, Default_Handler
.weak TIM8_UP_TIM13_IRQHandler
.thumb_set TIM8_UP_TIM13_IRQHandler, Default_Handler
.weak TIM8_TRG_COM_TIM14_IRQHandler
.thumb_set TIM8_TRG_COM_TIM14_IRQHandler, Default_Handler
.weak TIM8_CC_IRQHandler
.thumb_set TIM8_CC_IRQHandler, Default_Handler
.weak DMA1_Stream7_IRQHandler
.thumb_set DMA1_Stream7_IRQHandler, Default_Handler
.weak FSMC_IRQHandler
.thumb_set FSMC_IRQHandler, Default_Handler
.weak SDIO_IRQHandler
.thumb_set SDIO_IRQHandler, Default_Handler
.weak TIM5_IRQHandler
.thumb_set TIM5_IRQHandler, Default_Handler
.weak SPI3_IRQHandler
.thumb_set SPI3_IRQHandler, Default_Handler
.weak UART4_IRQHandler
.thumb_set UART4_IRQHandler, Default_Handler
.weak UART5_IRQHandler
.thumb_set UART5_IRQHandler, Default_Handler
.weak TIM6_DAC_IRQHandler
.thumb_set TIM6_DAC_IRQHandler, Default_Handler
.weak TIM7_IRQHandler
.thumb_set TIM7_IRQHandler, Default_Handler
.weak DMA2_Stream0_IRQHandler
.thumb_set DMA2_Stream0_IRQHandler, Default_Handler
.weak DMA2_Stream1_IRQHandler
.thumb_set DMA2_Stream1_IRQHandler, Default_Handler
.weak DMA2_Stream2_IRQHandler
.thumb_set DMA2_Stream2_IRQHandler, Default_Handler
.weak DMA2_Stream3_IRQHandler
.thumb_set DMA2_Stream3_IRQHandler, Default_Handler
.weak DMA2_Stream4_IRQHandler
.thumb_set DMA2_Stream4_IRQHandler, Default_Handler
.weak ETH_IRQHandler
.thumb_set ETH_IRQHandler, Default_Handler
.weak ETH_WKUP_IRQHandler
.thumb_set ETH_WKUP_IRQHandler, Default_Handler
.weak CAN2_TX_IRQHandler
.thumb_set CAN2_TX_IRQHandler, Default_Handler
.weak CAN2_RX0_IRQHandler
.thumb_set CAN2_RX0_IRQHandler, Default_Handler
.weak CAN2_RX1_IRQHandler
.thumb_set CAN2_RX1_IRQHandler, Default_Handler
.weak CAN2_SCE_IRQHandler
.thumb_set CAN2_SCE_IRQHandler, Default_Handler
.weak OTG_FS_IRQHandler
.thumb_set OTG_FS_IRQHandler, Default_Handler
.weak DMA2_Stream5_IRQHandler
.thumb_set DMA2_Stream5_IRQHandler, Default_Handler
.weak DMA2_Stream6_IRQHandler
.thumb_set DMA2_Stream6_IRQHandler, Default_Handler
.weak DMA2_Stream7_IRQHandler
.thumb_set DMA2_Stream7_IRQHandler, Default_Handler
.weak USART6_IRQHandler
.thumb_set USART6_IRQHandler, Default_Handler
.weak I2C3_EV_IRQHandler
.thumb_set I2C3_EV_IRQHandler, Default_Handler
.weak I2C3_ER_IRQHandler
.thumb_set I2C3_ER_IRQHandler, Default_Handler
.weak OTG_HS_EP1_OUT_IRQHandler
.thumb_set OTG_HS_EP1_OUT_IRQHandler, Default_Handler
.weak OTG_HS_EP1_IN_IRQHandler
.thumb_set OTG_HS_EP1_IN_IRQHandler, Default_Handler
.weak OTG_HS_WKUP_IRQHandler
.thumb_set OTG_HS_WKUP_IRQHandler, Default_Handler
.weak OTG_HS_IRQHandler
.thumb_set OTG_HS_IRQHandler, Default_Handler
.weak DCMI_IRQHandler
.thumb_set DCMI_IRQHandler, Default_Handler
.weak CRYP_IRQHandler
.thumb_set CRYP_IRQHandler, Default_Handler
.weak HASH_RNG_IRQHandler
.thumb_set HASH_RNG_IRQHandler, Default_Handler
.weak FPU_IRQHandler
.thumb_set FPU_IRQHandler, Default_Handler
.weak UART7_IRQHandler
.thumb_set UART7_IRQHandler, Default_Handler
.weak UART8_IRQHandler
.thumb_set UART8_IRQHandler, Default_Handler
.weak SPI4_IRQHandler
.thumb_set SPI4_IRQHandler, Default_Handler
.weak SPI5_IRQHandler
.thumb_set SPI5_IRQHandler, Default_Handler
.weak SPI6_IRQHandler
.thumb_set SPI6_IRQHandler, Default_Handler
.weak SAI1_IRQHandler
.thumb_set SAI1_IRQHandler, Default_Handler
.weak LTDC_IRQHandler
.thumb_set LTDC_IRQHandler, Default_Handler
.weak LTDC_ER_IRQHandler
.thumb_set LTDC_ER_IRQHandler, Default_Handler
.weak DMA2D_IRQHandler
.thumb_set DMA2D_IRQHandler, Default_Handler

.end
