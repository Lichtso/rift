#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __APPLE__
#define SYMBOL_NAME_PREFIX "_"
#else
#define SYMBOL_NAME_PREFIX
#endif

#define EXPORT __attribute__((visibility("default")))
#define ALIGN(bytes) __attribute__((aligned(bytes)))

#ifdef __x86_64__
#include "arch/x86_64.h"
#elif __aarch64__
#include "arch/aarch64.h"
#endif

#define GUEST_PAGE_TABLE_LEVELS 4
#define GUEST_HUGE_PAGE_TABLE_LEVELS 0
#define GUEST_PAGE_TABLE_ENTRY_SHIFT 3
#define GUEST_ENTRIES_PER_PAGE_SHIFT 9
#define GUEST_ENTRIES_PER_PAGE (1UL << GUEST_ENTRIES_PER_PAGE_SHIFT)
#define GUEST_PAGE_SIZE (1UL << (GUEST_ENTRIES_PER_PAGE_SHIFT + GUEST_PAGE_TABLE_ENTRY_SHIFT))
#define GUEST_ENTRY_ADDRESS_MASK (~((0xFFFFUL << 48) | (GUEST_PAGE_SIZE - 1)))

bool walk_page_table(bool write_access, uint64_t access_offset, uint64_t virtual_address, uint64_t* physical_address);
