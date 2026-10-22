/* kmain.c — titik masuk C kernel IntiOS
 *
 * Dipanggil kstart64 (kernel.S) dengan stack 0x200000 dan .bss sudah nol.
 * Urutan init: konsol -> GDT/TSS/IDT/PIC/MSR -> shell.
 */
#include <stdint.h>
#include "layout.h"
#include "console.h"

void cpu_init(void);
void shell(void);

void kmain(void) {
    console_init();

    kprintf("\n[intios] kmain: long mode 64-bit aktif, kernel di 0x100000\n");
    kprintf("[intios] boot BIOS nyata: int13h extended read, A20, paging 2MB\n");

    cpu_init();
    kprintf("[intios] GDT/TSS/IDT terpasang, PIC dimask, SYSCALL/SYSRET siap\n");
    kprintf("[intios] memori: program 0x400000 | stack user 0x800000-0x1000000\n");
    kprintf("[intios]         arena 0x1000000-0x3000000 (32 MB, first-fit)\n");

    shell();
}
