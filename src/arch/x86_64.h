#define NUMBER_OF_REGISTERS 18

// Page table entry
#define PT_PRE           (1UL << 0)   // present / valid
#define PT_RW            (1UL << 1)   // read-write
#define PT_USER          (1UL << 2)   // unprivileged
#define PT_ACC           (1UL << 5)   // accessed flag
#define PT_DIRTY         (1UL << 6)   // write accessed flag
#define PT_LEAF          (1UL << 7)   // block not a table
#define PT_G             (1UL << 8)   // keep in TLB on context switch
#define PT_NX            (1UL << 63)  // no execute

// CR0 bits
#define CR0_PE           (1U << 0)
#define CR0_NE           (1U << 5)
#define CR0_WP           (1U << 16)
#define CR0_PG           (1U << 31)

// CR4 bits
#define CR4_PAE          (1U << 5)
#define CR4_VMXE         (1U << 13)

// EFER bits
#define EFER_LME         (1U << 8)
#define EFER_LMA         (1U << 10)
#define EFER_NXE         (1U << 11)
