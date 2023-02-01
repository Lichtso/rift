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

struct loaded_object {
    uint64_t file_size;
    uint64_t number_of_symbols;
    uint64_t stack_pointer;
    struct host_to_guest_mapping file_data;
    struct host_to_guest_mapping writable_data;
    struct host_to_guest_mapping page_table;
    const char* symbol_names;
    void* symbol_table;
    struct vm* vm;
    int fd;
};

struct loaded_object* create_loaded_object(struct vm* vm, const char* path) {
    struct loaded_object* loaded_object = malloc(sizeof(struct loaded_object));
    loaded_object->vm = vm;
    loaded_object->fd = open(path, O_RDONLY);
    struct stat stat;
    fstat(loaded_object->fd, &stat);
    loaded_object->file_size = (uint64_t)stat.st_size;
    loaded_object->file_data.guest_address = 0;
    loaded_object->file_data.host_address = mmap(0, loaded_object->file_size, PROT_READ, MAP_FILE | MAP_PRIVATE, loaded_object->fd, 0);
    assert(loaded_object->file_data.host_address != MAP_FAILED);
    uint32_t magic = *(uint32_t*)loaded_object->file_data.host_address;
    size_t mapping_index = 2;
    struct elf64_hdr* elf_header = (struct elf64_hdr*)loaded_object->file_data.host_address;
    struct mach_header* mach_header = (struct mach_header*)loaded_object->file_data.host_address;
    switch(magic) {
        case ELF_MAGIC: {
            struct elf64_phdr* command = (struct elf64_phdr*)((uint64_t)elf_header + elf_header->e_phoff);
            for(size_t i = 0; i < elf_header->e_phnum; ++i) {
                if(command->p_type == PT_LOAD)
                    mapping_index += 2;
                ++command;
            }
        } break;
        case MACH_MAGIC: {
            struct mach_command* command = (struct mach_command*)(mach_header + 1);
            for(size_t i = 0; i < mach_header->ncmds; ++i) {
                if(command->cmd == LC_SEGMENT_64)
                    mapping_index += 2;
                command = (struct mach_command*)((uint64_t)command + command->cmdsize);
            }
        } break;
    }
    struct guest_internal_mapping mappings[mapping_index];
    mapping_index = 0;
    loaded_object->symbol_names = NULL;
    loaded_object->writable_data.length = 0;
    uint64_t next_virtual_address = 0, writable_data_preinit_length = 0;
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
                            writable_data_preinit_length = phdr->p_filesz;
                            loaded_object->writable_data.length += phdr->p_memsz;
                            loaded_object->writable_data.guest_address = loaded_object->file_data.guest_address + phdr->p_offset;
                            loaded_object->writable_data.host_address = (void*)((uint64_t)loaded_object->file_data.host_address + phdr->p_offset);
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
        for(size_t i = 0; i < mach_header->ncmds; ++i) {
            switch(mach_command->cmd) {
                case LC_SEGMENT_64: {
                    struct mach_command_segment_64* command = (struct mach_command_segment_64*)mach_command;
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
                    } else if(strcmp(command->segname, "__DATA") == 0) {
                        writable_data_preinit_length = command->filesize;
                        loaded_object->writable_data.length += command->vmsize;
                        loaded_object->writable_data.guest_address = loaded_object->file_data.guest_address + command->fileoff;
                        loaded_object->writable_data.host_address = (void*)((uint64_t)loaded_object->file_data.host_address + command->fileoff);
                        mappings[mapping_index].flags = MAPPING_READABLE | MAPPING_WRITABLE;
                    } else if(strcmp(command->segname, "__RODATA") == 0) {
                        mappings[mapping_index].flags = MAPPING_READABLE;
                    } else {
                        assert(false);
                    }
                    next_virtual_address = command->vmaddr + command->vmsize;
                    ++mapping_index;
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
    }
    mappings[mapping_index].virtual_address = next_virtual_address;
    mappings[mapping_index].physical_address = 0;
    mappings[mapping_index].flags = MAPPING_GAP;
    ++mapping_index;
    void* writable_data_source = loaded_object->writable_data.host_address;
    loaded_object->writable_data.host_address = valloc(loaded_object->writable_data.length);
    assert(loaded_object->writable_data.host_address);
    memcpy(loaded_object->writable_data.host_address, writable_data_source, writable_data_preinit_length);
    loaded_object->file_data.length = loaded_object->writable_data.guest_address;
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
    assert(munmap(loaded_object->file_data.host_address, loaded_object->file_size) == 0);
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
                if(strcmp(loaded_object->symbol_names + symbols[i].st_name, symbol_name) == 0) {
                    *virtual_address = symbols[i].st_value;
                    return true;
                }
        } break;
        case MACH_MAGIC: {
            struct mach_symbol_table_entry_64* symbols = (struct mach_symbol_table_entry_64*)loaded_object->symbol_table;
            for(size_t i = 0; i < loaded_object->number_of_symbols; ++i)
                if(strcmp(loaded_object->symbol_names + symbols[i].n_strx, symbol_name) == 0) {
                    *virtual_address = symbols[i].n_value;
                    return true;
                }
        } break;
    }
    return false;
}

bool resolve_symbol_host_address_in_loaded_object(struct loaded_object* loaded_object, const char* symbol_name, uint64_t length, void** host_address) {
    uint64_t virtual_address;
    uint64_t physical_address;
    return resolve_symbol_virtual_address_in_loaded_object(loaded_object, symbol_name, &virtual_address) &&
        resolve_address_using_page_table(&loaded_object->page_table, virtual_address, &physical_address) &&
        resolve_address_of_vm(loaded_object->vm, physical_address, host_address, length);
}

struct vcpu* create_vcpu_for_loaded_object(struct loaded_object* loaded_object, const char* entry_point) {
    uint64_t instruction_pointer;
    assert(resolve_symbol_virtual_address_in_loaded_object(loaded_object, entry_point, &instruction_pointer));
    struct vcpu* vcpu = create_vcpu(loaded_object->vm, loaded_object->page_table.guest_address);
    set_program_pointers_of_vcpu(vcpu, instruction_pointer, loaded_object->stack_pointer);
    return vcpu;
}
