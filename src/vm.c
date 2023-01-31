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

bool resolve_address_of_vm(struct vm* vm, uint64_t guest_address, void** host_address, uint64_t length) {
    size_t number_of_mappings = sizeof(vm->mappings) / sizeof(vm->mappings[0]);
    for(size_t slot = 0; slot < number_of_mappings; ++slot)
        if(vm->mappings[slot].length > 0 && (slot + 1 == number_of_mappings || guest_address < vm->mappings[slot + 1].guest_address)) {
            uint64_t offset = guest_address - vm->mappings[slot].guest_address;
            if(offset + length >= vm->mappings[slot].length)
                return false;
            *host_address = (void*)(offset + (uint64_t)vm->mappings[slot].host_address);
            return true;
        }
    return false;
}

#define GUEST_PAGE_TABLE_LEVELS 4
#define GUEST_HUGE_PAGE_TABLE_LEVELS 0
#define GUEST_ENTRIES_PER_PAGE_SHIFT 9
#define GUEST_ENTRIES_PER_PAGE (1UL << GUEST_ENTRIES_PER_PAGE_SHIFT)
#define GUEST_PAGE_SIZE (1UL << (GUEST_ENTRIES_PER_PAGE_SHIFT + 3))
#define GUEST_ENTRY_ADDRESS_MASK (~((0xFFFFUL << 48) | (GUEST_PAGE_SIZE - 1)))

#define MAPPING_LEVELS_LOOP \
    uint64_t end_virtual_address; \
    if(mapping_index + 1 < number_of_mappings) \
        end_virtual_address = mappings[mapping_index + 1].virtual_address; \
    else \
        end_virtual_address = level_page_size[GUEST_PAGE_TABLE_LEVELS]; \
    if(mapping->flags == MAPPING_GAP) \
        continue; \
    uint64_t gap_start_entry_index = 0, gap_end_entry_index = 0; \
    for(size_t parent_level = GUEST_PAGE_TABLE_LEVELS; parent_level > 0; --parent_level) { \
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
        uint64_t leaves_end_entry_index = end_virtual_address / level_page_size[level]; \
        if(level > GUEST_HUGE_PAGE_TABLE_LEVELS) \
            leaves_end_entry_index = leaves_start_entry_index;

#define MAPPING_LEVELS_LOOP_END \
        level_number_of_entries[level] += (gap_start_entry_index - start_entry_index) + (end_entry_index - gap_end_entry_index); \
        level_entry_index[level] = end_entry_index; \
        gap_start_entry_index = leaves_start_entry_index * GUEST_ENTRIES_PER_PAGE; \
        gap_end_entry_index = leaves_end_entry_index * GUEST_ENTRIES_PER_PAGE; \
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

void create_page_table(struct host_to_guest_mapping* page_table, uint64_t number_of_mappings, struct guest_internal_mapping mappings[number_of_mappings]) {
    assert(number_of_mappings > 0);
    uint64_t level_physical_address[GUEST_PAGE_TABLE_LEVELS];
    uint64_t level_page_size[GUEST_PAGE_TABLE_LEVELS + 1];
    uint64_t level_number_of_entries[GUEST_PAGE_TABLE_LEVELS + 1];
    uint64_t level_entry_index[GUEST_PAGE_TABLE_LEVELS];
    level_page_size[0] = GUEST_PAGE_SIZE;
    level_number_of_entries[GUEST_PAGE_TABLE_LEVELS] = 1;
    for(size_t level = 0; level < GUEST_PAGE_TABLE_LEVELS; ++level) {
        level_page_size[level + 1] = level_page_size[level] * GUEST_ENTRIES_PER_PAGE;
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
        MAPPING_LEVELS_LOOP_END
    }
    page_table->length = 0;
    for(size_t parent_level = GUEST_PAGE_TABLE_LEVELS; parent_level > 0; --parent_level) {
        size_t level = parent_level - 1;
        level_physical_address[level] = page_table->guest_address + page_table->length;
        page_table->length += level_number_of_entries[parent_level] * GUEST_PAGE_SIZE;
    }
    for(size_t level = 0; level < GUEST_PAGE_TABLE_LEVELS; ++level) {
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
        uint64_t* entries = (uint64_t*)(level_physical_address[GUEST_PAGE_TABLE_LEVELS - 1] - page_table->guest_address + (uint64_t)page_table->host_address);
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
            parent_start_entry_index = real_start_entry_index * GUEST_ENTRIES_PER_PAGE;
            parent_leaves_start_entry_index = leaves_start_entry_index * GUEST_ENTRIES_PER_PAGE;
            uint64_t page_offset = (level_number_of_entries[level] + real_start_entry_index - start_entry_index) * GUEST_PAGE_SIZE;
            if(level > 0)
                entries = (uint64_t*)(page_offset + level_physical_address[level - 1] - page_table->guest_address + (uint64_t)page_table->host_address);
        MAPPING_LEVELS_LOOP_END
    }
}

bool resolve_address_using_page_table(struct host_to_guest_mapping* page_table, uint64_t virtual_address, uint64_t* physical_address) {
    *physical_address = page_table->guest_address;
    size_t parent_level;
    for(parent_level = GUEST_PAGE_TABLE_LEVELS; parent_level > 0; --parent_level) {
        uint64_t slot_index = (virtual_address >> (GUEST_ENTRIES_PER_PAGE_SHIFT * parent_level + 3)) & (GUEST_ENTRIES_PER_PAGE - 1);
        uint64_t* entry = (uint64_t*)(*physical_address - page_table->guest_address + slot_index * sizeof(uint64_t) + (uint64_t)page_table->host_address);
        if((*entry & 1) == 0)
            return false;
        *physical_address = *entry & GUEST_ENTRY_ADDRESS_MASK;
#ifdef __x86_64__
        if((*entry & PT_LEAF) != 0)
#elif __aarch64__
        if((*entry & PT_NOT_LEAF) == 0)
#endif
            break;
    }
    *physical_address += virtual_address & ((1UL << (GUEST_ENTRIES_PER_PAGE_SHIFT * parent_level + 3)) - 1);
    return true;
}
