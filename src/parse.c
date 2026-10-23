/* parse.c — parser + compiler bytecode untuk bahasa Inti
 *
 * Alur: token -> pernyataan -> instruksi bytecode (dengan patching label)
 *       -> resolusi nama (variabel, konstanta bawaan, label data, fungsi)
 */
#include "inti.h"

/* ---------------- Konstanta bawaan (nomor syscall Linux x86-64) -------- */
typedef struct { const char *n; int64_t v; } Builtin;
static const Builtin BUILTINS[] = {
    {"SYS_read", 0}, {"SYS_write", 1}, {"SYS_open", 2}, {"SYS_close", 3},
    {"SYS_stat", 4}, {"SYS_lseek", 8}, {"SYS_mmap", 9}, {"SYS_brk", 12},
    {"SYS_getpid", 39}, {"SYS_getuid", 102}, {"SYS_kill", 62},
    {"SYS_exit", 60}, {"SYS_clock_gettime", 228},
    {"STDIN", 0}, {"STDOUT", 1}, {"STDERR", 2},
    {"true", 1}, {"false", 0},
};
static const int NBUILTINS = (int)(sizeof(BUILTINS) / sizeof(BUILTINS[0]));

/* ---------------- Keadaan parser ---------------- */
static Token *T; static int TN, P;
static Prog *PR;
static int CUR;                      /* indeks fungsi aktif */

static Token *pk(void)  { return &T[P]; }
static Token *pk2(void) { return &T[(P + 1 < TN) ? P + 1 : TN - 1]; }
static int at(TokType t)    { return T[P].type == t; }
static int eat(TokType t)   { if (T[P].type == t) { P++; return 1; } return 0; }
static void expect(TokType t, const char *what) {
    if (T[P].type != t) die(T[P].line, "diharapkan %s", what);
    P++;
}

/* ---------------- Pool operand ekstra (argumen call/sys) ---------------- */
static Operand *POOL; static int POOLN, POOLC;
static int pool_push(Operand o) {
    if (POOLN >= POOLC) { POOLC = POOLC ? POOLC * 2 : 32; POOL = xrealloc(POOL, (size_t)POOLC * sizeof(Operand)); }
    POOL[POOLN] = o; return POOLN++;
}

/* ---------------- Tabel samping per fungsi ---------------- */
typedef struct { int sid, slot; } VarEnt;
typedef struct { int sid, idx; } LabelEnt;
typedef struct { int ins, sid, line; } FixEnt;
typedef struct {
    VarEnt  *vars; int nv, cv;
    LabelEnt *lbl; int nl, cl;
    FixEnt  *fix; int nf, cf;
} FnSide;
static FnSide *SIDE; static int SIDE_CAP;

static FnSide *sd(int fi) { return &SIDE[fi]; }

static int fn_find_by_name(int sid) {
    for (int i = 0; i < PR->nfuncs; i++)
        if (PR->funcs[i]->name_id == sid) return i;
    return -1;
}

static int new_func(int name_sid) {
    if (fn_find_by_name(name_sid) >= 0)
        die(0, "fungsi '%s' didefinisikan dua kali", str_get(name_sid));
    if (PR->nfuncs >= SIDE_CAP) {
        SIDE_CAP = SIDE_CAP ? SIDE_CAP * 2 : 8;
        SIDE = xrealloc(SIDE, (size_t)SIDE_CAP * sizeof(FnSide));
        PR->funcs = xrealloc(PR->funcs, (size_t)SIDE_CAP * sizeof(Func*));
    }
    Func *f = xmalloc(sizeof(Func));
    memset(f, 0, sizeof *f);
    f->name_id = name_sid;
    int fi = PR->nfuncs++;
    PR->funcs[fi] = f;
    memset(&SIDE[fi], 0, sizeof(FnSide));
    return fi;
}

