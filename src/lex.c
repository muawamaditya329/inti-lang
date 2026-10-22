/* lex.c — tabel string + tokenizer untuk bahasa Inti */
#include <stdarg.h>
#include "inti.h"

/* ---------------- Tabel string ---------------- */
typedef struct { char *s; uint32_t len; } SEntry;
static SEntry *stab = NULL;
static int stab_n = 0, stab_cap = 0;

int str_intern(const char *s, size_t len) {
    for (int i = 0; i < stab_n; i++)
        if (stab[i].len == len && memcmp(stab[i].s, s, len) == 0) return i;
    if (stab_n >= stab_cap) {
        stab_cap = stab_cap ? stab_cap * 2 : 256;
        stab = xrealloc(stab, (size_t)stab_cap * sizeof(SEntry));
    }
    char *d = xmalloc(len + 1);
    memcpy(d, s, len); d[len] = 0;
    stab[stab_n].s = d; stab[stab_n].len = (uint32_t)len;
    return stab_n++;
}

const char *str_get(int id) { return stab[id].s; }

/* ---------------- Util ---------------- */
void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "inti: kehabisan memori host\n"); exit(1); }
    return p;
}
void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "inti: kehabisan memori host\n"); exit(1); }
    return q;
}

void die(int line, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "inti: error");
    if (line > 0) fprintf(stderr, " (baris %d)", line);
    fprintf(stderr, ": ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

/* ---------------- Tokenizer ---------------- */
typedef struct { const char *s; size_t len; size_t pos; int line; } Lexer;

static int is_id_start(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static int is_id_char (char c){ return is_id_start(c)||(c>='0'&&c<='9'); }

static Token *toks = NULL; static int ntok = 0, cap_tok = 0;
static void push_tok(Token t) {
    if (ntok >= cap_tok) { cap_tok = cap_tok ? cap_tok*2 : 1024; toks = xrealloc(toks, (size_t)cap_tok*sizeof(Token)); }
    toks[ntok++] = t;
}

static char peek(Lexer *L){ return L->pos < L->len ? L->s[L->pos] : 0; }
static char peek2(Lexer *L){ return L->pos+1 < L->len ? L->s[L->pos+1] : 0; }
static char next(Lexer *L){ return L->pos < L->len ? L->s[L->pos++] : 0; }

static int escape(char c, int *ok) {
    *ok = 1;
    switch (c) {
        case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
        case '0': return 0;    case '\\': return '\\'; case '\'': return '\'';
        case '"': return '"';  case '#': return '#';
        default: *ok = 0; return 0;
    }
}

static void lex_number(Lexer *L, Token *t) {
    int64_t v = 0; int base = 10;
    if (peek(L)=='0' && (peek2(L)=='x'||peek2(L)=='X')) { base=16; next(L); next(L); }
    else if (peek(L)=='0' && (peek2(L)=='b'||peek2(L)=='B')) { base=2; next(L); next(L); }
    char c = peek(L);
    int digits = 0;
    for (;;) {
        c = peek(L);
        int d;
        if (c>='0'&&c<='9') d = c-'0';
        else if (c>='a'&&c<='f') d = c-'a'+10;
        else if (c>='A'&&c<='F') d = c-'A'+10;
        else break;
        if (d >= base) break;
        v = v*base + d;
        digits++; next(L);
    }
    if (!digits) die(L->line, "angka tidak valid");
    t->type = T_NUM; t->num = v;
}

Token *tokenize(const char *src, size_t len, int *ntok_out) {
    Lexer L = { src, len, 0, 1 };
    ntok = 0;
    for (;;) {
        char c = peek(&L);
        /* spasi & komentar */
        if (c==' '||c=='\t'||c=='\r') { next(&L); continue; }
        if (c=='#') { while (peek(&L) && peek(&L)!='\n') next(&L); continue; }
        Token t; memset(&t, 0, sizeof t); t.line = L.line;
        if (c==0) { t.type = T_EOF; push_tok(t); break; }
        if (c=='\n') { next(&L); t.type = T_NEWLINE; t.line = L.line; push_tok(t); L.line++; continue; }
        if (is_id_start(c)) {
            size_t st = L.pos;
            while (is_id_char(peek(&L))) next(&L);
            size_t n = L.pos - st;
            t.sid = str_intern(L.s+st, n);
            const char *w = str_get(t.sid);
            if      (!strcmp(w,"fn"))       t.type = T_KW_FN;
            else if (!strcmp(w,"ret"))      t.type = T_KW_RET;
            else if (!strcmp(w,"if"))       t.type = T_KW_IF;
            else if (!strcmp(w,"else"))     t.type = T_KW_ELSE;
            else if (!strcmp(w,"while"))    t.type = T_KW_WHILE;
            else if (!strcmp(w,"break"))    t.type = T_KW_BREAK;
            else if (!strcmp(w,"continue")) t.type = T_KW_CONTINUE;
            else t.type = T_IDENT;
            push_tok(t); continue;
        }
        if (c>='0'&&c<='9') { lex_number(&L, &t); push_tok(t); continue; }
        if (c=='\'') {
            next(&L); int v, ok;
            char ch = next(&L);
            if (ch=='\\') v = escape(next(&L), &ok);
            else if (ch==0 || ch=='\'') die(L.line, "karakter literal kosong");
            else v = (unsigned char)ch;
            if (next(&L) != '\'') die(L.line, "karakter literal tidak ditutup");
            t.type = T_CHAR; t.num = v; push_tok(t); continue;
        }
        if (c=='"') {
            next(&L);
            /* kumpulkan byte string (bisa berisi \0) */
            uint8_t buf[4096]; size_t bn = 0;
            for (;;) {
                char ch = next(&L);
                if (ch==0)   die(L.line, "string tidak ditutup");
                if (ch=='"') break;
                if (ch=='\n') die(L.line, "newline di dalam string (pakai \\n)");
                if (bn >= sizeof buf) die(L.line, "string terlalu panjang (maks 4095)");
                if (ch=='\\') { int ok; int e = escape(next(&L), &ok); if(!ok) die(L.line, "escape tidak dikenal"); buf[bn++] = (uint8_t)e; }
                else buf[bn++] = (uint8_t)ch;
            }
            t.type = T_STR; t.sid = str_intern((const char*)buf, bn); t.num = (int64_t)bn;
            push_tok(t); continue;
        }
        next(&L);
        switch (c) {
            case ',': t.type = T_COMMA; break;
            case ':': t.type = T_COLON; break;
            case '{': t.type = T_LBRACE; break;
            case '}': t.type = T_RBRACE; break;
            case '-': t.type = T_MINUS; break;
            case '+': t.type = T_PLUS; break;
            case '@': t.type = T_AT; break;
            case '<': t.type = (peek(&L)=='=') ? (next(&L), T_LE) : T_LT; break;
            case '>': t.type = (peek(&L)=='=') ? (next(&L), T_GE) : T_GT; break;
            case '=': if (peek(&L)=='=') { next(&L); t.type = T_EQ; }
                      else { die(L.line, "'=' tunggal? gunakan 'set' untuk menugaskan, '==' untuk membandingkan"); }
                      break;
            case '!': if (peek(&L)=='=') { next(&L); t.type = T_NE; }
                      else { die(L.line, "'!' sendirian tidak didukung (pakai 'not')"); }
                      break;
            default: die(L.line, "karakter tidak dikenal: '%c'", c);
        }
        push_tok(t);
    }
    *ntok_out = ntok;
    return toks;
}
