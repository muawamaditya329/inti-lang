#include <stdio.h>
#include <stdlib.h>
/* mem.c — isi buffer 4 MiB byte-per-byte lalu jumlahkan (identik dengan mem.inti) */
int main(void) {
    long N = 4194304;
    unsigned char *buf = malloc((size_t)N);
    for (long i = 0; i < N; i++) buf[i] = (unsigned char)((i * 7) & 255);
    long long s = 0;
    for (long i = 0; i < N; i++) s += buf[i];
    printf("%lld\n", s);
    return 0;
}
