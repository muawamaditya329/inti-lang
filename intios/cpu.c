/* cpu.c — GDT, TSS, IDT, PIC, MSR SYSCALL/SYSRET
 *
 * Segmen (GDT, selector):
 *   0x00 null            0x08 kernel code (0xAF9A)    0x10 kernel data (0xCF92)
 *   0x18 user code (0xAFFA)   0x20 user data (0xCFF2)      0x28 TSS (64-bit)
 *
 * MSR:
 *   EFER.SCE  = 1                  -> instruksi syscall/sysret aktif
 *   LSTAR     = sys_entry          -> target syscall
 *   STAR      = (0x1B<<48)|(0x08<<32) -> CS/SS kernel & user
 *   FMASK     = 0                  -> RFLAGS user dijaga penuh di R11
 *
 * IDT: 256 gate interrupt (IF di-clear saat masuk). Vektor 0/6/13/14 ->
 * stub dengan penanganan error code benar; sisanya -> exc_gp umum.
 */
#include <stdint.h>
#include "layout.h"
#include "console.h"

static inline void outw_(uint16_t p, uint16_t v) {
    __asm__ volatile ("out %0, %1" :: "a"(v), "Nd"(p));
}
static inline void outb_(uint16_t p, uint8_t v) {
    __asm__ volatile ("out %0, %1" :: "a"(v), "Nd"(p));
}
static inline void wrmsr_(uint32_t msr, uint64_t v) {
    uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);
    __asm__ volatile ("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}
static inline uint64_t rdmsr_(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

/* ---------------- TSS ---------------- */
static uint8_t tss[104] __attribute__((aligned(16)));

/* ---------------- GDT ---------------- */
static uint64_t gdt[8] __attribute__((aligned(16)));
static struct { uint16_t len; uint64_t base; } __attribute__((packed)) gdtr;
static struct { uint16_t len; uint64_t base; } __attribute__((packed)) idtr;

static void gdt_put(int i, uint64_t e) { gdt[i] = e; }

static void gdt_put_tss(int i) {
    uint64_t base = (uint64_t)(uintptr_t)tss;
    uint64_t lim  = sizeof tss - 1;
    gdt[i]   = (lim & 0xFFFF)
             | ((base & 0xFFFFFF) << 16)
             | ((uint64_t)0x89 << 40)              /* TSS available, 64-bit */
             | ((uint64_t)((lim >> 16) & 0xF) << 48)
             | ((uint64_t)((base >> 24) & 0xFF) << 56);
    gdt[i + 1] = base >> 32;
}

/* ---------------- IDT ---------------- */
static uint64_t idt[256 * 2] __attribute__((aligned(16)));

extern void exc_de(void), exc_ud(void), exc_gp(void), exc_pf(void);

static void idt_put(int vec, void *h) {
    uint64_t a = (uint64_t)h;
    idt[vec * 2 + 0] = (a & 0xFFFF)
                     | ((uint64_t)0x08 << 16)          /* selector kernel code */
                     | ((uint64_t)0x8E00 << 32);       /* present, int gate 64 */
    idt[vec * 2 + 1] = a >> 32;
}

/* ---------------- helper system ---------------- */
static inline void lgdt_(void *p) { __asm__ volatile ("lgdt (%0)" :: "r"(p) : "memory"); }
static inline void lidt_(void *p) { __asm__ volatile ("lidt (%0)" :: "r"(p) : "memory"); }
static inline void ltr_(uint16_t s) { __asm__ volatile ("ltr %0" :: "r"(s)); }

extern void sys_entry(void);

void cpu_init(void) {
    /* PIC: mask SEMUA interupsi — kernel polling murni, tidak ada IRQ */
    outb_(0x21, 0xFF);
    outb_(0xA1, 0xFF);

    /* GDT */
    gdt_put(1, 0x00AF9A000000FFFFuLL);    /* 0x08 kernel code 64-bit (L=1) */
    gdt_put(2, 0x00CF92000000FFFFuLL);    /* 0x10 kernel data */
    gdt_put(3, 0x00AFFA000000FFFFuLL);    /* 0x18 user code   */
    gdt_put(4, 0x00CFF2000000FFFFuLL);    /* 0x20 user data   */
    gdt_put_tss(5);                       /* 0x28 TSS (2 qword) */
    gdtr.len = sizeof gdt - 1;
    gdtr.base = (uint64_t)(uintptr_t)gdt;
    lgdt_(&gdtr);

    /* reload CS: CS masih menunjuk deskriptor kode32 (L=0) dari GDT boot —
     * wajib dimuat ulang agar cache deskriptor = kode 64-bit milik kernel */
    __asm__ volatile (
        "lea 1f(%%rip), %%rax\n"
        "push $0x08\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        ::: "ax", "memory", "cc");

    /* segmen data */
    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        ::: "ax");

    /* TSS: RSP0 = stack exception (dipakai CPL3 -> CPL0 via IDT) */
    *(uint64_t *)(tss + 4) = RSP0_TOP;
    ltr_(0x28);

    /* IDT */
    for (int v = 0; v < 256; v++) idt_put(v, (void *)exc_gp);
    idt_put(0,  (void *)exc_de);
    idt_put(6,  (void *)exc_ud);
    idt_put(13, (void *)exc_gp);
    idt_put(14, (void *)exc_pf);
    idtr.len = sizeof idt - 1;
    idtr.base = (uint64_t)(uintptr_t)idt;
    lidt_(&idtr);

    /* MSR SYSCALL/SYSRET */
    wrmsr_(0xC0000080, rdmsr_(0xC0000080) | 1);                 /* EFER.SCE */
    wrmsr_(0xC0000081, ((uint64_t)0x1B << 48) | ((uint64_t)0x08 << 32)); /* STAR */
    wrmsr_(0xC0000082, (uint64_t)(uintptr_t)sys_entry);         /* LSTAR    */
    wrmsr_(0xC0000084, 0);                                      /* FMASK    */
}

void acpi_poweroff(void) {
    kprintf("IntiOS: mematikan mesin (ACPI S5, port 0x604)...\n");
    outw_(PM1A_CNT, SLP_S5);
    for (;;) __asm__ volatile ("hlt");
}