static int var_find(int fi, int sid) {
    FnSide *s = sd(fi);
    for (int i = 0; i < s->nv; i++) if (s->vars[i].sid == sid) return s->vars[i].slot;
    return -1;
}
static int var_define(int fi, int sid) {
    int r = var_find(fi, sid);
    if (r >= 0) return r;
    FnSide *s = sd(fi);
    if (s->nv >= s->cv) { s->cv = s->cv ? s->cv * 2 : 8; s->vars = xrealloc(s->vars, (size_t)s->cv * sizeof(VarEnt)); }
    s->vars[s->nv].sid = sid; s->vars[s->nv].slot = s->nv;
    return s->nv++;
}
static int label_find(int fi, int sid) {
    FnSide *s = sd(fi);
    for (int i = 0; i < s->nl; i++) if (s->lbl[i].sid == sid) return s->lbl[i].idx;
    return -1;
}
static void label_define(int fi, int sid, int idx, int line) {
    if (label_find(fi, sid) >= 0) die(line, "label '%s' ganda", str_get(sid));
    FnSide *s = sd(fi);
    if (s->nl >= s->cl) { s->cl = s->cl ? s->cl * 2 : 8; s->lbl = xrealloc(s->lbl, (size_t)s->cl * sizeof(LabelEnt)); }
    s->lbl[s->nl].sid = sid; s->lbl[s->nl].idx = idx; s->nl++;
}
static void fix_push(int fi, int ins, int sid, int line) {
    FnSide *s = sd(fi);
    if (s->nf >= s->cf) { s->cf = s->cf ? s->cf * 2 : 8; s->fix = xrealloc(s->fix, (size_t)s->cf * sizeof(FixEnt)); }
    s->fix[s->nf].ins = ins; s->fix[s->nf].sid = sid; s->fix[s->nf].line = line; s->nf++;
}

/* ---------------- Label data ---------------- */
static DataLabel *dl_find(int sid) {
    for (int i = 0; i < PR->ndatalbl; i++)
        if (PR->datalbl[i]->name_id == sid) return PR->datalbl[i];
    return NULL;
}
static void data_push_bytes(const uint8_t *b, int64_t n) {
    PR->data = xrealloc(PR->data, (size_t)(PR->data_len + n));
    memcpy(PR->data + PR->data_len, b, (size_t)n);
    PR->data_len += n;
}

/* ---------------- Emit instruksi ---------------- */
static int emit(int op, int line) {
    Func *f = PR->funcs[CUR];
    if (f->ncode >= f->cap) { f->cap = f->cap ? f->cap * 2 : 64; f->code = xrealloc(f->code, (size_t)f->cap * sizeof(Ins)); }
    Ins *i = &f->code[f->ncode];
    memset(i, 0, sizeof *i);
    i->op = op; i->line = line; i->tgt = -1;
    i->d.kind = K_DISC; i->a.kind = K_DISC; i->b.kind = K_DISC;
    return f->ncode++;
}

static Operand mk_const(int64_t v) { Operand o; o.kind = K_CONST; o.v = v; o.off = 0; return o; }
static Operand mk_disc(void)      { Operand o; o.kind = K_DISC;  o.v = 0; o.off = 0; return o; }
static Operand mk_pconst(int64_t v) { Operand o; o.kind = K_PCONST; o.v = v; o.off = 0; return o; }
static Operand mk_soff(int slot, int64_t off, uint8_t kind) {
    Operand o; o.kind = kind; o.v = slot; o.off = off; return o;
}

/* 1 selagi mengurai argumen 'sys': '@label_data' harus jadi pointer arena
 * (K_PCONST), bukan offset mentah — VM menerjemahkan K_PCONST menjadi
 * base+addr dengan cek batas. Di luar sys, perilaku lama dipertahankan. */
static int SYSCTX;

/* ---------------- Operand ---------------- */
static int is_jump_op(int op) {
    return op == OP_JMP || op == OP_JE || op == OP_JNE ||
           op == OP_JL || op == OP_JLE || op == OP_JG || op == OP_JGE;
}

