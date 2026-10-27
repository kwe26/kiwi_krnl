/* user program entry: [esp] = argc, argv, envp */
    .section .text
    .global _start
_start:
    xor %ebp, %ebp
    mov (%esp), %eax
    mov 4(%esp), %ecx
    mov 8(%esp), %edx
    and $-16, %esp
    sub $4, %esp
    push %edx
    push %ecx
    push %eax
    call __libc_main
1:  jmp 1b
