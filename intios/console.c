/* console.c — VGA teks 80x25 (0xB8000) + UART COM1 (0x3F8) + kprintf mini
 *
 * Semua output ditulis ke DUA media: VGA (untuk layar nyata di QEMU/mesin)
 * dan serial (untuk otomasi CI — log yang sama yang dilihat manusia).
 */
#include "console.h"

/* ---------------- register I/O ---------------- */
static inline void outb(uint16_t p, uint8_t v) {
    __asm__ volatile ("out %0, %1" :: "a"(v), "Nd"(p));
}
static inline uint8_t inb(uint16_t p) {
    uint8_t v;
    __asm__ volatile ("in %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

/* ---------------- VGA teks ---------------- */
#define VGA_MEM   ((volatile uint16_t *)0xB8000)
#define VGA_W     80
#define VGA_H     25

static int vx, vy;

void vga_clear(void) {
    for (int i = 0; i < VGA_W * VGA_H; i++)
        VGA_MEM[i] = 0x0720;               /* spasi, abu terang di hitam */
    vx = vy = 0;
}

static void vga_cursor(void) {
    int pos = vy * VGA_W + vx;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
}

static void vga_scroll(void) {
    for (int i = 0; i < VGA_W * (VGA_H - 1); i++)
        VGA_MEM[i] = VGA_MEM[i + VGA_W];
    for (int i = VGA_W * (VGA_H - 1); i < VGA_W * VGA_H; i++)
        VGA_MEM[i] = 0x0720;
}

static void vga_putc(char c) {
    if (c == '\n') {
        vx = 0;
        if (++vy >= VGA_H) { vga_scroll(); vy = VGA_H - 1; }
    } else if (c == '\r') {
        vx = 0;
    } else if (c == '\b') {
        if (vx > 0) vx--;
        VGA_MEM[vy * VGA_W + vx] = 0x0720;
    } else {
        VGA_MEM[vy * VGA_W + vx] = 0x0700 | (uint8_t)c;
        if (++vx >= VGA_W) {
            vx = 0;
            if (++vy >= VGA_H) { vga_scroll(); vy = VGA_H - 1; }
        }
    }
    vga_cursor();
}

/* ---------------- serial COM1 ---------------- */
static void sputc(char c) {
    for (int i = 0; i < 100000 && !(inb(0x3FD) & 0x20); i++) {}
    outb(0x3F8, (uint8_t)c);
}

void serial_putc(char c) { sputc(c); }

/* ---------------- konsol gabungan ---------------- */
void console_init(void) {
    vga_clear();
}

void console_putc(char c) {
    vga_putc(c);
    sputc(c);
}

void kputs(const char *s) {
    while (*s) console_putc(*s++);
}

/* ---------------- kprintf mini (tanpa libc) ---------------- */
static void put_str(const char *s) { while (*s) console_putc(*s++); }

static void put_u64(uint64_t v, unsigned base, int width, char pad) {
    char tmp[32];
    const char *dig = "0123456789abcdef";
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = dig[v % base]; v /= base; }
    while (n < width && n < 31) tmp[n++] = pad;
    while (n) console_putc(tmp[--n]);
}

void kprintf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { console_putc(*p); continue; }
        p++;
        char pad = ' ';
        int width = 0;
        if (*p == '0') { pad = '0'; p++; }
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        int lng = 0;
        while (*p == 'l') { lng++; p++; }
        switch (*p) {
        case 's': put_str(__builtin_va_arg(ap, const char *)); break;
        case 'c': console_putc((char)__builtin_va_arg(ap, int)); break;
        case 'd': {
            int64_t v = __builtin_va_arg(ap, int64_t);
            uint64_t u = (v < 0) ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
            if (v < 0) console_putc('-');
            put_u64(u, 10, width, pad);
            break;
        }
        case 'u': put_u64(__builtin_va_arg(ap, uint64_t), 10, width, pad); break;
        case 'x': put_u64(__builtin_va_arg(ap, uint64_t), 16, width, pad); break;
        case 'p': {
            uint64_t v = __builtin_va_arg(ap, uint64_t);
            put_str("0x");
            put_u64(v, 16, 8, '0');
            break;
        }
        case '%': console_putc('%'); break;
        default:  console_putc('%'); console_putc(*p); break;
        }
    }
    __builtin_va_end(ap);
}
