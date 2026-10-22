/* cg.h — antarmuka internal backend codegen x86-64 bahasa Inti
 *
 * Arsitektur Fase 1 (ROADMAP-OS):
 *   .inti -> lex.c/parse.c (bytecode, DIPAKAI BERSAMA interpreter)
 *         -> codegen.c (bytecode -> kode mesin x86-64 + runtime mesin)
 *         -> elfw.c   (penulis ELF64 langsung, TANPA assembler/linker)
 *
 * Binary hasil = ELF64 statis freestanding: tanpa libc, hanya syscall.
 */
#ifndef INTI_CG_H
#define INTI_CG_H

#include "inti.h"

#define INTI_BASE      0x400000ULL  /* vaddr dasar image */
#define INTI_CODE_OFF  0xB0u        /* ELF header (64) + 2 phdr (2x56) */

/* ---------------- Buffer byte ---------------- */
typedef struct { uint8_t *b; size_t n, cap; } Buf;
void buf_put8(Buf *b, unsigned v);
void buf_putn(Buf *b, const void *p, size_t n);

/* ---------------- Register x86-64 (nomor encoding) ---------------- */
enum { RAX = 0, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
       R8 = 8, R9, R10, R11, R12, R13, R14, R15 };

/* Kode kondisi (0F 80+cc) */
enum { CC_B = 2, CC_AE = 3, CC_E = 4, CC_NE = 5, CC_BE = 6, CC_A = 7,
       CC_NS = 9, CC_L = 12, CC_GE = 13, CC_LE = 14, CC_G = 15 };

/* ---------------- Label & fixup ---------------- */
void enc_use(Buf *b);        /* set buffer aktif untuk lbl/fix */
int  lbl_new(void);          /* label baru (belum terikat) */
void lbl_bind(int l);        /* ikat label ke posisi sekarang */
void fix_rel32(int l);       /* sisipkan rel32 -> label (patch di akhir) */
void fix_abs32(int l);       /* sisipkan imm32 absolut (alamat < 4 GB) */
void fix_glob(int goff);     /* disp32 -> variabel global segmen RW */
void fix_resolve(uint64_t data_vaddr, uint64_t text_vaddr); /* patch fixup */

/* ---------------- Encoder instruksi (reg 0..15) ---------------- */
void e_mov_rr(Buf *b, int dst, int src);          /* mov dst, src        */
void e_mov_rs(Buf *b, int reg, int64_t imm);      /* mov reg, imm64/32s  */
void e_mov_r32s(Buf *b, int reg, int64_t imm);    /* mov r32, imm32 (zero) */
void e_mov_r32_fix(Buf *b, int reg, int lbl);     /* mov r32, &label     */
void e_mov_mem_imm(Buf *b, int32_t disp, int64_t imm); /* mov qword [rbp+d], imm */
void e_mov_fp_ld(Buf *b, int reg, int32_t disp);  /* mov reg, [rbp+d]    */
void e_mov_fp_st(Buf *b, int32_t disp, int reg);  /* mov [rbp+d], reg    */
void e_mov_bi_ld(Buf *b, int reg, int base, int index);  /* mov reg, [base+index] */
void e_mov_bi_st(Buf *b, int base, int index, int reg);  /* mov [base+index], reg */
void e_movb_bi_st(Buf *b, int base, int index, int reg); /* mov byte [b+i], r8   */
void e_movb_bi_imm(Buf *b, int base, int index, int imm8);/* mov byte [b+i], imm */
void e_movzx_bi_ld(Buf *b, int reg, int base, int index);/* movzx r64, byte[b+i] */
void e_mov32_bi_imm(Buf *b, int base, int index, int32_t disp, int64_t imm);
void e_cmp32_bi_imm(Buf *b, int base, int index, int32_t disp, int64_t imm);
void e_grp1_rr(Buf *b, int ext, int dst, int src);/* add/or/and/sub/xor/cmp reg,reg */
void e_grp1_ri(Buf *b, int ext, int reg, int64_t imm);   /* ... reg, imm          */
void e_grp1_fp(Buf *b, int ext, int32_t disp, int64_t imm);/* ... [rbp+d], imm    */
void e_addb_ri(Buf *b, int reg8, int imm8);       /* add r8, imm8        */
void e_imul_rr(Buf *b, int dst, int src);         /* imul dst, src       */
void e_shift_cl(Buf *b, int ext, int reg);        /* shl/shr/sar reg, cl */
void e_unary(Buf *b, int ext, int reg);           /* F7 /ext: not2 neg3 div6 */
void e_dec_r(Buf *b, int reg);                    /* dec reg             */
void e_cqo(Buf *b);                               /* cqo                 */
void e_syscall(Buf *b);
void e_ret(Buf *b);
void e_cld(Buf *b);
void e_rep_movsb(Buf *b);
void e_xor32_rr(Buf *b, int dst, int src);        /* xor r32, r32 (nol)  */
void e_test_rr(Buf *b, int r);                    /* test r, r           */
void e_push(Buf *b, int r);
void e_pop(Buf *b, int r);
void e_jmp(Buf *b, int lbl);
void e_call(Buf *b, int lbl);
void e_jcc(Buf *b, int cc, int lbl);
void e_glob_ld(Buf *b, int reg, int goff);        /* mov reg, [global]   */
void e_glob_st(Buf *b, int goff, int reg);        /* mov [global], reg   */
void e_glob_st_imm(Buf *b, int goff, int64_t imm);
void e_add_reg_glob(Buf *b, int reg, int goff);   /* add reg, [global]   */
void e_cmp_reg_glob(Buf *b, int reg, int goff);   /* cmp reg, [global]   */
void e_and32_ri8(Buf *b, int reg, int imm8);      /* and r32, imm8       */
void e_lea_rd(Buf *b, int dst, int base, int32_t disp); /* lea dst, [base+d] */

