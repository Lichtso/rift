# Rift
Rift is a cross-platform C library which enables a user space process to run individual threads in kernel space.
Kernel space here only refers to a supervisor privileged execution environment, not the kernel of the operating system the process runs in. 
It does so by utilizing hardware virtualization without [partitions](https://en.wikipedia.org/wiki/OS-level_virtualization) / [namespaces](https://en.wikipedia.org/wiki/Linux_namespaces), [kernel extensions](https://en.wikipedia.org/wiki/Loadable_kernel_module) or otherwise modifying the kernel.
In other words it is not an [emulator](https://en.wikipedia.org/wiki/System_virtual_machine) (running an entire operating system) or [containerization solution](https://en.wikipedia.org/wiki/Containerization_(computing)) (running entire processes),
but instead fills the niche of only virtualizing selected threads of an otherwise normal process.

## Use Cases

### Performance & Resource Efficiency
Virtualization in itself is known to be a bit slower because it comes with a more expensive address translation mechanism.
However, after initialization it can run with little to no disruptions in the control flow, apart from those caused by the outer operating systems preemptive scheduling.
Contrast this with approaches using syscalls and signal handlers which causes a lot of overhead in saving and restoring the context, changing the privilege level, invalidating the TLB and other caches, etc.

This library gives you direct access to hardware features which are only available in kernel space:
- Memory Management Unit (MMU) 
    - Page tables for hardware address translation
    - Dirty page logging / tracking
    - Cache behavior configuration
- Interrupt controller
    - Timers
    - Poking other virtual threads

### Stability & Portability
Ironically, going closer to the hardware can also be less hacky and involve less workarounds.
Operating systems and their interfaces we use today are very old, architected with long outdated assumptions, come with lots of legacy, compatibility adapters and are notoriously hard to change.

### Security & Sandboxing
Browser engines run untrusted code in [separate processes](https://blogs.windows.com/msedgedev/2020/09/30/microsoft-edge-multi-process-architecture/) with [reduced permissions](https://en.wikipedia.org/wiki/Seccomp).
With this library, one can go a step further and completely prevent untrusted (possibly JIT compiled) code from reaching the outer operating system, because syscalls do not lead anywhere.

### Evaluation & Education
It is surprisingly hard (especially on x86-64, not so much on AArch64) to get a bare metal ISA machine without the hassle of setting up the entire execution environment (think firmware, boot loader, device drivers, kernel, etc.) correctly. Developers and students can experiment and prototype in kernel space with relative ease using this library instead.

## Supported Platforms
- Operating Systems: Linux (using [KVM](https://www.kernel.org/doc/Documentation/virtual/kvm/api.txt)) and macOS (using [HVF](https://developer.apple.com/documentation/hypervisor))
- CPU ISAs: x86-64 / amd64, AArch64 / arm64
- Executable Formats: ELF, Mach-O
- Compilers: GCC, LLVM Clang
- Remote Debugger: GDB 12, LLDB 14

## Getting Started
Checkout, build, run example in test mode.
```bash
git clone https://github.com/Lichtso/rift.git
cd rift/
make
build/example -t
```

If you use VSCode and have the [CodeLLDB extension](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb) installed,
you can also start the example as debugger server and then use the "Run and Debug" section in VSCode.
```bash
build/example -d
```

## Known Issues
- On x86-64 macOS `VMX_REASON_EPT_VIOLATION` is triggered for every guest page when it is first accessed.
- On x86-64 macOS `VMX_REASON_IRQ` is triggered for every time the thread is preempted by the watchdog timer of the outer kernel.
- GDB does not accept the x86-64 arch description XML.

## Shortcomings / Future Work
- Shared memory is currently the only form of communication with these threads as there are no other synchronization mechanisms (mutex, semaphore, barrier, etc.) for them.
- Interrupt controllers and interrupt handling are not implemented.
- Furthermore, spawning child processes by forking is undefined behavior for now.
