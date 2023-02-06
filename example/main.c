#include <assert.h>
#include <unistd.h>
#include <rift.h>

#ifdef __APPLE__
#define SYMBOL_NAME_PREFIX "_"
#else
#define SYMBOL_NAME_PREFIX
#endif

int main(int argc, char** argv) {
    // Configure vm, vcpu and load an object file
    struct vm* vm = create_vm();
    struct loaded_object* loaded_object = create_loaded_object(vm, "build/payload");
    struct vcpu* vcpu = create_vcpu_for_loaded_object(loaded_object, SYMBOL_NAME_PREFIX "start");

    int opt;
    while((opt = getopt(argc, argv, "dt")) != -1) {
        switch(opt) {
            case 'd': {
                // Run debugger server
                struct vcpu* vcpus[1] = { vcpu };
                struct debugger_server* debugger = create_debugger_server(sizeof(vcpus) / sizeof(vcpus[0]), vcpus, 12345, true);
                run_debugger_server(debugger);
                destroy_debugger_server(debugger);
            } break;
            case 't': {
                // Run test and check results
                run_vcpu(vcpu);
                uint8_t* data;
                assert(resolve_symbol_host_address_in_loaded_object(loaded_object, false, SYMBOL_NAME_PREFIX "data", 4, (void**)&data));
                assert(data[0] == 1 && data[1] == 5 && data[2] == 3 && data[3] == 4);
            } break;
        }
    }

    // Cleanup
    destroy_vcpu(vcpu);
    destroy_loaded_object(loaded_object);
    destroy_vm(vm);

    return 0;
}
