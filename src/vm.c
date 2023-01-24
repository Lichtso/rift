#include "platform.h"

#ifdef __linux__
void vm_ctl(struct vm* vm, uint32_t request, uint64_t param) {
    assert(ioctl(vm->fd, request, param) >= 0);
}
#endif

struct vm* create_vm() {
    struct vm* vm = malloc(sizeof(struct vm));
#ifdef __linux__
    vm->kvm_fd = open("/dev/kvm", O_RDWR);
    assert(vm->kvm_fd >= 0);
    int api_ver = ioctl(vm->kvm_fd, KVM_GET_API_VERSION, 0);
    assert(api_ver == KVM_API_VERSION);
    vm->fd = ioctl(vm->kvm_fd, KVM_CREATE_VM, 0);
    assert(vm->fd >= 0);
    vm_ctl(vm, KVM_CHECK_EXTENSION, KVM_CAP_IMMEDIATE_EXIT);
    vm_ctl(vm, KVM_CHECK_EXTENSION, KVM_CAP_USER_MEMORY);
#ifdef __aarch64__
    vm_ctl(vm, KVM_CHECK_EXTENSION, KVM_CAP_ONE_REG);
    vm_ctl(vm, KVM_CHECK_EXTENSION, KVM_CAP_ARM_PSCI_0_2);
#endif
#elif __APPLE__
    vm->initialized = 1;
#ifdef __x86_64__
    assert(hv_vm_create(HV_VM_DEFAULT) == 0);
#elif __aarch64__
    assert(hv_vm_create(NULL) == 0);
#endif
#endif
    return vm;
}

void destroy_vm(struct vm* vm) {
#ifdef __linux__
    assert(close(vm->fd) >= 0);
    assert(close(vm->kvm_fd) >= 0);
#elif __APPLE__
    assert(vm->initialized);
    assert(hv_vm_destroy() == 0);
#endif
}

void map_memory_of_vm(struct vm* vm, uint64_t guest_phys_addr, uint64_t vm_mem_size, void* host_addr) {
#ifdef __linux__
    struct kvm_userspace_memory_region memreg;
    memreg.slot = 0;
    memreg.flags = 0;
    memreg.guest_phys_addr = guest_phys_addr;
    memreg.memory_size = vm_mem_size;
    memreg.userspace_addr = (uint64_t)host_addr;
    vm_ctl(vm, KVM_SET_USER_MEMORY_REGION, (uint64_t)&memreg);
#elif __APPLE__
    assert(vm->initialized);
    assert(hv_vm_map(host_addr, guest_phys_addr, vm_mem_size, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC) == 0);
#endif
}

void unmap_memory_of_vm(struct vm* vm, uint64_t guest_phys_addr, uint64_t vm_mem_size) {
#ifdef __linux__
    (void)vm_mem_size;
    struct kvm_userspace_memory_region memreg;
    memreg.slot = 0;
    memreg.flags = 0;
    memreg.guest_phys_addr = guest_phys_addr;
    memreg.memory_size = 0;
    memreg.userspace_addr = 0;
    vm_ctl(vm, KVM_SET_USER_MEMORY_REGION, (uint64_t)&memreg);
#elif __APPLE__
    assert(vm->initialized);
    assert(hv_vm_unmap(guest_phys_addr, vm_mem_size) == 0);
#endif
}

void create_page_table(uint8_t* vm_mem, uint64_t page_table, uint64_t code_page, uint64_t stack_page) {
#ifdef __x86_64__
    *(uint64_t*)(vm_mem + page_table + 0x0000UL) = (page_table + 0x1000UL) | PT_RW | PT_PRE;            // Level 4, Entry 0
    *(uint64_t*)(vm_mem + page_table + 0x1000UL) = (page_table + 0x2000UL) | PT_RW | PT_PRE;            // Level 3, Entry 0
    *(uint64_t*)(vm_mem + page_table + 0x2000UL) = (page_table + 0x3000UL) | PT_RW | PT_PRE;            // Level 2, Entry 0
    *(uint64_t*)(vm_mem + page_table + 0x3000UL) = code_page                       | PT_PRE;            // Level 1, Entry 0
    *(uint64_t*)(vm_mem + page_table + 0x3008UL) = stack_page      | PT_NX | PT_RW | PT_PRE;            // Level 1, Entry 1
#elif __aarch64__
    *(uint64_t*)(vm_mem + page_table + 0x0000UL) = (page_table + 0x1000UL) | PT_ISH | PT_ACC | PT_PAGE; // Level 4, Entry 0
    *(uint64_t*)(vm_mem + page_table + 0x1000UL) = (page_table + 0x2000UL) | PT_ISH | PT_ACC | PT_PAGE; // Level 3, Entry 0
    *(uint64_t*)(vm_mem + page_table + 0x2000UL) = (page_table + 0x3000UL) | PT_ISH | PT_ACC | PT_PAGE; // Level 2, Entry 0
    *(uint64_t*)(vm_mem + page_table + 0x3000UL) = code_page       | PT_RO | PT_ISH | PT_ACC | PT_PAGE; // Level 1, Entry 0
    *(uint64_t*)(vm_mem + page_table + 0x3008UL) = stack_page      | PT_NX | PT_ISH | PT_ACC | PT_PAGE; // Level 1, Entry 1
#endif
}
