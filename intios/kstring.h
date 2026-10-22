/* kstring.h — helper string/memori mini freestanding (tanpa libc) */
#ifndef INTIOS_KSTRING_H
#define INTIOS_KSTRING_H

#include <stdint.h>
#include <stddef.h>

static inline size_t kstrlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static inline int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

static inline int kstrncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (uint8_t)a[i] - (uint8_t)b[i];
    }
    return 0;
}

static inline void *kmemcpy(void *d, const void *s, size_t n) {
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    while (n--) *dd++ = *ss++;
    return d;
}

#endif /* INTIOS_KSTRING_H */
