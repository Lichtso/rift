ENTRY(start)
MEMORY {
    TEXT (rx)      : ORIGIN = 0x00200000, LENGTH = 16K
    RODATA (r)     : ORIGIN = 0x00400000, LENGTH = 16K
    DATA (rw)      : ORIGIN = 0x00600000, LENGTH = 80K
}
SECTIONS {
    .text : {
        *(.text*)
        . = ALIGN(0x4000);
    } > TEXT
    .rodata : {
        *(.rodata*)
        . = ALIGN(0x4000);
    } > RODATA
    .data : {
        *(.data*)
        . = ALIGN(0x4000);
    } > DATA
    .bss : {
        *(.bss*)
        . = ALIGN(0x4000);
    } > DATA
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
