#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#ifdef __linux__
#include <stddef.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>
#ifdef __aarch64__
#include <malloc.h>
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

#include <rift.h>
#include <guest.h>

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
    struct host_to_guest_mapping* page_table;
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
