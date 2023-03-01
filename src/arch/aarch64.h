#define NUMBER_OF_REGISTERS 34

// Page table entry
#define PT_PRE           (1UL << 0)   // present / valid
#define PT_NOT_LEAF      (1UL << 1)   // table not a block
#define PT_MEM           (0UL << 2)   // attribute index: normal memory
#define PT_USER          (1UL << 6)   // unprivileged
#define PT_RO            (1UL << 7)   // read-only
#define PT_OSH           (2UL << 8)   // outter shareable
#define PT_ISH           (3UL << 8)   // inner shareable
#define PT_ACC           (1UL << 10)  // accessed flag
#define PT_NG            (1UL << 11)  // remove from TLB on context switch
#define PT_CONT          (1UL << 52)  // contiguous
#define PT_PNX           (1UL << 53)  // no execute (privileged)
#define PT_NX            (1UL << 54)  // no execute

// MSRs
#define ID_AA64MMFR0_EL1 0xC038
#define SCTLR_EL1        0xC080
#define TTBR0_EL1        0xC100
#define TTBR1_EL1        0xC101
#define TCR_EL1          0xC102
#define MAIR_EL1         0xC510
#define VBAR_EL1         0xC600
