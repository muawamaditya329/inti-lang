/* elfw.c — penulis ELF64 langsung (tanpa assembler/linker eksternal)
 *
 * Layout image:
 *   [page 0]  ELF header (64 B) + 2 program header (2x56 B) + .text (R+X)
 *   [page N]  .data global (R+W)  — file offset & vaddr selalu align 4096
 *
 * Segmen:
 *   PT_LOAD #1: offset 0        vaddr BASE           R+X  (header+teks+rodata)
 *   PT_LOAD #2: offset data_off vaddr BASE+data_off  R+W  (global)
 */
#include "cg.h"
#include <stdio.h>
#include <sys/stat.h>

static void fw8(FILE *f, unsigned v)  { fputc((int)(v & 0xff), f); }
static void fw16(FILE *f, unsigned v) { fw8(f, v); fw8(f, v >> 8); }
static void fw32(FILE *f, uint32_t v) { fw16(f, v & 0xffff); fw16(f, v >> 16); }
static void fw64(FILE *f, uint64_t v) { fw32(f, (uint32_t)v); fw32(f, (uint32_t)(v >> 32)); }

int elf64_write(const char *path, const Buf *text,
                const uint8_t *data, size_t data_sz, uint64_t entry) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "inti-cc: tidak bisa menulis '%s'\n", path); return 1; }

    size_t text_sz  = text->n;
    size_t data_off = (text_sz + 0xFFF) & ~(size_t)0xFFF;
    uint64_t base   = INTI_BASE;

    /* ---- ELF header ---- */
    fw8(f, 0x7f); fw8(f, 'E'); fw8(f, 'L'); fw8(f, 'F');
    fw8(f, 2);      /* EI_CLASS: 64-bit  */
    fw8(f, 1);      /* EI_DATA:  little-endian */
    fw8(f, 1);      /* EI_VERSION */
    fw8(f, 0);      /* EI_OSABI: System V */
    fw8(f, 0);      /* EI_ABIVERSION */
    for (int i = 0; i < 7; i++) fw8(f, 0);        /* pad */
    fw16(f, 2);     /* e_type: ET_EXEC */
    fw16(f, 62);    /* e_machine: EM_X86_64 */
    fw32(f, 1);     /* e_version */
    fw64(f, entry); /* e_entry */
    fw64(f, 64);    /* e_phoff */
    fw64(f, 0);     /* e_shoff: tanpa section header */
    fw32(f, 0);     /* e_flags */
    fw16(f, 64);    /* e_ehsize */
    fw16(f, 56);    /* e_phentsize */
    fw16(f, 2);     /* e_phnum */
    fw16(f, 0);     /* e_shentsize */
    fw16(f, 0);     /* e_shnum */
    fw16(f, 0);     /* e_shstrndx */

    /* ---- phdr[0]: R+X (header + kode + rodata) ---- */
    fw32(f, 1);                 /* PT_LOAD */
    fw32(f, 5);                 /* PF_R | PF_X */
    fw64(f, 0);                 /* p_offset */
    fw64(f, base);              /* p_vaddr */
    fw64(f, base);              /* p_paddr */
    fw64(f, data_off);          /* p_filesz */
    fw64(f, data_off);          /* p_memsz */
    fw64(f, 0x1000);            /* p_align */

    /* ---- phdr[1]: R+W (global) ---- */
    fw32(f, 1);
    fw32(f, 6);                 /* PF_R | PF_W */
    fw64(f, data_off);
    fw64(f, base + data_off);
    fw64(f, base + data_off);
    fw64(f, data_sz);
    fw64(f, data_sz);
    fw64(f, 0x1000);

    /* ---- isi image ----
     * TXT memesan INTI_CODE_OFF byte di depan untuk header; elfw menulis
     * header sendiri di file offset 0, lalu isi teks mulai file offset 0xB0
     * (= TXT offset 0xB0). Pemetaan vaddr: BASE + offset_TXT tetap konsisten
     * dengan fix_resolve. */
    if (text_sz > INTI_CODE_OFF)
        fwrite(text->b + INTI_CODE_OFF, 1, text_sz - INTI_CODE_OFF, f);
    for (size_t i = text_sz; i < data_off; i++) fw8(f, 0);   /* pad ke batas halaman */
    if (data_sz) fwrite(data, 1, data_sz, f);

    if (fclose(f) != 0) { fprintf(stderr, "inti-cc: gagal menulis '%s'\n", path); return 1; }
    if (chmod(path, 0755) != 0) {
        fprintf(stderr, "inti-cc: gagal chmod '%s'\n", path);
        return 1;
    }
    return 0;
}
