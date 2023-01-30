#define EXPORT __attribute__((visibility("default")))

static const char rodata[4] = { 1, 2, 3, 4 };
EXPORT char data[4] = { 1, 2, 3, 4 };
static char bss[0x10000UL];

void test() {
    data[1] = 5;
    bss[2] = rodata[0];
}

EXPORT void start() {
    test();
#ifdef __x86_64__
    __asm__("hlt");
#elif __aarch64__
    __asm__("ldr x0, #8\nhvc #0\n.long 0x84000008");
#endif
}
