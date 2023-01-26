#include "platform.h"

#ifdef __linux__
void vm_ctl(struct vm* vm, uint32_t request, uint64_t param) {
    assert(ioctl(vm->fd, request, param) >= 0);
}
#endif

struct vm* create_vm() {
    struct vm* vm = malloc(sizeof(struct vm));
    for(size_t slot = 0; slot < sizeof(vm->mappings) / sizeof(vm->mappings[0]); ++slot)
        vm->mappings[slot].length = 0;
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
    assert(hv_vm_destroy() == 0);
    (void)vm;
#endif
}

void map_memory_of_vm(struct vm* vm, struct host_to_guest_mapping* mapping) {
    size_t slot;
    for(slot = 0; slot < sizeof(vm->mappings) / sizeof(vm->mappings[0]); ++slot)
        if(vm->mappings[slot].length == 0)
            break;
    vm->mappings[slot].guest_address = mapping->guest_address;
    vm->mappings[slot].host_address = mapping->host_address;
    vm->mappings[slot].length = mapping->length;
#ifdef __linux__
    struct kvm_userspace_memory_region memreg;
    memreg.slot = (uint32_t)slot;
    memreg.flags = 0;
    memreg.guest_phys_addr = mapping->guest_address;
    memreg.memory_size = mapping->length;
    memreg.userspace_addr = (uint64_t)mapping->host_address;
    vm_ctl(vm, KVM_SET_USER_MEMORY_REGION, (uint64_t)&memreg);
#elif __APPLE__
    assert(hv_vm_map(
        mapping->host_address,
        mapping->guest_address,
        mapping->length,
        HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC) == 0);
#endif
}

void unmap_memory_of_vm(struct vm* vm, struct host_to_guest_mapping* mapping) {
    size_t slot;
    for(slot = 0; slot < sizeof(vm->mappings) / sizeof(vm->mappings[0]); ++slot)
        if(vm->mappings[slot].guest_address == mapping->guest_address)
            break;
    vm->mappings[slot].length = 0;
#ifdef __linux__
    struct kvm_userspace_memory_region memreg;
    memreg.slot = (uint32_t)slot;
    memreg.flags = 0;
    memreg.guest_phys_addr = mapping->guest_address;
    memreg.memory_size = 0;
    memreg.userspace_addr = 0;
    vm_ctl(vm, KVM_SET_USER_MEMORY_REGION, (uint64_t)&memreg);
#elif __APPLE__
    assert(hv_vm_unmap(mapping->guest_address, mapping->length) == 0);
#endif
}

#define PAGE_TABLE_LEVELS 4
#define HUGE_PAGE_TABLE_LEVELS 3
#define GUEST_PAGE_SIZE 0x1000
const uint64_t entries_per_page = GUEST_PAGE_SIZE / sizeof(uint64_t);

#define MAPPING_LEVELS_LOOP \
    uint64_t end_virtual_address; \
    if(mapping_index + 1 < number_of_mappings) \
        end_virtual_address = mappings[mapping_index + 1].virtual_address; \
    else \
        end_virtual_address = level_page_size[PAGE_TABLE_LEVELS]; \
    if(mapping->flags == MAPPING_GAP) \
        continue; \
    uint64_t gap_start_entry_index = 0, gap_end_entry_index = 0; \
    for(size_t parent_level = PAGE_TABLE_LEVELS; parent_level > 0; --parent_level) { \
        size_t level = parent_level - 1; \
        uint64_t start_entry_index = mapping->virtual_address / level_page_size[level]; \
        uint64_t end_entry_index = (end_virtual_address + level_page_size[level] - 1) / level_page_size[level]; \
        uint64_t real_start_entry_index = start_entry_index; \
        if(start_entry_index < level_entry_index[level]) \
            start_entry_index = level_entry_index[level]; \
        if(gap_end_entry_index < start_entry_index) \
            gap_start_entry_index = gap_end_entry_index = start_entry_index; \
        else if(gap_start_entry_index > end_entry_index) \
            gap_start_entry_index = gap_end_entry_index = end_entry_index; \
        if(gap_end_entry_index < gap_start_entry_index) \
            gap_end_entry_index = gap_start_entry_index; \
        uint64_t leaves_start_entry_index = (mapping->virtual_address + level_page_size[level] - 1) / level_page_size[level]; \
        uint64_t leaves_end_entry_index = end_virtual_address / level_page_size[level];

#define MAPPING_LEVELS_LOOP_END \
        level_number_of_entries[level] += (gap_start_entry_index - start_entry_index) + (end_entry_index - gap_end_entry_index); \
        level_entry_index[level] = end_entry_index; \
        gap_start_entry_index = leaves_start_entry_index * entries_per_page; \
        gap_end_entry_index = leaves_end_entry_index * entries_per_page; \
    }

#define WRITE_ENTRY { \
    bool is_leaf = leaves_start_entry_index <= entry_index && entry_index < leaves_end_entry_index; \
    assert(level > 0 || is_leaf); \
    uint64_t parent_relative_entry_index = (entry_index < parent_leaves_start_entry_index) \
        ? entry_index - parent_start_entry_index \
        : entry_index - parent_leaves_start_entry_index; \
    uint64_t relative_entry_index = entry_index - real_start_entry_index; \
    entries[parent_relative_entry_index] = is_leaf \
        ? leaf_proto_entry | (relative_entry_index * level_page_size[level] + mapping->physical_address) \
        : branch_proto_entry | ((relative_entry_index + level_number_of_entries[level]) * GUEST_PAGE_SIZE + level_physical_address[level - 1]); \
}