static Operand parse_operand(int is_dst) {
    Token *t = pk();
    int line = t->line;
    if (t->type == T_AT) {
        if (is_dst) die(line, "'@' tidak boleh di posisi tujuan");
        P++;
        if (at(T_NUM)) { int64_t v = T[P].num; P++; Operand o; o.kind = K_PCONST; o.v = v; o.off = 0; return o; }
        if (at(T_IDENT)) {
            int sid = T[P].sid; P++;
            int slot = var_find(CUR, sid);
            DataLabel *dl = slot < 0 ? dl_find(sid) : NULL;
            if (slot < 0 && !dl)
                die(line, "'@%s': variabel atau label data tidak dikenal", str_get(sid));
            /* ofset opsional: @buf+4 */
            int64_t off = 0;
            if ((at(T_PLUS) || at(T_MINUS)) && pk2()->type == T_NUM) {
                off = at(T_PLUS) ? pk2()->num : -pk2()->num;
                P += 2;
            }
            if (slot >= 0)
                return mk_soff(slot, off, off ? K_PSOFF : K_PSLOT);
            return SYSCTX ? mk_pconst(dl->addr + off)
                          : mk_const(dl->addr + off);
        }
        die(line, "'@' harus diikuti alamat atau variabel");
    }
    if (t->type == T_MINUS) {
        if (is_dst) die(line, "literal negatif tidak boleh jadi tujuan");
        P++;
        if (!at(T_NUM)) die(T[P].line, "tanda '-' hanya untuk literal angka negatif (pakai 'neg' untuk variabel)");
        Operand o = mk_const(-T[P].num); P++; return o;
    }
    if (t->type == T_NUM)  { P++; return mk_const(t->num); }
    if (t->type == T_CHAR) { P++; return mk_const(t->num); }
    if (t->type == T_IDENT) {
        int sid = t->sid; P++;
        if (is_dst) {
            if (at(T_PLUS) || at(T_MINUS))
                die(t->line, "alamat ber-ofset tidak boleh jadi tujuan (hitung dulu ke variabel lain)");
            if (!strcmp(str_get(sid), "_")) return mk_disc();
            int slot = var_define(CUR, sid);
            Operand o; o.kind = K_SLOT; o.v = slot; o.off = 0; return o;
        }
        int slot = var_find(CUR, sid);
        /* aritmetika alamat: nama+angka / nama-angka (pengalamatan ber-ofset) */
        if ((at(T_PLUS) || at(T_MINUS)) && pk2()->type == T_NUM) {
            int64_t off = at(T_PLUS) ? pk2()->num : -pk2()->num;
            P += 2;
            if (slot >= 0) return mk_soff(slot, off, K_SOFF);
            DataLabel *dl0 = dl_find(sid);
            if (dl0) return mk_const(dl0->addr + off);
            Operand o; o.kind = K_NAME; o.v = sid; o.off = off; return o;  /* maju: resolusi nanti */
        }
        if (slot >= 0) { Operand o; o.kind = K_SLOT; o.v = slot; o.off = 0; return o; }
        for (int i = 0; i < NBUILTINS; i++)
            if (!strcmp(str_get(sid), BUILTINS[i].n)) return mk_const(BUILTINS[i].v);
        DataLabel *dl = dl_find(sid);
        if (dl) return mk_const(dl->addr);
        Operand o; o.kind = K_NAME; o.v = sid; o.off = 0; return o;  /* ditunda ke resolusi */
    }
    if (t->type == T_STR) die(line, "string hanya boleh dipakai di perintah 'data'");
    die(line, "operand tidak valid");
    return mk_disc(); /* tak terjangkau */
}

static void endline(int line) {
    (void)line;
    /* hanya verifikasi akhir baris; konsumsi newline dilakukan loop pemanggil */
    if (at(T_NEWLINE) || at(T_RBRACE) || at(T_EOF)) return;
    die(T[P].line, "akhir baris tidak terduga di dekat '%s'",
        T[P].type == T_IDENT ? str_get(T[P].sid) : "(token)");
}

static void need_dst(Operand o, int line, const char *w) {
    if (o.kind != K_SLOT && o.kind != K_DISC)
        die(line, "tujuan '%s' harus variabel atau '_'", w);
}

/* ---------------- Pernyataan ---------------- */
static void parse_block(void);
static void parse_if(void);

/* kumpulan loop aktif (untuk break/continue) */
typedef struct { int fn; int top; int *brk; int nbrk, cbrk; } LoopCtx;
static LoopCtx *LOOPS; static int NLOOPS, CLOOPS;

