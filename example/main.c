#include <assert.h>
#include <rift.h>

#ifdef __APPLE__
#define SYMBOL_NAME_PREFIX "_"
#else
#define SYMBOL_NAME_PREFIX
#endif

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Configure vm, vcpu and load an object file
    struct vm* vm = create_vm();
    struct loaded_object* loaded_object = create_loaded_object(vm, "build/payload");
    struct vcpu* vcpu = create_vcpu_for_loaded_object(loaded_object, SYMBOL_NAME_PREFIX "start");

    // Run the object file
    run_vcpu(vcpu);

    // Check result
    uint8_t* data;
    assert(resolve_symbol_host_address_in_loaded_object(loaded_object, false, SYMBOL_NAME_PREFIX "data", 4, (void**)&data));
    assert(data[0] == 1 && data[1] == 5 && data[2] == 3 && data[3] == 4);

    // Cleanup
    destroy_vcpu(vcpu);
    destroy_loaded_object(loaded_object);
    destroy_vm(vm);

    return 0;
}
