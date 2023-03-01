#pragma once

#include <stdint.h>
#include <stdbool.h>

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

struct vm* create_vm();
void destroy_vm(struct vm* vm);
void map_memory_of_vm(struct vm* vm, struct host_to_guest_mapping* mapping);
void unmap_memory_of_vm(struct vm* vm, struct host_to_guest_mapping* mapping);
bool resolve_address_of_vm(struct vm* vm, uint64_t guest_address, void** host_address, uint64_t length);
void create_page_table(struct host_to_guest_mapping* page_table, uint64_t number_of_mappings, struct guest_internal_mapping mappings[number_of_mappings]);
bool resolve_address_using_page_table(struct host_to_guest_mapping* page_table, bool write, uint64_t virtual_address, uint64_t* physical_address);

struct vcpu* create_vcpu(struct vm* vm, struct host_to_guest_mapping* page_table, uint64_t interrupt_table_pointer);
void destroy_vcpu(struct vcpu* vcpu);
struct host_to_guest_mapping* get_page_table_of_vcpu(struct vcpu* vcpu);
uint64_t get_register_of_vcpu(struct vcpu* vcpu, uint64_t register_index);
void set_register_of_vcpu(struct vcpu* vcpu, uint64_t register_index, uint64_t value);
void run_vcpu(struct vcpu* vcpu);

struct loaded_object* create_loaded_object(struct vm* vm, const char* path);
void destroy_loaded_object(struct loaded_object* loaded_object);
bool resolve_symbol_virtual_address_in_loaded_object(struct loaded_object* loaded_object, const char* symbol_name, uint64_t* virtual_address);
bool resolve_symbol_host_address_in_loaded_object(struct loaded_object* loaded_object, bool write, const char* symbol_name, uint64_t length, void** host_address);
struct vcpu* create_vcpu_for_loaded_object(struct loaded_object* loaded_object, const char* interrupt_table, const char* entry_point);

struct debugger_server* create_debugger_server(uint64_t number_of_vcpus, struct vcpu* vcpus[number_of_vcpus], uint16_t port, bool localhost_only);
void destroy_debugger_server(struct debugger_server* debugger);
void run_debugger_server(struct debugger_server* debugger);
