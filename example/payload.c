static char data[4] = { 1, 2, 3, 4 };
static char bss[0x10000UL];
static const char rodata[4] = { 1, 2, 3, 4 };

void test() {
    bss[0] = rodata[0];
    data[1] = 5;
}

void start() {
    test();
#ifdef __x86_64__
    __asm__("hlt");
#elif __aarch64__
    __asm__("ldr x0, #8\nhvc #0\n.long 0x84000008");
#endif
}
