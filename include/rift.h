#pragma once

#include <stdint.h>

#if !defined(__x86_64__) && !defined(__aarch64__)
#error Unsupported OS
#endif

#if !defined(__linux__) && !defined(__APPLE__)
#error Unsupported ISA
#endif

struct vm;
struct vm* create_vm();
void destroy_vm(struct vm* vm);
void map_memory_of_vm(struct vm* vm, uint64_t guest_phys_addr, uint64_t vm_mem_size, void* host_addr);
void unmap_memory_of_vm(struct vm* vm, uint64_t guest_phys_addr, uint64_t vm_mem_size);

struct vcpu;
struct vcpu* create_vcpu(struct vm* vm, uint64_t page_table);
void destroy_vcpu(struct vcpu* vcpu);
void set_program_pointers_of_vcpu(struct vcpu* vcpu, uint64_t instruction_pointer, uint64_t stack_pointer);
void get_program_pointers_of_vcpu(struct vcpu* vcpu, uint64_t* instruction_pointer, uint64_t* stack_pointer);
void create_page_table(uint8_t* vm_mem, uint64_t page_table, uint64_t code_page, uint64_t stack_page);
void run_vcpu(struct vcpu* vcpu);
