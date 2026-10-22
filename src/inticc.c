/* inticc.c — titik masuk compiler native bahasa Inti (Fase 1-3)
 *
 * Pemakaian: inti-cc [opsi] file.inti -o keluaran
 * Alur: source -> parse.c (bytecode, dibagi dengan interpreter)
 *       -> codegen.c (kode mesin x86-64 + runtime mesin)
 *       -> elfw.c    (ELF64 statis, tanpa libc, tanpa assembler/linker)
 *       atau bin     (flat .ibin untuk IntiOS, Fase 3)
 *
 * Opsi:
 *   -o FILE           nama binary keluaran (wajib)
 *   --target=elf      ELF64 statis Linux (bawaan)
 *   --target=intios   flat binary .ibin untuk kernel IntiOS
 *   --arena-mb N      arena memori dibakar ke binary (MB, bawaan 64)
 *   --stack-mb N      ukuran stack mmap (MB, bawaan 16 — rekursi dalam)
 *   -h / --help       bantuan
 *   -v / --version    versi
 */
#include "inti.h"
#include "cg.h"

static void read_file(const char *path, char **out, size_t *outlen) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "inti-cc: tidak bisa membuka '%s'\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fprintf(stderr, "inti-cc: gagal membaca '%s'\n", path); exit(1); }
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc((size_t)sz + 1);
    if (sz && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "inti-cc: gagal membaca '%s'\n", path);
        exit(1);
    }
    buf[sz] = 0;
    fclose(f);
    *out = buf; *outlen = (size_t)sz;
}

static void usage(void) {
    printf("inti-cc v%s — compiler native Inti (x86-64)\n"
           "Target: ELF64 statis Linux (freestanding, syscall langsung)\n"
           "        atau flat binary .ibin untuk kernel IntiOS\n\n"
           "Pemakaian: inti-cc [opsi] file.inti -o keluaran\n"
           "  -o FILE           nama binary keluaran (wajib)\n"
           "  --target=elf      ELF64 statis Linux (bawaan)\n"
           "  --target=intios   flat binary untuk kernel IntiOS (arena 32 MB)\n"
           "  --arena-mb N      arena memori dibakar ke binary (MB, bawaan 64)\n"
           "  --stack-mb N      ukuran stack mmap (MB, bawaan 16; diabaikan utk intios)\n"
           "  -h, --help        bantuan ini\n"
           "  -v, --version     versi\n", INTI_VERSION);
}

int main(int argc, char **argv) {
    int64_t arena_mb = 64, stack_mb = 16;
    const char *file = NULL, *out = NULL;
    int target = TARGET_ELF;

    for (int k = 1; k < argc; k++) {
        const char *a = argv[k];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("inti-cc v%s\n", INTI_VERSION); return 0; }
        else if (!strcmp(a, "-o") && k + 1 < argc) out = argv[++k];
        else if (!strcmp(a, "--target=elf")) target = TARGET_ELF;
        else if (!strcmp(a, "--target=intios")) target = TARGET_INTIOS;
        else if (!strcmp(a, "--arena-mb") && k + 1 < argc) arena_mb = atoll(argv[++k]);
        else if (!strcmp(a, "--stack-mb") && k + 1 < argc) stack_mb = atoll(argv[++k]);
        else if (a[0] == '-' && a[1]) { fprintf(stderr, "inti-cc: opsi tidak dikenal '%s'\n", a); return 1; }
        else file = a;
    }
    if (!file || !out) { usage(); return 1; }
    if (arena_mb <= 0 || stack_mb <= 0 || arena_mb > 65536 || stack_mb > 65536) {
        fprintf(stderr, "inti-cc: nilai --arena-mb/--stack-mb harus 1..65536\n");
        return 1;
    }
    if (target == TARGET_INTIOS && arena_mb != 32) {
        /* kernel IntiOS menyediakan arena tetap 32 MB — paksa konsisten */
        arena_mb = 32;
    }

    char *src; size_t len;
    read_file(file, &src, &len);

    /* kap vstack/fstack tidak relevan untuk native (stack = mmap) — nilai bawaan */
    Prog *p = compile_source(src, len,
                             arena_mb * 1024 * 1024,
                             (16 * 1024 * 1024) / (int64_t)sizeof(int64_t),
                             262144);
    return codegen_emit(p, arena_mb, stack_mb, out, target);
}
