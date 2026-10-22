/* vm.c — mesin virtual bahasa Inti
 *
 * - dispatch switch dengan prediksi cabang (cepat untuk interpreter)
 * - value stack + frame stack manual (rekursi dalam tidak menggerus stack C)
 * - arena memori nyata (malloc host) dengan alokator blok first-fit
 * - jembatan syscall Linux asli lewat syscall(2): argumen '@addr'
 *   dikonversi menjadi pointer nyata ke dalam arena
 */
#define _GNU_SOURCE
#include "inti.h"
#include <unistd.h>
#include <sys/types.h>

/* ---------------- Alokator arena (blok ber-header, first-fit) ---------------- */
typedef struct {
    uint64_t size;    /* ukuran payload (sudah align 8) */
    uint32_t used;
    uint32_t magic;   /* penanda blok valid */
} BlkHdr;
#define BLK_MAGIC 0x494E5449u /* "INTI" */
#define HDR_SIZE  16

typedef struct {
    uint8_t *mem;
    int64_t  cap;
    int64_t  data_len;
    int64_t  top;      /* batas atas area terpakai */
} Arena;

static inline int64_t align8(int64_t x) { return (x + 7) & ~(int64_t)7; }

static void arena_init(Arena *a, Prog *p) {
    if (p->data_len + HDR_SIZE > p->arena_cap)
        die(0, "seksi data (%lld byte) lebih besar dari arena (%lld byte)",
            (long long)p->data_len, (long long)p->arena_cap);
    a->mem = xmalloc((size_t)p->arena_cap);
    a->cap = p->arena_cap;
    a->data_len = p->data_len;
    if (p->data_len) memcpy(a->mem, p->data, (size_t)p->data_len);
    a->top = align8(p->data_len);
}

static int64_t arena_alloc(Arena *a, int64_t size, int line) {
    if (size < 0) die(line, "alloc dengan ukuran negatif (%lld)", (long long)size);
    int64_t need = align8(size ? size : 1);
    /* first-fit: cari blok bebas */
    int64_t cur = align8(a->data_len);
    while (cur + HDR_SIZE <= a->top) {
        BlkHdr h;
        memcpy(&h, a->mem + cur, sizeof h);
        if (h.magic == BLK_MAGIC && !h.used && (int64_t)h.size >= need) {
            h.used = 1;
            memcpy(a->mem + cur, &h, sizeof h);
            return cur + HDR_SIZE;
        }
        cur += HDR_SIZE + (int64_t)h.size;
    }
    /* bump alokasi baru */
    if (a->top + HDR_SIZE + need > a->cap)
        die(line, "arena penuh: butuh %lld byte, sisa %lld (naikkan --arena-mb)",
            (long long)(HDR_SIZE + need), (long long)(a->cap - a->top));
    BlkHdr h = { (uint64_t)need, 1, BLK_MAGIC };
    memcpy(a->mem + a->top, &h, sizeof h);
    int64_t addr = a->top + HDR_SIZE;
    a->top += HDR_SIZE + need;
    return addr;
}

static void arena_free(Arena *a, int64_t addr, int line) {
    int64_t hoff = addr - HDR_SIZE;
    if (hoff < align8(a->data_len) || hoff + HDR_SIZE > a->top)
        die(line, "free: alamat %lld bukan blok yang valid", (long long)addr);
    BlkHdr h;
    memcpy(&h, a->mem + hoff, sizeof h);
    if (h.magic != BLK_MAGIC) die(line, "free: alamat %lld bukan blok yang valid", (long long)addr);
    h.used = 0;
    memcpy(a->mem + hoff, &h, sizeof h);
}

static inline void chk_addr(Arena *a, int64_t off, int64_t size, int line, const char *what) {
    if (off < 0 || off > a->cap - size)
        die(line, "%s: alamat %lld+%lld di luar arena (0..%lld)",
            what, (long long)off, (long long)size, (long long)a->cap);
}

/* ---------------- Frame & VM ---------------- */
typedef struct {
    Ins     *ip;
    int64_t *sl;
    Func    *fn;
    int      retdst;   /* slot tujuan nilai balik, -1 = dibuang */
} Frame;

