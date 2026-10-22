/* runtime.c — rutinitas runtime bahasa mesin untuk binary native Inti
 *
 * Semua rutinitas DIEMISIKAN SEBAGAI KODE MESIN ke dalam binary hasil
 * inti-cc (bukan libc, bukan objek terpisah). Kernel hanya dipanggil
 * lewat syscall mentah: write(1), exit(60). mmap ada di _start (codegen.c).
 *
 * Konvensi runtime "Inti ABI":
 *   - argumen di RDI, RSI, RDX, RCX (urut); hasil di RAX
 *   - wajib menjaga RBX, RBP, R12..R15 (kode hasilkan menitipkan
 *     frame pointer di RBP); runtime hanya memakai register caller-saved
 *   - stack ter-align 16 byte pada setiap titik call
 *
 * Pesan error runtime memakai format yang sama dengan interpreter
 * ("inti: error (baris N): <pesan>") sehingga fragmen .err test suite
 * tetap cocok di kedua backend.
 */
#include "cg.h"

int L_FMT, L_PRINTI, L_DIE, L_OOB, L_ALLOC, L_FREE;

#define BLK_MAGIC 0x494E5449LL  /* "INTI" (u32 little-endian) */
#define HDR_SZ    16

static int S_PREFIX, S_SEP, S_NL, S_MID, S_END;
static int S_NEG, S_FULL, S_FREEBAD;

/* write(fd, string rodata via label, len) */
static void wstr(Buf *b, int fd, int strlbl, int64_t len) {
    e_mov_r32s(b, RDI, fd);
    e_mov_r32_fix(b, RSI, strlbl);
    e_mov_rs(b, RDX, len);
    e_mov_r32s(b, RAX, 1);
    e_syscall(b);
}

/* write(fd, [rbp+dptr] = ptr, [rbp+dlen] = len) */
static void wmem(Buf *b, int fd, int32_t dptr, int32_t dlen) {
    e_mov_r32s(b, RDI, fd);
    e_mov_fp_ld(b, RSI, dptr);
    e_mov_fp_ld(b, RDX, dlen);
    e_mov_r32s(b, RAX, 1);
    e_syscall(b);
}

/* prolog standar helper runtime: entry rsp%16==8 -> push rbp -> %16==0 */
static void rt_prolog(Buf *b, int32_t locals) {
    e_push(b, RBP);
    e_mov_rr(b, RBP, RSP);
    if (locals) e_sub_ri(b, RSP, locals);
}

static void rt_epilog(Buf *b) {
    e_mov_rr(b, RSP, RBP);
    e_pop(b, RBP);
}

/* setelah call L_FMT: rax=len, string mulai di base+20-len.
 * Siapkan RDX=len, RSI=penunjuk karakter pertama. */
static void after_fmt(Buf *b, int base_reg) {
    e_mov_rr(b, RDX, RAX);
    e_lea_rd(b, RSI, base_reg, 20);
    e_sub_rr(b, RSI, RAX);
}