/* Wrapper grup-1 */
void e_add_rr(Buf*, int dst, int src); void e_sub_rr(Buf*, int dst, int src);
void e_and_rr(Buf*, int dst, int src); void e_or_rr (Buf*, int dst, int src);
void e_xor_rr(Buf*, int dst, int src); void e_cmp_rr(Buf*, int dst, int src);
void e_add_ri(Buf*, int reg, int64_t v); void e_sub_ri(Buf*, int reg, int64_t v);
void e_and_ri(Buf*, int reg, int64_t v); void e_or_ri (Buf*, int reg, int64_t v);
void e_xor_ri(Buf*, int reg, int64_t v); void e_cmp_ri(Buf*, int reg, int64_t v);

/* ---------------- Global segmen RW (byte offset di .data) ---------------- */
enum { G_FA = 0, G_FB = 8, G_BASE = 16, G_CAP = 24, G_TOP = 32,
       G_DLEN = 40, G_CAPM8 = 48, G_CAPM1 = 56, G_DATAN = 64 };

/* ---------------- Label rutinitas runtime mesin ---------------- */
extern int L_FMT, L_PRINTI, L_DIE, L_OOB, L_ALLOC, L_FREE;
void rt_emit_all(Buf *B);                 /* emisi seluruh runtime mesin  */
int  rt_addstr(const void *p, int len);   /* label string rodata (codegen.c) */

/* ---------------- Penulis ELF64 & flat binary ---------------- */
int elf64_write(const char *path, const Buf *text,
                const uint8_t *data, size_t data_sz, uint64_t entry);

/* Header blob program IntiOS (64 byte, di depan kode, dimuat di 0x400000) */
#define IP_MAGIC     0x3152544E49544E49ULL  /* "INTIPRG1" LE */
#define IP_HDRSZ     64
#define IP_PROGBASE  0x400000ULL   /* vaddr tempat kernel memuat program */
int bin_write(const char *path, const Buf *text, size_t data_sz, int64_t arena_mb);

/* ---------------- Target keluaran ---------------- */
enum {
    TARGET_ELF   = 0,   /* ELF64 statis freestanding (Fase 1-2, Linux)  */
    TARGET_INTIOS = 1   /* flat binary .ibin untuk IntiOS (Fase 3)      */
};

/* ---------------- Titik masuk codegen ---------------- */
int codegen_emit(Prog *p, int64_t arena_mb, int64_t stack_mb, const char *out,
                 int target);

#endif /* INTI_CG_H */
