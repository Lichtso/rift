#include "platform.h"

#define ELF_MAGIC      0x464C457F
#define PT_LOAD        0x1
#define SHT_SYMTAB     0x2
#define SHT_STRTAB     0x3

#define MACH_MAGIC     0xFEEDFACF
#define LC_SYMTAB      0x2
#define LC_SEGMENT_64  0x19

struct elf64_hdr {
    uint32_t e_ident;
    uint8_t  e_class;
    uint8_t  e_data;
    uint8_t  e_specversion;
    uint8_t  e_osabi;
    uint8_t  e_abiversion;
    uint8_t  reserved[7];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

struct elf64_shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct elf64_sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct mach_header {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct mach_command {
    uint32_t cmd;
    uint32_t cmdsize;
};

struct mach_command_segment_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct mach_symbol_table {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};

struct mach_symbol_table_entry_64 {
    uint32_t n_strx;
    uint8_t  n_type;
    uint8_t  n_sect;
    uint16_t n_desc;
    uint64_t n_value;
};

int compare_vmaddr_of_mach_segments(const void* ptr_a, const void* ptr_b)  {
    const struct mach_command_segment_64* a = *((const struct mach_command_segment_64**)ptr_a);
    const struct mach_command_segment_64* b = *((const struct mach_command_segment_64**)ptr_b);
    if(a->vmaddr > b->vmaddr) return 1;
    if(a->vmaddr < b->vmaddr) return -1;
    return 0;
}

#ifdef __x86_64__
struct interrupt_gate_64 {
    uint16_t offset0;
    uint16_t segment_selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset1;
    uint32_t offset2;
    uint32_t padding;
};
#endif

struct loaded_object {
    uint64_t number_of_symbols;
    uint64_t stack_pointer;
    uint64_t writable_data_preinit_length;
    struct host_to_guest_mapping file_data;
    struct host_to_guest_mapping writable_data;
    struct host_to_guest_mapping page_table;
    const char* symbol_names;
    void* symbol_table;
    struct vm* vm;
    int fd;
};

void add_data_segment_to_loaded_object(struct loaded_object* loaded_object, uint64_t file_offset, uint64_t file_size, uint64_t vm_size) {
    loaded_object->writable_data.guest_address = loaded_object->file_data.guest_address + loaded_object->file_data.length + loaded_object->writable_data.length;
    loaded_object->writable_data_preinit_length += file_size;
    loaded_object->writable_data.length += vm_size;
    void* writable_data_source = (void*)((uint64_t)loaded_object->file_data.host_address + file_offset);
    loaded_object->writable_data.host_address = valloc(loaded_object->writable_data.length);
    assert(loaded_object->writable_data.host_address);
    memcpy(loaded_object->writable_data.host_address, writable_data_source, loaded_object->writable_data_preinit_length);
}

struct loaded_object* create_loaded_object(struct vm* vm, const char* path) {
    struct loaded_object* loaded_object = malloc(sizeof(struct loaded_object));
    loaded_object->vm = vm;
    loaded_object->fd = open(path, O_RDONLY);
    struct stat stat;
    fstat(loaded_object->fd, &stat);
    uint64_t host_page_size = (uint64_t)sysconf(_SC_PAGESIZE);
    loaded_object->symbol_names = NULL;
    loaded_object->writable_data.length = 0;
    loaded_object->writable_data_preinit_length = 0;
    loaded_object->file_data.length = ((uint64_t)stat.st_size + host_page_size - 1) / host_page_size * host_page_size;
    loaded_object->file_data.guest_address = 0;
    loaded_object->file_data.host_address = mmap(0, loaded_object->file_data.length, PROT_READ, MAP_FILE | MAP_PRIVATE, loaded_object->fd, 0);
    assert(loaded_object->file_data.host_address != MAP_FAILED);
    uint32_t magic = *(uint32_t*)loaded_object->file_data.host_address;
    size_t number_of_segments = 0;
    struct elf64_hdr* elf_header = (struct elf64_hdr*)loaded_object->file_data.host_address;
    struct mach_header* mach_header = (struct mach_header*)loaded_object->file_data.host_address;
    switch(magic) {
        case ELF_MAGIC: {
            struct elf64_phdr* phdrs = (struct elf64_phdr*)((uint64_t)elf_header + elf_header->e_phoff);
            for(size_t i = 0; i < elf_header->e_phnum; ++i) {
                struct elf64_phdr* phdr = &phdrs[i];
                if(phdr->p_type == PT_LOAD)
                    ++number_of_segments;
            }
        } break;
        case MACH_MAGIC: {
            struct mach_command* mach_command = (struct mach_command*)(mach_header + 1);
            for(size_t i = 0; i < mach_header->ncmds; ++i) {
                if(mach_command->cmd == LC_SEGMENT_64)
                    ++number_of_segments;
                mach_command = (struct mach_command*)((uint64_t)mach_command + mach_command->cmdsize);
            }
        } break;
    }
    struct guest_internal_mapping mappings[number_of_segments * 2 + 1];
    uint64_t next_virtual_address = 0;
    size_t mapping_index = 0;
    if(magic == ELF_MAGIC) {
        struct elf64_phdr* phdrs = (struct elf64_phdr*)((uint64_t)elf_header + elf_header->e_phoff);
        for(size_t i = 0; i < elf_header->e_phnum; ++i) {
            struct elf64_phdr* phdr = &phdrs[i];
            switch(phdr->p_type) {
                case PT_LOAD: {
                    assert(loaded_object->writable_data.length == 0);
                    if(next_virtual_address < phdr->p_vaddr) {
                        mappings[mapping_index].virtual_address = next_virtual_address;
                        mappings[mapping_index].physical_address = 0;
                        mappings[mapping_index].flags = MAPPING_GAP;
                        ++mapping_index;
                    }
                    mappings[mapping_index].virtual_address = phdr->p_vaddr;
                    mappings[mapping_index].physical_address = phdr->p_offset;
                    switch(phdr->p_flags) {
                        case 5: // TEXT
                            mappings[mapping_index].flags = MAPPING_READABLE | MAPPING_EXECUTABLE;
                            break;
                        case 6: // DATA
                            add_data_segment_to_loaded_object(loaded_object, phdr->p_offset, phdr->p_filesz, phdr->p_memsz);
                            mappings[mapping_index].physical_address = loaded_object->writable_data.guest_address;
                            mappings[mapping_index].flags = MAPPING_READABLE | MAPPING_WRITABLE;
                            break;
                        case 4: // RODATA
                            mappings[mapping_index].flags = MAPPING_READABLE;
                            break;
                        default:
                            assert(false);
                            break;
                    }
                    ++mapping_index;
                    next_virtual_address = phdr->p_vaddr + phdr->p_memsz;
                } break;
            }
        }
        struct elf64_shdr* shdrs = (struct elf64_shdr*)((uint64_t)elf_header + elf_header->e_shoff);
        const char* shdr_names = (const char*)((uint64_t)elf_header + shdrs[elf_header->e_shstrndx].sh_offset);
        for(size_t i = 0; i < elf_header->e_shnum; ++i) {
            struct elf64_shdr* shdr = &shdrs[i];
            switch(shdr->sh_type) {
                case SHT_STRTAB: {
                    if(strcmp(shdr_names + shdr->sh_name, ".strtab") == 0)
                        loaded_object->symbol_names = (const char*)((uint64_t)elf_header + shdr->sh_offset);
                } break;
            }
        }
        assert(loaded_object->symbol_names);
        for(size_t i = 0; i < elf_header->e_shnum; ++i) {
            struct elf64_shdr* shdr = &shdrs[i];
            switch(shdr->sh_type) {
                case SHT_SYMTAB: {
                    loaded_object->symbol_table = (void*)((uint64_t)elf_header + shdr->sh_offset);
                    loaded_object->number_of_symbols = shdr->sh_size / sizeof(struct elf64_sym);
                } break;
            }
        }
    } else if(magic == MACH_MAGIC) {
        struct mach_command* mach_command = (struct mach_command*)(mach_header + 1);
        struct mach_command_segment_64* sorted_segments[number_of_segments];
        number_of_segments = 0;
        for(size_t i = 0; i < mach_header->ncmds; ++i) {
            switch(mach_command->cmd) {
                case LC_SEGMENT_64: {
                    struct mach_command_segment_64* command = (struct mach_command_segment_64*)mach_command;
                    if(strcmp(command->segname, "__TEXT") == 0 ||
                       strcmp(command->segname, "__RODATA") == 0 ||
                       strcmp(command->segname, "__DATA") == 0)
                    sorted_segments[number_of_segments++] = command;
                } break;
                case LC_SYMTAB: {
                    struct mach_symbol_table* command = (struct mach_symbol_table*)mach_command;
                    loaded_object->symbol_names = (const char*)((uint64_t)mach_header + command->stroff);
                    loaded_object->symbol_table = (void*)((uint64_t)mach_header + command->symoff);
                    loaded_object->number_of_symbols = command->nsyms;
                } break;
            }
            mach_command = (struct mach_command*)((uint64_t)mach_command + mach_command->cmdsize);
        }
        assert(loaded_object->symbol_names);
        qsort(sorted_segments, number_of_segments, sizeof(struct mach_command_segment_64*), compare_vmaddr_of_mach_segments);
        for(size_t i = 0; i < number_of_segments; ++i) {
            struct mach_command_segment_64* command = sorted_segments[i];
            assert(loaded_object->writable_data.length == 0);
            if(next_virtual_address < command->vmaddr) {
                mappings[mapping_index].virtual_address = next_virtual_address;
                mappings[mapping_index].physical_address = 0;
                mappings[mapping_index].flags = MAPPING_GAP;
                ++mapping_index;
            }
            mappings[mapping_index].virtual_address = command->vmaddr;
            mappings[mapping_index].physical_address = command->fileoff;
            if(strcmp(command->segname, "__TEXT") == 0) {
                mappings[mapping_index].flags = MAPPING_READABLE | MAPPING_EXECUTABLE;
            } else if(strcmp(command->segname, "__RODATA") == 0) {
                mappings[mapping_index].flags = MAPPING_READABLE;
            } else if(strcmp(command->segname, "__DATA") == 0) {
                add_data_segment_to_loaded_object(loaded_object, command->fileoff, command->filesize, command->vmsize);
                mappings[mapping_index].physical_address = loaded_object->writable_data.guest_address;
                mappings[mapping_index].flags = MAPPING_READABLE | MAPPING_WRITABLE;
            }
            ++mapping_index;
            next_virtual_address = command->vmaddr + command->vmsize;
        }
    }
    mappings[mapping_index].virtual_address = next_virtual_address;
    mappings[mapping_index].physical_address = 0;
    mappings[mapping_index].flags = MAPPING_GAP;
    ++mapping_index;
    loaded_object->page_table.guest_address = loaded_object->writable_data.guest_address + loaded_object->writable_data.length;
    create_page_table(&loaded_object->page_table, mapping_index, mappings);
    loaded_object->stack_pointer = next_virtual_address;
    map_memory_of_vm(loaded_object->vm, &loaded_object->file_data);
    map_memory_of_vm(loaded_object->vm, &loaded_object->writable_data);
    map_memory_of_vm(loaded_object->vm, &loaded_object->page_table);
    return loaded_object;
}

void destroy_loaded_object(struct loaded_object* loaded_object) {
    unmap_memory_of_vm(loaded_object->vm, &loaded_object->file_data);
    unmap_memory_of_vm(loaded_object->vm, &loaded_object->writable_data);
    unmap_memory_of_vm(loaded_object->vm, &loaded_object->page_table);
    assert(munmap(loaded_object->file_data.host_address, loaded_object->file_data.length) == 0);
    assert(close(loaded_object->fd) == 0);
    free(loaded_object->writable_data.host_address);
    free(loaded_object->page_table.host_address);
    free(loaded_object);
}

bool resolve_symbol_virtual_address_in_loaded_object(struct loaded_object* loaded_object, const char* symbol_name, uint64_t* virtual_address) {
    uint32_t magic = *(uint32_t*)loaded_object->file_data.host_address;
    switch(magic) {
        case ELF_MAGIC: {
            struct elf64_sym* symbols = (struct elf64_sym*)loaded_object->symbol_table;
            for(size_t i = 0; i < loaded_object->number_of_symbols; ++i)
                if((symbols[i].st_info & 0x10) != 0 && strcmp(loaded_object->symbol_names + symbols[i].st_name, symbol_name) == 0) {
                    *virtual_address = symbols[i].st_value;
                    return true;
                }
        } break;
        case MACH_MAGIC: {
            struct mach_symbol_table_entry_64* symbols = (struct mach_symbol_table_entry_64*)loaded_object->symbol_table;
            for(size_t i = 0; i < loaded_object->number_of_symbols; ++i)
                if(symbols[i].n_type == 0x0F && strcmp(loaded_object->symbol_names + symbols[i].n_strx, symbol_name) == 0) {
                    *virtual_address = symbols[i].n_value;
                    return true;
                }
        } break;
    }
    return false;
}

bool resolve_symbol_host_address_in_loaded_object(struct loaded_object* loaded_object, bool write_access, const char* symbol_name, uint64_t length, void** host_address) {
    uint64_t virtual_address;
    uint64_t physical_address;
    return resolve_symbol_virtual_address_in_loaded_object(loaded_object, symbol_name, &virtual_address) &&
        resolve_address_using_page_table(&loaded_object->page_table, write_access, virtual_address, &physical_address) &&
        resolve_address_of_vm(loaded_object->vm, physical_address, host_address, length);
}

struct vcpu* create_vcpu_for_loaded_object(struct loaded_object* loaded_object, const char* interrupt_table, const char* entry_point) {
    uint64_t instruction_pointer;
    assert(resolve_symbol_virtual_address_in_loaded_object(loaded_object, entry_point, &instruction_pointer));
    uint64_t interrupt_table_virtual_address = 0;
    if(interrupt_table) {
        assert(resolve_symbol_virtual_address_in_loaded_object(loaded_object, interrupt_table, &interrupt_table_virtual_address));
#ifdef __x86_64__
        uint64_t interrupt_table_physical_address;
        assert(resolve_address_using_page_table(&loaded_object->page_table, false, interrupt_table_virtual_address, &interrupt_table_physical_address));
        void* interrupt_table_host_address;
        assert(resolve_address_of_vm(loaded_object->vm, interrupt_table_physical_address, &interrupt_table_host_address, 0x1000));
        uint64_t* interrupt_table_src = (uint64_t*)interrupt_table_host_address;
        struct interrupt_gate_64* interrupt_table_dst = (struct interrupt_gate_64*)interrupt_table_host_address;
        for(size_t i = 0; i < 256; ++i) {
            uint64_t entry = interrupt_table_src[i * 2 + 1];
            interrupt_table_dst[i].offset0 = entry & 0xFFFFUL;
            interrupt_table_dst[i].offset1 = (entry >> 16) & 0xFFFFUL;
            interrupt_table_dst[i].offset2 = (entry >> 32) & 0xFFFFFFFFUL;
            interrupt_table_dst[i].padding = 0;
        }
#endif
    }
    struct vcpu* vcpu = create_vcpu(loaded_object->vm, &loaded_object->page_table, interrupt_table_virtual_address);
#ifdef __x86_64__
    set_register_of_vcpu(vcpu, 6, loaded_object->stack_pointer);
    set_register_of_vcpu(vcpu, 16, instruction_pointer);
#elif __aarch64__
    set_register_of_vcpu(vcpu, 31, loaded_object->stack_pointer);
    set_register_of_vcpu(vcpu, 32, instruction_pointer);
#endif
    return vcpu;
}
