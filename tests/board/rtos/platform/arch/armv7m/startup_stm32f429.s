.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

.global _start
.global g_pfnVectors
.global Reset_Handler

.section .isr_vector,"a",%progbits
.type g_pfnVectors, %object
_start:
g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word vPortSVCHandler
    .word DebugMon_Handler
    .word 0
    .word xPortPendSVHandler
    .word xPortSysTickHandler
    /* IRQ 0..5 */
    .rept 6
    .word Default_Handler
    .endr
    /* IRQ 6: the AP6181's out-of-band line (EXTI0).  The WICED port supplies
     * the strong handler; fault.c supplies a weak stub. */
    .word EXTI0_IRQHandler
    /* IRQ 7..48 */
    .rept 42
    .word Default_Handler
    .endr
    /* IRQ 49: SDIO controller.  Same split: the port's handler, a weak stub
     * for every image that never enables the line. */
    .word SDIO_IRQHandler
    /* IRQ 50..58 */
    .rept 9
    .word Default_Handler
    .endr
    /* IRQ 59: DMA2 stream 3, the SDIO data phase. */
    .word DMA2_Stream3_IRQHandler
    /* IRQ 60..76 */
    .rept 17
    .word Default_Handler
    .endr
    /* IRQ 77: OTG_HS.  The USB glue supplies the strong handler; fault.c
     * supplies a weak stub so a tick-only image still links. */
    .word OTG_HS_IRQHandler
    /* IRQ 78..81 */
    .rept 4
    .word Default_Handler
    .endr

.section .text.Reset_Handler,"ax",%progbits
.type Reset_Handler, %function
Reset_Handler:
    ldr r0, =_estack
    mov sp, r0

    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
copy_data:
    cmp r0, r1
    bcs copy_done
    ldr r3, [r2], #4
    str r3, [r0], #4
    b copy_data
copy_done:

    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r3, #0
zero_bss:
    cmp r0, r1
    bcs zero_done
    str r3, [r0], #4
    b zero_bss
zero_done:

    bl SystemInit
    bl main
hang:
    b hang

.size Reset_Handler, .-Reset_Handler

.section .text.Default_Handler,"ax",%progbits
.type Default_Handler, %function
Default_Handler:
    b Default_Handler
.size Default_Handler, .-Default_Handler
