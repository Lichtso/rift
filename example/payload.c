#include <stdint.h>

#ifdef __x86_64__
#define EXIT __asm__("hlt");
#elif __aarch64__
#define EXIT __asm__("ldr x0, #8\nhvc #0\n.long 0x84000008");
#endif
#define EXPORT __attribute__((visibility("default")))

EXPORT __attribute__((aligned(0x4000))) uint8_t empty_pages[0x10000000UL];
EXPORT uint64_t used_memory = 0x40000000UL;
static const uint8_t rodata[4] = { 5, 6, 7, 8 };
EXPORT uint8_t data[4] = { 1, 2, 3, 4 };

#include "benchmark.h"

EXPORT void test() {
    data[1] = rodata[0];
    EXIT
}
