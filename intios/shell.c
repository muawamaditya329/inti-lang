/* shell.c — shell IntiOS: banner, parser perintah, runner program user
 *
 * Perintah: help, list, run <nama>, mem, clear, shutdown.
 * Program user dieksekusi CPL3 lewat enter_user(); exit() program
 * memulihkan shell via user_die() (lihat sys.S).
 */
#include <stdint.h>
#include "layout.h"
#include "console.h"
#include "kstring.h"

int input_getchar(void);
void enter_user(uint64_t rip, uint64_t arena_base, uint64_t arena_size);

struct ProgEnt { const char *name; const void *start; uint64_t size; };
extern const struct ProgEnt prog_table[];
extern int64_t g_last_exit;

/* ---------------- readline dengan echo ---------------- */
static void readline(char *buf, int cap) {
    int n = 0;
    for (;;) {
        int c = input_getchar();
        if (c == 0x100) {                      /* EOF: baris kosong */
            if (n == 0) { buf[0] = 0; return; }
            break;
        }
        if (c == '\n') break;
        if (c == '\b' || c == 0x7F) {
            if (n > 0) {
                n--;
                console_putc('\b'); console_putc(' '); console_putc('\b');
            }
            continue;
        }
        if (c >= 32 && c < 127 && n < cap - 1) {
            buf[n++] = (char)c;
            console_putc((char)c);             /* echo */
        }
    }
    buf[n] = 0;
}

/* ---------------- jalankan program embedded ---------------- */
static void run_named(const char *name) {
    for (const struct ProgEnt *p = prog_table; p->name; p++) {
        if (kstrcmp(p->name, name) == 0) {
            if (p->size > PROG_MAX) {
                kprintf("program '%s' terlalu besar (%u byte, maks %u)\n",
                        name, p->size, PROG_MAX);
                return;
            }
            kmemcpy((void *)PROG_BASE, p->start, p->size);
            kprintf("=== %s: mulai (CPL3, %u byte) ===\n", name, p->size);
            g_last_exit = -1;
            /* .ibin: header 64B (magic u64 di +0, entry_off u32 di +8);
             * kode nyata mulai di PROG_BASE + entry_off, bukan +0. */
            uint32_t entry_off =
                *(const uint32_t *)((const uint8_t *)p->start + 8);
            if (entry_off >= p->size) {
                kprintf("header rusak: entry_off=%u\n", entry_off);
                return;
            }
            enter_user(PROG_BASE + entry_off, ARENA_BASE, ARENA_SIZE);
            kprintf("\n=== %s: selesai (exit=%d) ===\n", name, (int)g_last_exit);
            return;
        }
    }
    kprintf("tidak ada program '%s' (coba 'list')\n", name);
}

/* ---------------- shell utama ---------------- */
void shell(void) {
    static char line[192];

    kprintf("\nIntiOS 0.1.0 - kernel Inti (x86-64, boot BIOS nyata)\n");
    kprintf("ketik 'help' untuk daftar perintah\n");

    for (;;) {
        kprintf("\ninti> ");
        readline(line, (int)sizeof line);
        kprintf("\n");
        if (!line[0]) continue;

        if (kstrcmp(line, "help") == 0) {
            kprintf("perintah:\n");
            kprintf("  help        daftar perintah\n");
            kprintf("  list        program Inti ter-embed di image kernel\n");
            kprintf("  run <nama>  jalankan program (CPL3, syscall nyata)\n");
            kprintf("  mem         peta memori\n");
            kprintf("  clear       bersihkan layar\n");
            kprintf("  shutdown    matikan via ACPI S5\n");
        } else if (kstrcmp(line, "list") == 0) {
            kprintf("program ter-embed:\n");
            for (const struct ProgEnt *p = prog_table; p->name; p++)
                kprintf("  %-9s %u byte\n", p->name, p->size);
        } else if (kstrncmp(line, "run ", 4) == 0) {
            run_named(line + 4);
        } else if (kstrcmp(line, "mem") == 0) {
            kprintf("peta memori (identity map 1 GB, halaman 2 MB):\n");
            kprintf("  kernel       %p (%u KB maks)\n", KLOAD, 128);
            kprintf("  kernel stack %p\n", KSTACK_TOP);
            kprintf("  exception    %p (TSS.RSP0)\n", RSP0_TOP);
            kprintf("  program user %p (maks %u KB)\n", PROG_BASE, 1024);
            kprintf("  stack user   %p (8 MB)\n", USTACK_TOP);
            kprintf("  arena user   %p (%u MB)\n", ARENA_BASE, 32);
        } else if (kstrcmp(line, "clear") == 0) {
            vga_clear();
        } else if (kstrcmp(line, "shutdown") == 0) {
            extern void acpi_poweroff(void) __attribute__((noreturn));
            acpi_poweroff();
        } else {
            kprintf("perintah tidak dikenal: '%s' (coba 'help')\n", line);
        }
    }
}
