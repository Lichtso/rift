#include <inttypes.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <rift.h>

static uint8_t* empty_pages;
static uint64_t used_memory;
#include "benchmark.h"

#ifdef __APPLE__
#define SYMBOL_NAME_PREFIX "_"
#else
#define SYMBOL_NAME_PREFIX
#endif

#define RUN_HOST_BENCHMARK(name, madv) { \
    if(madv != 0) \
        assert(madvise(empty_pages, sizeof(empty_pages), madv) == 0); \
    start_time = clock(); \
    name(); \
    end_time = clock(); \
}

#define RUN_GUEST_BENCHMARK(name, madv) { \
    void* ptr; \
    assert(resolve_symbol_host_address_in_loaded_object(loaded_object, true, SYMBOL_NAME_PREFIX "used_memory", sizeof(uint64_t), &ptr)); \
    *((uint64_t*)ptr) = used_memory; \
    if(madv != 0) { \
        assert(resolve_symbol_host_address_in_loaded_object(loaded_object, true, SYMBOL_NAME_PREFIX "empty_pages", sizeof(empty_pages), &ptr)); \
        assert(madvise(ptr, sizeof(empty_pages), madv) == 0); \
    } \
    vcpu = create_vcpu_for_loaded_object(loaded_object, SYMBOL_NAME_PREFIX #name); \
    start_time = clock(); \
    run_vcpu(vcpu); \
    end_time = clock(); \
    destroy_vcpu(vcpu); \
}

int main(int argc, char** argv) {
    // Configure vm and load an object file
    struct vm* vm = create_vm();
    struct loaded_object* loaded_object = create_loaded_object(vm, "build/payload");
    struct vcpu* vcpu;

    assert(argc > 1 && strlen(argv[1]) == 2 && argv[1][0] == '-');
    switch(argv[1][1]) {
        case 'd': {
            // Run debugger server
            vcpu = create_vcpu_for_loaded_object(loaded_object, SYMBOL_NAME_PREFIX "test");
            struct vcpu* vcpus[1] = { vcpu };
            struct debugger_server* debugger = create_debugger_server(sizeof(vcpus) / sizeof(vcpus[0]), vcpus, 12345, true);
            run_debugger_server(debugger);
            destroy_debugger_server(debugger);
            destroy_vcpu(vcpu);
        } break;
        case 't': {
            // Run test
            vcpu = create_vcpu_for_loaded_object(loaded_object, SYMBOL_NAME_PREFIX "test");
            run_vcpu(vcpu);
            destroy_vcpu(vcpu);
            // Check results
            void* data;
            assert(resolve_symbol_host_address_in_loaded_object(loaded_object, false, SYMBOL_NAME_PREFIX "data", 4, &data));
            assert(((uint8_t*)data)[1] == 5);
        } break;
        case 'b': {
            assert(argc == 4);
            int which_one;
            sscanf(argv[2], "%d", &which_one);
            sscanf(argv[3], "%" PRIx64, &used_memory);
            clock_t start_time, end_time;
            empty_pages = valloc(0x10000000UL);
            switch(which_one) {
                case 0:
                    RUN_HOST_BENCHMARK(benchmark_linear_memory_access_pattern, MADV_SEQUENTIAL);
                    break;
                case 1:
                    RUN_GUEST_BENCHMARK(benchmark_linear_memory_access_pattern, MADV_SEQUENTIAL);
                    break;
                case 2:
                    RUN_HOST_BENCHMARK(benchmark_random_memory_access_pattern, MADV_RANDOM);
                    break;
                case 3:
                    RUN_GUEST_BENCHMARK(benchmark_random_memory_access_pattern, MADV_RANDOM);
                    break;
                default:
                    assert(false);
            }
            printf("%f\n", (double)(end_time - start_time) / CLOCKS_PER_SEC);
        } break;
    }

    destroy_loaded_object(loaded_object);
    destroy_vm(vm);
    return 0;
}