static void parse_cond(Operand *pa, Operand *pb, int *jf_op) {
    *pa = parse_operand(0);
    int t = pk()->type;
    if (t == T_LT || t == T_LE || t == T_GT || t == T_GE || t == T_EQ || t == T_NE) {
        P++;
        *pb = parse_operand(0);
        switch (t) {
            case T_LT: *jf_op = OP_JGE; break;
            case T_LE: *jf_op = OP_JG;  break;
            case T_GT: *jf_op = OP_JLE; break;
            case T_GE: *jf_op = OP_JL;  break;
            case T_EQ: *jf_op = OP_JNE; break;
            default:   *jf_op = OP_JE;  break;
        }
    } else {
        *pb = mk_const(0);
        *jf_op = OP_JE;   /* salah bila operand == 0 */
    }
}

static void parse_if(void) {
    int line = pk()->line;
    P++; /* 'if' */
    Operand a, b; int jf;
    parse_cond(&a, &b, &jf);
    int e1 = emit(OP_CMP, line);
    PR->funcs[CUR]->code[e1].a = a; PR->funcs[CUR]->code[e1].b = b;
    int j1 = emit(jf, line);
    expect(T_LBRACE, "'{' setelah kondisi if");
    parse_block();
    int save = P;
    while (at(T_NEWLINE)) P++;
    if (at(T_KW_ELSE)) {
        P++;
        int j2 = emit(OP_JMP, line);
        PR->funcs[CUR]->code[j1].tgt = PR->funcs[CUR]->ncode;
        if (at(T_KW_IF)) parse_if();          /* else if */
        else { expect(T_LBRACE, "'{' setelah 'else'"); parse_block(); }
        PR->funcs[CUR]->code[j2].tgt = PR->funcs[CUR]->ncode;
    } else {
        P = save;   /* kembalikan newline pemisah agar milik loop blok */
        PR->funcs[CUR]->code[j1].tgt = PR->funcs[CUR]->ncode;
    }
}

static void parse_while(void) {
    int line = pk()->line;
    P++; /* 'while' */
    Operand a, b; int jf;
    parse_cond(&a, &b, &jf);
    int top = PR->funcs[CUR]->ncode;
    int e1 = emit(OP_CMP, line);
    PR->funcs[CUR]->code[e1].a = a; PR->funcs[CUR]->code[e1].b = b;
    int j1 = emit(jf, line);
    if (NLOOPS >= CLOOPS) { CLOOPS = CLOOPS ? CLOOPS * 2 : 8; LOOPS = xrealloc(LOOPS, (size_t)CLOOPS * sizeof(LoopCtx)); }
    LOOPS[NLOOPS].fn = CUR; LOOPS[NLOOPS].top = top; LOOPS[NLOOPS].brk = NULL;
    LOOPS[NLOOPS].nbrk = 0; LOOPS[NLOOPS].cbrk = 0;
    NLOOPS++;
    expect(T_LBRACE, "'{' setelah kondisi while");
    parse_block();
    NLOOPS--;
    int jback = emit(OP_JMP, line);
    PR->funcs[CUR]->code[jback].tgt = top;
    PR->funcs[CUR]->code[j1].tgt = PR->funcs[CUR]->ncode;
    LoopCtx *lc = &LOOPS[NLOOPS];
    for (int i = 0; i < lc->nbrk; i++)
        PR->funcs[CUR]->code[lc->brk[i]].tgt = PR->funcs[CUR]->ncode;
    free(lc->brk); lc->brk = NULL; lc->nbrk = 0;
}

static void parse_break_continue(int is_break, int line) {
    P++;
    endline(line);
    if (NLOOPS == 0) die(line, "'%s' di luar loop while", is_break ? "break" : "continue");
    LoopCtx *lc = &LOOPS[NLOOPS - 1];
    if (is_break) {
        int e = emit(OP_JMP, line);
        if (lc->nbrk >= lc->cbrk) { lc->cbrk = lc->cbrk ? lc->cbrk * 2 : 4; lc->brk = xrealloc(lc->brk, (size_t)lc->cbrk * sizeof(int)); }
        lc->brk[lc->nbrk++] = e;
    } else {
        int e = emit(OP_JMP, line);
        PR->funcs[CUR]->code[e].tgt = lc->top;
    }
}

