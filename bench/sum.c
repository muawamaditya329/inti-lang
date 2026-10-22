#include <stdio.h>
/* sum.c — jumlah 1..100.000.000 (identik dengan sum.inti) */
int main(void) {
    long long s = 0;
    long long i = 1;
    for (; i <= 100000000LL; i++) s += i;
    printf("%lld\n", s);
    return 0;
}
