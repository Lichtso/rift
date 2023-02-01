ENTRY(start)
SECTIONS {
    .text 0x00200000 : {
        *(.text*)
        . = ALIGN(0x4000);
    }
    .rodata 0x00400000 : {
        *(.rodata*)
        . = ALIGN(0x4000);
    }
    .data 0x00600000 : {
        *(.data*)
        . = ALIGN(0x4000);
    }
    .bss : {
        *(.bss*)
        . = ALIGN(0x4000);
    }
    /DISCARD/ : {
        *(.note*)
        *(.dynamic*)
        *(.gnu*)
        *(.dynstr*)
        *(.dynsym*)
        *(.interp*)
        *(.eh_frame*)
    }
}