static void jump_mnemonic(int op, int line) {
    int e = emit(op, line);
    if (!at(T_IDENT)) die(T[P].line, "diharapkan nama label setelah lompatan");
    int sid = T[P].sid; P++;
    endline(line);
    fix_push(CUR, e, sid, line);
}

static void arith_mnemonic(int op, int line, const char *w) {
    Operand d = parse_operand(1);
    need_dst(d, line, w);
    expect(T_COMMA, "','");
    Operand a = parse_operand(0);
    Operand b = mk_disc();
    if (eat(T_COMMA)) b = parse_operand(0);
    endline(line);
    int e = emit(op, line);
    PR->funcs[CUR]->code[e].d = d;
    PR->funcs[CUR]->code[e].a = a;
    PR->funcs[CUR]->code[e].b = b;
}

static void parse_data(int line) {
    if (!at(T_IDENT)) die(line, "'data' butuh nama label");
    int sid = T[P].sid; P++;
    if (dl_find(sid)) die(line, "label data '%s' ganda", str_get(sid));
    expect(T_COMMA, "',' setelah nama data");
    int64_t addr = 0, dlen = 0;
    if (at(T_STR)) {                       /* data nama, "byte" */
        dlen = T[P].num;
        const char *bytes = str_get(T[P].sid);
        P++;
        addr = PR->data_len;
        data_push_bytes((const uint8_t*)bytes, (size_t)dlen);
    } else if (at(T_NUM)) {                /* data nama, N  — cadangkan N byte nol (gaya BSS) */
        dlen = T[P].num;
        P++;
        if (dlen < 0 || dlen > (16LL << 20))
            die(line, "'data' cadangan harus 0..%d byte", 16 << 20);
        addr = PR->data_len;
        static const uint8_t zeros[4096];
        int64_t left = dlen;
        while (left > 0) {
            int64_t chunk = left > 4096 ? 4096 : left;
            data_push_bytes(zeros, (size_t)chunk);
            left -= chunk;
        }
    } else {
        die(line, "'data' butuh string literal atau angka (jumlah byte nol)");
    }
    PR->datalbl = xrealloc(PR->datalbl, (size_t)(PR->ndatalbl + 1) * sizeof(DataLabel*));
    DataLabel *dl = xmalloc(sizeof(DataLabel));
    dl->name_id = sid; dl->addr = addr; dl->len = dlen;
    PR->datalbl[PR->ndatalbl++] = dl;
    endline(line);
}

