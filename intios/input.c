/* input.c — input karakter: serial COM1 + keyboard PS/2 (polling)
 *
 * Poling nyata port I/O — tanpa interupsi. Serial didahulukan (dipakai
 * otomasi CI), keyboard PS/2 port 0x60/0x64 dengan scancode set 1.
 * Karakter 0x04 (Ctrl-D) dari serial = EOF (semantik pipa Linux).
 */
#include <stdint.h>
#include "console.h"

static inline uint8_t inb_(uint16_t p) {
    uint8_t v;
    __asm__ volatile ("in %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

static int serial_ready(void) {
    return (inb_(0x3FD) & 0x01) != 0;      /* LSR bit0: data ready */
}

static int serial_get(void) {
    return inb_(0x3F8);
}

/* ---------------- keyboard PS/2 (scancode set 1) ---------------- */
static int shift;

static int kbd_decode(uint8_t sc) {
    /* indeks = scancode set 1; '?' = tak terpetakan.
     * 0x02-0x0D digit, 0x0E BS, 0x0F TAB, 0x10-0x1B q-p[], 0x1C ENTER,
     * 0x1D ctrl, 0x1E-0x28 a-l;', 0x29 `, 0x2A lshift, 0x2B \,
     * 0x2C-0x35 z-/, 0x36 rshift. */
    static const char m0[] = "\?\?1234567890-=\b\tqwertyuiop[]\n\?asdfghjkl;'`\?\\zxcvbnm,./\?\?\?";
    static const char m1[] = "\?\?!@#$%^&*()_+\?\tQWERTYUIOP{}\n\?ASDFGHJKL:\"~\?|ZXCVBNM<>?\?\?\?";
    if (sc < sizeof m0 - 1) {
        char c = shift ? m1[sc] : m0[sc];
        return (c == '?') ? -1 : (int)(uint8_t)c;
    }
    return -1;
}

static int kbd_ready(void) {
    return (inb_(0x64) & 0x01) != 0;       /* status bit0: buffer output */
}

static int kbd_get(void) {
    uint8_t sc = inb_(0x60);
    if (sc == 0xE0) return -1;             /* awal extended: abaikan sederhana */
    if (sc & 0x80) {                       /* key release */
        uint8_t m = (uint8_t)(sc & 0x7F);
        if (m == 0x2A || m == 0x36) shift = 0;
        return -1;
    }
    if (sc == 0x2A || sc == 0x36) { shift = 1; return -1; }
    return kbd_decode(sc);
}

/* ---------------- getchar gabungan (blocking) ----------------
 * Mengembalikan 0..255, atau 0x100 untuk EOF (Ctrl-D serial). */
int input_getchar(void) {
    for (;;) {
        if (serial_ready()) {
            int c = serial_get();
            if (c == 0x04) return 0x100;   /* Ctrl-D = EOF    */
            if (c == 0x0D) return '\n';    /* CR terminal -> LF */
            return c;
        }
        if (kbd_ready()) {
            int c = kbd_get();
            if (c >= 0) return c;
        }
        __asm__ volatile ("pause");
    }
}
