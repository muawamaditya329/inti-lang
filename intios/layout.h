/* layout.h — peta memori IntiOS (sumber kebenaran tunggal)
 *
 * QEMU dijalankan dengan -m 256. Identity map 1 GB (halaman 2 MB,
 * P|RW|US|PS): kernel, program user, stack, dan arena semua beralamat
 * sama (virtual = fisik) — desain mini-kernel single address space.
 */
#ifndef INTIOS_LAYOUT_H
#define INTIOS_LAYOUT_H

#define KLOAD       0x100000uLL   /* kernel ter-load di sini            */
#define KSTACK_TOP  0x200000uLL   /* kernel stack kmain (64KB ke bawah) */
#define RSP0_TOP    0x1F8000uLL   /* TSS.RSP0: stack exception (32KB)   */
#define SYSSTK_TOP  0x1FF000uLL   /* stack handler syscall (28KB)       */

#define PROG_BASE   0x400000uLL   /* program user dimuat di sini        */
#define PROG_MAX    0x100000uLL   /* batas ukuran program (1 MB)        */

#define USTACK_TOP  0x1000000uLL  /* rsp user (8 MB: 0x800000..1000000) */

#define ARENA_BASE  0x1000000uLL  /* arena program user                 */
#define ARENA_SIZE  0x2000000uLL  /* 32 MB (harus cocok --arena-mb 32)  */

/* PM1a_CNT untuk shutdown ACPI (QEMU -M pc / PIIX4) */
#define PM1A_CNT    0x604u
#define SLP_S5      0x2000u

#endif /* INTIOS_LAYOUT_H */