static void parse_stmt(void) {
    Token *t = pk();
    int line = t->line;
    switch (t->type) {
    case T_KW_IF:      parse_if(); return;
    case T_KW_WHILE:   parse_while(); return;
    case T_KW_BREAK:   parse_break_continue(1, line); return;
    case T_KW_CONTINUE:parse_break_continue(0, line); return;
    case T_KW_RET: {
        P++;
        int e = emit(OP_RET, line);
        if (!(at(T_NEWLINE) || at(T_RBRACE) || at(T_EOF))) {
            PR->funcs[CUR]->code[e].a = parse_operand(0);
        }
        endline(line);
        return;
    }
    case T_IDENT: {
        if (pk2()->type == T_COLON) {   /* label: (boleh diikuti instruksi) */
            label_define(CUR, t->sid, PR->funcs[CUR]->ncode, line);
            P += 2;
            if (!(at(T_NEWLINE) || at(T_RBRACE) || at(T_EOF)))
                parse_stmt();           /* lanjutkan sisa baris */
            return;
        }
        const char *w = str_get(t->sid);
        P++;
        Func *f = PR->funcs[CUR];

        if (!strcmp(w, "set")) {
            Operand d = parse_operand(1); need_dst(d, line, w);
            expect(T_COMMA, "','");
            Operand s = parse_operand(0);
            endline(line);
            int e = emit(OP_SET, line);
            f->code[e].d = d; f->code[e].a = s;
        }
        else if (!strcmp(w,"add")||!strcmp(w,"sub")||!strcmp(w,"mul")||!strcmp(w,"div")||!strcmp(w,"mod")||
                 !strcmp(w,"and")||!strcmp(w,"or") ||!strcmp(w,"xor")||!strcmp(w,"shl")||!strcmp(w,"shr")||!strcmp(w,"sar")) {
            int op = !strcmp(w,"add")?OP_ADD : !strcmp(w,"sub")?OP_SUB : !strcmp(w,"mul")?OP_MUL :
                     !strcmp(w,"div")?OP_DIV : !strcmp(w,"mod")?OP_MOD : !strcmp(w,"and")?OP_AND :
                     !strcmp(w,"or") ?OP_OR  : !strcmp(w,"xor")?OP_XOR : !strcmp(w,"shl")?OP_SHL :
                     !strcmp(w,"shr")?OP_SHR : OP_SAR;
            arith_mnemonic(op, line, w);
        }
        else if (!strcmp(w,"neg")||!strcmp(w,"not")) {
            int op = !strcmp(w,"neg") ? OP_NEG : OP_NOT;
            Operand d = parse_operand(1); need_dst(d, line, w);
            Operand a = d;
            if (eat(T_COMMA)) a = parse_operand(0);
            endline(line);
            int e = emit(op, line);
            f->code[e].d = d; f->code[e].a = a;
        }
        else if (!strcmp(w, "cmp")) {
            Operand a = parse_operand(0);
            expect(T_COMMA, "','");
            Operand b = parse_operand(0);
            endline(line);
            int e = emit(OP_CMP, line);
            f->code[e].a = a; f->code[e].b = b;
        }
        else if (!strcmp(w,"jmp")) jump_mnemonic(OP_JMP, line);
        else if (!strcmp(w,"je"))  jump_mnemonic(OP_JE,  line);
        else if (!strcmp(w,"jne")) jump_mnemonic(OP_JNE, line);
        else if (!strcmp(w,"jl"))  jump_mnemonic(OP_JL,  line);
        else if (!strcmp(w,"jle")) jump_mnemonic(OP_JLE, line);
        else if (!strcmp(w,"jg"))  jump_mnemonic(OP_JG,  line);
        else if (!strcmp(w,"jge")) jump_mnemonic(OP_JGE, line);
        else if (!strcmp(w, "call")) {
            Operand d = parse_operand(1); need_dst(d, line, w);
            expect(T_COMMA, "','");
            if (!at(T_IDENT)) die(line, "diharapkan nama fungsi setelah 'call'");
            Operand fnm; fnm.kind = K_NAME; fnm.v = T[P].sid; fnm.off = 0; P++;
            int e = emit(OP_CALL, line);
            f->code[e].d = d; f->code[e].a = fnm;
            int poff = POOLN, pn = 0;
            while (eat(T_COMMA)) {
                Operand o = parse_operand(0);
                pool_push(o); pn++;
            }
            endline(line);
            f->code[e].poff = poff; f->code[e].pn = pn;
        }
        else if (!strcmp(w, "alloc")) {
            Operand d = parse_operand(1); need_dst(d, line, w);
            expect(T_COMMA, "','");
            Operand sz = parse_operand(0);
            endline(line);
            int e = emit(OP_ALLOC, line);
            f->code[e].d = d; f->code[e].a = sz;
        }
        else if (!strcmp(w, "free")) {
            Operand p = parse_operand(0);
            endline(line);
            int e = emit(OP_FREE, line);
            f->code[e].a = p;
        }
        else if (!strcmp(w, "load") || !strcmp(w, "loadb")) {
            Operand d = parse_operand(1); need_dst(d, line, w);
            expect(T_COMMA, "','");
            Operand p = parse_operand(0);
            endline(line);
            int e = emit(!strcmp(w,"load") ? OP_LOAD : OP_LOADB, line);
            f->code[e].d = d; f->code[e].a = p;
        }
        else if (!strcmp(w, "store") || !strcmp(w, "storeb")) {
            Operand p = parse_operand(0);
            expect(T_COMMA, "','");
            Operand v = parse_operand(0);
            endline(line);
            if (p.kind == K_DISC) die(line, "alamat store tidak boleh '_'");
            int e = emit(!strcmp(w,"store") ? OP_STORE : OP_STOREB, line);
            f->code[e].a = p; f->code[e].b = v;
        }
        else if (!strcmp(w, "sys")) {
            Operand d = parse_operand(1); need_dst(d, line, w);
            expect(T_COMMA, "','");
            Operand nr = parse_operand(0);
            int e = emit(OP_SYS, line);
            f->code[e].d = d; f->code[e].a = nr;
            int poff = POOLN, pn = 0;
            SYSCTX = 1;
            while (eat(T_COMMA)) {
                if (pn >= 6) die(line, "'sys' maksimal 6 argumen");
                Operand o = parse_operand(0);
                pool_push(o); pn++;
            }
            SYSCTX = 0;
            endline(line);
            f->code[e].poff = poff; f->code[e].pn = pn;
        }
        else if (!strcmp(w, "printi")) {
            Operand a = parse_operand(0); endline(line);
            int e = emit(OP_PRINTI, line); f->code[e].a = a;
        }
        else if (!strcmp(w, "prints")) {
            Operand a = parse_operand(0);
            expect(T_COMMA, "','");
            Operand b = parse_operand(0);
            endline(line);
            int e = emit(OP_PRINTS, line); f->code[e].a = a; f->code[e].b = b;
        }
        else if (!strcmp(w, "printc")) {
            Operand a = parse_operand(0); endline(line);
            int e = emit(OP_PRINTC, line); f->code[e].a = a;
        }
        else if (!strcmp(w, "slen")) {
            Operand d = parse_operand(1); need_dst(d, line, w);
            expect(T_COMMA, "','");
            if (!at(T_IDENT)) die(line, "'slen' butuh nama label data");
            Operand a; a.kind = K_NAME; a.v = T[P].sid; P++;  /* sengaja K_NAME */
            endline(line);
            int e = emit(OP_SLEN, line); f->code[e].d = d; f->code[e].a = a;
        }
        else if (!strcmp(w, "exit")) {
            Operand a = mk_const(0);
            if (!(at(T_NEWLINE) || at(T_RBRACE) || at(T_EOF))) a = parse_operand(0);
            endline(line);
            int e = emit(OP_EXIT, line); f->code[e].a = a;
        }
        else if (!strcmp(w, "data")) parse_data(line);
        else die(line, "instruksi tidak dikenal: '%s'", w);
        return;
    }
    default:
        die(line, "pernyataan tidak dikenal (mulai dari token tak terduga)");
    }
}