uint64_t create_page_table(struct host_to_guest_mapping* page_table, size_t number_of_mappings, struct guest_internal_mapping mappings[number_of_mappings]) {
    assert(number_of_mappings > 0);
    uint64_t level_physical_address[PAGE_TABLE_LEVELS];
    uint64_t level_page_size[PAGE_TABLE_LEVELS + 1];
    uint64_t level_number_of_entries[PAGE_TABLE_LEVELS + 1];
    uint64_t level_entry_index[PAGE_TABLE_LEVELS];
    level_page_size[0] = GUEST_PAGE_SIZE;
    level_number_of_entries[PAGE_TABLE_LEVELS] = 1;
    for(size_t level = 0; level < PAGE_TABLE_LEVELS; ++level) {
        level_page_size[level + 1] = level_page_size[level] * entries_per_page;
        level_number_of_entries[level] = 0;
        level_entry_index[level] = 0;
    }
    for(size_t mapping_index = 0; mapping_index < number_of_mappings; ++mapping_index) {
        struct guest_internal_mapping* mapping = &mappings[mapping_index];
        if(mapping_index == 0)
            assert(mapping->virtual_address == 0);
        else {
            assert(mapping->virtual_address == mapping->virtual_address / GUEST_PAGE_SIZE * GUEST_PAGE_SIZE);
            struct guest_internal_mapping* prev_mapping = &mappings[mapping_index - 1];
            assert(prev_mapping->flags != MAPPING_GAP || mapping->flags != MAPPING_GAP);
            assert(prev_mapping->virtual_address != mapping->virtual_address);
            if(prev_mapping->flags != MAPPING_GAP && mapping->flags != MAPPING_GAP) {
                uint64_t prev_end_physical_address = prev_mapping->physical_address + (mapping->virtual_address - prev_mapping->virtual_address);
                assert(prev_end_physical_address != mapping->physical_address || prev_mapping->flags != mapping->flags);
            }
        }
        MAPPING_LEVELS_LOOP
            (void)real_start_entry_index;
            assert(level < HUGE_PAGE_TABLE_LEVELS || mapping->flags == MAPPING_GAP || leaves_start_entry_index >= leaves_end_entry_index);
        MAPPING_LEVELS_LOOP_END
    }
    page_table->length = 0;
    for(size_t parent_level = PAGE_TABLE_LEVELS; parent_level > 0; --parent_level) {
        size_t level = parent_level - 1;
        level_physical_address[level] = page_table->guest_address + page_table->length;
        page_table->length += level_number_of_entries[parent_level] * GUEST_PAGE_SIZE;
    }
    for(size_t level = 0; level < PAGE_TABLE_LEVELS; ++level) {
        level_number_of_entries[level] = 0;
        level_entry_index[level] = 0;
    }
    uint64_t host_page_size = (uint64_t)sysconf(_SC_PAGESIZE);
    page_table->length = (page_table->length + host_page_size - 1) / host_page_size * host_page_size;
    page_table->host_address = valloc(page_table->length);
    assert(page_table->host_address);
    uint64_t branch_proto_entry;
#ifdef __x86_64__
    branch_proto_entry = PT_PRE | PT_RW;
#elif __aarch64__
    branch_proto_entry = PT_ISH | PT_ACC | PT_PRE;
#endif
    for(size_t mapping_index = 0; mapping_index < number_of_mappings; ++mapping_index) {
        struct guest_internal_mapping* mapping = &mappings[mapping_index];
        uint64_t mapping_proto_entry = 0;
#ifdef __x86_64__
        if((mapping->flags & MAPPING_READABLE) != 0)
            mapping_proto_entry |= PT_PRE;
        if((mapping->flags & MAPPING_WRITABLE) != 0)
            mapping_proto_entry |= PT_RW;
        if((mapping->flags & MAPPING_EXECUTABLE) == 0)
            mapping_proto_entry |= PT_NX;
#elif __aarch64__
        if((mapping->flags & MAPPING_READABLE) != 0)
            mapping_proto_entry |= PT_ISH | PT_ACC | PT_PRE;
        if((mapping->flags & MAPPING_WRITABLE) == 0)
            mapping_proto_entry |= PT_RO;
        if((mapping->flags & MAPPING_EXECUTABLE) == 0)
            mapping_proto_entry |= PT_NX;
#endif
        uint64_t parent_start_entry_index = 0, parent_leaves_start_entry_index = 0;
        uint64_t* entries = (uint64_t*)(level_physical_address[PAGE_TABLE_LEVELS - 1] - page_table->guest_address + (uint64_t)page_table->host_address);
        MAPPING_LEVELS_LOOP
            uint64_t leaf_proto_entry = mapping_proto_entry;
            if(level > 0)
#ifdef __x86_64__
                leaf_proto_entry |= PT_LEAF;
#elif __aarch64__
                leaf_proto_entry &= ~PT_NOT_LEAF;
#endif
            for(uint64_t entry_index = start_entry_index; entry_index < gap_start_entry_index; ++entry_index)
                WRITE_ENTRY
            for(uint64_t entry_index = gap_end_entry_index; entry_index < end_entry_index; ++entry_index)
                WRITE_ENTRY
            parent_start_entry_index = real_start_entry_index * entries_per_page;
            parent_leaves_start_entry_index = leaves_start_entry_index * entries_per_page;
            uint64_t page_offset = (level_number_of_entries[level] + real_start_entry_index - start_entry_index) * GUEST_PAGE_SIZE;
            if(level > 0)
                entries = (uint64_t*)(page_offset + level_physical_address[level - 1] - page_table->guest_address + (uint64_t)page_table->host_address);
        MAPPING_LEVELS_LOOP_END
    }
    return level_physical_address[PAGE_TABLE_LEVELS - 1];
}
