/* main.c — titik masuk interpreter Inti
 *
 * Pemakaian: inti [opsi] file.inti [-- argumen? (belum didukung)]
 * Opsi:
 *   --arena-mb N   ukuran arena memori dalam MB (bawaan 64)
 *   --stack-mb N   ukuran tumpukan nilai dalam MB (bawaan 16)
 *   --frames N     kapasitas frame pemanggilan (bawaan 262144)
 *   -h / --help    bantuan
 *   -v / --version versi
 */
#include "inti.h"

static void read_file(const char *path, char **out, size_t *outlen) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "inti: tidak bisa membuka '%s'\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fprintf(stderr, "inti: gagal membaca '%s'\n", path); exit(1); }
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc((size_t)sz + 1);
    if (sz && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "inti: gagal membaca '%s'\n", path);
        exit(1);
    }
    buf[sz] = 0;
    fclose(f);
    *out = buf; *outlen = (size_t)sz;
}

static void usage(void) {
    printf("Inti v%s — bahasa tingkat rendah bergaya assembly untuk Linux x86-64\n"
           "Pemakaian: inti [opsi] file.inti\n"
           "  --arena-mb N   ukuran arena memori (MB, bawaan 64)\n"
           "  --stack-mb N   ukuran tumpukan nilai (MB, bawaan 16)\n"
           "  --frames N     kapasitas frame pemanggilan (bawaan 262144)\n"
           "  -h, --help     bantuan ini\n"
           "  -v, --version  versi\n", INTI_VERSION);
}

int main(int argc, char **argv) {
    int64_t arena_mb = 64, stack_mb = 16, frames = 262144;
    const char *file = NULL;

    for (int k = 1; k < argc; k++) {
        const char *a = argv[k];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(); return 0; }
        else if (!strcmp(a, "-v") || !strcmp(a, "--version")) { printf("inti v%s\n", INTI_VERSION); return 0; }
        else if (!strcmp(a, "--arena-mb") && k + 1 < argc) arena_mb = atoll(argv[++k]);
        else if (!strcmp(a, "--stack-mb") && k + 1 < argc) stack_mb = atoll(argv[++k]);
        else if (!strcmp(a, "--frames")   && k + 1 < argc) frames = atoll(argv[++k]);
        else if (a[0] == '-' && a[1]) { fprintf(stderr, "inti: opsi tidak dikenal '%s'\n", a); return 1; }
        else file = a;
    }
    if (!file) { usage(); return 1; }
    if (arena_mb <= 0 || stack_mb <= 0 || frames <= 0) {
        fprintf(stderr, "inti: nilai opsi harus positif\n");
        return 1;
    }

    char *src; size_t len;
    read_file(file, &src, &len);

    Prog *p = compile_source(src, len,
                             arena_mb * 1024 * 1024,
                             (stack_mb * 1024 * 1024) / (int64_t)sizeof(int64_t),
                             frames);
    vm_run(p);
    return 0;
}
