/* encode.c — encoder instruksi x86-64 untuk backend Inti
 *
 * Semua emisi bersifat position-dependent (ET_EXEC non-PIE, vaddr < 4 GB),
 * sehingga alamat global/string memakai disp32/imm32 absolut.
 * Label & fixup: rel32 (cabang/call), abs32 (alamat rodata), glob (data RW).
 */
#include "cg.h"

/* ---------------- Buffer ---------------- */
void buf_put8(Buf *b, unsigned v) {
    if (b->n + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 4096;
        b->b = xrealloc(b->b, b->cap);
    }
    b->b[b->n++] = (uint8_t)v;
}

void buf_putn(Buf *b, const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) buf_put8(b, ((const uint8_t *)p)[i]);
}

static void put32le(Buf *b, uint32_t v) {
    buf_put8(b, v & 0xff);
    buf_put8(b, (v >> 8) & 0xff);
    buf_put8(b, (v >> 16) & 0xff);
    buf_put8(b, (v >> 24) & 0xff);
}

static void put64le(Buf *b, uint64_t v) {
    put32le(b, (uint32_t)v);
    put32le(b, (uint32_t)(v >> 32));
}

/* ---------------- Label & fixup ---------------- */
static Buf *CB = NULL;               /* buffer aktif */

void enc_use(Buf *b) { CB = b; }

typedef struct { size_t pos; int64_t arg; int type; } FixT;
enum { F_REL32 = 0, F_ABS32 = 1, F_GLOB = 2 };

static size_t *LBL = NULL;  static int NL, CL;
static FixT   *FIX = NULL;  static int NF, CF;

int lbl_new(void) {
    if (NL >= CL) { CL = CL ? CL * 2 : 64; LBL = xrealloc(LBL, (size_t)CL * sizeof(size_t)); }
    LBL[NL] = (size_t)-1;
    return NL++;
}

void lbl_bind(int l) {
    if (LBL[l] != (size_t)-1) die(0, "codegen: label terikat dua kali");
    LBL[l] = CB->n;
}

static void fix_push(int type, int64_t arg) {
    if (NF >= CF) { CF = CF ? CF * 2 : 64; FIX = xrealloc(FIX, (size_t)CF * sizeof(FixT)); }
    FIX[NF].pos = CB->n; FIX[NF].arg = arg; FIX[NF].type = type; NF++;
    put32le(CB, 0);   /* ruang fixup */
}

void fix_rel32(int l) { fix_push(F_REL32, l); }
void fix_abs32(int l) { fix_push(F_ABS32, l); }
void fix_glob(int goff) { fix_push(F_GLOB, goff); }

void fix_resolve(uint64_t data_vaddr, uint64_t text_vaddr) {
    for (int i = 0; i < NF; i++) {
        uint32_t v;
        if (FIX[i].type == F_REL32) {
            int64_t tgt = (int64_t)LBL[FIX[i].arg];
            if (tgt == -1) die(0, "codegen: label rel32 tidak pernah terikat (id=%d, pos=%zu)", (int)FIX[i].arg, FIX[i].pos);
            v = (uint32_t)(uint64_t)(int64_t)(tgt - (int64_t)(FIX[i].pos + 4));
        } else if (FIX[i].type == F_ABS32) {
            if (LBL[FIX[i].arg] == (size_t)-1) die(0, "codegen: label abs32 tidak pernah terikat (id=%d, pos=%zu)", (int)FIX[i].arg, FIX[i].pos);
            v = (uint32_t)(text_vaddr + LBL[FIX[i].arg]);
        } else {
            v = (uint32_t)(data_vaddr + (uint64_t)FIX[i].arg);
        }
        CB->b[FIX[i].pos + 0] = v & 0xff;
        CB->b[FIX[i].pos + 1] = (v >> 8) & 0xff;
        CB->b[FIX[i].pos + 2] = (v >> 16) & 0xff;
        CB->b[FIX[i].pos + 3] = (v >> 24) & 0xff;
    }
}

