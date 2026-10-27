ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    kernel_start = .;

    .text : {
        *(.multiboot)
        *(.text .text.*)
    }
    .rodata ALIGN(4096) : { *(.rodata .rodata.*) }
    .data ALIGN(4096) : { *(.data .data.*) }
    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss .bss.*)
    }
    . = ALIGN(4096);
    kernel_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
