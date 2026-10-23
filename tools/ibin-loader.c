/*
 * ibin-loader.c — pemuat flat binary .ibin (ABI IntiOS) di atas Linux.
 *
 * Bukti lintas-platform: blob .ibin yang sama dijalankan apa adanya di
 * dalam IntiOS (QEMU / hardware) DAN di Linux melalui pemuat ini.
 * Pemuat meniru persis apa yang dilakukan kernel IntIOS (lihat
 * intios/shell.c run_named + intios/layout.h):
 *
 *   blob   -> 0x400000   (PROG_BASE,  vaddr absolut sudah dibakar di kode)
 *   arena  -> 0x1000000  (ARENA_BASE, 32 MB, RDI=basis RSI=ukuran)
 *   stack  -> rsp = 0x1000000 (USTACK_TOP, tumbuh ke bawah 8 MB)
 *   entry  -> PROG_BASE + entry_off (header +8, u32)
 *
 * Syscall di dalam program (read 0 / write 1 / exit 60) dieksekusi
 * langsung oleh kernel Linux — nomornya memang kompatibel.
 *
 * Build: gcc -O2 -no-pie -Wl,-Ttext-segment=0x20000000 -o ibin-loader ibin-loader.c
 * (binari loader sendiri TIDAK boleh menempati 0x400000.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <sys/ucontext.h>
#define REG_RIP_X 16 /* REG_RIP di Linux x86-64 gregs */

static void segv_report(int sig, siginfo_t *si, void *uc) {
    ucontext_t *u = (ucontext_t *)uc;
    fprintf(stderr, "ibin-loader: SIGSEGV rip=%p addr=%p (off=0x%llx)\n",
            (void *)u->uc_mcontext.gregs[REG_RIP_X], si->si_addr,
            (unsigned long long)((uint64_t)u->uc_mcontext.gregs[REG_RIP_X] - 0x400000ULL));
    _exit(139);
}

#define PROG_BASE   0x400000ULL
#define ARENA_BASE  0x1000000ULL
#define ARENA_SIZE  0x2000000ULL   /* 32 MB */
#define USTACK_TOP  0x1000000ULL
#define USTACK_SIZE 0x800000ULL    /* 8 MB di bawah arena */
#define IP_MAGIC    0x3152544E49544E49ULL /* "INTIPRG1" LE */
#define HDRSZ       64

static uint64_t hdr_u32(const uint8_t *h, int off) {
    return (uint64_t)h[off] | ((uint64_t)h[off+1] << 8) |
           ((uint64_t)h[off+2] << 16) | ((uint64_t)h[off+3] << 24);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "penggunaan: ibin-loader program.ibin [arg...]\n");
        return 2;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror(argv[1]); return 2; }
    /* ukuran file */
    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (sz < HDRSZ) { fprintf(stderr, "ibin-loader: file terlalu kecil\n"); return 2; }
    uint8_t *blob = malloc((size_t)sz);
    if (!blob || read(fd, blob, (size_t)sz) != sz) {
        fprintf(stderr, "ibin-loader: gagal membaca %s\n", argv[1]); return 2;
    }
    close(fd);

    struct sigaction sa;
    sa.sa_sigaction = segv_report;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);

    if (*(uint64_t *)blob != IP_MAGIC) {
        fprintf(stderr, "ibin-loader: magic salah (bukan .ibin)\n"); return 2;
    }
    uint64_t entry_off = hdr_u32(blob, 8);
    uint64_t code_sz   = hdr_u32(blob, 12);
    uint64_t data_off  = hdr_u32(blob, 16);
    if (entry_off < HDRSZ || entry_off >= (uint64_t)sz ||
        data_off > (uint64_t)sz || code_sz == 0) {
        fprintf(stderr, "ibin-loader: header rusak (entry=%llu code=%u data_off=%llu)\n",
                (unsigned long long)entry_off, (unsigned)code_sz,
                (unsigned long long)data_off);
        return 2;
    }

    /* petakan wilayah persis seperti IntiOS (identity, RWX — single
     * address space, halaman 2MB di kernel, di sini halaman 4K) */
    void *q;
    uint64_t prog_len = ((uint64_t)sz + 0xFFF) & ~0xFFFULL;
    q = mmap((void *)PROG_BASE, prog_len, PROT_READ | PROT_WRITE | PROT_EXEC,
             MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (q == MAP_FAILED) { perror("mmap prog"); return 2; }
    q = mmap((void *)(USTACK_TOP - USTACK_SIZE), USTACK_SIZE,
             PROT_READ | PROT_WRITE,
             MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (q == MAP_FAILED) { perror("mmap stack"); return 2; }
    q = mmap((void *)ARENA_BASE, ARENA_SIZE, PROT_READ | PROT_WRITE,
             MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (q == MAP_FAILED) { perror("mmap arena"); return 2; }

    memcpy((void *)PROG_BASE, blob, (size_t)sz);
    /* wilayah di luar blob (sampai akhir halaman) harus nol — mmap anon
     * sudah nol, memcpy tidak menyentuhnya */

    uint64_t entry = PROG_BASE + entry_off;
    fflush(NULL);
    __asm__ volatile (
        "mov %0, %%rdi\n\t"          /* RDI = basis arena   */
        "mov %1, %%rsi\n\t"          /* RSI = ukuran arena  */
        "mov %2, %%rsp\n\t"          /* RSP = puncak stack  */
        "xor %%rbp, %%rbp\n\t"
        "and $-16, %%rsp\n\t"
        "push $0\n\t"                /* ret-address dummy (nol) */
        "jmp *%3\n"
        :: "r"((uint64_t)ARENA_BASE), "r"((uint64_t)ARENA_SIZE),
           "r"((uint64_t)USTACK_TOP), "r"(entry)
        : "rdi", "rsi", "memory");
    __builtin_unreachable();
}
