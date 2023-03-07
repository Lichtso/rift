#include <guest.h>

bool walk_page_table(bool write_access, uint64_t access_offset, uint64_t virtual_address, uint64_t* physical_address) {
    size_t parent_level = GUEST_PAGE_TABLE_LEVELS;
    while(1) {
        uint64_t slot_index = (virtual_address >> (GUEST_ENTRIES_PER_PAGE_SHIFT * parent_level + GUEST_PAGE_TABLE_ENTRY_SHIFT)) & (GUEST_ENTRIES_PER_PAGE - 1);
        uint64_t* entry = (uint64_t*)(*physical_address + access_offset + slot_index * sizeof(uint64_t));
#ifdef __x86_64__
        if((*entry & PT_PRE) == 0 || (write_access && (*entry & PT_RW) == 0))
#elif __aarch64__
        if((*entry & PT_PRE) == 0 || (write_access && (*entry & PT_RO) != 0))
#endif
            return false;
        *physical_address = *entry & GUEST_ENTRY_ADDRESS_MASK;
#ifdef __x86_64__
        if(parent_level == 1 || (*entry & PT_LEAF) != 0)
#elif __aarch64__
        if(parent_level == 1 || (*entry & PT_NOT_LEAF) == 0)
#endif
            break;
        --parent_level;
    }
    *physical_address += virtual_address & (((uint64_t)1UL << (GUEST_ENTRIES_PER_PAGE_SHIFT * parent_level + GUEST_PAGE_TABLE_ENTRY_SHIFT)) - 1UL);
    return true;
}
