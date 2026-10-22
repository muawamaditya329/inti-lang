/* console.h — konsol VGA teks + serial COM1 + kprintf mini */
#ifndef INTIOS_CONSOLE_H
#define INTIOS_CONSOLE_H

#include <stdint.h>

void console_init(void);
void console_putc(char c);          /* ke VGA + serial, apa adanya */
void serial_putc(char c);           /* hanya serial                */
void kputs(const char *s);
void kprintf(const char *fmt, ...); /* %s %c %d %u %x %p %llx %llu */
void vga_clear(void);

#endif /* INTIOS_CONSOLE_H */
