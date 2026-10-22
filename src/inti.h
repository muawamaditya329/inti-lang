/*
 * inti.h — header bersama untuk bahasa pemrograman INTI
 * Bahasa tingkat rendah bergaya assembly untuk Linux x86-64.
 *
 * Arsitektur: source (.inti) -> tokenizer -> parser/compiler ke bytecode
 *             -> VM berbasis switch-dispatch (tanpa GC, tanpa boxing).
 */
#ifndef INTI_H
#define INTI_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTI_VERSION "0.3.0"

/* ---------------- Tabel string (interning) ---------------- */
int  str_intern(const char *s, size_t len); /* id unik untuk string */
const char *str_get(int id);

/* ---------------- Tokenizer ---------------- */
typedef enum {
    T_EOF = 0, T_NEWLINE,
    T_IDENT, T_NUM, T_CHAR, T_STR,
    T_COMMA, T_COLON, T_LBRACE, T_RBRACE,
    T_LT, T_LE, T_GT, T_GE, T_EQ, T_NE, T_ASSIGN, T_MINUS, T_PLUS, T_AT,
    T_KW_FN, T_KW_RET, T_KW_IF, T_KW_ELSE, T_KW_WHILE,
    T_KW_BREAK, T_KW_CONTINUE
} TokType;

typedef struct {
    TokType type;
    int64_t num;   /* T_NUM / T_CHAR */
    int     sid;   /* T_IDENT / T_STR -> id strtab */
    int     line;
} Token;

/* Tokenisasi seluruh file. Mengembalikan array diakhiri T_EOF.
 * Keluar via die() jika ada karakter liar / string tak ditutup. */
Token *tokenize(const char *src, size_t len, int *ntok);

/* ---------------- Opcode VM ---------------- */
typedef enum {
    /* pergerakan & aritmetika (semua int64) */
    OP_SET = 0, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_AND, OP_OR, OP_XOR, OP_SHL, OP_SHR, OP_SAR, OP_NEG, OP_NOT,
    /* bandingkan & lompat mentah (gaya assembly) */
    OP_CMP, OP_JMP, OP_JE, OP_JNE, OP_JL, OP_JLE, OP_JG, OP_JGE,
    /* fungsi */
    OP_CALL, OP_RET,
    /* memori */
    OP_ALLOC, OP_FREE, OP_LOAD, OP_STORE, OP_LOADB, OP_STOREB,
    /* syscall & io */
    OP_SYS, OP_PRINTI, OP_PRINTS, OP_PRINTC, OP_SLEN, OP_EXIT,
    OP_COUNT
} Op;

/* ---------------- Operand ---------------- */
typedef enum {
    K_CONST = 0,  /* v = nilai langsung */
    K_SLOT  = 1,  /* v = indeks slot variabel pada frame */
    K_DISC  = 2,  /* tujuan dibuang (_) / operand kosong */
    K_NAME  = 3,  /* nama belum terselesaikan (v = strtab id) — hanya fase parse */
    K_FN    = 4,  /* v = indeks fungsi — hasil resolusi OP_CALL */
    K_SOFF  = 7,  /* nilai = sl[v] + off (pengalamatan ber-ofset) */
    K_PCONST = 5, /* v = alamat arena -> pointer nyata ke syscall */
    K_PSLOT  = 6, /* sl[v] = alamat arena -> pointer nyata */
    K_PSOFF  = 8  /* sl[v]+off = alamat arena -> pointer nyata */
} OKind;

typedef struct { int64_t v; int64_t off; uint8_t kind; } Operand;

/* ---------------- Instruksi ---------------- */
typedef struct {
    int     op;
    int     line;
    Operand d, a, b;   /* d = tujuan; a,b = sumber (tergantung op) */
    int     poff, pn;  /* operand ekstra (argumen call/sys) di pool */
    int     tgt;       /* indeks lompatan (per fungsi) */
} Ins;

/* ---------------- Fungsi ---------------- */
typedef struct {
    int   name_id;
    int  *params;      /* strtab id parameter */
    int   arity;
    int   nslots;      /* jumlah slot variabel lokal */
    Ins  *code;
    int   ncode, cap;
} Func;

/* Label data: data nama, "teks" */
typedef struct { int name_id; int64_t addr; int64_t len; } DataLabel;

/* ---------------- Program utuh ---------------- */
typedef struct {
    Func      **funcs;    int nfuncs;
    DataLabel **datalbl;  int ndatalbl;
    uint8_t    *data;     int64_t data_len;   /* seksi data (alamat 0..) */
    Operand    *pool;     int pool_n;         /* operand ekstra call/sys */
    int64_t     arena_cap;
    int64_t     vstack_cap;   /* kapasitas value stack (slot) */
    int64_t     fstack_cap;   /* kapasitas frame stack */
} Prog;

/* ---------------- Parser/compiler ---------------- */
/* Tokenize + parse + resolusi. Keluar via die() bila error kompilasi. */
Prog *compile_source(const char *src, size_t len, int64_t arena_cap,
                     int64_t vstack_cap, int64_t fstack_cap);

/* ---------------- VM ---------------- */
/* Menjalankan program; memanggil exit() saat selesai / OP_EXIT / ret teratas. */
void vm_run(Prog *p);

/* ---------------- Util ---------------- */
void die(int line, const char *fmt, ...);   /* cetak error & exit(1) */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);

#endif /* INTI_H */
