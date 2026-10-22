/* sys.c — dispatcher syscall (nomor kompatibel Linux) + laporan exception
 *
 * Didukung (dipakai runtime kode-mesin hasil inti-cc):
 *   0  read  (fd=0, buf, len)  -> baris dari serial/keyboard, EOF = Ctrl-D
 *   1  write (fd=1/2, buf, len)-> konsol VGA + serial
 *   60 exit  (code)            -> kembali ke shell IntiOS
 * Nomor lain -> -ENOSYS (-38), persis semangat syscall Linux.
 */
#include <stdint.h>
#include "layout.h"
#include "console.h"
#include "kstring.h"

int64_t g_last_exit;
extern int64_t g_exit_code;              /* didefinisikan di sys.S (.bss) */
extern void user_die(void) __attribute__((noreturn));
int input_getchar(void);

/* ---------------- write ---------------- */
static int64_t sys_write(uint64_t fd, const char *buf, uint64_t len) {
    if (fd != 1 && fd != 2) return -9;          /* -EBADF  */
    if (len > (1uLL << 20)) return -22;         /* -EINVAL */
    for (uint64_t i = 0; i < len; i++) {
        console_putc(buf[i]);                   /* \n langsung dicetak: */
    }                                           /* byte-exact utk CI    */
    return (int64_t)len;
}

/* ---------------- read (line-buffered, EOF = Ctrl-D 0x04) ---------------- */
static int64_t sys_read(uint64_t fd, char *buf, uint64_t len) {
    if (fd != 0) return -9;                     /* -EBADF  */
    if (len == 0 || len > (1uLL << 20)) return -22;
    uint64_t n = 0;
    for (;;) {
        int c = input_getchar();                /* blocking */
        if (c == 0x100) {                       /* EOF      */
            if (n == 0) return 0;               /* EOF tepat awal -> 0 */
            return (int64_t)n;                  /* EOF ditahan utk bacaan berikut */
        }
        buf[n++] = (char)c;
        if (c == '\n') break;                   /* line-buffered */
        if (n >= len) break;                    /* buffer penuh  */
    }
    return (int64_t)n;
}

/* ---------------- dispatcher ---------------- */
int64_t sys_dispatch(uint64_t n, uint64_t *args) {
    switch (n) {
    case 0:  return sys_read (args[0], (char *)args[1], args[2]);
    case 1:  return sys_write(args[0], (const char *)args[1], args[2]);
    case 60:                                 /* exit: pulih ke shell */
        g_last_exit = (int64_t)(args[0] & 0xFF);
        g_exit_code = g_last_exit;
        user_die();                          /* tidak kembali */
    default:
        return -38;                          /* -ENOSYS */
    }
}

/* ---------------- laporan exception ---------------- */
const char *exc_name(int v) {
    switch (v) {
    case 0:  return "#DE divide-error";
    case 6:  return "#UD opcode-tak-sah";
    case 13: return "#GP proteksi-umum";
    case 14: return "#PF page-fault";
    default: return "exception";
    }
}

/* frame: [0]=vektor [1]=err [2]=RIP user [3]=CS user [4]=RFLAGS
 *        [5]=RSP user [6]=SS user */
void exc_report(uint64_t *fr) {
    kprintf("\n[intios] exception %d (%s), err=%p, rip=%p, rsp=%p\n",
            (int)fr[0], exc_name((int)fr[0]), fr[1], fr[2], fr[5]);
    kprintf("[intios] program dihentikan, kembali ke shell\n");
    g_exit_code = 128 + (int64_t)fr[0];
    g_last_exit = g_exit_code;
    user_die();                                  /* tidak kembali */
}
