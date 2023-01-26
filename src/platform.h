#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __linux__
#include <fcntl.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/kvm.h>
#ifdef __aarch64__
#include <malloc.h>
#include <stddef.h>
#endif
#elif __APPLE__
#ifdef __x86_64__
#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vmx.h>
#include <Hypervisor/hv_arch_vmx.h>
#elif __aarch64__
#include <Hypervisor/Hypervisor.h>
#endif
#endif

#ifdef __x86_64__
#include "arch/x86_64.h"
#elif __aarch64__
#include "arch/aarch64.h"
#endif

#include <rift.h>

struct vm {
    struct host_to_guest_mapping mappings[32];
#ifdef __linux__
    int kvm_fd, fd;
#endif
};

#ifdef __linux__
void vm_ctl(struct vm* vm, uint32_t request, uint64_t param);
#endif

struct vcpu {
    struct vm* vm;
#ifdef __linux__
    int fd;
    struct kvm_run* kvm_run;
#elif __APPLE__
#ifdef __x86_64__
    hv_vcpuid_t id;
#elif __aarch64__
    hv_vcpu_t id;
    hv_vcpu_exit_t* exit;
#endif
#endif
};
