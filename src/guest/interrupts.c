#include <guest.h>

#ifdef __x86_64__
__asm__(
    ".global " SYMBOL_NAME_PREFIX "break_point_isr\n"
    SYMBOL_NAME_PREFIX "break_point_isr:\n"
    "iretq\n"
);

__asm__(
    ".global " SYMBOL_NAME_PREFIX "page_fault_isr\n"
    SYMBOL_NAME_PREFIX "page_fault_isr:\n"
    "push %rdi\n"
    "push %rsi\n"
    "push %rdx\n"
    "movq 0x20(%rsp), %rdi\n"
    "movq %cr2, %rsi\n"
    "movq 0x18(%rsp), %rdx\n"
    "call " SYMBOL_NAME_PREFIX "page_fault_handler\n"
    "pop %rdx\n"
    "pop %rsi\n"
    "pop %rdi\n"
    "addq $8, %rsp\n"
    "iretq\n"
);

extern void break_point_isr();
extern void page_fault_isr();

union interrupt_gate_64 {
    struct {
        uint16_t padding0;
        uint16_t segment_selector;
        uint8_t ist;
        uint8_t type_attributes;
        uint8_t padding1[2];
        void(*handler)();
    };
    uint64_t entries[2];
};

EXPORT ALIGN(0x1000) union interrupt_gate_64 interrupt_table[512] = {
    [3] = { .type_attributes = 0x8F, .segment_selector = 8, .handler = break_point_isr },
    [14] = { .type_attributes = 0x8F, .segment_selector = 8, .handler = page_fault_isr },
    [256] = { .entries = { 0, 0x00209B0000000000UL } }, // GDT entry: code segment
    [257] = { .entries = { 0x00008B0000000FFFUL, 0 } }, // GDT entry: task state segment
};
#elif __aarch64__
__asm__(
    ".align	14\n"
    ".global " SYMBOL_NAME_PREFIX "interrupt_table\n"
    SYMBOL_NAME_PREFIX "interrupt_table:\n"
    "sub sp, sp, #0x20\n"
    "stp x0, x1, [sp, #0x00]\n"
    "stp x2, x3, [sp, #0x10]\n"
    "mrs x0, ELR_EL1\n"
    "mrs x1, FAR_EL1\n"
    "mrs x2, ESR_EL1\n"
    "lsr x3, x2, #24\n"
    "and x3, x3, #0xFC\n"
    "cmp x3, #0x94\n"
    "beq 1f\n"
    "cmp x3, #0xF0\n"
    "beq 2f\n"
    "b return_from_exception\n"
    "1:\n"
    "bl " SYMBOL_NAME_PREFIX "page_fault_handler\n"
    "b return_from_exception\n"
    "2:\n"
    "add x0, x0, 4\n"
    "msr ELR_EL1, x0\n"
    "b return_from_exception\n"
    "return_from_exception:\n"
    "ldp x0, x1, [sp, #0x00]\n"
    "ldp x2, x3, [sp, #0x10]\n"
    "add sp, sp, #0x20\n"
    "eret\n"
    ".align	7\n"
    "b " SYMBOL_NAME_PREFIX "interrupt_table\n"
    ".align	7\n"
    "b " SYMBOL_NAME_PREFIX "interrupt_table\n"
    ".align	7\n"
    "b " SYMBOL_NAME_PREFIX "interrupt_table\n"
    ".align	7\n"
    "b " SYMBOL_NAME_PREFIX "interrupt_table\n"
    ".align	7\n"
    "b " SYMBOL_NAME_PREFIX "interrupt_table\n"
    ".align	7\n"
    "b " SYMBOL_NAME_PREFIX "interrupt_table\n"
    ".align	7\n"
    "b " SYMBOL_NAME_PREFIX "interrupt_table\n"
);
#endif
