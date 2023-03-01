static uint64_t host_page_size = 0;

#if __linux__
#include <linux/userfaultfd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

static int uffd = 0;

void* page_fault_handler(void* arg) {
    (void)arg;
    struct pollfd pollfd[1];
    struct uffd_msg request;
    while(1) {
        pollfd[0].fd = uffd;
        pollfd[0].events = POLLIN;
        int nready = poll(pollfd, sizeof(pollfd) / sizeof(pollfd[0]), -1);
        assert(nready == 1 && (pollfd[0].revents & POLLIN) && !(pollfd[0].revents & POLLERR));
        ssize_t read_result = read(uffd, &request, sizeof(request));
        if(read_result == -1) {
            assert(errno == EAGAIN);
            continue;
        }
        assert(read_result == sizeof(request));
        assert(request.event & UFFD_EVENT_PAGEFAULT);
        uint64_t page_address = (uint64_t)request.arg.pagefault.address / host_page_size * host_page_size;
        if(request.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP) {
            struct uffdio_writeprotect response;
            response.range.start = page_address;
            response.range.len = host_page_size;
            response.mode = 0;
            assert(ioctl(uffd, UFFDIO_WRITEPROTECT, &response) >= 0);
        } else {
            struct uffdio_zeropage response;
            response.range.start = page_address;
            response.range.len = host_page_size;
            response.mode = 0;
            assert(ioctl(uffd, UFFDIO_ZEROPAGE, &response) >= 0);
            assert(response.zeropage == (ssize_t)host_page_size);
        }
    }
    return NULL;
}

void register_page_fault_handler(void* base, uint64_t length) {
    host_page_size = (uint64_t)sysconf(_SC_PAGESIZE);
    assert(host_page_size > 0);
    uffd = (int)syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    assert(uffd >= 0);
    struct uffdio_api uffdio_api = {
        .api = UFFD_API,
        .features = UFFD_FEATURE_PAGEFAULT_FLAG_WP,
    };
    assert(ioctl(uffd, UFFDIO_API, &uffdio_api) >= 0);
    struct uffdio_register uffdio_register = {
        .range = { .start = (unsigned long)base, .len = length },
        .mode = UFFDIO_REGISTER_MODE_MISSING | UFFDIO_REGISTER_MODE_WP,
    };
    assert(ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) >= 0);
    assert(uffdio_register.ioctls & (UFFD_API_RANGE_IOCTLS | UFFD_API_IOCTLS));
    struct uffdio_writeprotect uffdio_wp = {
        .range = { .start = (unsigned long)base, .len = length },
        .mode = UFFDIO_WRITEPROTECT_MODE_WP,
    };
    assert(ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) >= 0);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, page_fault_handler, NULL) >= 0);
}
#else
void page_fault_handler(int signal, siginfo_t* info, void* context) {
    (void)signal;
    (void)context;
    uint64_t page_address = (uint64_t)info->si_addr / host_page_size * host_page_size;
    assert(mprotect((void*)page_address, host_page_size, PROT_READ | PROT_WRITE) >= 0);
}

void register_page_fault_handler(void* base, uint64_t length) {
    host_page_size = (uint64_t)sysconf(_SC_PAGESIZE);
    assert(host_page_size > 0);
    struct sigaction action;
    action.sa_sigaction = page_fault_handler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    assert(sigaction(SIGBUS, &action, NULL) >= 0);
    assert(sigaction(SIGSEGV, &action, NULL) >= 0);
    assert(mprotect(base, length, PROT_READ) >= 0);
}
#endif
