#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <rift.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Define memory layout
    uint64_t page_table = 0x0000UL;
    uint64_t code_page = 0x4000UL;
    uint64_t stack_page = 0x5000UL;
    uint64_t vm_mem_size = 0x8000UL;
    uint64_t guest_instruction_pointer = 0x0040UL;
    uint64_t guest_stack_pointer = 0x2000UL;

    // Allocate page aligned memory for the virtual machine
    uint8_t* vm_mem = valloc(vm_mem_size);
    assert(vm_mem);
    create_page_table(vm_mem, page_table, code_page, stack_page);

    // Assemble guest machine code
#ifdef __x86_64__
    uint8_t* instructions = vm_mem + code_page + guest_instruction_pointer;
    instructions[0] = 0x90; // nop
    instructions[1] = 0x50; // push rax
    instructions[2] = 0xF4; // hlt
#elif __aarch64__
    uint32_t* instructions = (uint32_t*)(vm_mem + code_page + guest_instruction_pointer);
    instructions[0] = 0xD503201F; // nop
    instructions[1] = 0xF81F8FE0; // str x0, [sp, #-8]!  // push x0
    instructions[2] = 0x58000040; // ldr x0, #8
    instructions[3] = 0xD4000002; // hvc #0
    instructions[4] = 0x84000008; // constant 0x84000008 // SYSTEM_OFF function ID
#endif

    // Configure vm and vcpu
    struct vm* vm = create_vm();
    map_memory_of_vm(vm, 0, vm_mem_size, vm_mem);
    struct vcpu* vcpu = create_vcpu(vm, page_table);
    set_program_pointers_of_vcpu(vcpu, guest_instruction_pointer, guest_stack_pointer);

    // Run
    run_vcpu(vcpu);

    // Check result
    uint64_t new_guest_stack_pointer;
    get_program_pointers_of_vcpu(vcpu, &guest_instruction_pointer, &new_guest_stack_pointer);
    assert(new_guest_stack_pointer == guest_stack_pointer - 8); // Should have decreased by 8 bytes. This proves that it ran in 64 bit mode.

    // Cleanup
    destroy_vcpu(vcpu);
    unmap_memory_of_vm(vm, 0, vm_mem_size);
    destroy_vm(vm);
    free(vm_mem);

    printf("Success\n");
    return 0;
}
