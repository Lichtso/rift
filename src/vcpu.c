#include "platform.h"

#ifdef __linux__
void vcpu_ctl(struct vcpu* vcpu, uint32_t request, uint64_t param) {
    assert(ioctl(vcpu->fd, request, param) >= 0);
}
#ifdef __aarch64__
#define REG_ID(field) KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | offsetof(struct kvm_regs, field) / sizeof(uint32_t)
#define MSR_ID(field) KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM64_SYSREG | field

uint64_t rreg(struct vcpu* vcpu, uint64_t id) {
    uint64_t v;
    struct kvm_one_reg reg;
    reg.id = id;
    reg.addr = (uint64_t)&v;
    vcpu_ctl(vcpu, KVM_GET_ONE_REG, (uint64_t)&reg);
    return v;
}

void wreg(struct vcpu* vcpu, uint64_t id, uint64_t v) {
    struct kvm_one_reg reg;
    reg.id = id;
    reg.addr = (uint64_t)&v;
    vcpu_ctl(vcpu, KVM_SET_ONE_REG, (uint64_t)&reg);
}
#endif
#elif __APPLE__
#ifdef __x86_64__
uint64_t rvmcs(struct vcpu* vcpu, uint32_t id) {
    uint64_t v;
    assert(hv_vmx_vcpu_read_vmcs(vcpu->id, id, &v) == 0);
    return v;
}

void wvmcs(struct vcpu* vcpu, uint32_t id, uint64_t v) {
    assert(hv_vmx_vcpu_write_vmcs(vcpu->id, id, v) == 0);
}
#endif
#endif

