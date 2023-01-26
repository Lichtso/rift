#pragma once

#include <stdint.h>

#if !defined(__x86_64__) && !defined(__aarch64__)
#error Unsupported OS
#endif

#if !defined(__linux__) && !defined(__APPLE__)
#error Unsupported ISA
#endif

struct host_to_guest_mapping {
    uint64_t guest_address;
    void* host_address;
    uint64_t length;
};

#define MAPPING_GAP        0
#define MAPPING_READABLE   (1 << 0)
#define MAPPING_WRITABLE   (1 << 1)
#define MAPPING_EXECUTABLE (1 << 2)
struct guest_internal_mapping {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint8_t flags;
};

struct vm;
struct vm* create_vm();
void destroy_vm(struct vm* vm);
void map_memory_of_vm(struct vm* vm, struct host_to_guest_mapping* mapping);
void unmap_memory_of_vm(struct vm* vm, struct host_to_guest_mapping* mapping);
uint64_t create_page_table(struct host_to_guest_mapping* page_table, size_t number_of_mappings, struct guest_internal_mapping mappings[number_of_mappings]);

struct vcpu;
struct vcpu* create_vcpu(struct vm* vm, uint64_t page_table_root);
void destroy_vcpu(struct vcpu* vcpu);
void set_program_pointers_of_vcpu(struct vcpu* vcpu, uint64_t instruction_pointer, uint64_t stack_pointer);
void get_program_pointers_of_vcpu(struct vcpu* vcpu, uint64_t* instruction_pointer, uint64_t* stack_pointer);
void run_vcpu(struct vcpu* vcpu);