void rt_emit_all(Buf *B) {
    enc_use(B);

    S_PREFIX  = rt_addstr("inti: error (baris ", 19);
    S_SEP     = rt_addstr("): ", 3);
    S_NL      = rt_addstr("\n", 1);
    S_MID     = rt_addstr(": alamat ", 9);
    S_END     = rt_addstr(" di luar arena\n", 15);
    S_NEG     = rt_addstr("alloc dengan ukuran negatif", 27);
    S_FULL    = rt_addstr("arena penuh (naikkan --arena-mb pada inti-cc)", 45);
    S_FREEBAD = rt_addstr("free: alamat bukan blok yang valid", 34);

    L_FMT    = lbl_new();
    L_PRINTI = lbl_new();
    L_DIE    = lbl_new();
    L_OOB    = lbl_new();
    L_ALLOC  = lbl_new();
    L_FREE   = lbl_new();

    /* ============ __rt_fmt(rdi=v, rsi=buf) -> rax = panjang desimal ============ */
    lbl_bind(L_FMT);
    {
        e_mov_rr(B, RAX, RDI);
        e_xor32_rr(B, R9, R9);            /* flag negatif */
        e_test_rr(B, RAX);
        int Lpos = lbl_new();
        e_jcc(B, CC_NS, Lpos);            /* jns */
        e_unary(B, 3, RAX);               /* neg rax (|INT64_MIN| benar sbg u64) */
        e_mov_r32s(B, R9, 1);
        lbl_bind(Lpos);

        e_lea_rd(B, R8, RSI, 20);         /* r8 = ujung buffer (20 digit cukup) */
        e_mov_rs(B, RCX, 10);
        int Lloop = lbl_new();
        lbl_bind(Lloop);
        e_xor32_rr(B, RDX, RDX);
        e_unary(B, 6, RCX);               /* div rcx (unsigned) */
        e_addb_ri(B, RDX, '0');
        e_dec_r(B, R8);
        e_movb_bi_st(B, R8, -1, RDX);     /* mov [r8], dl */
        e_test_rr(B, RAX);
        e_jcc(B, CC_NE, Lloop);

        e_test_rr(B, R9);
        int Lw = lbl_new();
        e_jcc(B, CC_E, Lw);
        e_dec_r(B, R8);
        e_movb_bi_imm(B, R8, -1, '-');
        lbl_bind(Lw);

        e_mov_rr(B, RAX, RSI);
        e_add_ri(B, RAX, 20);
        e_sub_rr(B, RAX, R8);             /* len = (buf+20) - r8 */
    }
    e_ret(B);

    /* ============ __rt_printi(rdi=v): tulis desimal ke fd 1 ============ */
    lbl_bind(L_PRINTI);
    {
        rt_prolog(B, 32);
        e_mov_rr(B, RSI, RSP);
        e_call(B, L_FMT);
        after_fmt(B, RSP);
        e_mov_r32s(B, RDI, 1);
        e_mov_r32s(B, RAX, 1);
        e_syscall(B);
        rt_epilog(B);
        e_ret(B);
    }

    /* ======== __rt_die(rdi=line, rsi=msg, rdx=len): cetak pesan & exit(1) ======== */
    lbl_bind(L_DIE);
    {
        rt_prolog(B, 48);
        e_mov_fp_st(B, -8, RDI);
        e_mov_fp_st(B, -16, RSI);
        e_mov_fp_st(B, -24, RDX);
        wstr(B, 2, S_PREFIX, 19);
        e_mov_fp_ld(B, RDI, -8);          /* line */
        e_mov_rr(B, RSI, RSP);
        e_call(B, L_FMT);
        after_fmt(B, RSP);
        e_mov_r32s(B, RDI, 2);
        e_mov_r32s(B, RAX, 1);
        e_syscall(B);
        wstr(B, 2, S_SEP, 3);
        wmem(B, 2, -16, -24);             /* pesan asli */
        wstr(B, 2, S_NL, 1);
        e_mov_r32s(B, RDI, 1);
        e_mov_r32s(B, RAX, 60);
        e_syscall(B);
    }

    /* ==== __rt_oob(rdi=line, rsi=what, rdx=whatlen, rcx=off): cetak & exit(1) ==== */
    lbl_bind(L_OOB);
    {
        rt_prolog(B, 64);
        e_mov_fp_st(B, -8, RDI);
        e_mov_fp_st(B, -16, RSI);
        e_mov_fp_st(B, -24, RDX);
        e_mov_fp_st(B, -32, RCX);
        wmem(B, 2, -16, -24);             /* jenis akses: "load"/"store"/... */
        wstr(B, 2, S_MID, 9);
        e_mov_fp_ld(B, RDI, -32);         /* alamat */
        e_mov_rr(B, RSI, RSP);
        e_call(B, L_FMT);
        after_fmt(B, RSP);
        e_mov_r32s(B, RDI, 2);
        e_mov_r32s(B, RAX, 1);
        e_syscall(B);
        wstr(B, 2, S_END, 15);
        e_mov_r32s(B, RDI, 1);
        e_mov_r32s(B, RAX, 60);
        e_syscall(B);
    }

    /* ======== __rt_alloc(rdi=size, rsi=line) -> rax=addr (first-fit arena) ======== */
    lbl_bind(L_ALLOC);
    {
        int Lok = lbl_new(), Lsz = lbl_new(), Lloop = lbl_new();
        int Lbump = lbl_new(), Lfull = lbl_new(), Lnext = lbl_new();

        e_test_rr(B, RDI);
        e_jcc(B, CC_NS, Lok);             /* size >= 0 -> lanjut */
        e_mov_rr(B, RDI, RSI);            /* size < 0: die(line, S_NEG) */
        e_mov_r32_fix(B, RSI, S_NEG);
        e_mov_rs(B, RDX, 27);
        e_call(B, L_DIE);
        lbl_bind(Lok);

        e_mov_rr(B, R11, RDI);            /* need = align8(size ? size : 1) */
        e_test_rr(B, R11);
        e_jcc(B, CC_NE, Lsz);
        e_mov_rs(B, R11, 1);
        lbl_bind(Lsz);
        e_add_ri(B, R11, 7);
        e_and_ri(B, R11, -8);

        e_glob_ld(B, R8, G_DLEN);         /* cur = align8(data_len) */
        e_add_ri(B, R8, 7);
        e_and_ri(B, R8, -8);
        e_glob_ld(B, R9, G_TOP);          /* top */
        e_glob_ld(B, R10, G_BASE);        /* base */

        lbl_bind(Lloop);
        e_mov_rr(B, RAX, R8);
        e_add_ri(B, RAX, HDR_SZ);
        e_cmp_rr(B, RAX, R9);
        e_jcc(B, CC_A, Lbump);            /* cur+16 > top -> tidak ada blok bebas */

        e_mov_rr(B, RCX, R10);
        e_add_rr(B, RCX, R8);             /* rcx = &header */
        e_mov_bi_ld(B, RAX, RCX, -1);     /* hsize = [rcx] */
        e_cmp32_bi_imm(B, RCX, -1, 8, 0);
        e_jcc(B, CC_NE, Lnext);           /* used -> lewati */
        e_cmp32_bi_imm(B, RCX, -1, 12, BLK_MAGIC);
        e_jcc(B, CC_NE, Lnext);           /* magic rusak -> lewati */
        e_cmp_rr(B, RAX, R11);
        e_jcc(B, CC_B, Lnext);            /* hsize < need -> lewati */

        e_mov32_bi_imm(B, RCX, -1, 8, 1); /* used = 1 */
        e_lea_rd(B, RAX, R8, HDR_SZ);     /* addr = cur + 16 */
        e_ret(B);

        lbl_bind(Lnext);
        e_add_ri(B, R8, HDR_SZ);
        e_add_rr(B, R8, RAX);
        e_jmp(B, Lloop);

        lbl_bind(Lbump);
        e_mov_rr(B, RAX, R9);
        e_add_ri(B, RAX, HDR_SZ);
        e_add_rr(B, RAX, R11);
        e_cmp_reg_glob(B, RAX, G_CAP);
        e_jcc(B, CC_A, Lfull);            /* top+16+need > cap -> arena penuh */

        e_mov_bi_st(B, R10, R9, R11);          /* [base+top] = need */
        e_mov32_bi_imm(B, R10, R9, 8, 1);      /* used = 1 */
        e_mov32_bi_imm(B, R10, R9, 12, BLK_MAGIC);
        e_lea_rd(B, RAX, R9, HDR_SZ);
        e_add_rr(B, RAX, R11);
        e_glob_st(B, G_TOP, RAX);              /* top += 16 + need */
        e_lea_rd(B, RAX, R9, HDR_SZ);
        e_ret(B);

        lbl_bind(Lfull);
        e_mov_rr(B, RDI, RSI);            /* die(line, S_FULL) */
        e_mov_r32_fix(B, RSI, S_FULL);
        e_mov_rs(B, RDX, 45);
        e_call(B, L_DIE);
    }

    /* ======== __rt_free(rdi=addr, rsi=line): tandai blok bebas ======== */
    lbl_bind(L_FREE);
    {
        int Lbad = lbl_new();

        e_mov_rr(B, RAX, RDI);
        e_sub_ri(B, RAX, HDR_SZ);         /* hoff = addr - 16 */

        e_glob_ld(B, RCX, G_DLEN);        /* hoff < align8(data_len) -> salah */
        e_add_ri(B, RCX, 7);
        e_and_ri(B, RCX, -8);
        e_cmp_rr(B, RAX, RCX);
        e_jcc(B, CC_B, Lbad);

        e_lea_rd(B, RCX, RAX, HDR_SZ);    /* hoff+16 > top -> salah */
        e_cmp_reg_glob(B, RCX, G_TOP);
        e_jcc(B, CC_A, Lbad);

        e_glob_ld(B, RDX, G_BASE);
        e_add_rr(B, RDX, RAX);            /* rdx = &header */
        e_cmp32_bi_imm(B, RDX, -1, 12, BLK_MAGIC);
        e_jcc(B, CC_NE, Lbad);            /* magic rusak -> salah */

        e_mov32_bi_imm(B, RDX, -1, 8, 0); /* used = 0 */
        e_ret(B);

        lbl_bind(Lbad);
        e_mov_rr(B, RDI, RSI);            /* die(line, S_FREEBAD) */
        e_mov_r32_fix(B, RSI, S_FREEBAD);
        e_mov_rs(B, RDX, 34);
        e_call(B, L_DIE);
    }
}
