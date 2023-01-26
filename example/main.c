#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <rift.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Define memory layout
    struct host_to_guest_mapping host_to_guest_mapping = {
        .guest_address = 0x20000UL,
        .length = 0x4000UL,
    };
    uint64_t guest_instruction_pointer = 0x200040UL;
    uint64_t guest_stack_pointer = 0x402000UL;
    struct guest_internal_mapping guest_internal_mappings[5] = {
        {
            .virtual_address = 0x0000UL,
            .physical_address = 0,
            .flags = MAPPING_GAP,
        },
        {
            .virtual_address = 0x200000UL,
            .physical_address = host_to_guest_mapping.guest_address + 0x0000UL,
            .flags = MAPPING_READABLE | MAPPING_EXECUTABLE,
        },
        {
            .virtual_address = 0x202000UL,
            .physical_address = 0,
            .flags = MAPPING_GAP,
        },
        {
            .virtual_address = 0x400000UL,
            .physical_address = host_to_guest_mapping.guest_address + 0x2000UL,
            .flags = MAPPING_READABLE | MAPPING_WRITABLE,
        },
        {
            .virtual_address = 0x402000UL,
            .physical_address = 0,
            .flags = MAPPING_GAP,
        },
    };

    // Configure vm
    struct vm* vm = create_vm();
    struct host_to_guest_mapping page_table;
    page_table.guest_address = 0x10000UL;
    uint64_t guest_page_table_root = create_page_table(&page_table, sizeof(guest_internal_mappings) / sizeof(guest_internal_mappings[0]), guest_internal_mappings);
    map_memory_of_vm(vm, &page_table);
    host_to_guest_mapping.host_address = valloc(host_to_guest_mapping.length);
    assert(host_to_guest_mapping.host_address);
    map_memory_of_vm(vm, &host_to_guest_mapping);

    // Assemble guest machine code
#ifdef __x86_64__
    uint8_t* instructions = (uint8_t*)((uint64_t)host_to_guest_mapping.host_address + guest_instruction_pointer - guest_internal_mappings[1].virtual_address);
    instructions[0] = 0x90; // nop
    instructions[1] = 0x50; // push rax
    instructions[2] = 0xF4; // hlt
#elif __aarch64__
    uint32_t* instructions = (uint32_t*)((uint64_t)host_to_guest_mapping.host_address + guest_instruction_pointer - guest_internal_mappings[1].virtual_address);
    instructions[0] = 0xD503201F; // nop
    instructions[1] = 0xF81F8FE0; // str x0, [sp, #-8]!  // push x0
    instructions[2] = 0x58000040; // ldr x0, #8
    instructions[3] = 0xD4000002; // hvc #0
    instructions[4] = 0x84000008; // constant 0x84000008 // SYSTEM_OFF function ID
#endif

    // Configure and run vcpu
    struct vcpu* vcpu = create_vcpu(vm, guest_page_table_root);
    set_program_pointers_of_vcpu(vcpu, guest_instruction_pointer, guest_stack_pointer);
    run_vcpu(vcpu);

    // Check result
    uint64_t new_guest_stack_pointer;
    get_program_pointers_of_vcpu(vcpu, &guest_instruction_pointer, &new_guest_stack_pointer);
    assert(new_guest_stack_pointer == guest_stack_pointer - 8); // Should have decreased by 8 bytes. This proves that it ran in 64 bit mode.

    // Cleanup
    destroy_vcpu(vcpu);
    unmap_memory_of_vm(vm, &host_to_guest_mapping);
    free(host_to_guest_mapping.host_address);
    unmap_memory_of_vm(vm, &page_table);
    free(page_table.host_address);
    destroy_vm(vm);

    return 0;
}