static void parse_block(void) {
    for (;;) {
        while (at(T_NEWLINE)) P++;
        if (at(T_RBRACE)) { P++; return; }
        if (at(T_EOF)) die(T[P].line, "blok '{' tidak ditutup dengan '}'");
        if (at(T_KW_FN)) die(T[P].line, "'fn' hanya boleh di tingkat atas");
        parse_stmt();
        if (at(T_NEWLINE)) P++;
        else if (!(at(T_RBRACE) || at(T_EOF)))
            die(T[P].line, "pernyataan harus diakhiri baris baru atau '}'");
    }
}

static void parse_fn(void) {
    int line = pk()->line;
    P++; /* 'fn' */
    if (!at(T_IDENT)) die(line, "diharapkan nama fungsi setelah 'fn'");
    int name = T[P].sid; P++;
    if (!strcmp(str_get(name), "__main__")) die(line, "nama '__main__' dipakai sistem");
    int params[64], np = 0;
    while (eat(T_COMMA)) {
        if (!at(T_IDENT)) die(T[P].line, "diharapkan nama parameter");
        if (np >= 64) die(line, "maksimal 64 parameter");
        params[np++] = T[P].sid; P++;
    }
    expect(T_LBRACE, "'{' untuk badan fungsi");
    int fi = new_func(name);
    int saved = CUR; CUR = fi;
    Func *f = PR->funcs[fi];
    f->params = xmalloc((size_t)(np ? np : 1) * sizeof(int));
    f->arity = np;
    for (int k = 0; k < np; k++) f->params[k] = params[k];
    for (int k = 0; k < np; k++) var_define(fi, params[k]);
    parse_block();
    emit(OP_RET, line);   /* penutup: jatuh ke akhir = return 0 */
    CUR = saved;
}

