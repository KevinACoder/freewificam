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
    /* IRQ 0..48 */
    .rept 49
    .word Default_Handler
    .endr
    /* IRQ 49: SDIO.  The SDIO glue supplies the strong handler when it arms
     * DAT1; fault.c supplies the weak stub. */
    .word SDIO_IRQHandler
    /* IRQ 50..76 */
    .rept 27
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