/* ---------------- Primitif ---------------- */
static void rex(Buf *b, int w, int r, int x, int bb) {
    if (w || r || x || bb) buf_put8(b, 0x40 | (w << 3) | (r << 2) | (x << 1) | bb);
}

static void modrm(Buf *b, int mod, int reg, int rm) {
    buf_put8(b, (unsigned)((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
}

static void sib(Buf *b, int scale, int index, int base) {
    buf_put8(b, (unsigned)((scale << 6) | ((index & 7) << 3) | (base & 7)));
}

static int fits8(int64_t v)  { return v >= -128 && v <= 127; }
static int fits32(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

/* disp untuk [rbp+disp]: mod 01 (disp8) bila muat, selain itu mod 10 (disp32) */
static void fp_modrm_disp(Buf *b, int regfield, int32_t disp) {
    if (fits8(disp)) { modrm(b, 1, regfield, 5); buf_put8(b, (unsigned)(int8_t)disp); }
    else             { modrm(b, 2, regfield, 5); put32le(b, (uint32_t)disp); }
}

/* ---------------- mov ---------------- */
void e_mov_rr(Buf *b, int dst, int src) {   /* mov dst, src */
    rex(b, 1, src >> 3, 0, dst >> 3);
    buf_put8(b, 0x89);
    modrm(b, 3, src, dst);
}

void e_mov_rs(Buf *b, int reg, int64_t imm) { /* mov reg, imm (sign-ext 32 atau 64) */
    if (fits32(imm)) {
        rex(b, 1, 0, 0, reg >> 3);
        buf_put8(b, 0xC7);
        modrm(b, 3, 0, reg);
        put32le(b, (uint32_t)imm);
    } else {
        rex(b, 1, 0, 0, reg >> 3);
        buf_put8(b, 0xB8 | (reg & 7));
        put64le(b, (uint64_t)imm);
    }
}

void e_mov_r32s(Buf *b, int reg, int64_t imm) { /* mov r32, imm32 (zero-extend) */
    rex(b, 0, 0, 0, reg >> 3);
    buf_put8(b, 0xB8 | (reg & 7));
    put32le(b, (uint32_t)imm);
}

void e_mov_r32_fix(Buf *b, int reg, int lbl) { /* mov r32, &label (patch abs32) */
    rex(b, 0, 0, 0, reg >> 3);
    buf_put8(b, 0xB8 | (reg & 7));
    fix_abs32(lbl);
}

void e_mov_mem_imm(Buf *b, int32_t disp, int64_t imm) { /* mov qword [rbp+disp], imm32 */
    rex(b, 1, 0, 0, 0);
    buf_put8(b, 0xC7);
    fp_modrm_disp(b, 0, disp);
    put32le(b, (uint32_t)imm);
}

void e_mov_fp_ld(Buf *b, int reg, int32_t disp) { /* mov reg, [rbp+disp] */
    rex(b, 1, reg >> 3, 0, 0);
    buf_put8(b, 0x8B);
    fp_modrm_disp(b, reg, disp);
}

void e_mov_fp_st(Buf *b, int32_t disp, int reg) { /* mov [rbp+disp], reg */
    rex(b, 1, reg >> 3, 0, 0);
    buf_put8(b, 0x89);
    fp_modrm_disp(b, reg, disp);
}

/* [base+index] — index < 0 berarti tanpa indeks (SIB index=100).
 * Catatan: base tidak boleh RBP/R12/RSP (SIB khusus) — dipakai encoder internal. */
static void bi_pre(Buf *b, int *mod, int base, int index) {
    (void)b; (void)index;
    if (base == RBP || base == R12 || base == RSP) die(0, "codegen: base register tak didukung");
    *mod = 0;
}

void e_mov_bi_ld(Buf *b, int reg, int base, int index) {
    int mod; bi_pre(b, &mod, base, index);
    rex(b, 1, reg >> 3, (index >= 0 ? index : 4) >> 3, base >> 3);
    buf_put8(b, 0x8B);
    modrm(b, mod, reg, 4);
    sib(b, 0, index >= 0 ? index : 4, base);
}

void e_mov_bi_st(Buf *b, int base, int index, int reg) {
    int mod; bi_pre(b, &mod, base, index);
    rex(b, 1, reg >> 3, (index >= 0 ? index : 4) >> 3, base >> 3);
    buf_put8(b, 0x89);
    modrm(b, mod, reg, 4);
    sib(b, 0, index >= 0 ? index : 4, base);
}

void e_movb_bi_st(Buf *b, int base, int index, int reg) {
    int mod; bi_pre(b, &mod, base, index);
    rex(b, 0, reg >> 3, (index >= 0 ? index : 4) >> 3, base >> 3);
    buf_put8(b, 0x88);
    modrm(b, mod, reg, 4);
    sib(b, 0, index >= 0 ? index : 4, base);
}

void e_movb_bi_imm(Buf *b, int base, int index, int imm8) {
    int mod; bi_pre(b, &mod, base, index);
    rex(b, 0, 0, (index >= 0 ? index : 4) >> 3, base >> 3);
    buf_put8(b, 0xC6);
    modrm(b, mod, 0, 4);
    sib(b, 0, index >= 0 ? index : 4, base);
    buf_put8(b, (unsigned)(int8_t)imm8);
}

void e_movzx_bi_ld(Buf *b, int reg, int base, int index) {
    int mod; bi_pre(b, &mod, base, index);
    rex(b, 1, reg >> 3, (index >= 0 ? index : 4) >> 3, base >> 3);
    buf_put8(b, 0x0F);
    buf_put8(b, 0xB6);
    modrm(b, mod, reg, 4);
    sib(b, 0, index >= 0 ? index : 4, base);
}

void e_mov32_bi_imm(Buf *b, int base, int index, int32_t disp, int64_t imm) {
    int mod; bi_pre(b, &mod, base, index);
    rex(b, 0, 0, (index >= 0 ? index : 4) >> 3, base >> 3);
    buf_put8(b, 0xC7);
    modrm(b, 1, 0, 4);
    sib(b, 0, index >= 0 ? index : 4, base);
    buf_put8(b, (unsigned)(int8_t)disp);
    put32le(b, (uint32_t)imm);
}

void e_cmp32_bi_imm(Buf *b, int base, int index, int32_t disp, int64_t imm) {
    int mod; bi_pre(b, &mod, base, index);
    rex(b, 0, 0, (index >= 0 ? index : 4) >> 3, base >> 3);
    buf_put8(b, 0x81);
    modrm(b, 1, 7, 4);
    sib(b, 0, index >= 0 ? index : 4, base);
    buf_put8(b, (unsigned)(int8_t)disp);
    put32le(b, (uint32_t)imm);
}

/* ---------------- grup 1 (add/or/and/sub/xor/cmp) ----------------
 * ext: 0 ADD, 1 OR, 4 AND, 5 SUB, 6 XOR, 7 CMP */
static const uint8_t GRP1_OP[8] = {0x01, 0x09, 0x11, 0x19, 0x21, 0x29, 0x31, 0x39};

void e_grp1_rr(Buf *b, int ext, int dst, int src) { /* op r/m64(dst), r64(src) */
    rex(b, 1, src >> 3, 0, dst >> 3);
    buf_put8(b, GRP1_OP[ext]);
    modrm(b, 3, src, dst);
}

void e_grp1_ri(Buf *b, int ext, int reg, int64_t imm) { /* op r64, imm */
    rex(b, 1, 0, 0, reg >> 3);
    if (fits8(imm)) { buf_put8(b, 0x83); modrm(b, 3, ext, reg); buf_put8(b, (unsigned)(int8_t)imm); }
    else            { buf_put8(b, 0x81); modrm(b, 3, ext, reg); put32le(b, (uint32_t)imm); }
}

void e_grp1_fp(Buf *b, int ext, int32_t disp, int64_t imm) { /* op [rbp+disp], imm */
    rex(b, 1, 0, 0, 0);
    if (fits8(imm)) { buf_put8(b, 0x83); fp_modrm_disp(b, ext, disp); buf_put8(b, (unsigned)(int8_t)imm); }
    else            { buf_put8(b, 0x81); fp_modrm_disp(b, ext, disp); put32le(b, (uint32_t)imm); }
}

void e_addb_ri(Buf *b, int reg8, int imm8) { /* add r8, imm8 */
    rex(b, 0, 0, 0, reg8 >> 3);
    buf_put8(b, 0x80);
    modrm(b, 3, 0, reg8);
    buf_put8(b, (unsigned)(int8_t)imm8);
}

/* ---------------- lainnya ---------------- */
void e_imul_rr(Buf *b, int dst, int src) {
    rex(b, 1, dst >> 3, 0, src >> 3);
    buf_put8(b, 0x0F);
    buf_put8(b, 0xAF);
    modrm(b, 3, dst, src);
}

void e_shift_cl(Buf *b, int ext, int reg) { /* ext: 4 SHL, 5 SHR, 7 SAR */
    rex(b, 1, 0, 0, reg >> 3);
    buf_put8(b, 0xD3);
    modrm(b, 3, ext, reg);
}

void e_unary(Buf *b, int ext, int reg) { /* F7: 2 NOT, 3 NEG, 6 DIV, 7 IDIV */
    rex(b, 1, 0, 0, reg >> 3);
    buf_put8(b, 0xF7);
    modrm(b, 3, ext, reg);
}

void e_dec_r(Buf *b, int reg) {
    rex(b, 1, 0, 0, reg >> 3);
    buf_put8(b, 0xFF);
    modrm(b, 3, 1, reg);
}

void e_cqo(Buf *b)     { buf_put8(b, 0x48); buf_put8(b, 0x99); }
void e_syscall(Buf *b) { buf_put8(b, 0x0F); buf_put8(b, 0x05); }
void e_ret(Buf *b)     { buf_put8(b, 0xC3); }
void e_cld(Buf *b)     { buf_put8(b, 0xFC); }
void e_rep_movsb(Buf *b) { buf_put8(b, 0xF3); buf_put8(b, 0xA4); }

void e_xor32_rr(Buf *b, int dst, int src) {
    rex(b, 0, dst >> 3, 0, src >> 3);
    buf_put8(b, 0x31);
    modrm(b, 3, dst, src);
}

void e_test_rr(Buf *b, int r) {
    rex(b, 1, r >> 3, 0, r >> 3);   /* REX.R WAJIB untuk reg field r>=8 */
    buf_put8(b, 0x85);
    modrm(b, 3, r, r);
}

void e_push(Buf *b, int r) { rex(b, 0, 0, 0, r >> 3); buf_put8(b, 0x50 | (r & 7)); }
void e_pop (Buf *b, int r) { rex(b, 0, 0, 0, r >> 3); buf_put8(b, 0x58 | (r & 7)); }

void e_jmp(Buf *b, int lbl)  { buf_put8(b, 0xE9); fix_rel32(lbl); }
void e_call(Buf *b, int lbl) { buf_put8(b, 0xE8); fix_rel32(lbl); }

void e_jcc(Buf *b, int cc, int lbl) {
    buf_put8(b, 0x0F);
    buf_put8(b, 0x80 | (unsigned)cc);
    fix_rel32(lbl);
}

/* ---------------- global segmen RW ([disp32] absolut) ----------------
 * PENTING x86-64: ModRM mod=00 rm=101 = RIP-RELATIVE. Untuk alamat absolut
 * dipakai SIB base=101 (mod=00) = disp32 absolut murni. */
static void abs_modrm_sib(Buf *b, int regfield) {
    modrm(b, 0, regfield, 4);   /* rm=100 -> SIB */
    sib(b, 0, 4, 5);            /* index=none, base=101 -> disp32 absolut */
}

void e_glob_ld(Buf *b, int reg, int goff) {
    rex(b, 1, reg >> 3, 0, 0);
    buf_put8(b, 0x8B);
    abs_modrm_sib(b, reg);
    fix_glob(goff);
}

void e_glob_st(Buf *b, int goff, int reg) {
    rex(b, 1, reg >> 3, 0, 0);
    buf_put8(b, 0x89);
    abs_modrm_sib(b, reg);
    fix_glob(goff);
}

void e_glob_st_imm(Buf *b, int goff, int64_t imm) {
    if (fits32(imm)) {
        rex(b, 1, 0, 0, 0);
        buf_put8(b, 0xC7);
        abs_modrm_sib(b, 0);
        fix_glob(goff);
        put32le(b, (uint32_t)imm);
    } else {  /* muat lewat rax (rax bebas pada titik pemanggilan) */
        e_mov_rs(b, RAX, imm);
        e_glob_st(b, goff, RAX);
    }
}

void e_add_reg_glob(Buf *b, int reg, int goff) { /* add reg, [global] */
    rex(b, 1, reg >> 3, 0, 0);
    buf_put8(b, 0x03);
    abs_modrm_sib(b, reg);
    fix_glob(goff);
}

void e_cmp_reg_glob(Buf *b, int reg, int goff) { /* cmp reg, [global] */
    rex(b, 1, reg >> 3, 0, 0);
    buf_put8(b, 0x3B);
    abs_modrm_sib(b, reg);
    fix_glob(goff);
}

void e_and32_ri8(Buf *b, int reg, int imm8) {
    rex(b, 0, 0, 0, reg >> 3);
    buf_put8(b, 0x83);
    modrm(b, 3, 4, reg);
    buf_put8(b, (unsigned)(int8_t)imm8);
}

void e_lea_rd(Buf *b, int dst, int base, int32_t disp) {
    if (base == RBP || base == R12) die(0, "codegen: lea base tak didukung");
    rex(b, 1, dst >> 3, 0, base >> 3);
    buf_put8(b, 0x8D);
    if (base == RSP) {              /* [rsp+disp] wajib lewat SIB */
        if (fits8(disp)) { modrm(b, 1, dst, 4); sib(b, 0, 4, 4); buf_put8(b, (unsigned)(int8_t)disp); }
        else             { modrm(b, 2, dst, 4); sib(b, 0, 4, 4); put32le(b, (uint32_t)disp); }
        return;
    }
    if (fits8(disp)) { modrm(b, 1, dst, base); buf_put8(b, (unsigned)(int8_t)disp); }
    else             { modrm(b, 2, dst, base); put32le(b, (uint32_t)disp); }
}

/* ---------------- Wrapper nyaman (grup-1) ---------------- */
void e_add_rr(Buf *b, int d, int s) { e_grp1_rr(b, 0, d, s); }
void e_or_rr (Buf *b, int d, int s) { e_grp1_rr(b, 1, d, s); }
void e_and_rr(Buf *b, int d, int s) { e_grp1_rr(b, 4, d, s); }
void e_sub_rr(Buf *b, int d, int s) { e_grp1_rr(b, 5, d, s); }
void e_xor_rr(Buf *b, int d, int s) { e_grp1_rr(b, 6, d, s); }
void e_cmp_rr(Buf *b, int d, int s) { e_grp1_rr(b, 7, d, s); }

void e_add_ri(Buf *b, int r, int64_t v) { e_grp1_ri(b, 0, r, v); }
void e_or_ri (Buf *b, int r, int64_t v) { e_grp1_ri(b, 1, r, v); }
void e_and_ri(Buf *b, int r, int64_t v) { e_grp1_ri(b, 4, r, v); }
void e_sub_ri(Buf *b, int r, int64_t v) { e_grp1_ri(b, 5, r, v); }
void e_xor_ri(Buf *b, int r, int64_t v) { e_grp1_ri(b, 6, r, v); }
void e_cmp_ri(Buf *b, int r, int64_t v) { e_grp1_ri(b, 7, r, v); }
