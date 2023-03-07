#include <guest.h>

EXPORT ALIGN(0x4000) uint8_t empty_pages[0x10000000UL];
EXPORT uint64_t used_memory = 0x40000000UL;

#include "benchmark.h"

void page_fault_handler(uint64_t fault_instruction_pointer, uint64_t fault_access_address, uint64_t fault_code) {
    (void)fault_instruction_pointer;
    (void)fault_code;
    used_memory = fault_access_address;
    EXIT
}

EXPORT void test() {
    ((uint8_t*)0xDEADBEEF)[0] = 0;
    EXIT
}
