/* codegen.c — backend: bytecode Inti -> kode mesin x86-64 -> ELF64
 *
 * Prinsip:
 *  - Semantik DIJAMIN identik dengan vm.c (Fase 0): semua keputusan
 *    pengodean mengikuti makro V()/ST() dan alur OP_* di VM.
 *  - Variabel = slot di stack mesin pada [rbp-S + 8k]; S = align16(nslots*8).
 *  - cmp/jcc: pasangan terakhir (fa/fb di VM) = dua global RW; peephole
 *    memakai flag CPU langsung bila jcc tepat setelah cmp.
 *  - CALL: argumen di-push terbalik (arg0 tertinggi) -> [rbp+16+8k];
 *    nilai balik di RAX; disalin ke slot tujuan oleh pemanggil.
 *  - DIV/MOD: cek nol + kasus INT64_MIN/-1 inline (persis vm.c).
 *  - Runtime (alloc/free/printi/die/oob) = kode mesin di runtime.c.
 *  - _start: mmap stack + mmap arena + salin data + call __main__ + exit.
 */
#include "cg.h"

/* Semua emisi codegen menuju TXT — makro tipis tanpa argumen buffer.
 * (runtime.c memakai fungsi aslinya dengan Buf* eksplisit.) */
#define e_mov_rr(r, s)          e_mov_rr(&TXT, (r), (s))
#define e_mov_rs(r, i)          e_mov_rs(&TXT, (r), (i))
#define e_mov_r32s(r, i)        e_mov_r32s(&TXT, (r), (i))
#define e_mov_r32_fix(r, l)     e_mov_r32_fix(&TXT, (r), (l))
#define e_mov_mem_imm(d, i)     e_mov_mem_imm(&TXT, (d), (i))
#define e_mov_fp_ld(r, d)       e_mov_fp_ld(&TXT, (r), (d))
#define e_mov_fp_st(d, r)       e_mov_fp_st(&TXT, (d), (r))
#define e_mov_bi_ld(r, b, i)    e_mov_bi_ld(&TXT, (r), (b), (i))
#define e_mov_bi_st(b, i, r)    e_mov_bi_st(&TXT, (b), (i), (r))
#define e_movb_bi_st(b, i, r)   e_movb_bi_st(&TXT, (b), (i), (r))
#define e_movzx_bi_ld(r, b, i)  e_movzx_bi_ld(&TXT, (r), (b), (i))
#define e_grp1_fp(e, d, i)      e_grp1_fp(&TXT, (e), (d), (i))
#define e_add_rr(d, s)          e_add_rr(&TXT, (d), (s))
#define e_sub_rr(d, s)          e_sub_rr(&TXT, (d), (s))
#define e_and_rr(d, s)          e_and_rr(&TXT, (d), (s))
#define e_or_rr(d, s)           e_or_rr(&TXT, (d), (s))
#define e_xor_rr(d, s)          e_xor_rr(&TXT, (d), (s))
#define e_cmp_rr(d, s)          e_cmp_rr(&TXT, (d), (s))
#define e_add_ri(r, i)          e_add_ri(&TXT, (r), (i))
#define e_sub_ri(r, i)          e_sub_ri(&TXT, (r), (i))
#define e_and_ri(r, i)          e_and_ri(&TXT, (r), (i))
#define e_cmp_ri(r, i)          e_cmp_ri(&TXT, (r), (i))
#define e_imul_rr(d, s)         e_imul_rr(&TXT, (d), (s))
#define e_shift_cl(e, r)        e_shift_cl(&TXT, (e), (r))
#define e_unary(e, r)           e_unary(&TXT, (e), (r))
#define e_cqo()                 e_cqo(&TXT)
#define e_syscall()             e_syscall(&TXT)
#define e_ret()                 e_ret(&TXT)
#define e_cld()                 e_cld(&TXT)
#define e_rep_movsb()           e_rep_movsb(&TXT)
#define e_xor32_rr(d, s)        e_xor32_rr(&TXT, (d), (s))
#define e_test_rr(r)            e_test_rr(&TXT, (r))
#define e_push(r)               e_push(&TXT, (r))
#define e_pop(r)                e_pop(&TXT, (r))
#define e_jmp(l)                e_jmp(&TXT, (l))
#define e_call(l)               e_call(&TXT, (l))
#define e_jcc(c, l)             e_jcc(&TXT, (c), (l))
#define e_glob_ld(r, g)         e_glob_ld(&TXT, (r), (g))
#define e_glob_st(g, r)         e_glob_st(&TXT, (g), (r))
#define e_glob_st_imm(g, i)     e_glob_st_imm(&TXT, (g), (i))
#define e_add_reg_glob(r, g)    e_add_reg_glob(&TXT, (r), (g))
#define e_cmp_reg_glob(r, g)    e_cmp_reg_glob(&TXT, (r), (g))
#define e_and32_ri8(r, i)       e_and32_ri8(&TXT, (r), (i))