struct vcpu* create_vcpu(struct vm* vm, uint64_t page_table_root) {
    struct vcpu* vcpu = malloc(sizeof(struct vcpu));
    vcpu->vm = vm;
#ifdef __linux__
    vcpu->fd = ioctl(vm->fd, KVM_CREATE_VCPU, 0);
    assert(vcpu->fd >= 0);
    size_t vcpu_mmap_size = (size_t)ioctl(vm->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    assert(vcpu_mmap_size > 0);
    vcpu->kvm_run = mmap(NULL, vcpu_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->fd, 0);
    assert(vcpu->kvm_run != MAP_FAILED);
#ifdef __aarch64__
    struct kvm_vcpu_init vcpu_init;
    vm_ctl(vm, KVM_ARM_PREFERRED_TARGET, (uint64_t)&vcpu_init);
    vcpu_init.features[0] |= 1 << KVM_ARM_VCPU_PSCI_0_2;
    vcpu_ctl(vcpu, KVM_ARM_VCPU_INIT, (uint64_t)&vcpu_init);
    uint64_t mmfr = rreg(vcpu, MSR_ID(ID_AA64MMFR0_EL1));
#endif
#elif __APPLE__
#ifdef __x86_64__
    assert(hv_vcpu_create(&vcpu->id, HV_VCPU_DEFAULT) == 0);
    // Configure control registers
    wvmcs(vcpu, VMCS_CTRL_CPU_BASED, CPU_BASED_HLT | CPU_BASED_CR8_LOAD | CPU_BASED_CR8_STORE | CPU_BASED_SECONDARY_CTLS);
    wvmcs(vcpu, VMCS_CTRL_CPU_BASED2, 0);
    wvmcs(vcpu, VMCS_CTRL_VMEXIT_CONTROLS, 0);
    wvmcs(vcpu, VMCS_CTRL_VMENTRY_CONTROLS, VMENTRY_GUEST_IA32E);
    // Enable MSR access
    assert(hv_vcpu_enable_native_msr(vcpu->id, 0xc0000102, 1) == 0); // MSR_KERNELGSBASE
#elif __aarch64__
    assert(hv_vcpu_create(&vcpu->id, &vcpu->exit, NULL) == 0);
    uint64_t mmfr;
    assert(hv_vcpu_get_sys_reg(vcpu->id, HV_SYS_REG_ID_AA64MMFR0_EL1, &mmfr) == 0);
#endif
#endif
#ifdef __x86_64__
    // Configure segmentation
    uint64_t descriptors[3] = {
        0x0000000000000000UL,
        0x00209B0000000000UL, // code segment
        0x00008B0000000FFFUL, // task state segment
    };
#ifdef __linux__
    struct kvm_sregs sregs;
    vcpu_ctl(vcpu, KVM_GET_SREGS, (uint64_t)&sregs);
    uint16_t selectors[8] = { 1, 0, 0, 0, 0, 0, 2, 0 }; // CS, DS, ES, FS, GS, SS, TR, LDT
#elif __APPLE__
    uint16_t selectors[8] = { 0, 1, 0, 0, 0, 0, 0, 2 }; // ES, CS, SS, DS, FS, GS, LDT, TR
#endif
    for(uint32_t segment_index = 0UL; segment_index < sizeof selectors / sizeof *selectors; ++segment_index) {
        uint16_t selector = selectors[segment_index];
        uint64_t segment_descriptor = descriptors[selector];
        uint64_t base = (segment_descriptor >> 16 & 0xFFFFFFL) | ((segment_descriptor >> 56 & 0xFFL) << 24);
        uint64_t limit = (segment_descriptor & 0xFFFFL) | ((segment_descriptor >> 48 & 0xFL) << 16);
        uint64_t access_rights = (segment_descriptor >> 40 & 0xFFL) | ((segment_descriptor >> 52 & 0xFL) << 12);
#ifdef __linux__
        struct kvm_segment* segment = &((struct kvm_segment*)&sregs)[segment_index];
        segment->base = base;
        segment->limit = (uint32_t)limit;
        segment->selector = (uint16_t)(selector << 3);
        segment->present = (uint8_t)(access_rights >> 7);
        segment->type = (uint8_t)access_rights;
        segment->dpl = (uint8_t)(access_rights >> 5);
        segment->db = (uint8_t)(access_rights >> 14);
        segment->s = (uint8_t)(access_rights >> 4);
        segment->l = (uint8_t)(access_rights >> 13);
        segment->g = (uint8_t)(access_rights >> 15);
#elif __APPLE__
        if(selector == 0UL)
            access_rights = 0x10000UL;
        wvmcs(vcpu, VMCS_GUEST_ES, selector * 8UL);
        wvmcs(vcpu, VMCS_GUEST_ES_BASE + segment_index * 2UL, base);
        wvmcs(vcpu, VMCS_GUEST_ES_LIMIT + segment_index * 2UL, limit);
        wvmcs(vcpu, VMCS_GUEST_ES_AR + segment_index * 2UL, access_rights);
#endif
    }
    // Configure system registers
    uint64_t cr0 = CR0_PG | CR0_WP | CR0_NE | CR0_PE;
    uint64_t cr4 = CR4_PAE;
    uint64_t efer = EFER_NXE | EFER_LMA | EFER_LME;
#ifdef __linux__
    sregs.cr0 = cr0;
    sregs.cr3 = page_table_root;
    sregs.cr4 = cr4;
    sregs.efer = efer;
    vcpu_ctl(vcpu, KVM_SET_SREGS, (uint64_t)&sregs);
#elif __APPLE__
    wvmcs(vcpu, VMCS_GUEST_CR0, cr0);
    wvmcs(vcpu, VMCS_GUEST_CR3, page_table_root);
    wvmcs(vcpu, VMCS_GUEST_CR4, CR4_VMXE | cr4);
    wvmcs(vcpu, VMCS_GUEST_IA32_EFER, efer);
#endif
#elif __aarch64__
    assert((mmfr & 0xF) >= 1); // At least 36 bits physical address range
    assert(((mmfr >> 28) & 0xF) != 0xF); // 4KB granule supported
    uint64_t mair_el1 =
        (0xFFUL << 0); // PT_MEM: Normal Memory, Inner Write-back non-transient (RW), Outer Write-back non-transient (RW).
    uint64_t tcr_el1 =
        (9UL << 32) |  // IPS=48 bits (256TB)
        (1UL << 23) |  // EPD1 disable higher half
        (0UL << 14) |  // TG0=4k
        (3UL << 12) |  // SH0=3 inner
        (1UL << 10) |  // ORGN0=1 write back
        (1UL << 8) |   // IRGN0=1 write back
        (0UL << 7) |   // EPD0 enable lower half
        (16UL << 0);   // T0SZ=16, 4 levels
    uint64_t sctlr_el1 =
        0xC00800UL |   // set mandatory reserved bits
        (1UL << 0);    // enable MMU
    uint64_t pstate =
        (5UL << 0);    // PSR_MODE_EL1H
#ifdef __linux__
    wreg(vcpu, MSR_ID(MAIR_EL1), mair_el1);
    wreg(vcpu, MSR_ID(TCR_EL1), tcr_el1);
    wreg(vcpu, MSR_ID(TTBR0_EL1), page_table_root);
    wreg(vcpu, MSR_ID(TTBR1_EL1), page_table_root);
    wreg(vcpu, MSR_ID(SCTLR_EL1), sctlr_el1);
    wreg(vcpu, REG_ID(regs.pstate), pstate);
#elif __APPLE__
    assert(hv_vcpu_set_sys_reg(vcpu->id, HV_SYS_REG_MAIR_EL1, mair_el1) == 0);
    assert(hv_vcpu_set_sys_reg(vcpu->id, HV_SYS_REG_TCR_EL1, tcr_el1) == 0);
    assert(hv_vcpu_set_sys_reg(vcpu->id, HV_SYS_REG_TTBR0_EL1, page_table_root) == 0);
    assert(hv_vcpu_set_sys_reg(vcpu->id, HV_SYS_REG_TTBR1_EL1, page_table_root) == 0);
    assert(hv_vcpu_set_sys_reg(vcpu->id, HV_SYS_REG_SCTLR_EL1, sctlr_el1) == 0);
    assert(hv_vcpu_set_reg(vcpu->id, HV_REG_CPSR, pstate) == 0);
#endif
#endif
    return vcpu;
}

void destroy_vcpu(struct vcpu* vcpu) {
#ifdef __linux__
    size_t vcpu_mmap_size = (size_t)ioctl(vcpu->vm->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    assert(vcpu_mmap_size > 0);
    assert(munmap(vcpu->kvm_run, vcpu_mmap_size) >= 0);
    assert(close(vcpu->fd) >= 0);
#elif __APPLE__
    assert(hv_vcpu_destroy(vcpu->id) == 0);
#endif
}

void set_program_pointers_of_vcpu(struct vcpu* vcpu, uint64_t instruction_pointer, uint64_t stack_pointer) {
#ifdef __x86_64__
#ifdef __linux__
    struct kvm_regs regs;
    memset(&regs, 0, sizeof(regs));
    regs.rflags = 1L<<1;
    regs.rip = instruction_pointer;
    regs.rsp = stack_pointer;
    vcpu_ctl(vcpu, KVM_SET_REGS, (uint64_t)&regs);
#elif __APPLE__
    wvmcs(vcpu, VMCS_GUEST_RFLAGS, 1L<<1);
    wvmcs(vcpu, VMCS_GUEST_RIP, instruction_pointer);
    wvmcs(vcpu, VMCS_GUEST_RSP, stack_pointer);
#endif
#elif __aarch64__
#ifdef __linux__
    wreg(vcpu, REG_ID(regs.pc), instruction_pointer);
    wreg(vcpu, REG_ID(sp_el1), stack_pointer);
#elif __APPLE__
    assert(hv_vcpu_set_reg(vcpu->id, HV_REG_PC, instruction_pointer) == 0);
    assert(hv_vcpu_set_sys_reg(vcpu->id, HV_SYS_REG_SP_EL1, stack_pointer) == 0);
#endif
#endif
}

void get_program_pointers_of_vcpu(struct vcpu* vcpu, uint64_t* instruction_pointer, uint64_t* stack_pointer) {
#ifdef __x86_64__
#ifdef __linux__
    struct kvm_regs regs;
    vcpu_ctl(vcpu, KVM_GET_REGS, (uint64_t)&regs);
    *instruction_pointer = regs.rip;
    *stack_pointer = regs.rsp;
#elif __APPLE__
    *instruction_pointer = rvmcs(vcpu, VMCS_GUEST_RIP);
    *stack_pointer = rvmcs(vcpu, VMCS_GUEST_RSP);
#endif
#elif __aarch64__
#ifdef __linux__
    *instruction_pointer = rreg(vcpu, REG_ID(regs.pc));
    *stack_pointer = rreg(vcpu, REG_ID(sp_el1));
#elif __APPLE__
    assert(hv_vcpu_get_reg(vcpu->id, HV_REG_PC, instruction_pointer) == 0);
    assert(hv_vcpu_get_sys_reg(vcpu->id, HV_SYS_REG_SP_EL1, stack_pointer) == 0);
#endif
#endif
}

void run_vcpu(struct vcpu* vcpu) {
    int stop = 0;
    while(!stop) {
#ifdef __linux__
        vcpu_ctl(vcpu, KVM_RUN, 0);
        uint32_t exit_reason = vcpu->kvm_run->exit_reason;
#elif __APPLE__
        assert(hv_vcpu_run(vcpu->id) == 0);
#ifdef __x86_64__
        uint32_t exit_reason = (uint32_t)rvmcs(vcpu, VMCS_RO_EXIT_REASON);
#elif __aarch64__
        uint32_t exit_reason = vcpu->exit->reason;
#endif
#endif
        switch(exit_reason) {
#ifdef __linux__
#ifdef __x86_64__
            case KVM_EXIT_HLT:
                printf("HLT\n");
                stop = 1;
                break;
#elif __aarch64__
            case KVM_EXIT_SYSTEM_EVENT:
                switch(vcpu->kvm_run->system_event.type) {
                    case KVM_SYSTEM_EVENT_SHUTDOWN:
                        printf("EVENT_SHUTDOWN\n");
                        break;
                    case KVM_SYSTEM_EVENT_RESET:
                        printf("EVENT_RESET\n");
                        break;
                    case KVM_SYSTEM_EVENT_CRASH:
                        printf("EVENT_CRASH\n");
                        break;
                    default:
                        printf("Unhandled KVM_EXIT_SYSTEM_EVENT\n");
                        break;
                }
                stop = 1;
                break;
#endif
#elif __APPLE__
#ifdef __x86_64__
            case VMX_REASON_HLT:
                printf("HLT\n");
                stop = 1;
                break;
            case VMX_REASON_IRQ:
                printf("IRQ\n");
                break;
            case VMX_REASON_EPT_VIOLATION:
                printf("EPT_VIOLATION\n");
                break;
#elif __aarch64__
            case HV_EXIT_REASON_CANCELED:
                printf("CANCELED\n");
                stop = 1;
                break;
            case HV_EXIT_REASON_EXCEPTION:
                printf("EXCEPTION\n");
                stop = 1;
                break;
            case HV_EXIT_REASON_VTIMER_ACTIVATED:
                printf("VTIMER_ACTIVATED\n");
                break;
            case HV_EXIT_REASON_UNKNOWN:
                printf("UNKNOWN\n");
                stop = 1;
                break;
#endif
#endif
            default:
                fprintf(stderr,	"Unexpected exit %u %d\n", exit_reason & 0xFFFFFF, (uint8_t)(exit_reason >> 31));
                stop = 1;
                break;
        }
    }
}