/* ---------------- Resolusi nama ---------------- */
static void resolve_operand(Ins *in, Operand *o, int is_fntarget, int is_slen) {
    if (o->kind != K_NAME) return;
    const char *w = str_get((int)o->v);
    if (is_fntarget) {
        int fi = fn_find_by_name((int)o->v);
        if (fi < 0) die(in->line, "fungsi '%s' tidak dikenal", w);
        Func *f = PR->funcs[fi];
        if (in->pn != f->arity)
            die(in->line, "fungsi '%s' butuh %d argumen, diberi %d", w, f->arity, in->pn);
        o->kind = K_FN; o->v = fi;
        return;
    }
    if (is_slen) {
        if (o->off != 0)
            die(in->line, "'slen' butuh label data murni tanpa ofset");
        DataLabel *dl = dl_find((int)o->v);
        if (!dl) die(in->line, "'slen' butuh label data, bukan '%s'", w);
        o->kind = K_CONST; o->v = dl->len;
        return;
    }
    for (int i = 0; i < NBUILTINS; i++)
        if (!strcmp(w, BUILTINS[i].n)) { o->kind = K_CONST; o->v = BUILTINS[i].v + o->off; return; }
    DataLabel *dl = dl_find((int)o->v);
    if (dl) { o->kind = K_CONST; o->v = dl->addr + o->off; return; }
    die(in->line, "nama tidak dikenal: '%s'", w);
}

static void resolve_all(void) {
    for (int fi = 0; fi < PR->nfuncs; fi++) {
        Func *f = PR->funcs[fi];
        FnSide *s = &SIDE[fi];
        for (int k = 0; k < s->nf; k++) {
            int li = label_find(fi, s->fix[k].sid);
            if (li < 0) die(s->fix[k].line, "label '%s' tidak dikenal", str_get(s->fix[k].sid));
            f->code[s->fix[k].ins].tgt = li;
        }
        for (int c = 0; c < f->ncode; c++) {
            Ins *in = &f->code[c];
            if (is_jump_op(in->op)) continue;   /* operand lompatan sudah DISC */
            resolve_operand(in, &in->d, 0, 0);
            resolve_operand(in, &in->a, in->op == OP_CALL, in->op == OP_SLEN);
            resolve_operand(in, &in->b, 0, 0);
            for (int k = 0; k < in->pn; k++)
                resolve_operand(in, &POOL[in->poff + k], 0, 0);
        }
        f->nslots = s->nv;
    }
}

/* ---------------- Titik masuk kompilasi ---------------- */
Prog *compile_source(const char *src, size_t len, int64_t arena_cap,
                     int64_t vstack_cap, int64_t fstack_cap) {
    int ntok = 0;
    T = tokenize(src, len, &ntok); TN = ntok; P = 0;
    PR = xmalloc(sizeof(Prog));
    memset(PR, 0, sizeof(Prog));
    PR->arena_cap = arena_cap;
    PR->vstack_cap = vstack_cap;
    PR->fstack_cap = fstack_cap;
    PR->funcs = xmalloc(8 * sizeof(Func*));
    SIDE_CAP = 8;
    SIDE = xmalloc((size_t)SIDE_CAP * sizeof(FnSide));
    CUR = new_func(str_intern("__main__", 8));
    for (;;) {
        while (at(T_NEWLINE)) P++;
        if (at(T_EOF)) break;
        if (at(T_KW_FN)) { parse_fn(); continue; }
        parse_stmt();
        if (at(T_NEWLINE)) P++;
        else if (at(T_RBRACE)) die(T[P].line, "'}' tidak seimbang di tingkat atas");
        else if (!at(T_EOF)) die(T[P].line, "akhir baris tidak terduga");
    }
    emit(OP_RET, 0);   /* penutup __main__ */
    resolve_all();
    PR->pool = POOL; PR->pool_n = POOLN;
    return PR;
}
