/*
 * Kiwi kernel entry: multiboot header, interrupt stubs, context switch.
 */
#define MB_MAGIC 0x1BADB002
#define MB_FLAGS 0x00000003        /* page-align modules, memory map */

    .section .multiboot, "a"
    .align 4
    .long MB_MAGIC
    .long MB_FLAGS
    .long -(MB_MAGIC + MB_FLAGS)

    .section .bss
    .align 16
boot_stack:
    .skip 16384
    .global boot_stack_top
boot_stack_top:

    .section .text
    .global _start
_start:
    cli
    mov $boot_stack_top, %esp
    xor %ebp, %ebp
    push %ebx                  /* multiboot info */
    push %eax                  /* multiboot magic */
    call kmain
1:  cli
    hlt
    jmp 1b

/* void gdt_flush(uint32_t gdtr_ptr) */
    .global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $2f
2:  mov $0x28, %ax
    ltr %ax
    ret

/* ---- interrupt stubs ---- */
.macro ISR_NOERR n
    .global isr\n
isr\n:
    push $0
    push $\n
    jmp isr_common
.endm

.macro ISR_ERR n
    .global isr\n
isr\n:
    push $\n
    jmp isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31
ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47
ISR_NOERR 128

isr_common:
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    cld
    push %esp
    call isr_dispatch
    add $4, %esp
    .global trap_return
trap_return:
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    add $8, %esp
    iret

/* void switch_context(uint32_t *save_esp, uint32_t new_esp) */
    .global switch_context
switch_context:
    mov 4(%esp), %eax
    mov 8(%esp), %edx
    push %ebp
    push %ebx
    push %esi
    push %edi
    mov %esp, (%eax)
    mov %edx, %esp
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret
