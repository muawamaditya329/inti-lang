#include <stdio.h>
/* fib.c — fibonacci rekursif fib(27) (identik dengan fib.inti) */
long long fib(long long n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main(void) {
    printf("%lld\n", fib(27));
    return 0;
}