static Prog   *P;
static int64_t ARENA, STACKB;
static Buf     TXT;
static int    *FNL;                    /* label per fungsi */
static int     L_START, L_DATAIMG;
static int     S_DIV0, S_MMOFF, S_LOAD, S_STORE, S_LOADB, S_STOREB, S_PRINTS, S_SYSAT;

static Func  *CF;
static int32_t curS;                   /* S = align16(nslots*8) fungsi aktif */
static int     last_cmp;               /* 1 bila instruksi sebelumnya = CMP  */
static int    *ILBL;                   /* label per indeks instruksi (per fungsi) */

static int32_t slotoff(int k) { return (int32_t)(-(int64_t)curS + 8 * (int64_t)k); }
static int32_t argoff(int k)  { return (int32_t)(16 + 8 * (int64_t)k); }

static int64_t align8(int64_t x) { return (x + 7) & ~(int64_t)7; }
static int fits32_(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

/* ---------------- Tabel string rodata ---------------- */
typedef struct { int lbl; const void *p; int64_t len; } StrEnt;
static StrEnt *STRS; static int NS, CS_;

int rt_addstr(const void *p, int len) {
    if (NS >= CS_) { CS_ = CS_ ? CS_ * 2 : 32; STRS = xrealloc(STRS, (size_t)CS_ * sizeof(StrEnt)); }
    STRS[NS].lbl = lbl_new(); STRS[NS].p = p; STRS[NS].len = len;
    return STRS[NS++].lbl;
}

static void str_flush(void) {
    for (int i = 0; i < NS; i++) {
        lbl_bind(STRS[i].lbl);
        buf_putn(&TXT, STRS[i].p, (size_t)STRS[i].len);
    }
}

/* write(fd, string rodata via label, len) — versi codegen untuk _start */
static void cg_wstr(int fd, int strlbl, int64_t len) {
    e_mov_r32s(RDI, fd);
    e_mov_r32_fix(RSI, strlbl);
    e_mov_rs(RDX, len);
    e_mov_r32s(RAX, 1);
    e_syscall();
}

/* stub "akses di luar arena": (line, what, len, off di reg) -> __rt_oob */
static void oob_stub(int line, int whatlbl, int64_t whatlen, int offreg) {
    e_mov_r32s(RDI, line);
    e_mov_r32_fix(RSI, whatlbl);
    e_mov_r32s(RDX, whatlen);
    e_mov_rr(RCX, offreg);
    e_call(L_OOB);
}

/* ---------------- Operand: muat / simpan (paritas dengan V()/ST()) ---------------- */
static void add_imm(int reg, int64_t imm) {
    if (fits32_(imm)) e_add_ri(reg, imm);
    else { e_mov_rs(R11, imm); e_add_rr(reg, R11); }
}


static void get(Operand *o, int reg) {
    switch (o->kind) {
    case K_CONST: e_mov_rs(reg, o->v); break;
    case K_SLOT:  e_mov_fp_ld(reg, slotoff((int)o->v)); break;
    case K_SOFF:  e_mov_fp_ld(reg, slotoff((int)o->v)); add_imm(reg, o->off); break;
    case K_DISC:  /* VM: V(K_DISC) = sl[0] */
        e_mov_fp_ld(reg, slotoff(0)); break;
    case K_PSLOT: /* di luar sys: VM membaca nilai variabelnya */
        e_mov_fp_ld(reg, slotoff((int)o->v)); break;
    case K_PSOFF:
        e_mov_fp_ld(reg, slotoff((int)o->v)); add_imm(reg, o->off); break;
    case K_PCONST: /* VM: V(K_PCONST) = sl[v] — perilaku patologis disamakan */
        e_mov_fp_ld(reg, slotoff((int)(o->v & 0x7fffffff))); break;
    default:
        die(0, "codegen: jenis operand tak terdukung (%d)", o->kind);
    }
}

static void put(int reg, Operand *d) {
    if (d->kind == K_DISC) return;
    if (d->kind != K_SLOT) die(0, "codegen: tujuan tak valid (kind %d)", d->kind);
    e_mov_fp_st(slotoff((int)d->v), reg);
}

/* ---------------- Aritmetika ---------------- */
/* ext grup-1: 0 ADD, 1 OR, 4 AND, 5 SUB, 6 XOR, 7 CMP */
static int arith_ext(int op) {
    switch (op) {
    case OP_ADD: return 0; case OP_OR: return 1; case OP_AND: return 4;
    case OP_SUB: return 5; case OP_XOR: return 6;
    default: return -1;
    }
}

static void gen_arith(Ins *in) {
    int ext;
    /* peephole: op [slot], imm untuk bentuk dua-operand dengan konstanta */
    if (in->b.kind == K_DISC && in->a.kind == K_CONST && in->d.kind == K_SLOT &&
        (ext = arith_ext(in->op)) >= 0 && fits32_(in->a.v)) {
        e_grp1_fp(ext, slotoff((int)in->d.v), in->a.v);
        return;
    }
    if (in->b.kind == K_DISC) { get(&in->d, RAX); get(&in->a, RCX); }
    else                      { get(&in->a, RAX); get(&in->b, RCX); }
    switch (in->op) {
    case OP_ADD: e_add_rr(RAX, RCX); break;
    case OP_SUB: e_sub_rr(RAX, RCX); break;
    case OP_MUL: e_imul_rr(RAX, RCX); break;
    case OP_AND: e_and_rr(RAX, RCX); break;
    case OP_OR:  e_or_rr(RAX, RCX); break;
    case OP_XOR: e_xor_rr(RAX, RCX); break;
    case OP_SHL: e_shift_cl(4, RAX); break;
    case OP_SHR: e_shift_cl(5, RAX); break;
    default:     e_shift_cl(7, RAX); break; /* SAR */
    }
    put(RAX, &in->d);
}

static void gen_divmod(Ins *in) {
    int is_div = (in->op == OP_DIV);
    int Ldiv0 = lbl_new(), Lok = lbl_new(), Ldone = lbl_new(), Lend = lbl_new();
    if (in->b.kind == K_DISC) { get(&in->d, RAX); get(&in->a, RCX); }
    else                      { get(&in->a, RAX); get(&in->b, RCX); }
    e_test_rr(RCX);
    e_jcc(CC_E, Ldiv0);                            /* y == 0 -> error */
    e_mov_rs(RDX, (int64_t)INT64_MIN);             /* x == INT64_MIN? */
    e_cmp_rr(RAX, RDX);
    e_jcc(CC_NE, Lok);
    e_cmp_ri(RCX, -1);                             /* y == -1? */
    e_jcc(CC_NE, Lok);
    if (!is_div) e_xor32_rr(RAX, RAX);             /* mod -> 0; div -> x (sudah di rax) */
    e_jmp(Ldone);
    lbl_bind(Lok);
    e_cqo();
    e_unary(7, RCX);                               /* idiv rcx */
    if (!is_div) e_mov_rr(RAX, RDX);               /* sisa -> rax */
    lbl_bind(Ldone);
    put(RAX, &in->d);
    e_jmp(Lend);
    lbl_bind(Ldiv0);
    e_mov_r32s(RDI, in->line);
    e_mov_r32_fix(RSI, S_DIV0);
    e_mov_r32s(RDX, 20);
    e_call(L_DIE);
    lbl_bind(Lend);
}

/* ---------------- Akses memori arena ---------------- */
static void gen_load(Ins *in, int is_byte) {
    int Loob = lbl_new(), Lend = lbl_new();
    get(&in->a, RAX);
    e_cmp_reg_glob(RAX, is_byte ? G_CAPM1 : G_CAPM8);
    e_jcc(CC_A, Loob);
    e_glob_ld(RCX, G_BASE);
    if (is_byte) e_movzx_bi_ld(RAX, RCX, RAX);
    else         e_mov_bi_ld(RAX, RCX, RAX);
    put(RAX, &in->d);
    e_jmp(Lend);
    lbl_bind(Loob);
    oob_stub(in->line, is_byte ? S_LOADB : S_LOAD, is_byte ? 5 : 4, RAX);
    lbl_bind(Lend);
}

static void gen_store(Ins *in, int is_byte) {
    int Loob = lbl_new(), Lend = lbl_new();
    get(&in->a, RAX);                              /* alamat */
    get(&in->b, RCX);                              /* nilai  */
    e_cmp_reg_glob(RAX, is_byte ? G_CAPM1 : G_CAPM8);
    e_jcc(CC_A, Loob);
    e_glob_ld(RDX, G_BASE);
    if (is_byte) e_movb_bi_st(RDX, RAX, RCX);
    else         e_mov_bi_st(RDX, RAX, RCX);
    e_jmp(Lend);
    lbl_bind(Loob);
    oob_stub(in->line, is_byte ? S_STOREB : S_STORE, is_byte ? 6 : 5, RAX);
    lbl_bind(Lend);
}

static void gen_prints(Ins *in) {
    int Loob = lbl_new(), Lend = lbl_new();
    get(&in->a, RAX);                              /* ofset */
    get(&in->b, RCX);                              /* panjang */
    e_glob_ld(RDX, G_CAP);
    e_sub_rr(RDX, RCX);                            /* cap - len (CF bila len > cap) */
    e_jcc(CC_B, Loob);
    e_cmp_rr(RAX, RDX);
    e_jcc(CC_A, Loob);
    e_glob_ld(RSI, G_BASE);
    e_add_rr(RSI, RAX);
    e_mov_r32s(RDI, 1);
    e_mov_rr(RDX, RCX);
    e_mov_r32s(RAX, 1);
    e_syscall();
    e_jmp(Lend);
    lbl_bind(Loob);
    oob_stub(in->line, S_PRINTS, 6, RAX);
    lbl_bind(Lend);
}

/* ---------------- sys (syscall nyata) ---------------- */
static const int SYSREG[6] = { RDI, RSI, RDX, R10, R8, R9 };

static void gen_sys(Ins *in) {
    int Ldone = lbl_new();
    int stubreg[6], nstub = 0;
    int Lstub[6];

    for (int k = 0; k < in->pn; k++) {
        Operand *o = &P->pool[in->poff + k];
        int rg = SYSREG[k];
        if (o->kind == K_PCONST || o->kind == K_PSLOT || o->kind == K_PSOFF) {
            if (o->kind == K_PCONST) e_mov_rs(rg, o->v);
            else {
                e_mov_fp_ld(rg, slotoff((int)o->v));
                if (o->kind == K_PSOFF) add_imm(rg, o->off);
            }
            e_cmp_reg_glob(rg, G_CAPM1);           /* alamat arena? */
            Lstub[nstub] = lbl_new();
            e_jcc(CC_A, Lstub[nstub]);
            stubreg[nstub] = rg;
            nstub++;
            e_add_reg_glob(rg, G_BASE);            /* reg = base + off */
        } else {
            get(o, rg);
        }
    }
    get(&in->a, RAX);                              /* nomor syscall */
    e_syscall();
    put(RAX, &in->d);
    e_jmp(Ldone);
    for (int k = 0; k < nstub; k++) {
        lbl_bind(Lstub[k]);
        oob_stub(in->line, S_SYSAT, 4, stubreg[k]);
    }
    lbl_bind(Ldone);
}

/* ---------------- Satu instruksi bytecode ---------------- */
static int jcc_cc(int op) {
    switch (op) {
    case OP_JE: return CC_E;  case OP_JNE: return CC_NE;
    case OP_JL: return CC_L;  case OP_JLE: return CC_LE;
    case OP_JG: return CC_G;  default:     return CC_GE; /* JGE */
    }
}

static int ilbl_of(int idx) {
    if (ILBL[idx] < 0) die(0, "codegen: label instruksi %d tidak pra-dibuat", idx);
    return ILBL[idx];
}

static void gen_ins(Ins *in) {
    switch (in->op) {

    case OP_SET:
        if (in->d.kind == K_DISC) break;           /* ST(_) = tidak ada efek */
        if (in->a.kind == K_CONST && fits32_(in->a.v)) {
            e_mov_mem_imm(slotoff((int)in->d.v), in->a.v);
        } else {
            get(&in->a, RAX);
            put(RAX, &in->d);
        }
        last_cmp = 0;
        break;

    case OP_ADD: case OP_SUB: case OP_MUL: case OP_AND: case OP_OR: case OP_XOR:
    case OP_SHL: case OP_SHR: case OP_SAR:
        gen_arith(in);
        last_cmp = 0;
        break;

    case OP_DIV: case OP_MOD:
        gen_divmod(in);
        last_cmp = 0;
        break;

    case OP_NEG:
        if (in->d.kind == K_DISC) break;
        get(&in->a, RAX);
        e_unary(3, RAX);
        put(RAX, &in->d);
        last_cmp = 0;
        break;

    case OP_NOT:
        if (in->d.kind == K_DISC) break;
        get(&in->a, RAX);
        e_unary(2, RAX);
        put(RAX, &in->d);
        last_cmp = 0;
        break;

    case OP_CMP:
        get(&in->a, RAX);
        get(&in->b, RCX);
        e_glob_st(G_FA, RAX);
        e_glob_st(G_FB, RCX);
        e_cmp_rr(RAX, RCX);
        last_cmp = 1;
        break;

    case OP_JMP:
        e_jmp(ilbl_of((int)in->tgt));
        last_cmp = 0;
        break;

    case OP_JE: case OP_JNE: case OP_JL: case OP_JLE: case OP_JG: case OP_JGE:
        if (last_cmp) {
            e_jcc(jcc_cc(in->op), ilbl_of((int)in->tgt));   /* flag CPU dari cmp */
        } else {
            e_glob_ld(RAX, G_FA);                  /* muat pasangan terakhir */
            e_glob_ld(RCX, G_FB);
            e_cmp_rr(RAX, RCX);
            e_jcc(jcc_cc(in->op), ilbl_of((int)in->tgt));
        }
        last_cmp = 0;
        break;

    case OP_CALL: {
        int pn = in->pn, pad = pn & 1;
        if (pad) e_sub_ri(RSP, 8);
        for (int k = pn - 1; k >= 0; k--) {        /* push terbalik: arg0 di atas */
            get(&P->pool[in->poff + k], RAX);
            e_push(RAX);
        }
        e_call(FNL[(int)in->a.v]);
        if (pn) e_add_ri(RSP, 8 * (pn + pad));
        if (in->d.kind == K_SLOT)
            e_mov_fp_st(slotoff((int)in->d.v), RAX);
        last_cmp = 0;
        break;
    }

    case OP_RET:
        if (in->a.kind == K_DISC) e_xor32_rr(RAX, RAX);
        else get(&in->a, RAX);
        e_mov_rr(RSP, RBP);
        e_pop(RBP);
        e_ret();
        last_cmp = 0;
        break;

    case OP_ALLOC:
        get(&in->a, RDI);
        e_mov_r32s(RSI, in->line);
        e_call(L_ALLOC);
        put(RAX, &in->d);
        last_cmp = 0;
        break;

    case OP_FREE:
        get(&in->a, RDI);
        e_mov_r32s(RSI, in->line);
        e_call(L_FREE);
        last_cmp = 0;
        break;

    case OP_LOAD:  gen_load(in, 0);  last_cmp = 0; break;
    case OP_LOADB: gen_load(in, 1);  last_cmp = 0; break;
    case OP_STORE:  gen_store(in, 0); last_cmp = 0; break;
    case OP_STOREB: gen_store(in, 1); last_cmp = 0; break;

    case OP_PRINTS: gen_prints(in); last_cmp = 0; break;

    case OP_PRINTI:
        get(&in->a, RDI);
        e_call(L_PRINTI);
        last_cmp = 0;
        break;

    case OP_PRINTC:
        get(&in->a, RAX);
        e_push(RAX);
        e_mov_r32s(RDI, 1);
        e_mov_rr(RSI, RSP);
        e_mov_r32s(RDX, 1);
        e_mov_r32s(RAX, 1);
        e_syscall();
        e_pop(RAX);
        last_cmp = 0;
        break;

    case OP_SLEN:                                  /* a = K_CONST (panjang) */
        get(&in->a, RAX);
        put(RAX, &in->d);
        last_cmp = 0;
        break;

    case OP_EXIT:
        get(&in->a, RDI);
        e_and32_ri8(RDI, 0xff);
        e_mov_r32s(RAX, 60);
        e_syscall();
        last_cmp = 0;
        break;

    case OP_SYS:
        gen_sys(in);
        last_cmp = 0;
        break;

    default:
        die(0, "codegen: opcode tidak dikenal (%d)", in->op);
    }
}

/* ---------------- Satu fungsi ---------------- */
static void gen_func(int fi) {
    Func *f = P->funcs[fi];
    CF = f;
    curS = (int32_t)(((int64_t)f->nslots * 8 + 15) & ~(int64_t)15);
    last_cmp = 0;
    ILBL = xmalloc((size_t)(f->ncode ? f->ncode : 1) * sizeof(int));
    for (int i = 0; i < f->ncode; i++) ILBL[i] = -1;

    /* pra-buat label untuk SEMUA target lompatan (maju & mundur) agar
     * lompatan mundur punya label yang terikat di posisi yang tepat */
    for (int i = 0; i < f->ncode; i++) {
        int op = f->code[i].op;
        if (op == OP_JMP || (op >= OP_JE && op <= OP_JGE)) {
            int t = (int)f->code[i].tgt;
            if (t >= 0 && t < f->ncode && ILBL[t] < 0) ILBL[t] = lbl_new();
        }
    }

    lbl_bind(FNL[fi]);
    e_push(RBP);
    e_mov_rr(RBP, RSP);
    if (curS) e_sub_ri(RSP, curS);

    for (int k = f->arity; k < f->nslots; k++)     /* nolkan slot non-param */
        e_mov_mem_imm(slotoff(k), 0);
    for (int k = 0; k < f->arity; k++) {           /* salin argumen */
        e_mov_fp_ld(RAX, argoff(k));
        e_mov_fp_st(slotoff(k), RAX);
    }

    for (int i = 0; i < f->ncode; i++) {
        if (ILBL[i] >= 0) lbl_bind(ILBL[i]);
        gen_ins(&f->code[i]);
    }
    free(ILBL);
    ILBL = NULL;
}

/* ---------------- _start ELF: mmap stack+arena, salin data, call __main__ ---------------- */
static void gen_start_elf(void) {
    int Lfail = lbl_new();

    lbl_bind(L_START);

    /* mmap(rsp baru): addr=0, len, PROT_READ|WRITE, MAP_PRIVATE|ANONYMOUS, fd=-1, off=0 */
    e_xor32_rr(RDI, RDI);
    e_mov_rs(RSI, STACKB);
    e_mov_r32s(RDX, 3);
    e_mov_r32s(R10, 0x22);
    e_mov_rs(R8, -1);
    e_xor32_rr(R9, R9);
    e_mov_r32s(RAX, 9);
    e_syscall();
    e_cmp_ri(RAX, -4096);
    e_jcc(CC_A, Lfail);
    e_add_ri(RAX, STACKB);
    e_and_ri(RAX, -16);
    e_mov_rr(RSP, RAX);                            /* pindah ke stack besar */

    /* mmap arena */
    e_xor32_rr(RDI, RDI);
    e_mov_rs(RSI, ARENA);
    e_mov_r32s(RDX, 3);
    e_mov_r32s(R10, 0x22);
    e_mov_rs(R8, -1);
    e_xor32_rr(R9, R9);
    e_mov_r32s(RAX, 9);
    e_syscall();
    e_cmp_ri(RAX, -4096);
    e_jcc(CC_A, Lfail);

    e_glob_st(G_BASE, RAX);
    e_glob_st_imm(G_CAP, ARENA);
    e_glob_st_imm(G_CAPM8, ARENA - 8);
    e_glob_st_imm(G_CAPM1, ARENA - 1);
    e_glob_st_imm(G_DLEN, P->data_len);
    e_glob_st_imm(G_TOP, align8(P->data_len));

    if (P->data_len) {                             /* salin image data -> arena */
        e_mov_rr(RDI, RAX);
        e_mov_r32_fix(RSI, L_DATAIMG);
        e_mov_rs(RCX, P->data_len);
        e_cld();
        e_rep_movsb();
    }

    e_call(FNL[0]);                          /* __main__ */
    e_mov_rr(RDI, RAX);
    e_and32_ri8(RDI, 0xff);
    e_mov_r32s(RAX, 60);
    e_syscall();

    lbl_bind(Lfail);
    cg_wstr(2, S_MMOFF, 17);
    e_mov_r32s(RDI, 1);
    e_mov_r32s(RAX, 60);
    e_syscall();
}

/* ---------------- _start IntiOS: ABI kernel (RDI=basis arena, RSI=ukuran) ----------------
 * Tidak ada mmap: kernel IntiOS sudah menyiapkan stack user, arena, dan
 * menyalin blob program ke 0x400000 sebelum masuk CPL3. exit(60) = kembali
 * ke shell IntiOS (ditangani kernel via SYSCALL/SYSRET). */
static void gen_start_intios(void) {
    lbl_bind(L_START);

    e_glob_st(G_BASE, RDI);                        /* basis arena dari kernel  */
    e_glob_st(G_CAP, RSI);                         /* ukuran arena dari kernel */
    e_glob_st_imm(G_CAPM8, ARENA - 8);
    e_glob_st_imm(G_CAPM1, ARENA - 1);
    e_glob_st_imm(G_DLEN, P->data_len);
    e_glob_st_imm(G_TOP, align8(P->data_len));

    if (P->data_len) {                             /* salin image data -> arena */
        e_glob_ld(RDI, G_BASE);
        e_mov_r32_fix(RSI, L_DATAIMG);
        e_mov_rs(RCX, P->data_len);
        e_cld();
        e_rep_movsb();
    }

    e_call(FNL[0]);                                /* __main__ */
    e_mov_rr(RDI, RAX);
    e_and32_ri8(RDI, 0xff);
    e_mov_r32s(RAX, 60);
    e_syscall();                                   /* exit -> kernel -> shell  */
}

/* ---------------- Orkestrasi ---------------- */
int codegen_emit(Prog *p, int64_t arena_mb, int64_t stack_mb, const char *out,
                 int target) {
    P = p;
    ARENA  = arena_mb * 1024 * 1024;
    STACKB = stack_mb * 1024 * 1024;

    if (P->data_len + 16 > ARENA)
        die(0, "seksi data (%lld byte) lebih besar dari arena (%lld byte)",
            (long long)P->data_len, (long long)ARENA);

    static const uint8_t Z64[G_DATAN] = {0};

    if (target == TARGET_INTIOS) {
        /* flat binary: tanpa padding ELF — kode mulai tepat setelah header 64B */
    } else {
        for (size_t i = 0; i < INTI_CODE_OFF; i++) buf_put8(&TXT, 0); /* ruang ELF hdr */
    }
    enc_use(&TXT);

    FNL = xmalloc((size_t)(P->nfuncs ? P->nfuncs : 1) * sizeof(int));
    for (int i = 0; i < P->nfuncs; i++) FNL[i] = lbl_new();
    L_START   = lbl_new();
    L_DATAIMG = lbl_new();

    S_DIV0   = rt_addstr("pembagian dengan nol", 20);
    S_MMOFF  = rt_addstr("inti: mmap gagal\n", 17);
    S_LOAD   = rt_addstr("load", 4);
    S_STORE  = rt_addstr("store", 5);
    S_LOADB  = rt_addstr("loadb", 5);
    S_STOREB = rt_addstr("storeb", 6);
    S_PRINTS = rt_addstr("prints", 6);
    S_SYSAT  = rt_addstr("sys@", 4);

    if (target == TARGET_INTIOS) gen_start_intios();
    else                         gen_start_elf();
    rt_emit_all(&TXT);
    for (int fi = 0; fi < P->nfuncs; fi++) gen_func(fi);

    /* image data user harus terikat TEPAT sebelum byte-nya ditulis —
     * _start menyalin P->data_len byte dari L_DATAIMG ke arena[0..) */
    lbl_bind(L_DATAIMG);
    if (P->data_len) buf_putn(&TXT, P->data, (size_t)P->data_len);
    str_flush();                                   /* string runtime menyusul */

    uint64_t data_vaddr, text_vaddr;
    if (target == TARGET_INTIOS) {
        text_vaddr = IP_PROGBASE + IP_HDRSZ;                  /* 0x400064 */
        data_vaddr = text_vaddr + ((TXT.n + 0xFFF) & ~(uint64_t)0xFFF);
        fix_resolve(data_vaddr, text_vaddr);
        return bin_write(out, &TXT, G_DATAN, arena_mb) != 0;
    }
    data_vaddr = INTI_BASE + ((TXT.n + 0xFFF) & ~(uint64_t)0xFFF);
    fix_resolve(data_vaddr, INTI_BASE);

    if (elf64_write(out, &TXT, Z64, G_DATAN, INTI_BASE + INTI_CODE_OFF) != 0)
        return 1;
    return 0;
}

/* ---------------- Penulis flat binary IntiOS (.ibin) ----------------
 * Layout file (dimuat kernel di 0x400000, identitas terpetakan):
 *   +0x00   header 64B  (magic, entry_off=64, ukuran, offset data)
 *   +0x40   kode + rodata + image data (mulai di vaddr 0x400064)
 *   +pad    global RW 64 byte (di-nolkan di file, alamat via disp32 absolut)
 * Tidak ada BSS: segmen RW hanya G_DATAN byte dan ditulis eksplisit.
 * Urutan byte header little-endian penuh (magic u64, field u32). */
static void hdr_put32(uint8_t *h, int off, uint64_t v) {
    for (int i = 0; i < 4; i++) h[off + i] = (uint8_t)(v >> (8 * i));
}

int bin_write(const char *path, const Buf *text, size_t data_sz, int64_t arena_mb) {
    uint64_t code_end = IP_HDRSZ + text->n;
    uint64_t data_off = (code_end + 0xFFF) & ~(uint64_t)0xFFF;

    uint8_t hdr[IP_HDRSZ];
    for (size_t i = 0; i < IP_HDRSZ; i++) hdr[i] = 0;
    for (int i = 0; i < 8; i++) hdr[i] = (uint8_t)(IP_MAGIC >> (8 * i));
    hdr_put32(hdr,  8, IP_HDRSZ);        /* entry_off  = 64   */
    hdr_put32(hdr, 12, text->n);         /* code_size       */
    hdr_put32(hdr, 16, data_off);        /* data_off        */
    hdr_put32(hdr, 20, data_sz);         /* data_size       */
    hdr_put32(hdr, 24, (uint64_t)arena_mb); /* arena_mb (informasi) */
    hdr_put32(hdr, 28, 0);               /* stack: kernel (8 MB) */

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(hdr, 1, IP_HDRSZ, f) != IP_HDRSZ ||
        (text->n && fwrite(text->b, 1, text->n, f) != text->n)) {
        fclose(f); return -1;
    }
    for (uint64_t i = code_end; i < data_off; i++) fputc(0, f);
    for (size_t i = 0; i < data_sz; i++) fputc(0, f);
    if (fclose(f) != 0) return -1;
    return 0;
}
