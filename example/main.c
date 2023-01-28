#include <stdio.h>
#include <rift.h>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // Configure vm, vcpu and load an object file
    struct vm* vm = create_vm();
    struct loaded_object* loaded_object = load_object_file(vm, "build/payload");
    struct vcpu* vcpu = create_vcpu_for_object_file(loaded_object);

    // Run the object file
    run_vcpu(vcpu);

    // Cleanup
    destroy_vcpu(vcpu);
    unload_object_file(loaded_object);
    destroy_vm(vm);

    return 0;
}