#define V(o)  ((o)->kind == K_CONST ? (o)->v \
              : ((o)->kind == K_SOFF  ? sl[(o)->v] + (o)->off \
              : sl[(o)->v]))
#define ST(o, x) do { if ((o)->kind == K_SLOT) sl[(o)->v] = (x); } while (0)

void vm_run(Prog *p) {
    Arena ar;
    arena_init(&ar, p);

    int64_t *vstack = xmalloc((size_t)p->vstack_cap * sizeof(int64_t));
    Frame   *fstack = xmalloc((size_t)p->fstack_cap * sizeof(Frame));
    Operand *pool   = p->pool;
    Func   **F      = p->funcs;

    Func *fn = F[0];
    if (fn->ncode == 0) exit(0);
    int64_t *sl = vstack;
    memset(sl, 0, (size_t)fn->nslots * sizeof(int64_t));
    int64_t vsp = fn->nslots;
    Ins *ip = fn->code;
    fstack[0].ip = ip; fstack[0].sl = sl; fstack[0].fn = fn; fstack[0].retdst = -1;
    int64_t fp = 1;

    int64_t fa = 0, fb = 0;   /* flag pembanding dari cmp terakhir */

    for (;;) {
        Ins *i = ip++;
        switch (i->op) {

        case OP_SET:
            ST(&i->d, V(&i->a));
            break;

        case OP_ADD: case OP_SUB: case OP_MUL:
        case OP_AND: case OP_OR: case OP_XOR:
        case OP_SHL: case OP_SHR: case OP_SAR: {
            int64_t x, y;
            if (i->b.kind == K_DISC) { x = V(&i->d); y = V(&i->a); }
            else                     { x = V(&i->a); y = V(&i->b); }
            int64_t r = 0;
            switch (i->op) {
                case OP_ADD: r = x + y; break;
                case OP_SUB: r = x - y; break;
                case OP_MUL: r = x * y; break;
                case OP_AND: r = x & y; break;
                case OP_OR:  r = x | y; break;
                case OP_XOR: r = x ^ y; break;
                case OP_SHL: r = (int64_t)((uint64_t)x << (y & 63)); break;
                case OP_SHR: r = (int64_t)((uint64_t)x >> (y & 63)); break;
                default:     r = x >> (y & 63); /* SAR */ break;
            }
            ST(&i->d, r);
            break;
        }

        case OP_DIV: case OP_MOD: {
            int64_t x, y;
            if (i->b.kind == K_DISC) { x = V(&i->d); y = V(&i->a); }
            else                     { x = V(&i->a); y = V(&i->b); }
            if (y == 0) die(i->line, "pembagian dengan nol");
            int64_t r;
            if (x == INT64_MIN && y == -1) r = (i->op == OP_DIV) ? x : 0;
            else r = (i->op == OP_DIV) ? x / y : x % y;
            ST(&i->d, r);
            break;
        }

        case OP_NEG: ST(&i->d, -V(&i->a)); break;
        case OP_NOT: ST(&i->d, ~V(&i->a)); break;

        case OP_CMP:
            fa = V(&i->a); fb = V(&i->b);
            break;

        case OP_JMP: ip = fn->code + i->tgt; break;
        case OP_JE:  if (fa == fb) ip = fn->code + i->tgt; break;
        case OP_JNE: if (fa != fb) ip = fn->code + i->tgt; break;
        case OP_JL:  if (fa <  fb) ip = fn->code + i->tgt; break;
        case OP_JLE: if (fa <= fb) ip = fn->code + i->tgt; break;
        case OP_JG:  if (fa >  fb) ip = fn->code + i->tgt; break;
        case OP_JGE: if (fa >= fb) ip = fn->code + i->tgt; break;

        case OP_CALL: {
            Func *f = F[i->a.v];
            if (vsp + f->nslots > p->vstack_cap)
                die(i->line, "tumpukan nilai habis (rekursi terlalu dalam, --stack-mb)");
            if (fp >= p->fstack_cap)
                die(i->line, "tumpukan frame habis (rekursi terlalu dalam)");
            int64_t *nsl = vstack + vsp;
            vsp += f->nslots;
            memset(nsl, 0, (size_t)f->nslots * sizeof(int64_t));
            for (int k = 0; k < i->pn; k++) nsl[k] = V(&pool[i->poff + k]);
            fstack[fp].ip = ip; fstack[fp].sl = sl; fstack[fp].fn = fn;
            fstack[fp].retdst = (i->d.kind == K_SLOT) ? (int)i->d.v : -1;
            fp++;
            sl = nsl; fn = f; ip = f->code;
            break;
        }

        case OP_RET: {
            int64_t rv = (i->a.kind == K_DISC) ? 0 : V(&i->a);
            fp--;
            if (fp == 0) exit((int)(rv & 0xff));   /* ret di __main__ = kode keluar */
            vsp -= fn->nslots;
            Frame *fr = &fstack[fp];
            if (fr->retdst >= 0) fr->sl[fr->retdst] = rv;
            ip = fr->ip; sl = fr->sl; fn = fr->fn;
            break;
        }

        case OP_ALLOC: {
            int64_t sz = V(&i->a);
            ST(&i->d, arena_alloc(&ar, sz, i->line));
            break;
        }

        case OP_FREE: arena_free(&ar, V(&i->a), i->line); break;

        case OP_LOAD: {
            int64_t off = V(&i->a);
            chk_addr(&ar, off, 8, i->line, "load");
            int64_t v;
            memcpy(&v, ar.mem + off, 8);
            ST(&i->d, v);
            break;
        }

        case OP_STORE: {
            int64_t off = V(&i->a);
            chk_addr(&ar, off, 8, i->line, "store");
            int64_t v = V(&i->b);
            memcpy(ar.mem + off, &v, 8);
            break;
        }

        case OP_LOADB: {
            int64_t off = V(&i->a);
            chk_addr(&ar, off, 1, i->line, "loadb");
            ST(&i->d, ar.mem[off]);
            break;
        }

        case OP_STOREB: {
            int64_t off = V(&i->a);
            chk_addr(&ar, off, 1, i->line, "storeb");
            ar.mem[off] = (uint8_t)(V(&i->b) & 0xff);
            break;
        }

        case OP_SYS: {
            long nr = (long)V(&i->a);
            long args[6] = {0, 0, 0, 0, 0, 0};
            for (int k = 0; k < i->pn; k++) {
                Operand *o = &pool[i->poff + k];
                if (o->kind == K_PCONST) {
                    chk_addr(&ar, o->v, 1, i->line, "sys@");
                    args[k] = (long)(ar.mem + o->v);
                } else if (o->kind == K_PSLOT || o->kind == K_PSOFF) {
                    int64_t off = sl[o->v] + (o->kind == K_PSOFF ? o->off : 0);
                    chk_addr(&ar, off, 1, i->line, "sys@");
                    args[k] = (long)(ar.mem + off);
                } else {
                    args[k] = (long)V(o);
                }
            }
            long r = syscall(nr, args[0], args[1], args[2], args[3], args[4], args[5]);
            ST(&i->d, (int64_t)r);
            break;
        }

        case OP_PRINTI: {
            char buf[32];
            int n = snprintf(buf, sizeof buf, "%lld", (long long)V(&i->a));
            if (n > 0) { ssize_t w = write(1, buf, (size_t)n); (void)w; }
            break;
        }

        case OP_PRINTS: {
            int64_t off = V(&i->a), len = V(&i->b);
            chk_addr(&ar, off, len, i->line, "prints");
            ssize_t w = write(1, ar.mem + off, (size_t)len);
            (void)w;
            break;
        }

        case OP_PRINTC: {
            char c = (char)(V(&i->a) & 0xff);
            ssize_t w = write(1, &c, 1);
            (void)w;
            break;
        }

        case OP_SLEN: ST(&i->d, i->a.v); break;

        case OP_EXIT: exit((int)(V(&i->a) & 0xff));

        default:
            die(i->line, "opcode tidak dikenal (%d)", i->op);
        }
    }
}
