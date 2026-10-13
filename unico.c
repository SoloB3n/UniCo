/*
 * UniCo — un codice unico.
 *
 * Interprete completo del linguaggio UniCo in un solo file C99,
 * senza dipendenze oltre alla libreria standard.
 *
 *   cc -O2 -o unico unico.c -lm
 *
 * Per incorporarlo in un altro programma:
 *   #define UNICO_NO_MAIN
 *   #include "unico.c"
 * e usa uc_init(), uc_def(), uc_run().
 */
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define UC_VERSION   "0.3.0"
#define UC_STACK_MAX 65536
#define UC_DEPTH_MAX 1800

#if defined(__GNUC__) || defined(__clang__)
#define NORET __attribute__((noreturn))
#else
#define NORET
#endif

/* ================================================================== */
/*  Valori e oggetti                                                   */
/* ================================================================== */

typedef enum { T_NIL, T_BOOL, T_NUM, T_STR, T_CELL, T_FUNC, T_NATIVE } VType;
typedef enum { O_STR, O_CELL, O_FUNC, O_NATIVE, O_ENV } OType;

typedef struct Obj { OType type; unsigned char marked, perm; struct Obj *next; } Obj;
typedef struct { VType t; union { int b; double n; Obj *o; } u; } Value;

typedef struct { Obj h; size_t len; uint32_t hash; int ascii; char chars[]; } Str;
typedef struct { Value key, val; } Entry;
/* La cella: una parte ordinata (lista) + una parte con chiavi (mappa). */
typedef struct {
    Obj h;
    Value *arr; int alen, acap;
    Entry *ent; int elen, ecap;
    int *idx; int icap;
} Cell;
/* loop = ambiente di una singola iterazione di "per" (vedi eval_for) */
typedef struct Env { Obj h; struct Env *parent; Cell *vars; int loop; } Env;
typedef struct Node Node;
typedef struct { Obj h; Node *node; Env *env; } Func;
typedef Value (*NativeFn)(int argc, Value *argv);
typedef struct { Obj h; NativeFn fn; const char *name; } Native;

#define VNIL      ((Value){ T_NIL, { 0 } })
#define AS_STR(v)  ((Str *)(v).u.o)
#define AS_CELL(v) ((Cell *)(v).u.o)
#define AS_FUNC(v) ((Func *)(v).u.o)
#define AS_NAT(v)  ((Native *)(v).u.o)

static Value vnum(double d) { Value v; v.t = T_NUM; v.u.n = d; return v; }
static Value vbool(int b) { Value v; v.t = T_BOOL; v.u.b = !!b; return v; }
static Value vobj(VType t, void *o) { Value v; v.t = t; v.u.o = (Obj *)o; return v; }
#define VSTR(s)  vobj(T_STR, (s))
#define VCELL(c) vobj(T_CELL, (c))

typedef struct { char *s; size_t len, cap; } Buf;

/* ================================================================== */
/*  AST                                                                */
/* ================================================================== */

typedef enum {
    N_NUM, N_STR, N_INTERP, N_IDENT, N_SELF, N_TRUE, N_FALSE, N_NIL, N_CELL,
    N_BLOCK, N_UNARY, N_BINARY, N_AND, N_OR, N_COALESCE, N_ASSIGN, N_CALL,
    N_INDEX, N_FIELD, N_FUNC, N_IF, N_WHILE, N_FOR, N_RANGE, N_MATCH, N_ARM,
    N_WILD, N_RETURN, N_BREAK, N_CONTINUE, N_SPREAD
} NType;

struct Node {
    NType t; int line; int op; int flag; double num; Str *str;
    const char *file;
    Node *a, *b, *c;
    Node **kids, **keys; int nk, ck;
    Str **names; int nn;
};

/* ================================================================== */
/*  Stato dell'interprete                                              */
/* ================================================================== */

enum { U_NONE, U_RETURN, U_BREAK, U_CONTINUE };

static struct {
    Obj *objects; size_t nobj, next_gc;
    Value stack[UC_STACK_MAX]; int sp;
    Env *envs[UC_DEPTH_MAX + 64]; int esp;
    Obj **gray; int ngray, cgray;
    Env *globals;
    int unwind; Value ret;
    jmp_buf *err; Value errval;
    int line, depth;
    Str *s_self;
    uint64_t rng;
    const char *file;                 /* file del codice in esecuzione */
    struct { const char *name, *file; int line; } frames[UC_DEPTH_MAX + 1];
    Buf trace;                        /* catena di chiamate dell'ultimo errore */
    Cell *modules;                    /* moduli già importati, per percorso */
    size_t bytes, next_bytes;         /* memoria stimata in uso */
    /* limiti per eseguire codice non fidato */
    int sandbox, fatal;
    long steps, max_steps;
    double max_time; clock_t t0;
    size_t mem_limit;
    int lang;                         /* lingua dei messaggi: LANG_IT o LANG_EN */
} vm;

enum { LANG_IT, LANG_EN };
/* Ogni testo mostrato all'utente esiste in italiano e in inglese. */
#define L(it, en) (vm.lang == LANG_EN ? (en) : (it))

static Value eval(Node *n, Env *e);
static Value eval_block(Node *b, Env *e);
static Value call_value(Value f, int argc, Value *argv, Value self, int has_self);
static void to_buf(void *b, Value v, int quote, int depth);
static void gc(void);

/* ================================================================== */
/*  Utilità                                                            */
/* ================================================================== */

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fputs("unico: memoria esaurita\n", stderr); exit(1); }
    return q;
}

static void buf_put(Buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 64;
        while (c < b->len + n + 1) c *= 2;
        b->s = xrealloc(b->s, c); b->cap = c;
    }
    memcpy(b->s + b->len, s, n); b->len += n; b->s[b->len] = 0;
}
static void buf_puts(Buf *b, const char *s) { buf_put(b, s, strlen(s)); }
static void buf_putc(Buf *b, char c) { buf_put(b, &c, 1); }

static uint32_t hash_bytes(const char *s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}

static const char *mfind(const char *h, size_t hl, const char *n, size_t nl) {
    if (nl == 0) return h;
    for (size_t i = 0; i + nl <= hl; i++)
        if (h[i] == n[0] && memcmp(h + i, n, nl) == 0) return h + i;
    return NULL;
}

static int u8len(unsigned char c) {
    return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
}

/* ================================================================== */
/*  Errori                                                             */
/* ================================================================== */

static Str *str_new(const char *s, size_t n);

static void buf_fmt(Buf *b, const char *fmt, ...) {
    char tmp[512]; va_list ap;
    va_start(ap, fmt); int n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (n > 0) buf_put(b, tmp, n < (int)sizeof tmp ? (size_t)n : sizeof tmp - 1);
}

/* Fotografa la catena di chiamate attiva, dalla più interna. */
static void capture_trace(void) {
    vm.trace.len = 0;
    if (vm.trace.s) vm.trace.s[0] = 0;
    for (int i = vm.depth - 1, shown = 0; i >= 0; i--, shown++) {
        if (shown == 12) { buf_fmt(&vm.trace, L("  ... e altre %d chiamate\n", "  ... and %d more calls\n"), i + 1); break; }
        if (vm.frames[i].file)
            buf_fmt(&vm.trace, L("  in %s, chiamata da %s:%d\n", "  in %s, called from %s:%d\n"),
                    vm.frames[i].name, vm.frames[i].file, vm.frames[i].line);
        else
            buf_fmt(&vm.trace, L("  in %s, chiamata alla riga %d\n", "  in %s, called at line %d\n"),
                    vm.frames[i].name, vm.frames[i].line);
    }
}

NORET static void uc_throw(Value v) {
    vm.errval = v;
    capture_trace();
    if (vm.err) longjmp(*vm.err, 1);
    Buf b = { 0 }; to_buf(&b, v, 0, 0);
    fprintf(stderr, "%s: %s\n", L("errore", "error"), b.s ? b.s : "");
    exit(1);
}

NORET static void rt_error(const char *fmt, ...) {
    char msg[512], full[600];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    if (vm.file) snprintf(full, sizeof full, "%s:%d: %s", vm.file, vm.line, msg);
    else snprintf(full, sizeof full, L("riga %d: %s", "line %d: %s"), vm.line, msg);
    uc_throw(VSTR(str_new(full, strlen(full))));
}

/* Errore dovuto a un limite: prova() non può intercettarlo. */
NORET static void limit_error(const char *msg) {
    vm.fatal = 1;
    rt_error("%s", msg);
}

/* ================================================================== */
/*  Memoria e garbage collector (mark & sweep)                         */
/* ================================================================== */

/* Ogni crescita di memoria passa di qui: con un limite attivo, oltre il
   doppio del limite ci si ferma subito (il controllo preciso avviene nei
   punti sicuri, dopo una raccolta). */
static void mem_grow(size_t n) {
    vm.bytes += n;
    if (vm.mem_limit && vm.bytes > vm.mem_limit * 2) limit_error(L("superato il limite di memoria", "memory limit exceeded"));
}

static void *gc_alloc(size_t sz, OType t) {
    mem_grow(sz);
    Obj *o = calloc(1, sz);
    if (!o) { fputs("unico: memoria esaurita\n", stderr); exit(1); }
    o->type = t; o->next = vm.objects; vm.objects = o; vm.nobj++;
    return o;
}

static void mark_obj(Obj *o) {
    if (!o || o->marked) return;
    o->marked = 1;
    if (o->type == O_STR || o->type == O_NATIVE) return;
    if (vm.ngray == vm.cgray) {
        vm.cgray = vm.cgray ? vm.cgray * 2 : 256;
        vm.gray = xrealloc(vm.gray, vm.cgray * sizeof(Obj *));
    }
    vm.gray[vm.ngray++] = o;
}
static void mark_val(Value v) { if (v.t >= T_STR) mark_obj(v.u.o); }

static void blacken(Obj *o) {
    switch (o->type) {
    case O_CELL: {
        Cell *c = (Cell *)o;
        for (int i = 0; i < c->alen; i++) mark_val(c->arr[i]);
        for (int i = 0; i < c->elen; i++) { mark_val(c->ent[i].key); mark_val(c->ent[i].val); }
        break;
    }
    case O_ENV: mark_obj((Obj *)((Env *)o)->parent); mark_obj((Obj *)((Env *)o)->vars); break;
    case O_FUNC: mark_obj((Obj *)((Func *)o)->env); break;
    default: break;
    }
}

static size_t obj_size(Obj *o) {
    switch (o->type) {
    case O_STR: return sizeof(Str) + ((Str *)o)->len + 1;
    case O_CELL: {
        Cell *c = (Cell *)o;
        return sizeof(Cell) + c->acap * sizeof(Value) + c->ecap * sizeof(Entry) + c->icap * sizeof(int);
    }
    case O_ENV: return sizeof(Env);
    case O_FUNC: return sizeof(Func);
    default: return sizeof(Native);
    }
}

static void free_obj(Obj *o) {
    vm.bytes -= obj_size(o);
    if (o->type == O_CELL) {
        Cell *c = (Cell *)o;
        free(c->arr); free(c->ent); free(c->idx);
    }
    free(o);
}

static void gc(void) {
    mark_obj((Obj *)vm.globals);
    for (int i = 0; i < vm.sp; i++) mark_val(vm.stack[i]);
    for (int i = 0; i < vm.esp; i++) mark_obj((Obj *)vm.envs[i]);
    mark_val(vm.ret); mark_val(vm.errval);
    mark_obj((Obj *)vm.modules);
    while (vm.ngray) blacken(vm.gray[--vm.ngray]);
    Obj **p = &vm.objects;
    while (*p) {
        Obj *o = *p;
        if (o->perm || o->marked) { o->marked = 0; p = &o->next; }
        else { *p = o->next; free_obj(o); vm.nobj--; }
    }
    vm.next_gc = vm.nobj * 2 < 50000 ? 50000 : vm.nobj * 2;
    vm.next_bytes = vm.bytes * 2 < (16u << 20) ? (16u << 20) : vm.bytes * 2;
    if (vm.mem_limit && vm.next_bytes > vm.mem_limit) vm.next_bytes = vm.mem_limit;
}
/* Punto sicuro: tra un'istruzione e l'altra. Qui gira il garbage collector
   e si controllano i limiti di passi, tempo e memoria. */
static void safe_point(void) {
    vm.steps++;
    if (vm.max_steps && vm.steps > vm.max_steps) limit_error(L("superato il limite di passi", "step limit exceeded"));
    if (vm.max_time > 0 && (vm.steps & 1023) == 0 &&
        (double)(clock() - vm.t0) / CLOCKS_PER_SEC > vm.max_time)
        limit_error(L("superato il limite di tempo", "time limit exceeded"));
#ifdef UC_GC_STRESS
    gc();
#else
    if (vm.nobj >= vm.next_gc || vm.bytes >= vm.next_bytes) gc();
#endif
    if (vm.mem_limit && vm.bytes > vm.mem_limit) {
        gc();
        if (vm.bytes > vm.mem_limit) limit_error(L("superato il limite di memoria", "memory limit exceeded"));
    }
}
#define MAYBE_GC() safe_point()

/* ================================================================== */
/*  Stringhe                                                           */
/* ================================================================== */

static Str *str_new(const char *s, size_t n) {
    Str *x = gc_alloc(sizeof(Str) + n + 1, O_STR);
    memcpy(x->chars, s, n); x->chars[n] = 0;
    x->len = n; x->hash = hash_bytes(s, n); x->ascii = 1;
    for (size_t i = 0; i < n; i++) if ((unsigned char)s[i] >= 0x80) { x->ascii = 0; break; }
    return x;
}
static Value vstr(const char *s) { return VSTR(str_new(s, strlen(s))); }
static Value vbuf(Buf *b) { Value v = VSTR(str_new(b->s ? b->s : "", b->len)); free(b->s); return v; }

/* Stringhe permanenti e condivise: nomi e letterali del codice sorgente. */
static Str **itab; static size_t itcap, itcount;
static Str *str_intern(const char *s, size_t n) {
    if ((itcount + 1) * 2 > itcap) {
        size_t nc = itcap ? itcap * 2 : 512;
        Str **nt = calloc(nc, sizeof(Str *));
        for (size_t i = 0; i < itcap; i++) if (itab[i]) {
            size_t j = itab[i]->hash & (nc - 1);
            while (nt[j]) j = (j + 1) & (nc - 1);
            nt[j] = itab[i];
        }
        free(itab); itab = nt; itcap = nc;
    }
    uint32_t h = hash_bytes(s, n);
    size_t i = h & (itcap - 1);
    while (itab[i]) {
        Str *x = itab[i];
        if (x->hash == h && x->len == n && memcmp(x->chars, s, n) == 0) return x;
        i = (i + 1) & (itcap - 1);
    }
    Str *x = str_new(s, n); x->h.perm = 1;
    itab[i] = x; itcount++;
    return x;
}

static long u8count(Str *s) {
    if (s->ascii) return (long)s->len;
    long n = 0;
    for (size_t i = 0; i < s->len; i += u8len((unsigned char)s->chars[i])) n++;
    return n;
}
static size_t u8off(Str *s, long ci) {
    if (s->ascii) return (size_t)ci > s->len ? s->len : (size_t)ci;
    size_t i = 0;
    while (ci-- > 0 && i < s->len) i += u8len((unsigned char)s->chars[i]);
    return i > s->len ? s->len : i;
}

/* ================================================================== */
/*  Celle                                                              */
/* ================================================================== */

static Cell *cell_new(void) { return gc_alloc(sizeof(Cell), O_CELL); }

static void cell_push(Cell *c, Value v) {
    if (c->alen == c->acap) {
        int nc = c->acap ? c->acap * 2 : 4;
        mem_grow((nc - c->acap) * sizeof(Value));
        c->acap = nc;
        c->arr = xrealloc(c->arr, c->acap * sizeof(Value));
    }
    c->arr[c->alen++] = v;
}

static int key_ok(Value k) { return k.t == T_STR || k.t == T_NUM || k.t == T_BOOL; }

static uint32_t key_hash(Value k) {
    if (k.t == T_STR) return AS_STR(k)->hash;
    if (k.t == T_BOOL) return k.u.b ? 1231 : 1237;
    double d = k.u.n == 0 ? 0 : k.u.n;
    uint64_t b; memcpy(&b, &d, 8);
    b ^= b >> 33; b *= 0xff51afd7ed558ccdULL; b ^= b >> 33;
    return (uint32_t)b;
}

static int key_eq(Value a, Value b) {
    if (a.t != b.t) return 0;
    switch (a.t) {
    case T_STR: {
        Str *x = AS_STR(a), *y = AS_STR(b);
        return x == y || (x->hash == y->hash && x->len == y->len && memcmp(x->chars, y->chars, x->len) == 0);
    }
    case T_NUM: return a.u.n == b.u.n;
    case T_BOOL: return a.u.b == b.u.b;
    default: return 0;
    }
}

/* Fino a SMALL_MAP chiavi niente tabella hash: una ricerca lineare costa
   meno (le variabili di una funzione sono quasi sempre poche, e i nomi
   condivisi si confrontano per puntatore). */
#define SMALL_MAP 8

static int cell_find(Cell *c, Value k) {
    if (!c->icap) {
        for (int i = 0; i < c->elen; i++) if (key_eq(c->ent[i].key, k)) return i;
        return -1;
    }
    uint32_t m = c->icap - 1, i = key_hash(k) & m;
    for (;;) {
        int e = c->idx[i];
        if (!e) return -1;
        if (key_eq(c->ent[e - 1].key, k)) return e - 1;
        i = (i + 1) & m;
    }
}

static void cell_index_put(Cell *c, int ei) {
    uint32_t m = c->icap - 1, i = key_hash(c->ent[ei].key) & m;
    while (c->idx[i]) i = (i + 1) & m;
    c->idx[i] = ei + 1;
}

static void cell_reindex(Cell *c) {
    int cap = 8;
    while (cap < c->elen * 2 + 2) cap *= 2;
    if (cap > c->icap) mem_grow((cap - c->icap) * sizeof(int));
    else vm.bytes -= (c->icap - cap) * sizeof(int);
    free(c->idx);
    c->idx = calloc(cap, sizeof(int)); c->icap = cap;
    for (int i = 0; i < c->elen; i++) cell_index_put(c, i);
}

static void cell_set_key(Cell *c, Value k, Value v) {
    int f = cell_find(c, k);
    if (f >= 0) { c->ent[f].val = v; return; }
    if (c->elen == c->ecap) {
        int nc = c->ecap ? c->ecap * 2 : 4;
        mem_grow((nc - c->ecap) * sizeof(Entry));
        c->ecap = nc;
        c->ent = xrealloc(c->ent, c->ecap * sizeof(Entry));
    }
    c->ent[c->elen].key = k; c->ent[c->elen].val = v; c->elen++;
    if (c->elen <= SMALL_MAP) return;
    if (c->elen * 2 > c->icap) cell_reindex(c); else cell_index_put(c, c->elen - 1);
}

static int cell_get_key(Cell *c, Value k, Value *out) {
    int f = cell_find(c, k);
    if (f < 0) return 0;
    *out = c->ent[f].val; return 1;
}

static void cell_del_key(Cell *c, int f) {
    memmove(c->ent + f, c->ent + f + 1, (c->elen - f - 1) * sizeof(Entry));
    c->elen--;
    if (c->elen > SMALL_MAP) { cell_reindex(c); return; }
    vm.bytes -= c->icap * sizeof(int);
    free(c->idx); c->idx = NULL; c->icap = 0;
}

static int num_index(double d, long *out) {
    if (d != floor(d) || fabs(d) > 1e15) return 0;
    *out = (long)d; return 1;
}

static Value cell_get(Cell *c, Value k) {
    long i;
    if (k.t == T_NUM && num_index(k.u.n, &i)) {
        if (i < 0) i += c->alen;
        if (i >= 0 && i < c->alen) return c->arr[i];
    }
    if (!key_ok(k)) return VNIL;
    int f = cell_find(c, k);
    return f < 0 ? VNIL : c->ent[f].val;
}

static const char *tname(Value v);

static void cell_set(Cell *c, Value k, Value v) {
    long i;
    if (k.t == T_NUM && num_index(k.u.n, &i)) {
        long j = i < 0 ? i + c->alen : i;
        if (j >= 0 && j < c->alen) { c->arr[j] = v; return; }
        if (j == c->alen && cell_find(c, k) < 0) { cell_push(c, v); return; }
    }
    if (!key_ok(k)) rt_error(L("una chiave non può essere di tipo %s", "a key cannot be of type %s"), tname(k));
    cell_set_key(c, k, v);
}

static Cell *cell_copy(Cell *s) {
    Cell *c = cell_new();
    for (int i = 0; i < s->alen; i++) cell_push(c, s->arr[i]);
    for (int i = 0; i < s->elen; i++) cell_set_key(c, s->ent[i].key, s->ent[i].val);
    return c;
}

/* ================================================================== */
/*  Ambienti (variabili)                                               */
/* ================================================================== */

static Env *env_new(Env *parent) {
    Env *e = gc_alloc(sizeof(Env), O_ENV);
    e->parent = parent; e->vars = cell_new();
    return e;
}
static int env_lookup(Env *e, Str *name, Value *out) {
    for (; e; e = e->parent) if (cell_get_key(e->vars, VSTR(name), out)) return 1;
    return 0;
}
static Env *env_owner(Env *e, Str *name) {
    Value dummy;
    for (; e; e = e->parent) if (cell_get_key(e->vars, VSTR(name), &dummy)) return e;
    return NULL;
}
static void setv(Env *e, Str *name, Value v) { cell_set_key(e->vars, VSTR(name), v); }

static void push(Value v) {
    if (vm.sp >= UC_STACK_MAX) rt_error(L("pila esaurita", "stack exhausted"));
    vm.stack[vm.sp++] = v;
}

/* ================================================================== */
/*  Operazioni sui valori                                              */
/* ================================================================== */

/* Nomi dei tipi: tipo() li dà sempre in italiano, type() sempre in
   inglese, così un programma funziona allo stesso modo in ogni lingua. */
static const char *tname_in(Value v, int lang) {
    static const char *it[] = { "nulla", "logico", "numero", "testo", "cella", "funzione", "funzione" };
    static const char *en[] = { "nil", "bool", "number", "string", "cell", "function", "function" };
    return (lang == LANG_EN ? en : it)[v.t];
}
static const char *tname(Value v) { return tname_in(v, vm.lang); }

static int truthy(Value v) { return !(v.t == T_NIL || (v.t == T_BOOL && !v.u.b)); }

static void fmt_num(Buf *b, double d) {
    char tmp[64];
    if (isnan(d)) buf_puts(b, "nan");
    else if (isinf(d)) buf_puts(b, d > 0 ? "inf" : "-inf");
    else {
        if (d == floor(d) && fabs(d) < 1e15) snprintf(tmp, sizeof tmp, "%.0f", d == 0 ? 0.0 : d);
        else snprintf(tmp, sizeof tmp, "%.14g", d);
        buf_puts(b, tmp);
    }
}

static int is_id0(int c) { return isalpha(c) || c == '_' || c >= 0x80; }
static int is_id(int c) { return isalnum(c) || c == '_' || c >= 0x80; }

static void quote_str(Buf *b, Str *s) {
    buf_putc(b, '"');
    for (size_t i = 0; i < s->len; i++) {
        char c = s->chars[i];
        switch (c) {
        case '"': buf_puts(b, "\\\""); break;
        case '\\': buf_puts(b, "\\\\"); break;
        case '\n': buf_puts(b, "\\n"); break;
        case '\t': buf_puts(b, "\\t"); break;
        case '{': buf_puts(b, "\\{"); break;
        default: buf_putc(b, c);
        }
    }
    buf_putc(b, '"');
}

static void to_buf(void *bp, Value v, int q, int depth) {
    Buf *b = bp;
    switch (v.t) {
    case T_NIL: buf_puts(b, L("nulla", "nil")); break;
    case T_BOOL: buf_puts(b, v.u.b ? L("vero", "true") : L("falso", "false")); break;
    case T_NUM: fmt_num(b, v.u.n); break;
    case T_STR:
        if (q) quote_str(b, AS_STR(v));
        else buf_put(b, AS_STR(v)->chars, AS_STR(v)->len);
        break;
    case T_CELL: {
        Cell *c = AS_CELL(v);
        if (depth > 8) { buf_puts(b, "[...]"); break; }
        buf_putc(b, '[');
        for (int i = 0; i < c->alen; i++) {
            if (i) buf_puts(b, ", ");
            to_buf(b, c->arr[i], 1, depth + 1);
        }
        for (int i = 0; i < c->elen; i++) {
            if (i || c->alen) buf_puts(b, ", ");
            Value k = c->ent[i].key;
            int bare = 0;
            if (k.t == T_STR && AS_STR(k)->len && is_id0((unsigned char)AS_STR(k)->chars[0])) {
                bare = 1;
                for (size_t j = 0; j < AS_STR(k)->len; j++)
                    if (!is_id((unsigned char)AS_STR(k)->chars[j])) bare = 0;
            }
            to_buf(b, k, !bare, depth + 1);
            buf_puts(b, ": ");
            to_buf(b, c->ent[i].val, 1, depth + 1);
        }
        buf_putc(b, ']');
        break;
    }
    case T_FUNC: {
        Str *nm = AS_FUNC(v)->node->str;
        buf_puts(b, L("<funzione", "<function"));
        if (nm) { buf_putc(b, ' '); buf_puts(b, nm->chars); }
        buf_putc(b, '>');
        break;
    }
    case T_NATIVE: buf_puts(b, L("<funzione nativa ", "<native function ")); buf_puts(b, AS_NAT(v)->name); buf_putc(b, '>'); break;
    }
}

static Value tostr(Value v) {
    if (v.t == T_STR) return v;
    Buf b = { 0 }; to_buf(&b, v, 0, 0);
    return vbuf(&b);
}

static int val_eq(Value a, Value b, int depth) {
    if (a.t != b.t) return 0;
    switch (a.t) {
    case T_NIL: return 1;
    case T_BOOL: case T_NUM: case T_STR: return key_eq(a, b);
    case T_CELL: {
        Cell *x = AS_CELL(a), *y = AS_CELL(b);
        if (x == y) return 1;
        if (depth > 32 || x->alen != y->alen || x->elen != y->elen) return 0;
        for (int i = 0; i < x->alen; i++) if (!val_eq(x->arr[i], y->arr[i], depth + 1)) return 0;
        for (int i = 0; i < x->elen; i++) {
            Value o;
            if (!cell_get_key(y, x->ent[i].key, &o) || !val_eq(x->ent[i].val, o, depth + 1)) return 0;
        }
        return 1;
    }
    default: return a.u.o == b.u.o;
    }
}

/* Confronto per ordinamento: strict = errore se i tipi non sono confrontabili. */
static int cmp_vals(Value a, Value b, int strict) {
    if (a.t == T_NUM && b.t == T_NUM) return a.u.n < b.u.n ? -1 : a.u.n > b.u.n;
    if (a.t == T_STR && b.t == T_STR) {
        Str *x = AS_STR(a), *y = AS_STR(b);
        size_t n = x->len < y->len ? x->len : y->len;
        int r = memcmp(x->chars, y->chars, n);
        if (r) return r < 0 ? -1 : 1;
        return x->len < y->len ? -1 : x->len > y->len;
    }
    if (strict) rt_error(L("impossibile confrontare %s con %s", "cannot compare %s with %s"), tname(a), tname(b));
    return a.t < b.t ? -1 : a.t > b.t;
}

static int contains(Value c, Value x) {
    if (c.t == T_STR) {
        if (x.t != T_STR) return 0;
        return mfind(AS_STR(c)->chars, AS_STR(c)->len, AS_STR(x)->chars, AS_STR(x)->len) != NULL;
    }
    if (c.t == T_CELL) {
        Cell *k = AS_CELL(c);
        for (int i = 0; i < k->alen; i++) if (val_eq(k->arr[i], x, 0)) return 1;
        return key_ok(x) && cell_find(k, x) >= 0;
    }
    if (c.t == T_NIL) return 0;
    rt_error(L("'in' non funziona su %s", "'in' does not work on %s"), tname(c));
}

/* Token: definiti qui perché gli operatori binari usano gli stessi codici. */
#define TOKENS(X) \
    X(EOF, "fine del file") X(NL, "a capo") X(NUM, "numero") X(STR, "testo") X(IDENT, "nome") \
    X(SELFF, "@campo") X(AT, "@") X(INFIX, "`infisso`") \
    X(LP, "(") X(RP, ")") X(LB, "[") X(RB, "]") X(LC, "{") X(RC, "}") X(COMMA, ",") X(DOT, ".") \
    X(COLON, ":") X(SEMI, ";") X(PLUS, "+") X(MINUS, "-") X(STAR, "*") X(SLASH, "/") X(PCT, "%") \
    X(EQ, "=") X(EQEQ, "==") X(NE, "!=") X(LT, "<") X(LE, "<=") X(GT, ">") X(GE, ">=") \
    X(PLUSEQ, "+=") X(MINUSEQ, "-=") X(STAREQ, "*=") X(SLASHEQ, "/=") X(BANG, "!") \
    X(ANDAND, "&&") X(OROR, "||") X(PIPE, "|") X(PIPEGT, "|>") X(ARROW, "=>") \
    X(DOTDOT, "..") X(DOTDOTLT, "..<") X(QQ, "??") X(CARET, "^") X(ELLIPSIS, "...") \
    X(IF, "se/if") X(ELSE, "altrimenti/else") X(WHILE, "mentre/while") X(FOR, "per/for") \
    X(IN, "in") X(FN, "fn") X(RETURN, "ritorna/return") X(BREAK, "basta/break") \
    X(CONTINUE, "continua/continue") X(TRUE, "vero/true") X(FALSE, "falso/false") \
    X(NIL, "nulla/nil") X(AND, "and") X(OR, "or") X(NOT, "not") X(MATCH, "scegli/match")
#define X_ENUM(a, b) TK_##a,
#define X_NAME(a, b) b,
typedef enum { TOKENS(X_ENUM) TK__COUNT } Tk;
static const char *TKN[] = { TOKENS(X_NAME) };

static const char *tkname(int t) {
    if (vm.lang == LANG_EN) switch (t) {
        case TK_EOF: return "end of file";
        case TK_NL: return "newline";
        case TK_NUM: return "number";
        case TK_STR: return "string";
        case TK_IDENT: return "name";
        case TK_SELFF: return "@field";
        case TK_INFIX: return "`infix`";
        default: break;
    }
    return TKN[t];
}

static Value binop(int op, Value a, Value b) {
    switch (op) {
    case TK_PLUS:
        if (a.t == T_NUM && b.t == T_NUM) return vnum(a.u.n + b.u.n);
        if (a.t == T_STR || b.t == T_STR) {
            Buf bf = { 0 }; to_buf(&bf, a, 0, 0); to_buf(&bf, b, 0, 0);
            return vbuf(&bf);
        }
        if (a.t == T_CELL && b.t == T_CELL) {
            Cell *c = cell_copy(AS_CELL(a)), *y = AS_CELL(b);
            for (int i = 0; i < y->alen; i++) cell_push(c, y->arr[i]);
            for (int i = 0; i < y->elen; i++) cell_set_key(c, y->ent[i].key, y->ent[i].val);
            return VCELL(c);
        }
        break;
    case TK_MINUS: if (a.t == T_NUM && b.t == T_NUM) return vnum(a.u.n - b.u.n); break;
    case TK_STAR:
        if (a.t == T_NUM && b.t == T_NUM) return vnum(a.u.n * b.u.n);
        if (a.t == T_STR && b.t == T_NUM) {
            if (vm.mem_limit && AS_STR(a)->len * b.u.n > (double)vm.mem_limit) limit_error(L("superato il limite di memoria", "memory limit exceeded"));
            Buf bf = { 0 };
            for (long i = 0; i < (long)b.u.n; i++) buf_put(&bf, AS_STR(a)->chars, AS_STR(a)->len);
            return vbuf(&bf);
        }
        if (a.t == T_CELL && b.t == T_NUM) {
            if (vm.mem_limit && AS_CELL(a)->alen * b.u.n * sizeof(Value) > (double)vm.mem_limit)
                limit_error(L("superato il limite di memoria", "memory limit exceeded"));
            Cell *c = cell_new(), *x = AS_CELL(a);
            for (long i = 0; i < (long)b.u.n; i++)
                for (int j = 0; j < x->alen; j++) cell_push(c, x->arr[j]);
            return VCELL(c);
        }
        break;
    case TK_SLASH: if (a.t == T_NUM && b.t == T_NUM) return vnum(a.u.n / b.u.n); break;
    case TK_PCT:
        if (a.t == T_NUM && b.t == T_NUM) {
            double r = fmod(a.u.n, b.u.n);
            if (r != 0 && ((r < 0) != (b.u.n < 0))) r += b.u.n;
            return vnum(r);
        }
        break;
    case TK_EQEQ: return vbool(val_eq(a, b, 0));
    case TK_NE: return vbool(!val_eq(a, b, 0));
    case TK_LT: return vbool(cmp_vals(a, b, 1) < 0);
    case TK_LE: return vbool(cmp_vals(a, b, 1) <= 0);
    case TK_GT: return vbool(cmp_vals(a, b, 1) > 0);
    case TK_GE: return vbool(cmp_vals(a, b, 1) >= 0);
    case TK_IN: return vbool(contains(b, a));
    }
    rt_error(L("operazione '%s' non valida tra %s e %s", "invalid operation '%s' between %s and %s"), tkname(op), tname(a), tname(b));
}

static Value index_get(Value o, Value k) {
    if (o.t == T_CELL) return cell_get(AS_CELL(o), k);
    if (o.t == T_STR) {
        long i; Str *s = AS_STR(o);
        if (k.t != T_NUM || !num_index(k.u.n, &i)) rt_error(L("un testo si indicizza con un numero intero", "a string must be indexed with an integer"));
        long n = u8count(s);
        if (i < 0) i += n;
        if (i < 0 || i >= n) return VNIL;
        size_t off = u8off(s, i);
        size_t l = u8len((unsigned char)s->chars[off]);
        if (off + l > s->len) l = s->len - off;
        return VSTR(str_new(s->chars + off, l));
    }
    rt_error(L("impossibile indicizzare un valore di tipo %s", "cannot index a value of type %s"), tname(o));
}

static void index_set(Value o, Value k, Value v) {
    if (o.t != T_CELL) rt_error(L("si possono modificare solo le celle, non %s", "only cells can be modified, not %s"), tname(o));
    cell_set(AS_CELL(o), k, v);
}

/* ================================================================== */
/*  Lexer                                                              */
/* ================================================================== */

typedef struct {
    Tk t; int line; double num; Str *s;
    const char *raw; int rawlen; int interp;
} Token;
typedef struct { Token *v; int n, cap; } TokVec;

static const struct { const char *w; Tk t; } KW[] = {
    { "if", TK_IF }, { "se", TK_IF }, { "else", TK_ELSE }, { "altrimenti", TK_ELSE },
    { "while", TK_WHILE }, { "mentre", TK_WHILE }, { "for", TK_FOR }, { "per", TK_FOR },
    { "in", TK_IN }, { "fn", TK_FN }, { "funz", TK_FN },
    { "return", TK_RETURN }, { "ritorna", TK_RETURN }, { "break", TK_BREAK }, { "basta", TK_BREAK },
    { "continue", TK_CONTINUE }, { "continua", TK_CONTINUE },
    { "true", TK_TRUE }, { "vero", TK_TRUE }, { "false", TK_FALSE }, { "falso", TK_FALSE },
    { "nil", TK_NIL }, { "nulla", TK_NIL }, { "and", TK_AND }, { "or", TK_OR },
    { "not", TK_NOT }, { "non", TK_NOT }, { "match", TK_MATCH }, { "scegli", TK_MATCH },
};

static const struct { const char *s; Tk t; } OPS[] = {
    { "...", TK_ELLIPSIS }, { "..<", TK_DOTDOTLT }, { "==", TK_EQEQ }, { "!=", TK_NE }, { "<=", TK_LE }, { ">=", TK_GE },
    { "+=", TK_PLUSEQ }, { "-=", TK_MINUSEQ }, { "*=", TK_STAREQ }, { "/=", TK_SLASHEQ },
    { "&&", TK_ANDAND }, { "||", TK_OROR }, { "|>", TK_PIPEGT }, { "=>", TK_ARROW },
    { "..", TK_DOTDOT }, { "??", TK_QQ },
    { "+", TK_PLUS }, { "-", TK_MINUS }, { "*", TK_STAR }, { "/", TK_SLASH }, { "%", TK_PCT },
    { "=", TK_EQ }, { "<", TK_LT }, { ">", TK_GT }, { "!", TK_BANG }, { "|", TK_PIPE },
    { "(", TK_LP }, { ")", TK_RP }, { "[", TK_LB }, { "]", TK_RB }, { "{", TK_LC }, { "}", TK_RC },
    { ",", TK_COMMA }, { ".", TK_DOT }, { ":", TK_COLON }, { ";", TK_SEMI }, { "^", TK_CARET },
};

/* Dopo questi token un "a capo" non chiude l'istruzione. */
static int cont_tok(Tk t) {
    switch (t) {
    case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH: case TK_PCT: case TK_EQ:
    case TK_EQEQ: case TK_NE: case TK_LT: case TK_LE: case TK_GT: case TK_GE:
    case TK_PLUSEQ: case TK_MINUSEQ: case TK_STAREQ: case TK_SLASHEQ: case TK_ANDAND:
    case TK_OROR: case TK_AND: case TK_OR: case TK_NOT: case TK_PIPEGT: case TK_ARROW:
    case TK_DOTDOT: case TK_DOTDOTLT: case TK_QQ: case TK_COMMA: case TK_DOT: case TK_LP:
    case TK_LB: case TK_LC: case TK_COLON: case TK_INFIX: case TK_IN: case TK_PIPE:
        return 1;
    default: return 0;
    }
}

static Token *emit(TokVec *tv, Tk t, int line) {
    if (tv->n == tv->cap) {
        tv->cap = tv->cap ? tv->cap * 2 : 256;
        tv->v = xrealloc(tv->v, tv->cap * sizeof(Token));
    }
    Token *k = &tv->v[tv->n++];
    memset(k, 0, sizeof *k); k->t = t; k->line = line;
    return k;
}

NORET static void lex_error(int line, const char *msg) { vm.line = line; rt_error("%s", msg); }

static void lex(const char *src, size_t len, int line, TokVec *tv) {
    const unsigned char *p = (const unsigned char *)src, *end = p + len;
    char st[256]; int sd = 0;
    while (p < end) {
        int c = *p, L = line;
        if (c == '\n') {
            int top = sd ? st[sd - 1] : '{';
            if (top == '{' && tv->n && tv->v[tv->n - 1].t != TK_NL && !cont_tok(tv->v[tv->n - 1].t)) {
                const unsigned char *q = p;
                while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
                int cont = q + 1 < end && ((q[0] == '|' && q[1] == '>') || (q[0] == '.' && is_id0(q[1])));
                if (!cont) emit(tv, TK_NL, line);
            }
            line++; p++; continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') { p++; continue; }
        if (c == '#' || (c == '/' && p + 1 < end && p[1] == '/')) {
            while (p < end && *p != '\n') p++;
            continue;
        }
        if (isdigit(c)) {
            char buf[80]; int bl = 0; double d = 0;
#define PUTB(ch) do { if (bl < 78) buf[bl++] = (char)(ch); } while (0)
            if (c == '0' && p + 1 < end && (p[1] == 'x' || p[1] == 'X')) {
                p += 2;
                while (p < end && (isxdigit(*p) || *p == '_')) {
                    if (*p != '_') d = d * 16 + (isdigit(*p) ? *p - '0' : (tolower(*p) - 'a' + 10));
                    p++;
                }
            } else {
                while (p < end && (isdigit(*p) || *p == '_')) { if (*p != '_') PUTB(*p); p++; }
                if (p + 1 < end && *p == '.' && isdigit(p[1])) {
                    PUTB('.'); p++;
                    while (p < end && (isdigit(*p) || *p == '_')) { if (*p != '_') PUTB(*p); p++; }
                }
                if (p < end && (*p == 'e' || *p == 'E')) {
                    const unsigned char *q = p + 1;
                    if (q < end && (*q == '+' || *q == '-')) q++;
                    if (q < end && isdigit(*q)) {
                        PUTB('e'); p++;
                        if (*p == '+' || *p == '-') PUTB(*p++);
                        while (p < end && isdigit(*p)) PUTB(*p++);
                    }
                }
                buf[bl] = 0; d = strtod(buf, NULL);
            }
#undef PUTB
            emit(tv, TK_NUM, L)->num = d;
            continue;
        }
        if (c == '"' || c == '\'') {
            int q = c, depth = 0;
            const unsigned char *s = ++p;
            while (p < end) {
                if (*p == '\\' && p + 1 < end) { if (p[1] == '\n') line++; p += 2; continue; }
                if (*p == q && depth == 0) break;
                if (*p == '\n') line++;
                if (q == '"') {
                    if (*p == '{') depth++;
                    else if (*p == '}' && depth > 0) depth--;
                    else if (*p == '"' && depth > 0) {
                        p++;
                        while (p < end && *p != '"') { if (*p == '\\') p++; else if (*p == '\n') line++; p++; }
                    }
                }
                p++;
            }
            if (p >= end)
                lex_error(L, depth > 0
                    ? L("testo non terminato: c'è una '{' aperta (per scrivere una graffa "
                        "nel testo usa \\{ oppure gli apici '...')",
                        "unterminated string: there is an open '{' (to write a brace "
                        "in a string use \\{ or single quotes '...')")
                    : L("testo non terminato: manca la virgoletta di chiusura",
                        "unterminated string: the closing quote is missing"));
            Token *t = emit(tv, TK_STR, L);
            t->raw = (const char *)s; t->rawlen = (int)(p - s); t->interp = q == '"';
            p++;
            continue;
        }
        if (is_id0(c)) {
            const unsigned char *s = p;
            while (p < end && is_id(*p)) p++;
            size_t n = p - s; Tk kw = TK_IDENT;
            for (size_t i = 0; i < sizeof KW / sizeof KW[0]; i++)
                if (strlen(KW[i].w) == n && memcmp(KW[i].w, s, n) == 0) { kw = KW[i].t; break; }
            Token *t = emit(tv, kw, L);
            if (kw == TK_IDENT) t->s = str_intern((const char *)s, n);
            continue;
        }
        if (c == '@') {
            p++;
            if (p < end && is_id0(*p)) {
                const unsigned char *s = p;
                while (p < end && is_id(*p)) p++;
                emit(tv, TK_SELFF, L)->s = str_intern((const char *)s, p - s);
            } else emit(tv, TK_AT, L);
            continue;
        }
        if (c == '`') {
            const unsigned char *s = ++p;
            while (p < end && is_id(*p)) p++;
            if (p >= end || *p != '`' || p == s) lex_error(L, L("infisso non valido: usa `nome`", "invalid infix: use `name`"));
            emit(tv, TK_INFIX, L)->s = str_intern((const char *)s, p - s);
            p++;
            continue;
        }
        int found = 0;
        for (size_t i = 0; i < sizeof OPS / sizeof OPS[0]; i++) {
            size_t n = strlen(OPS[i].s);
            if ((size_t)(end - p) >= n && memcmp(p, OPS[i].s, n) == 0) {
                Tk t = OPS[i].t;
                if (t == TK_LP || t == TK_LB || t == TK_LC) {
                    if (sd >= 255) lex_error(L, L("troppe parentesi annidate", "too many nested brackets"));
                    st[sd++] = (char)OPS[i].s[0];
                } else if (t == TK_RP || t == TK_RB || t == TK_RC) {
                    if (sd) sd--;
                    /* chiusura di un blocco: chiude anche l'ultima istruzione */
                }
                emit(tv, t, L);
                p += n; found = 1;
                break;
            }
        }
        if (!found) {
            char msg[64]; snprintf(msg, sizeof msg, L("carattere inatteso '%c'", "unexpected character '%c'"), c);
            lex_error(L, msg);
        }
    }
    emit(tv, TK_EOF, line);
}

/* ================================================================== */
/*  Parser                                                             */
/* ================================================================== */

typedef struct { Token *t; int n, i; } P;

static Tk pt(P *p) { return p->t[p->i].t; }
static Tk pt2(P *p, int k) { int j = p->i + k; return j < p->n ? p->t[j].t : TK_EOF; }
static Token *adv(P *p) { Token *t = &p->t[p->i]; if (t->t != TK_EOF) p->i++; return t; }
static int acc(P *p, Tk t) { if (pt(p) == t) { adv(p); return 1; } return 0; }

NORET static void syn(P *p, const char *what) {
    vm.line = p->t[p->i].line;
    rt_error(L("errore di sintassi: atteso %s, trovato '%s'", "syntax error: expected %s, found '%s'"), what, tkname(pt(p)));
}
static Token *expect(P *p, Tk t, const char *what) { if (pt(p) != t) syn(p, what); return adv(p); }

static const char *parse_file;   /* file in analisi: ogni nodo lo ricorda */

static Node *nn(NType t, int line) {
    Node *n = calloc(1, sizeof(Node));
    if (!n) { fputs("unico: memoria esaurita\n", stderr); exit(1); }
    n->t = t; n->line = line; n->file = parse_file;
    return n;
}

static int has_func(Node *n) {
    if (!n) return 0;
    if (n->t == N_FUNC) return 1;
    if (has_func(n->a) || has_func(n->b) || has_func(n->c)) return 1;
    for (int i = 0; i < n->nk; i++)
        if (has_func(n->kids[i]) || has_func(n->keys[i])) return 1;
    return 0;
}

static void free_node(Node *n) {
    if (!n) return;
    free_node(n->a); free_node(n->b); free_node(n->c);
    for (int i = 0; i < n->nk; i++) { free_node(n->kids[i]); free_node(n->keys[i]); }
    free(n->kids); free(n->keys); free(n->names); free(n);
}
static void addk(Node *n, Node *k, Node *key) {
    if (n->nk == n->ck) {
        n->ck = n->ck ? n->ck * 2 : 4;
        n->kids = xrealloc(n->kids, n->ck * sizeof(Node *));
        n->keys = xrealloc(n->keys, n->ck * sizeof(Node *));
    }
    n->kids[n->nk] = k; n->keys[n->nk] = key; n->nk++;
}
static void addname(Node *n, Str *s) {
    n->names = xrealloc(n->names, (n->nn + 1) * sizeof(Str *));
    n->names[n->nn++] = s;
}

static Node *parse_expr(P *p);
static Node *parse_block(P *p);
static Node *parse_postfix(P *p);
static Node *parse_range(P *p);
static Node *parse_program(P *p);
static Node *parse_or(P *p);
static Node *parse_spread_or_expr(P *p);

static Node *parse_string(Token *t) {
    Buf lit = { 0 }; Node *parts = NULL;
    const char *r = t->raw; int n = t->rawlen;
    for (int i = 0; i < n; i++) {
        char c = r[i];
        if (c == '\\' && i + 1 < n) {
            char e = r[++i];
            switch (e) {
            case 'n': buf_putc(&lit, '\n'); break;
            case 't': buf_putc(&lit, '\t'); break;
            case 'r': buf_putc(&lit, '\r'); break;
            case '0': buf_putc(&lit, '\0'); break;
            case '\n': break;
            case '\\': case '"': case '\'': case '{': case '}': buf_putc(&lit, e); break;
            default: buf_putc(&lit, '\\'); buf_putc(&lit, e);
            }
            continue;
        }
        if (c == '{' && t->interp) {
            int j = i + 1, depth = 1;
            while (j < n) {
                if (r[j] == '"') { j++; while (j < n && r[j] != '"') { if (r[j] == '\\') j++; j++; } }
                else if (r[j] == '{') depth++;
                else if (r[j] == '}' && --depth == 0) break;
                j++;
            }
            if (j >= n) {
                vm.line = t->line;
                rt_error(L("'{' senza '}' nel testo (per scrivere una graffa usa \\{ oppure gli apici '...')", "'{' without '}' in string (to write a brace use \\{ or single quotes '...')"));
            }
            if (!parts) parts = nn(N_INTERP, t->line);
            if (lit.len) {
                Node *s = nn(N_STR, t->line); s->str = str_intern(lit.s, lit.len);
                addk(parts, s, NULL); lit.len = 0;
            }
            TokVec tv = { 0 };
            lex(r + i + 1, j - i - 1, t->line, &tv);
            P sp = { tv.v, tv.n, 0 };
            while (pt(&sp) == TK_NL) adv(&sp);
            Node *e = parse_expr(&sp);
            while (pt(&sp) == TK_NL) adv(&sp);
            if (pt(&sp) != TK_EOF) syn(&sp, L("'}' a fine espressione", "'}' after the expression"));
            free(tv.v);
            addk(parts, e, NULL);
            i = j;
            continue;
        }
        buf_putc(&lit, c);
    }
    if (!parts) {
        Node *s = nn(N_STR, t->line);
        s->str = str_intern(lit.s ? lit.s : "", lit.len);
        free(lit.s);
        return s;
    }
    if (lit.len) {
        Node *s = nn(N_STR, t->line); s->str = str_intern(lit.s, lit.len);
        addk(parts, s, NULL);
    }
    free(lit.s);
    return parts;
}

/* Parametri: "a", "b = valore predefinito", "...resto" (solo per ultimo).
   I valori predefiniti stanno in f->kids, uno per parametro (o NULL);
   f->flag = 1 se l'ultimo parametro raccoglie gli argomenti in più. */
static void parse_params(P *p, Node *f, Tk close) {
    if (acc(p, close)) return;
    do {
        if (f->flag) syn(p, L("nient'altro dopo il parametro '...'", "nothing after the '...' parameter"));
        int rest = acc(p, TK_ELLIPSIS);
        addname(f, expect(p, TK_IDENT, L("nome di parametro", "a parameter name"))->s);
        Node *def = NULL;
        if (!rest && acc(p, TK_EQ)) def = parse_or(p);
        addk(f, def, NULL);
        if (rest) f->flag = 1;
    } while (acc(p, TK_COMMA));
    expect(p, close, close == TK_PIPE ? "'|'" : "')'");
}

static Node *parse_if(P *p, Token *t) {
    Node *n = nn(N_IF, t->line);
    n->a = parse_expr(p);
    n->b = parse_block(p);
    int save = p->i;
    while (pt(p) == TK_NL) adv(p);
    if (acc(p, TK_ELSE)) {
        if (pt(p) == TK_IF) { Token *t2 = adv(p); n->c = parse_if(p, t2); }
        else n->c = parse_block(p);
    } else p->i = save;
    return n;
}

static Node *parse_cell(P *p, Token *t) {
    Node *n = nn(N_CELL, t->line);
    while (pt(p) != TK_RB) {
        Node *key = NULL;
        if ((pt(p) == TK_IDENT || pt(p) == TK_STR || pt(p) == TK_NUM) && pt2(p, 1) == TK_COLON) {
            Token *k = adv(p); adv(p);
            if (k->t == TK_NUM) { key = nn(N_NUM, k->line); key->num = k->num; }
            else if (k->t == TK_IDENT) { key = nn(N_STR, k->line); key->str = k->s; }
            else {
                key = parse_string(k);
                if (key->t != N_STR) { vm.line = k->line; rt_error(L("una chiave non può contenere {interpolazioni}", "a key cannot contain {interpolations}")); }
            }
        }
        addk(n, key ? parse_expr(p) : parse_spread_or_expr(p), key);
        if (!acc(p, TK_COMMA)) break;
    }
    expect(p, TK_RB, "']'");
    return n;
}

/* Nei pattern, "_" accetta tutto e un nome nudo cattura il valore. */
static void mark_pattern(Node *pat) {
    if (pat->t == N_IDENT && !pat->flag) {
        if (pat->str->len == 1 && pat->str->chars[0] == '_') pat->t = N_WILD;
        else pat->flag = 2;
    } else if (pat->t == N_CELL) {
        for (int i = 0; i < pat->nk; i++) mark_pattern(pat->kids[i]);
    }
}

static Node *parse_match(P *p, Token *t) {
    Node *n = nn(N_MATCH, t->line);
    n->a = parse_expr(p);
    expect(p, TK_LC, "'{'");
    for (;;) {
        while (pt(p) == TK_NL || pt(p) == TK_SEMI || pt(p) == TK_COMMA) adv(p);
        if (acc(p, TK_RC)) break;
        if (pt(p) == TK_EOF) syn(p, "'}'");
        Node *arm = nn(N_ARM, p->t[p->i].line);
        do {
            Node *pat = parse_range(p);
            mark_pattern(pat);
            addk(arm, pat, NULL);
        } while (acc(p, TK_PIPE));
        if (acc(p, TK_IF)) arm->a = parse_expr(p);
        expect(p, TK_ARROW, "'=>'");
        arm->b = parse_expr(p);
        addk(n, arm, NULL);
    }
    return n;
}

static int ends_expr(Tk t) {
    return t == TK_NL || t == TK_SEMI || t == TK_RC || t == TK_RP || t == TK_RB || t == TK_EOF || t == TK_COMMA;
}

static Node *parse_primary(P *p) {
    Token *t = adv(p);
    Node *n;
    switch (t->t) {
    case TK_NUM: n = nn(N_NUM, t->line); n->num = t->num; return n;
    case TK_STR: return parse_string(t);
    case TK_IDENT: n = nn(N_IDENT, t->line); n->str = t->s; return n;
    case TK_CARET:
        n = nn(N_IDENT, t->line);
        n->str = expect(p, TK_IDENT, L("nome dopo '^'", "a name after '^'"))->s; n->flag = 1;
        return n;
    case TK_AT: return nn(N_SELF, t->line);
    case TK_SELFF:
        n = nn(N_FIELD, t->line); n->a = nn(N_SELF, t->line); n->str = t->s;
        return n;
    case TK_TRUE: return nn(N_TRUE, t->line);
    case TK_FALSE: return nn(N_FALSE, t->line);
    case TK_NIL: return nn(N_NIL, t->line);
    case TK_LP: n = parse_expr(p); expect(p, TK_RP, "')'"); return n;
    case TK_LB: return parse_cell(p, t);
    case TK_LC: p->i--; return parse_block(p);
    case TK_PIPE: case TK_OROR: {
        Node *f = nn(N_FUNC, t->line);
        if (t->t == TK_PIPE) parse_params(p, f, TK_PIPE);
        f->a = parse_expr(p);
        return f;
    }
    case TK_FN: {
        Node *f = nn(N_FUNC, t->line);
        Token *name = pt(p) == TK_IDENT ? adv(p) : NULL;
        expect(p, TK_LP, "'('");
        parse_params(p, f, TK_RP);
        f->a = acc(p, TK_EQ) ? parse_expr(p) : parse_block(p);
        if (!name) return f;
        f->str = name->s;
        Node *id = nn(N_IDENT, t->line); id->str = name->s;
        n = nn(N_ASSIGN, t->line); n->op = TK_EQ; n->a = id; n->b = f;
        return n;
    }
    case TK_IF: return parse_if(p, t);
    case TK_WHILE:
        n = nn(N_WHILE, t->line); n->a = parse_expr(p); n->b = parse_block(p);
        return n;
    case TK_FOR:
        n = nn(N_FOR, t->line);
        addname(n, expect(p, TK_IDENT, L("nome di variabile", "a variable name"))->s);
        if (acc(p, TK_COMMA)) addname(n, expect(p, TK_IDENT, L("nome di variabile", "a variable name"))->s);
        expect(p, TK_IN, "'in'");
        n->a = parse_expr(p);
        n->b = parse_block(p);
        n->flag = has_func(n->b);   /* serve un ambiente nuovo a ogni giro */
        return n;
    case TK_MATCH: return parse_match(p, t);
    case TK_RETURN:
        n = nn(N_RETURN, t->line);
        if (!ends_expr(pt(p))) n->a = parse_expr(p);
        return n;
    case TK_BREAK: return nn(N_BREAK, t->line);
    case TK_CONTINUE: return nn(N_CONTINUE, t->line);
    default:
        if (t->t != TK_EOF) p->i--;
        syn(p, L("un'espressione", "an expression"));
    }
}

/* "...espressione" dentro una chiamata o una cella: espande una cella. */
static Node *parse_spread_or_expr(P *p) {
    if (pt(p) != TK_ELLIPSIS) return parse_expr(p);
    Node *n = nn(N_SPREAD, adv(p)->line);
    n->a = parse_expr(p);
    return n;
}

static Node *parse_postfix(P *p) {
    Node *n = parse_primary(p);
    for (;;) {
        Tk t = pt(p);
        if (t == TK_LP) {
            Node *c = nn(N_CALL, adv(p)->line); c->a = n;
            while (pt(p) != TK_RP) {
                addk(c, parse_spread_or_expr(p), NULL);
                if (!acc(p, TK_COMMA)) break;
            }
            expect(p, TK_RP, "')'");
            n = c;
        } else if (t == TK_LB) {
            Node *x = nn(N_INDEX, adv(p)->line); x->a = n;
            x->b = parse_expr(p);
            expect(p, TK_RB, "']'");
            n = x;
        } else if (t == TK_DOT) {
            Node *x = nn(N_FIELD, adv(p)->line); x->a = n;
            x->str = expect(p, TK_IDENT, L("nome di campo dopo '.'", "a field name after '.'"))->s;
            n = x;
        } else break;
    }
    return n;
}

static Node *parse_unary(P *p) {
    Tk t = pt(p);
    if (t == TK_MINUS || t == TK_BANG || t == TK_NOT) {
        int line = adv(p)->line;
        Node *a = parse_unary(p);
        if (t == TK_MINUS && a->t == N_NUM) { a->num = -a->num; return a; }
        Node *n = nn(N_UNARY, line); n->op = t == TK_MINUS ? '-' : '!'; n->a = a;
        return n;
    }
    return parse_postfix(p);
}

static Node *bin(NType nt, int op, Node *l, Node *r, int line) {
    Node *n = nn(nt, line); n->op = op; n->a = l; n->b = r;
    return n;
}

static Node *parse_mul(P *p) {
    Node *l = parse_unary(p);
    while (pt(p) == TK_STAR || pt(p) == TK_SLASH || pt(p) == TK_PCT) {
        Token *t = adv(p); l = bin(N_BINARY, t->t, l, parse_unary(p), t->line);
    }
    return l;
}
static Node *parse_add(P *p) {
    Node *l = parse_mul(p);
    while (pt(p) == TK_PLUS || pt(p) == TK_MINUS) {
        Token *t = adv(p); l = bin(N_BINARY, t->t, l, parse_mul(p), t->line);
    }
    return l;
}
static Node *parse_range(P *p) {
    Node *l = parse_add(p);
    if (pt(p) == TK_DOTDOT || pt(p) == TK_DOTDOTLT) {
        Token *t = adv(p); l = bin(N_RANGE, t->t, l, parse_add(p), t->line);
    }
    return l;
}
static Node *parse_infix(P *p) {
    Node *l = parse_range(p);
    while (pt(p) == TK_INFIX) {
        Token *t = adv(p);
        Node *c = nn(N_CALL, t->line), *f = nn(N_IDENT, t->line);
        f->str = t->s; c->a = f;
        addk(c, l, NULL); addk(c, parse_range(p), NULL);
        l = c;
    }
    return l;
}
static Node *parse_cmp(P *p) {
    Node *l = parse_infix(p);
    for (;;) {
        Tk t = pt(p);
        if (t == TK_EQEQ || t == TK_NE || t == TK_LT || t == TK_LE || t == TK_GT || t == TK_GE || t == TK_IN) {
            Token *k = adv(p); l = bin(N_BINARY, t, l, parse_infix(p), k->line);
        } else if (t == TK_NOT && pt2(p, 1) == TK_IN) {
            int line = adv(p)->line; adv(p);
            Node *u = nn(N_UNARY, line); u->op = '!';
            u->a = bin(N_BINARY, TK_IN, l, parse_infix(p), line);
            l = u;
        } else break;
    }
    return l;
}
static Node *parse_and(P *p) {
    Node *l = parse_cmp(p);
    while (pt(p) == TK_AND || pt(p) == TK_ANDAND) { Token *t = adv(p); l = bin(N_AND, 0, l, parse_cmp(p), t->line); }
    return l;
}
static Node *parse_or(P *p) {
    Node *l = parse_and(p);
    while (pt(p) == TK_OR || pt(p) == TK_OROR) { Token *t = adv(p); l = bin(N_OR, 0, l, parse_and(p), t->line); }
    return l;
}
static Node *parse_coal(P *p) {
    Node *l = parse_or(p);
    while (pt(p) == TK_QQ) { Token *t = adv(p); l = bin(N_COALESCE, 0, l, parse_or(p), t->line); }
    return l;
}
static Node *parse_pipe(P *p) {
    Node *l = parse_coal(p);
    while (pt(p) == TK_PIPEGT) {
        Token *t = adv(p);
        Node *r = parse_postfix(p);
        if (r->t == N_CALL) {
            addk(r, NULL, NULL);
            memmove(r->kids + 1, r->kids, (r->nk - 1) * sizeof(Node *));
            r->kids[0] = l;
        } else {
            Node *c = nn(N_CALL, t->line); c->a = r; addk(c, l, NULL);
            r = c;
        }
        l = r;
    }
    return l;
}
static Node *parse_expr(P *p) {
    Node *l = parse_pipe(p);
    Tk t = pt(p);
    if (t == TK_EQ || t == TK_PLUSEQ || t == TK_MINUSEQ || t == TK_STAREQ || t == TK_SLASHEQ) {
        if (l->t != N_IDENT && l->t != N_INDEX && l->t != N_FIELD) syn(p, L("un nome, un indice o un campo prima di '='", "a name, an index or a field before '='"));
        Token *k = adv(p);
        return bin(N_ASSIGN, t, l, parse_expr(p), k->line);
    }
    return l;
}

static void parse_stmts(P *p, Node *blk, Tk end) {
    for (;;) {
        while (pt(p) == TK_NL || pt(p) == TK_SEMI) adv(p);
        if (pt(p) == end) break;
        if (pt(p) == TK_EOF) syn(p, end == TK_RC ? "'}'" : L("fine del file", "end of file"));
        addk(blk, parse_expr(p), NULL);
        Tk t = pt(p);
        if (t != TK_NL && t != TK_SEMI && t != end) syn(p, L("fine dell'istruzione", "end of statement"));
    }
}
static Node *parse_block(P *p) {
    Node *b = nn(N_BLOCK, expect(p, TK_LC, "'{'")->line);
    parse_stmts(p, b, TK_RC);
    adv(p);
    return b;
}
static Node *parse_program(P *p) {
    Node *b = nn(N_BLOCK, 1);
    parse_stmts(p, b, TK_EOF);
    return b;
}

/* ================================================================== */
/*  Valutatore                                                         */
/* ================================================================== */

static void range_eval(Node *r, Env *e, double *a, double *b) {
    Value x = eval(r->a, e), y = eval(r->b, e);
    if (x.t != T_NUM || y.t != T_NUM) rt_error(L("gli estremi di un intervallo devono essere numeri", "range bounds must be numbers"));
    *a = x.u.n; *b = y.u.n;
}
static int in_range(double x, double a, double b, int excl) {
    if (a <= b) return x >= a && (excl ? x < b : x <= b);
    return x <= a && (excl ? x > b : x >= b);
}

/* Esegue il corpo di un ciclo: 0 = prosegui, 1 = basta, 2 = ritorna. */
static int run_body(Node *body, Env *e) {
    MAYBE_GC();
    eval(body, e);
    if (!vm.unwind) return 0;
    if (vm.unwind == U_BREAK) { vm.unwind = U_NONE; return 1; }
    if (vm.unwind == U_CONTINUE) { vm.unwind = U_NONE; return 0; }
    return 2;
}

/* Se il corpo crea funzioni, ogni giro ha un ambiente proprio: così ogni
   closure ricorda il valore della variabile di quel giro. Altrimenti le
   variabili del ciclo stanno nell'ambiente della funzione (più veloce). */
static Value eval_for(Node *n, Env *e) {
    Str *k = n->names[0], *v2 = n->nn > 1 ? n->names[1] : NULL;
    int s, fresh = n->flag, slot = -1;
    Env *le = e;
#define BIND(key, val) do { \
        if (fresh) { \
            le = env_new(e); le->loop = 1; \
            if (slot < 0) { \
                if (vm.esp >= UC_DEPTH_MAX + 60) rt_error(L("cicli annidati troppo profondi", "loops nested too deeply")); \
                slot = vm.esp++; \
            } \
            vm.envs[slot] = le; \
        } \
        if (v2) { setv(le, k, (key)); setv(le, v2, (val)); } else setv(le, k, (val)); \
    } while (0)
#define STEP() do { s = run_body(n->b, le); if (s) goto done; } while (0)
    int base = vm.sp;
    if (n->a->t == N_RANGE) {
        double a, b; range_eval(n->a, e, &a, &b);
        int ex = n->a->op == TK_DOTDOTLT; double st = a <= b ? 1 : -1; long cnt = 0;
        for (double i = a; st > 0 ? (ex ? i < b : i <= b) : (ex ? i > b : i >= b); i += st) {
            BIND(vnum(cnt), vnum(i)); cnt++;
            STEP();
        }
        goto done;
    }
    Value it = eval(n->a, e);
    if (vm.unwind) return it;
    push(it);
    switch (it.t) {
    case T_NIL: break;
    case T_NUM:
        for (double i = 0; i < it.u.n; i++) { BIND(vnum(i), vnum(i)); STEP(); }
        break;
    case T_CELL: {
        Cell *c = AS_CELL(it);
        for (int i = 0; i < c->alen; i++) { BIND(vnum(i), c->arr[i]); STEP(); }
        for (int i = 0; i < c->elen; i++) { BIND(c->ent[i].key, c->ent[i].val); STEP(); }
        break;
    }
    case T_STR: {
        Str *str = AS_STR(it); long idx = 0;
        for (size_t i = 0; i < str->len; idx++) {
            size_t l = u8len((unsigned char)str->chars[i]);
            if (i + l > str->len) l = str->len - i;
            BIND(vnum(idx), VSTR(str_new(str->chars + i, l)));
            i += l;
            STEP();
        }
        break;
    }
    case T_FUNC: case T_NATIVE:
        /* iteratore: chiama la funzione finché restituisce nulla */
        for (long idx = 0;; idx++) {
            Value v = call_value(it, 0, NULL, VNIL, 0);
            if (v.t == T_NIL) break;
            BIND(vnum(idx), v);
            STEP();
        }
        break;
    default:
        rt_error(L("impossibile iterare su un valore di tipo %s", "cannot iterate over a value of type %s"), tname(it));
    }
done:
    vm.sp = base;
    if (slot >= 0) vm.esp = slot;
    return VNIL;
#undef BIND
#undef STEP
}

static Value eval_call(Node *n, Env *e) {
    int base = vm.sp, argstart, has_self = 0;
    Value fn, self = VNIL;
    Node *cal = n->a;
    if (cal->t == N_FIELD) {
        Value obj = eval(cal->a, e);
        if (vm.unwind) return obj;
        push(obj);
        Value m = obj.t == T_CELL ? cell_get(AS_CELL(obj), VSTR(cal->str)) : VNIL;
        if (m.t != T_NIL) { fn = m; self = obj; has_self = 1; }
        else if (!env_lookup(e, cal->str, &fn)) {
            vm.line = n->line;
            rt_error(L("'%s' non è né un campo di %s né una funzione", "'%s' is neither a field of %s nor a function"), cal->str->chars, tname(obj));
        }
        vm.sp = base;
        push(fn); push(obj);
        /* metodo: obj è il "@"; altrimenti è il primo argomento (chiamata universale) */
        argstart = has_self ? vm.sp : vm.sp - 1;
    } else {
        fn = eval(cal, e);
        if (vm.unwind) return fn;
        push(fn);
        argstart = vm.sp;
    }
    for (int i = 0; i < n->nk; i++) {
        Node *k = n->kids[i];
        Value v = eval(k->t == N_SPREAD ? k->a : k, e);
        if (vm.unwind) { vm.sp = base; return v; }
        if (k->t == N_SPREAD) {
            if (v.t == T_NIL) continue;
            if (v.t != T_CELL) rt_error(L("'...' si usa solo con una cella, non %s", "'...' only works on a cell, not %s"), tname(v));
            Cell *c = AS_CELL(v);
            for (int j = 0; j < c->alen; j++) push(c->arr[j]);
        } else push(v);
    }
    vm.line = n->line;
    Value r = call_value(fn, vm.sp - argstart, &vm.stack[argstart], self, has_self);
    vm.sp = base;
    return r;
}

static Value eval_assign(Node *n, Env *e) {
    Node *t = n->a; int op = n->op;
    int bop = op == TK_PLUSEQ ? TK_PLUS : op == TK_MINUSEQ ? TK_MINUS :
              op == TK_STAREQ ? TK_STAR : op == TK_SLASHEQ ? TK_SLASH : 0;
    if (t->t == N_IDENT) {
        Env *target = e;
        Value dummy;
        /* le variabili nuove vanno nella funzione, non nel singolo giro del ciclo */
        while (target->loop && !cell_get_key(target->vars, VSTR(t->str), &dummy)) target = target->parent;
        if (t->flag == 1) {
            target = env_owner(e->parent, t->str);
            if (!target) rt_error(L("nessuna variabile esterna chiamata '%s'", "no outer variable named '%s'"), t->str->chars);
        }
        Value v = eval(n->b, e);
        if (vm.unwind) return v;
        if (bop) {
            Value cur;
            if (!env_lookup(target, t->str, &cur)) rt_error(L("nome non definito: '%s'", "undefined name: '%s'"), t->str->chars);
            v = binop(bop, cur, v);
        }
        setv(target, t->str, v);
        return v;
    }
    int base = vm.sp;
    Value obj = eval(t->a, e);
    if (vm.unwind) return obj;
    push(obj);
    Value key;
    if (t->t == N_FIELD) key = VSTR(t->str);
    else {
        key = eval(t->b, e);
        if (vm.unwind) { vm.sp = base; return key; }
    }
    push(key);
    Value v = eval(n->b, e);
    if (vm.unwind) { vm.sp = base; return v; }
    vm.line = n->line;
    if (bop) v = binop(bop, index_get(obj, key), v);
    index_set(obj, key, v);
    vm.sp = base;
    return v;
}

static int match_pat(Node *pat, Value v, Env *e) {
    switch (pat->t) {
    case N_WILD: return 1;
    case N_IDENT:
        if (pat->flag == 2) { setv(e, pat->str, v); return 1; }
        break;
    case N_RANGE: {
        double a, b; range_eval(pat, e, &a, &b);
        return v.t == T_NUM && in_range(v.u.n, a, b, pat->op == TK_DOTDOTLT);
    }
    case N_CELL: {
        /* destrutturazione: [a, b, chiave: c] */
        if (v.t != T_CELL) return 0;
        Cell *c = AS_CELL(v);
        int pos = 0, keyed = 0;
        for (int i = 0; i < pat->nk; i++) {
            Node *k = pat->keys[i];
            Value sub;
            if (k) {
                keyed = 1;
                Value kv = k->t == N_NUM ? vnum(k->num) : VSTR(k->str);
                if (!key_ok(kv) || cell_find(c, kv) < 0) return 0;
                sub = cell_get(c, kv);
            } else {
                if (pos >= c->alen) return 0;
                sub = c->arr[pos++];
            }
            if (!match_pat(pat->kids[i], sub, e) || vm.unwind) return 0;
        }
        /* senza chiavi nel pattern, la cella deve essere una lista pura */
        return pos == c->alen && (keyed || c->elen == 0);
    }
    default: break;
    }
    Value x = eval(pat, e);
    return !vm.unwind && val_eq(v, x, 0);
}

static Value eval_match(Node *n, Env *e) {
    Value s = eval(n->a, e);
    if (vm.unwind) return s;
    int base = vm.sp;
    push(s);
    for (int i = 0; i < n->nk; i++) {
        Node *arm = n->kids[i];
        int ok = 0;
        for (int j = 0; j < arm->nk && !ok; j++) {
            ok = match_pat(arm->kids[j], s, e);
            if (vm.unwind) { vm.sp = base; return VNIL; }
        }
        if (ok && arm->a) {
            ok = truthy(eval(arm->a, e));
            if (vm.unwind) { vm.sp = base; return VNIL; }
        }
        if (ok) { vm.sp = base; return eval(arm->b, e); }
    }
    vm.sp = base;
    return VNIL;
}

static Value eval(Node *n, Env *e) {
    vm.line = n->line; vm.file = n->file;
    switch (n->t) {
    case N_NUM: return vnum(n->num);
    case N_STR: return VSTR(n->str);
    case N_TRUE: return vbool(1);
    case N_FALSE: return vbool(0);
    case N_NIL: return VNIL;
    case N_INTERP: {
        Buf b = { 0 };
        for (int i = 0; i < n->nk; i++) {
            Node *k = n->kids[i];
            if (k->t == N_STR) buf_put(&b, k->str->chars, k->str->len);
            else {
                Value v = eval(k, e);
                if (vm.unwind) { free(b.s); return v; }
                to_buf(&b, v, 0, 0);
            }
        }
        return vbuf(&b);
    }
    case N_IDENT: {
        Value v;
        if (!env_lookup(n->flag == 1 ? e->parent : e, n->str, &v))
            rt_error(L("nome non definito: '%s'", "undefined name: '%s'"), n->str->chars);
        return v;
    }
    case N_SELF: {
        Value v;
        return env_lookup(e, vm.s_self, &v) ? v : VNIL;
    }
    case N_CELL: {
        Cell *c = cell_new();
        int base = vm.sp;
        push(VCELL(c));
        for (int i = 0; i < n->nk; i++) {
            Node *x = n->kids[i];
            Value v = eval(x->t == N_SPREAD ? x->a : x, e);
            if (vm.unwind) { vm.sp = base; return v; }
            Node *k = n->keys[i];
            if (x->t == N_SPREAD) {
                if (v.t == T_NIL) continue;
                if (v.t != T_CELL) rt_error(L("'...' si usa solo con una cella, non %s", "'...' only works on a cell, not %s"), tname(v));
                Cell *s = AS_CELL(v);
                for (int j = 0; j < s->alen; j++) cell_push(c, s->arr[j]);
                for (int j = 0; j < s->elen; j++) cell_set_key(c, s->ent[j].key, s->ent[j].val);
            } else if (k) cell_set(c, k->t == N_NUM ? vnum(k->num) : VSTR(k->str), v);
            else cell_push(c, v);
        }
        vm.sp = base;
        return VCELL(c);
    }
    case N_BLOCK: return eval_block(n, e);
    case N_UNARY: {
        Value v = eval(n->a, e);
        if (vm.unwind) return v;
        if (n->op == '-') {
            if (v.t != T_NUM) rt_error(L("'-' richiede un numero, non %s", "'-' needs a number, not %s"), tname(v));
            return vnum(-v.u.n);
        }
        return vbool(!truthy(v));
    }
    case N_BINARY: {
        if (n->op == TK_IN && n->b->t == N_RANGE) {
            Value x = eval(n->a, e);
            if (vm.unwind) return x;
            double a, b; range_eval(n->b, e, &a, &b);
            return vbool(x.t == T_NUM && in_range(x.u.n, a, b, n->b->op == TK_DOTDOTLT));
        }
        Value a = eval(n->a, e);
        if (vm.unwind) return a;
        int base = vm.sp;
        push(a);
        Value b = eval(n->b, e);
        vm.sp = base;
        if (vm.unwind) return b;
        vm.line = n->line;
        return binop(n->op, a, b);
    }
    case N_AND: { Value a = eval(n->a, e); if (vm.unwind || !truthy(a)) return a; return eval(n->b, e); }
    case N_OR: { Value a = eval(n->a, e); if (vm.unwind || truthy(a)) return a; return eval(n->b, e); }
    case N_COALESCE: { Value a = eval(n->a, e); if (vm.unwind || a.t != T_NIL) return a; return eval(n->b, e); }
    case N_ASSIGN: return eval_assign(n, e);
    case N_CALL: return eval_call(n, e);
    case N_INDEX: {
        Value o = eval(n->a, e);
        if (vm.unwind) return o;
        int base = vm.sp;
        push(o);
        Value k = eval(n->b, e);
        vm.sp = base;
        if (vm.unwind) return k;
        vm.line = n->line;
        return index_get(o, k);
    }
    case N_FIELD: {
        Value o = eval(n->a, e);
        if (vm.unwind) return o;
        if (o.t == T_CELL) return cell_get(AS_CELL(o), VSTR(n->str));
        rt_error(L("impossibile leggere il campo '%s' da un valore di tipo %s", "cannot read field '%s' of a value of type %s"), n->str->chars, tname(o));
    }
    case N_FUNC: {
        Func *f = gc_alloc(sizeof(Func), O_FUNC);
        f->node = n; f->env = e;
        return vobj(T_FUNC, f);
    }
    case N_IF: {
        Value c = eval(n->a, e);
        if (vm.unwind) return c;
        if (truthy(c)) return eval(n->b, e);
        return n->c ? eval(n->c, e) : VNIL;
    }
    case N_WHILE:
        for (;;) {
            MAYBE_GC();
            Value c = eval(n->a, e);
            if (vm.unwind) return c;
            if (!truthy(c)) break;
            int s = run_body(n->b, e);
            if (s) break;
        }
        return VNIL;
    case N_FOR: return eval_for(n, e);
    case N_RANGE: {
        double a, b; range_eval(n, e, &a, &b);
        int ex = n->op == TK_DOTDOTLT; double st = a <= b ? 1 : -1;
        if (fabs(b - a) > 1e8) rt_error(L("intervallo troppo grande", "range too large"));
        Cell *c = cell_new();
        for (double i = a; st > 0 ? (ex ? i < b : i <= b) : (ex ? i > b : i >= b); i += st) cell_push(c, vnum(i));
        return VCELL(c);
    }
    case N_MATCH: return eval_match(n, e);
    case N_RETURN: {
        Value v = n->a ? eval(n->a, e) : VNIL;
        if (vm.unwind) return v;
        vm.ret = v; vm.unwind = U_RETURN;
        return v;
    }
    case N_BREAK: vm.unwind = U_BREAK; return VNIL;
    case N_CONTINUE: vm.unwind = U_CONTINUE; return VNIL;
    default: rt_error(L("nodo inatteso", "unexpected node"));
    }
}

static Value eval_block(Node *b, Env *e) {
    Value last = VNIL;
    for (int i = 0; i < b->nk; i++) {
        MAYBE_GC();
        last = eval(b->kids[i], e);
        if (vm.unwind) return last;
    }
    return last;
}

static Value call_value(Value f, int argc, Value *argv, Value self, int has_self) {
    if (f.t == T_NATIVE) return AS_NAT(f)->fn(argc, argv);
    if (f.t != T_FUNC) rt_error(L("impossibile chiamare un valore di tipo %s", "cannot call a value of type %s"), tname(f));
    Func *fn = AS_FUNC(f);
    Node *d = fn->node;
    if (vm.depth >= UC_DEPTH_MAX) rt_error(L("ricorsione troppo profonda", "recursion too deep"));
    Env *ne = env_new(fn->env);
    vm.frames[vm.depth].name = d->str ? d->str->chars : L("<funzione anonima>", "<anonymous function>");
    vm.frames[vm.depth].file = vm.file;
    vm.frames[vm.depth].line = vm.line;
    vm.envs[vm.esp++] = ne; vm.depth++;
    if (has_self) setv(ne, vm.s_self, self);
    int fixed = d->flag ? d->nn - 1 : d->nn;
    for (int i = 0; i < fixed; i++) setv(ne, d->names[i], i < argc ? argv[i] : VNIL);
    if (d->flag) {
        Cell *rest = cell_new();
        for (int i = fixed; i < argc; i++) cell_push(rest, argv[i]);
        setv(ne, d->names[fixed], VCELL(rest));
    }
    /* valori predefiniti: valutati nella chiamata, per gli argomenti mancanti
       o nulli; possono usare i parametri precedenti */
    for (int i = 0; i < fixed; i++) {
        if (!d->kids[i] || (i < argc && argv[i].t != T_NIL)) continue;
        Value dv = eval(d->kids[i], ne);
        setv(ne, d->names[i], dv);
    }
    int line = vm.line; const char *file = vm.file;
    Value r = d->a->t == N_BLOCK ? eval_block(d->a, ne) : eval(d->a, ne);
    if (vm.unwind == U_RETURN) r = vm.ret;
    vm.unwind = U_NONE; vm.ret = VNIL;
    vm.esp--; vm.depth--; vm.line = line; vm.file = file;
    return r;
}

/* Analizza ed esegue del codice in un ambiente. Gli errori risalgono. */
static Value exec_src(const char *src, size_t len, Env *env, const char *file) {
    const char *pf = parse_file, *of = vm.file;
    int ol = vm.line;
    parse_file = vm.file = file;
    TokVec tv = { 0 };
    lex(src, len, 1, &tv);
    P p = { tv.v, tv.n, 0 };
    Node *prog = parse_program(&p);
    free(tv.v);
    parse_file = pf;
    Value v = eval_block(prog, env);
    if (vm.unwind == U_RETURN) v = vm.ret;
    vm.unwind = U_NONE; vm.ret = VNIL;
    /* senza funzioni nessuno può riferirsi all'albero: lo si libera subito
       (così esegui() in un ciclo non consuma memoria) */
    if (!has_func(prog)) free_node(prog);
    vm.file = of; vm.line = ol;
    return v;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = xrealloc(NULL, n);
    memcpy(d, s, n);
    return d;
}

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    Buf b = { 0 }; char tmp[8192]; size_t k;
    while ((k = fread(tmp, 1, sizeof tmp, f)) > 0) buf_put(&b, tmp, k);
    fclose(f);
    if (!b.s) b.s = calloc(1, 1);
    *n = b.len;
    return b.s;
}

/* ================================================================== */
/*  Funzioni native                                                    */
/* ================================================================== */

#define NAT(name) static Value name(int argc, Value *argv)
static void deny(const char *fn);
#define ARG(i) ((i) < argc ? argv[i] : VNIL)

static double need_num(int argc, Value *argv, int i, const char *fn) {
    Value v = ARG(i);
    if (v.t != T_NUM) rt_error(L("%s: l'argomento %d deve essere un numero, non %s", "%s: argument %d must be a number, not %s"), fn, i + 1, tname(v));
    return v.u.n;
}
static Str *need_str(int argc, Value *argv, int i, const char *fn) {
    Value v = ARG(i);
    if (v.t != T_STR) rt_error(L("%s: l'argomento %d deve essere un testo, non %s", "%s: argument %d must be a string, not %s"), fn, i + 1, tname(v));
    return AS_STR(v);
}
static Cell *need_cell(int argc, Value *argv, int i, const char *fn) {
    Value v = ARG(i);
    if (v.t != T_CELL) rt_error(L("%s: l'argomento %d deve essere una cella, non %s", "%s: argument %d must be a cell, not %s"), fn, i + 1, tname(v));
    return AS_CELL(v);
}

static void out_args(int argc, Value *argv, int nl) {
    Buf b = { 0 };
    for (int i = 0; i < argc; i++) { if (i && nl) buf_putc(&b, ' '); to_buf(&b, argv[i], 0, 0); }
    if (nl) buf_putc(&b, '\n');
    if (b.len) fwrite(b.s, 1, b.len, stdout);
    free(b.s);
}
NAT(n_print) { out_args(argc, argv, 1); return VNIL; }
NAT(n_write) { out_args(argc, argv, 0); fflush(stdout); return VNIL; }
NAT(n_input) {
    if (argc) { out_args(argc, argv, 0); fflush(stdout); }
    Buf b = { 0 }; int c;
    while ((c = getchar()) != EOF && c != '\n') buf_putc(&b, (char)c);
    if (c == EOF && !b.len) return VNIL;
    if (b.len && b.s[b.len - 1] == '\r') b.s[--b.len] = 0;
    return vbuf(&b);
}
NAT(n_str) { return argc ? tostr(argv[0]) : vstr(""); }
NAT(n_num) {
    Value v = ARG(0);
    if (v.t == T_NUM) return v;
    if (v.t == T_BOOL) return vnum(v.u.b);
    if (v.t != T_STR) return VNIL;
    const char *s = AS_STR(v)->chars; char *end;
    while (isspace((unsigned char)*s)) s++;
    if (!*s) return VNIL;
    double d = strtod(s, &end);
    while (isspace((unsigned char)*end)) end++;
    return *end ? VNIL : vnum(d);
}
NAT(n_type) { return vstr(tname_in(ARG(0), LANG_EN)); }
NAT(n_tipo) { return vstr(tname_in(ARG(0), LANG_IT)); }

/* Sceglie la lingua da un nome: "it", "italiano", "it_IT.UTF-8"... */
static int lang_from(const char *s) {
    if (!s || !*s) return -1;
    if (!strncmp(s, "it", 2)) return LANG_IT;
    if (!strncmp(s, "en", 2) || !strcmp(s, "C") || !strcmp(s, "POSIX")) return LANG_EN;
    return -2;   /* lingua non disponibile */
}
/* lingua() restituisce la lingua attuale; lingua("en") la cambia. */
NAT(n_lang) {
    if (argc && argv[0].t != T_NIL) {
        int l = lang_from(need_str(argc, argv, 0, "language")->chars);
        if (l < 0) rt_error(L("lingua non disponibile: '%s' (disponibili: it, en)",
                              "language not available: '%s' (available: it, en)"), AS_STR(argv[0])->chars);
        vm.lang = l;
    }
    return vstr(vm.lang == LANG_EN ? "en" : "it");
}
NAT(n_len) {
    Value v = ARG(0);
    if (v.t == T_STR) return vnum((double)u8count(AS_STR(v)));
    if (v.t == T_CELL) return vnum(AS_CELL(v)->alen + AS_CELL(v)->elen);
    if (v.t == T_NIL) return vnum(0);
    rt_error(L("len: %s non ha una lunghezza", "len: %s has no length"), tname(v));
}
NAT(n_push) {
    Cell *c = need_cell(argc, argv, 0, "push");
    for (int i = 1; i < argc; i++) cell_push(c, argv[i]);
    return argv[0];
}
NAT(n_pop) {
    Cell *c = need_cell(argc, argv, 0, "pop");
    return c->alen ? c->arr[--c->alen] : VNIL;
}
NAT(n_remove) {
    Cell *c = need_cell(argc, argv, 0, "remove");
    Value k = ARG(1); long i;
    if (k.t == T_NUM && num_index(k.u.n, &i)) {
        if (i < 0) i += c->alen;
        if (i >= 0 && i < c->alen) {
            Value v = c->arr[i];
            memmove(c->arr + i, c->arr + i + 1, (c->alen - i - 1) * sizeof(Value));
            c->alen--;
            return v;
        }
    }
    if (!key_ok(k)) return VNIL;
    int f = cell_find(c, k);
    if (f < 0) return VNIL;
    Value v = c->ent[f].val;
    cell_del_key(c, f);
    return v;
}
NAT(n_keys) {
    Cell *c = need_cell(argc, argv, 0, "keys"), *r = cell_new();
    for (int i = 0; i < c->alen; i++) cell_push(r, vnum(i));
    for (int i = 0; i < c->elen; i++) cell_push(r, c->ent[i].key);
    return VCELL(r);
}
NAT(n_values) {
    Cell *c = need_cell(argc, argv, 0, "values"), *r = cell_new();
    for (int i = 0; i < c->alen; i++) cell_push(r, c->arr[i]);
    for (int i = 0; i < c->elen; i++) cell_push(r, c->ent[i].val);
    return VCELL(r);
}
NAT(n_has) {
    Value o = ARG(0), k = ARG(1); long i;
    if (o.t == T_STR) return vbool(contains(o, k));
    Cell *c = need_cell(argc, argv, 0, "has");
    if (k.t == T_NUM && num_index(k.u.n, &i)) {
        if (i < 0) i += c->alen;
        if (i >= 0 && i < c->alen) return vbool(1);
    }
    return vbool(key_ok(k) && cell_find(c, k) >= 0);
}
static void slice_bounds(int argc, Value *argv, long n, long *a, long *b) {
    *a = ARG(1).t == T_NUM ? (long)ARG(1).u.n : 0;
    *b = ARG(2).t == T_NUM ? (long)ARG(2).u.n : n;
    if (*a < 0) *a += n;
    if (*b < 0) *b += n;
    if (*a < 0) *a = 0;
    if (*b > n) *b = n;
    if (*a > *b) *a = *b;
}
NAT(n_slice) {
    Value v = ARG(0); long a, b;
    if (v.t == T_STR) {
        Str *s = AS_STR(v);
        slice_bounds(argc, argv, u8count(s), &a, &b);
        size_t oa = u8off(s, a), ob = u8off(s, b);
        return VSTR(str_new(s->chars + oa, ob - oa));
    }
    Cell *c = need_cell(argc, argv, 0, "slice"), *r = cell_new();
    slice_bounds(argc, argv, c->alen, &a, &b);
    for (long i = a; i < b; i++) cell_push(r, c->arr[i]);
    return VCELL(r);
}
NAT(n_join) {
    Cell *c = need_cell(argc, argv, 0, "join");
    Str *sep = argc > 1 ? need_str(argc, argv, 1, "join") : NULL;
    Buf b = { 0 };
    for (int i = 0; i < c->alen; i++) {
        if (i && sep) buf_put(&b, sep->chars, sep->len);
        to_buf(&b, c->arr[i], 0, 0);
    }
    return vbuf(&b);
}
NAT(n_split) {
    Str *s = need_str(argc, argv, 0, "split");
    Cell *r = cell_new();
    const char *p = s->chars, *end = p + s->len;
    if (ARG(1).t == T_NIL) {
        while (p < end) {
            while (p < end && isspace((unsigned char)*p)) p++;
            const char *q = p;
            while (q < end && !isspace((unsigned char)*q)) q++;
            if (q > p) cell_push(r, VSTR(str_new(p, q - p)));
            p = q;
        }
        return VCELL(r);
    }
    Str *sep = need_str(argc, argv, 1, "split");
    if (!sep->len) {
        while (p < end) {
            size_t l = u8len((unsigned char)*p);
            if (p + l > end) l = end - p;
            cell_push(r, VSTR(str_new(p, l)));
            p += l;
        }
        return VCELL(r);
    }
    for (;;) {
        const char *q = mfind(p, end - p, sep->chars, sep->len);
        if (!q) { cell_push(r, VSTR(str_new(p, end - p))); break; }
        cell_push(r, VSTR(str_new(p, q - p)));
        p = q + sep->len;
    }
    return VCELL(r);
}
static Value map_ascii(Str *s, int up) {
    Str *r = str_new(s->chars, s->len);
    unsigned char *c = (unsigned char *)r->chars;
    for (size_t i = 0; i < r->len; i++) {
        if (c[i] == 0xC3 && i + 1 < r->len) {
            /* lettere accentate latine (à è é ì ò ù ...) in UTF-8 */
            unsigned char d = c[++i];
            if (up && d >= 0xA0 && d <= 0xBE && d != 0xB7) c[i] = d - 0x20;
            else if (!up && d >= 0x80 && d <= 0x9E && d != 0x97) c[i] = d + 0x20;
        } else if (c[i] < 0x80) c[i] = (unsigned char)(up ? toupper(c[i]) : tolower(c[i]));
    }
    r->hash = hash_bytes(r->chars, r->len);
    return VSTR(r);
}
NAT(n_upper) { return map_ascii(need_str(argc, argv, 0, "upper"), 1); }
NAT(n_lower) { return map_ascii(need_str(argc, argv, 0, "lower"), 0); }
NAT(n_trim) {
    Str *s = need_str(argc, argv, 0, "trim");
    const char *a = s->chars, *b = s->chars + s->len;
    while (a < b && isspace((unsigned char)*a)) a++;
    while (b > a && isspace((unsigned char)b[-1])) b--;
    return VSTR(str_new(a, b - a));
}
NAT(n_replace) {
    Str *s = need_str(argc, argv, 0, "replace"), *x = need_str(argc, argv, 1, "replace"), *y = need_str(argc, argv, 2, "replace");
    if (!x->len) return argv[0];
    Buf b = { 0 };
    const char *p = s->chars, *end = p + s->len;
    for (;;) {
        const char *q = mfind(p, end - p, x->chars, x->len);
        if (!q) { buf_put(&b, p, end - p); break; }
        buf_put(&b, p, q - p); buf_put(&b, y->chars, y->len);
        p = q + x->len;
    }
    return vbuf(&b);
}
NAT(n_find) {
    Value o = ARG(0), x = ARG(1);
    if (o.t == T_STR) {
        Str *s = AS_STR(o), *n = need_str(argc, argv, 1, "find");
        const char *q = mfind(s->chars, s->len, n->chars, n->len);
        if (!q) return VNIL;
        long idx = 0;
        for (const char *p = s->chars; p < q; p += u8len((unsigned char)*p)) idx++;
        return vnum(idx);
    }
    Cell *c = need_cell(argc, argv, 0, "find");
    for (int i = 0; i < c->alen; i++) if (val_eq(c->arr[i], x, 0)) return vnum(i);
    return VNIL;
}
NAT(n_reverse) {
    Value v = ARG(0);
    if (v.t == T_STR) {
        Str *s = AS_STR(v); Buf b = { 0 };
        const char *end = s->chars + s->len, *p = end;
        while (p > s->chars) {
            const char *q = p - 1;
            while (q > s->chars && ((unsigned char)*q & 0xC0) == 0x80) q--;
            buf_put(&b, q, p - q);
            p = q;
        }
        return vbuf(&b);
    }
    Cell *c = need_cell(argc, argv, 0, "reverse"), *r = cell_new();
    for (int i = c->alen - 1; i >= 0; i--) cell_push(r, c->arr[i]);
    return VCELL(r);
}
NAT(n_copy) {
    Value v = ARG(0);
    return v.t == T_CELL ? VCELL(cell_copy(AS_CELL(v))) : v;
}

static Value sort_fn; static int sort_has;
static int sort_less(Value a, Value b) {
    if (!sort_has) return cmp_vals(a, b, 0) < 0;
    Value args[2] = { a, b };
    Value r = call_value(sort_fn, 2, args, VNIL, 0);
    return r.t == T_NUM ? r.u.n < 0 : truthy(r);
}
static void msort(Value *a, Value *tmp, int n) {
    if (n < 2) return;
    int m = n / 2, i = 0, j = m, k = 0;
    msort(a, tmp, m); msort(a + m, tmp, n - m);
    while (i < m && j < n) tmp[k++] = sort_less(a[j], a[i]) ? a[j++] : a[i++];
    while (i < m) tmp[k++] = a[i++];
    while (j < n) tmp[k++] = a[j++];
    memcpy(a, tmp, n * sizeof(Value));
}
NAT(n_sort) {
    Cell *c = need_cell(argc, argv, 0, "sort"), *r = cell_new();
    for (int i = 0; i < c->alen; i++) cell_push(r, c->arr[i]);
    push(VCELL(r));
    Value pf = sort_fn; int ph = sort_has;
    sort_fn = ARG(1); sort_has = sort_fn.t == T_FUNC || sort_fn.t == T_NATIVE;
    Value *tmp = malloc((r->alen + 1) * sizeof(Value));
    msort(r->arr, tmp, r->alen);
    free(tmp);
    sort_fn = pf; sort_has = ph;
    vm.sp--;
    return VCELL(r);
}

#define MATH1(name, expr) NAT(name) { double x = need_num(argc, argv, 0, #name); return vnum(expr); }
MATH1(n_floor, floor(x))
MATH1(n_ceil, ceil(x))
MATH1(n_round, round(x))
MATH1(n_abs, fabs(x))
MATH1(n_sqrt, sqrt(x))
MATH1(n_sin, sin(x))
MATH1(n_cos, cos(x))
MATH1(n_tan, tan(x))
MATH1(n_log, log(x))
MATH1(n_exp, exp(x))
NAT(n_pow) { return vnum(pow(need_num(argc, argv, 0, "pow"), need_num(argc, argv, 1, "pow"))); }
static Value minmax(int argc, Value *argv, int mx) {
    if (argc == 1 && argv[0].t == T_CELL) { argc = AS_CELL(argv[0])->alen; argv = AS_CELL(argv[0])->arr; }
    if (!argc) return VNIL;
    Value best = argv[0];
    for (int i = 1; i < argc; i++) {
        int c = cmp_vals(argv[i], best, 1);
        if (mx ? c > 0 : c < 0) best = argv[i];
    }
    return best;
}
NAT(n_min) { return minmax(argc, argv, 0); }
NAT(n_max) { return minmax(argc, argv, 1); }
static double rnd(void) {
    vm.rng ^= vm.rng << 13; vm.rng ^= vm.rng >> 7; vm.rng ^= vm.rng << 17;
    return (vm.rng >> 11) * (1.0 / 9007199254740992.0);
}
NAT(n_random) {
    if (argc == 0) return vnum(rnd());
    if (argc == 1) return vnum(floor(rnd() * need_num(argc, argv, 0, "random")));
    double a = need_num(argc, argv, 0, "random"), b = need_num(argc, argv, 1, "random");
    return vnum(a + floor(rnd() * (b - a + 1)));
}
NAT(n_clock) { (void)argc; (void)argv; return vnum((double)clock() / CLOCKS_PER_SEC); }
NAT(n_time) { (void)argc; (void)argv; return vnum((double)time(NULL)); }
NAT(n_error) { uc_throw(argc ? argv[0] : vstr("errore")); }
NAT(n_assert) {
    if (truthy(ARG(0))) return vbool(1);
    if (argc > 1) rt_error(L("verifica fallita: %s", "assertion failed: %s"), AS_STR(tostr(argv[1]))->chars);
    rt_error(L("verifica fallita", "assertion failed"));
}
NAT(n_try) {
    Value f = ARG(0), h = ARG(1);
    jmp_buf jb; jmp_buf *prev = vm.err;
    int sp = vm.sp, esp = vm.esp, depth = vm.depth, line = vm.line;
    const char *file = vm.file;
    vm.err = &jb;
    if (setjmp(jb) == 0) {
        Value r = call_value(f, 0, NULL, VNIL, 0);
        vm.err = prev;
        return r;
    }
    vm.err = prev; vm.sp = sp; vm.esp = esp; vm.depth = depth; vm.line = line; vm.file = file;
    vm.unwind = U_NONE; vm.ret = VNIL;
    if (vm.fatal) uc_throw(vm.errval);   /* i limiti non si possono intercettare */
    Value ev = vm.errval;
    vm.errval = VNIL;
    if (h.t == T_NIL) return VNIL;
    Value a[1] = { ev };
    return call_value(h, 1, a, VNIL, 0);
}
NAT(n_read_file) {
    deny("read_file");
    size_t n; char *s = slurp(need_str(argc, argv, 0, "read_file")->chars, &n);
    if (!s) return VNIL;
    Value v = VSTR(str_new(s, n)); free(s);
    return v;
}
NAT(n_write_file) {
    deny("write_file");
    Str *path = need_str(argc, argv, 0, "write_file");
    Value data = tostr(ARG(1));
    FILE *f = fopen(path->chars, argc > 2 && truthy(argv[2]) ? "ab" : "wb");
    if (!f) return vbool(0);
    size_t w = fwrite(AS_STR(data)->chars, 1, AS_STR(data)->len, f);
    fclose(f);
    return vbool(w == AS_STR(data)->len);
}
static void deny(const char *fn) {
    if (vm.sandbox) rt_error(L("%s: non disponibile in modalità protetta", "%s: not available in sandbox mode"), fn);
}

/* importa("x") cerca prima accanto al file che lo chiama, poi nella
   cartella corrente; prova anche ad aggiungere ".uc". Ogni modulo viene
   eseguito una volta sola: le importazioni successive riusano la cella. */
NAT(n_import) {
    deny("import");
    Str *path = need_str(argc, argv, 0, "import");
    const char *bases[2] = { NULL, "" };
    Buf dir = { 0 };
    if (path->chars[0] != '/' && vm.file) {
        const char *sl = strrchr(vm.file, '/');
        if (sl) { buf_put(&dir, vm.file, sl - vm.file + 1); bases[0] = dir.s; }
    }
    char *src = NULL; size_t n = 0; Buf full = { 0 };
    for (int i = 0; i < 2 && !src; i++) {
        if (!bases[i]) continue;
        for (int ext = 0; ext < 2 && !src; ext++) {
            full.len = 0;
            buf_puts(&full, bases[i]); buf_put(&full, path->chars, path->len);
            if (ext) buf_puts(&full, ".uc");
            src = slurp(full.s, &n);
        }
    }
    free(dir.s);
    if (!src) { free(full.s); rt_error(L("import: impossibile trovare '%s'", "import: cannot find '%s'"), path->chars); }
    Value key = vbuf(&full), cached;
    if (cell_get_key(vm.modules, key, &cached)) { free(src); return cached; }
    if (vm.esp >= UC_DEPTH_MAX) rt_error(L("troppi import annidati", "imports nested too deeply"));
    Env *me = env_new(vm.globals);
    /* registrato prima di eseguirlo: le importazioni circolari non si ripetono */
    cell_set_key(vm.modules, key, VCELL(me->vars));
    vm.envs[vm.esp++] = me;
    exec_src(src, n, me, xstrdup(AS_STR(key)->chars));
    free(src);
    vm.esp--;
    return VCELL(me->vars);
}
NAT(n_eval) {
    Str *s = need_str(argc, argv, 0, "eval");
    return exec_src(s->chars, s->len, vm.globals, "<esegui>");
}
static int utf8_enc(long c, char *b) {
    if (c < 0 || c > 0x10FFFF) c = 0xFFFD;
    if (c < 0x80) { b[0] = (char)c; return 1; }
    if (c < 0x800) { b[0] = (char)(0xC0 | c >> 6); b[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { b[0] = (char)(0xE0 | c >> 12); b[1] = (char)(0x80 | ((c >> 6) & 63)); b[2] = (char)(0x80 | (c & 63)); return 3; }
    b[0] = (char)(0xF0 | c >> 18); b[1] = (char)(0x80 | ((c >> 12) & 63));
    b[2] = (char)(0x80 | ((c >> 6) & 63)); b[3] = (char)(0x80 | (c & 63));
    return 4;
}
NAT(n_chr) {
    char b[4];
    int n = utf8_enc((long)need_num(argc, argv, 0, "chr"), b);
    return VSTR(str_new(b, n));
}

/* ---------- JSON ---------- */

static void json_str(Buf *b, const char *s, size_t n) {
    buf_putc(b, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': buf_puts(b, "\\\""); break;
        case '\\': buf_puts(b, "\\\\"); break;
        case '\n': buf_puts(b, "\\n"); break;
        case '\r': buf_puts(b, "\\r"); break;
        case '\t': buf_puts(b, "\\t"); break;
        case '\b': buf_puts(b, "\\b"); break;
        case '\f': buf_puts(b, "\\f"); break;
        default:
            if (c < 0x20) buf_fmt(b, "\\u%04x", c);
            else buf_putc(b, (char)c);
        }
    }
    buf_putc(b, '"');
}

static void json_nl(Buf *b, int indent, int level) {
    if (!indent) return;
    buf_putc(b, '\n');
    for (int i = 0; i < indent * level; i++) buf_putc(b, ' ');
}

/* Una cella senza chiavi diventa un array; altrimenti un oggetto
   (gli elementi in ordine prendono come chiave "0", "1", ...). */
static void json_enc(Buf *b, Value v, int indent, int level) {
    if (level > 100) rt_error(L("to_json: struttura troppo profonda (c'è un riferimento circolare?)", "to_json: structure too deep (is there a circular reference?)"));
    switch (v.t) {
    case T_NIL: buf_puts(b, "null"); break;
    case T_BOOL: buf_puts(b, v.u.b ? "true" : "false"); break;
    case T_NUM: {
        double d = v.u.n; char tmp[40];
        if (!isfinite(d)) { buf_puts(b, "null"); break; }
        if (d == floor(d) && fabs(d) < 1e15) snprintf(tmp, sizeof tmp, "%.0f", d == 0 ? 0.0 : d);
        else {
            snprintf(tmp, sizeof tmp, "%.15g", d);
            if (strtod(tmp, NULL) != d) snprintf(tmp, sizeof tmp, "%.17g", d);
        }
        buf_puts(b, tmp);
        break;
    }
    case T_STR: json_str(b, AS_STR(v)->chars, AS_STR(v)->len); break;
    case T_CELL: {
        Cell *c = AS_CELL(v);
        int total = c->alen + c->elen, k = 0;
        if (!c->elen) {
            buf_putc(b, '[');
            for (int i = 0; i < c->alen; i++) {
                if (i) buf_putc(b, ',');
                json_nl(b, indent, level + 1);
                json_enc(b, c->arr[i], indent, level + 1);
            }
            if (c->alen) json_nl(b, indent, level);
            buf_putc(b, ']');
            break;
        }
        buf_putc(b, '{');
        for (int i = 0; i < c->alen; i++, k++) {
            if (k) buf_putc(b, ',');
            json_nl(b, indent, level + 1);
            buf_fmt(b, "\"%d\":", i);
            if (indent) buf_putc(b, ' ');
            json_enc(b, c->arr[i], indent, level + 1);
        }
        for (int i = 0; i < c->elen; i++, k++) {
            if (k) buf_putc(b, ',');
            json_nl(b, indent, level + 1);
            Value key = c->ent[i].key;
            if (key.t == T_STR) json_str(b, AS_STR(key)->chars, AS_STR(key)->len);
            else {
                Buf kb = { 0 }; to_buf(&kb, key, 0, 0);
                json_str(b, kb.s, kb.len); free(kb.s);
            }
            buf_putc(b, ':');
            if (indent) buf_putc(b, ' ');
            json_enc(b, c->ent[i].val, indent, level + 1);
        }
        if (total) json_nl(b, indent, level);
        buf_putc(b, '}');
        break;
    }
    default: rt_error(L("to_json: una funzione non si può convertire in JSON", "to_json: a function cannot be converted to JSON"));
    }
}

NAT(n_to_json) {
    int indent = 0;
    if (argc > 1 && argv[1].t == T_NUM) indent = (int)argv[1].u.n;
    else if (argc > 1 && truthy(argv[1])) indent = 2;
    if (indent < 0) indent = 0;
    if (indent > 16) indent = 16;
    Buf b = { 0 };
    json_enc(&b, ARG(0), indent, 0);
    return vbuf(&b);
}

typedef struct { const char *s, *p, *end; } JP;

NORET static void json_err(JP *j, const char *msg) {
    rt_error(L("from_json: %s (al carattere %ld)", "from_json: %s (at character %ld)"), msg, (long)(j->p - j->s) + 1);
}
static void jws(JP *j) { while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++; }
static int jlit(JP *j, const char *w) {
    size_t n = strlen(w);
    if ((size_t)(j->end - j->p) >= n && memcmp(j->p, w, n) == 0) { j->p += n; return 1; }
    return 0;
}
static long jhex4(JP *j) {
    if (j->end - j->p < 4) json_err(j, L("sequenza \\u incompleta", "incomplete \\u sequence"));
    long v = 0;
    for (int i = 0; i < 4; i++) {
        char c = *j->p++;
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else json_err(j, L("cifra esadecimale non valida", "invalid hex digit"));
    }
    return v;
}
static Value jstr(JP *j) {
    Buf b = { 0 };
    j->p++;
    for (;;) {
        if (j->p >= j->end) { free(b.s); json_err(j, L("testo non terminato", "unterminated string")); }
        char c = *j->p++;
        if (c == '"') break;
        if ((unsigned char)c < 0x20) { free(b.s); json_err(j, L("carattere di controllo nel testo", "control character in string")); }
        if (c != '\\') { buf_putc(&b, c); continue; }
        if (j->p >= j->end) { free(b.s); json_err(j, L("escape incompleto", "incomplete escape")); }
        char e = *j->p++;
        switch (e) {
        case '"': case '\\': case '/': buf_putc(&b, e); break;
        case 'n': buf_putc(&b, '\n'); break;
        case 't': buf_putc(&b, '\t'); break;
        case 'r': buf_putc(&b, '\r'); break;
        case 'b': buf_putc(&b, '\b'); break;
        case 'f': buf_putc(&b, '\f'); break;
        case 'u': {
            long cp = jhex4(j);
            if (cp >= 0xD800 && cp <= 0xDBFF && j->end - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') {
                j->p += 2;
                long lo = jhex4(j);
                if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            }
            char u[4]; int n = utf8_enc(cp, u);
            buf_put(&b, u, n);
            break;
        }
        default: free(b.s); json_err(j, L("escape non valido", "invalid escape"));
        }
    }
    return vbuf(&b);
}
static Value jval(JP *j, int depth) {
    if (depth > 200) json_err(j, L("struttura troppo profonda", "structure too deep"));
    jws(j);
    if (j->p >= j->end) json_err(j, L("fine inattesa", "unexpected end"));
    char c = *j->p;
    if (c == '"') return jstr(j);
    if (c == '[') {
        Cell *r = cell_new();
        j->p++; jws(j);
        if (j->p < j->end && *j->p == ']') { j->p++; return VCELL(r); }
        for (;;) {
            cell_push(r, jval(j, depth + 1));
            jws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == ']') { j->p++; return VCELL(r); }
            json_err(j, L("attesa ',' o ']'", "expected ',' or ']'"));
        }
    }
    if (c == '{') {
        Cell *r = cell_new();
        j->p++; jws(j);
        if (j->p < j->end && *j->p == '}') { j->p++; return VCELL(r); }
        for (;;) {
            jws(j);
            if (j->p >= j->end || *j->p != '"') json_err(j, L("attesa una chiave tra virgolette", "expected a quoted key"));
            Value k = jstr(j);
            jws(j);
            if (j->p >= j->end || *j->p != ':') json_err(j, L("atteso ':'", "expected ':'"));
            j->p++;
            cell_set_key(r, k, jval(j, depth + 1));
            jws(j);
            if (j->p < j->end && *j->p == ',') { j->p++; continue; }
            if (j->p < j->end && *j->p == '}') { j->p++; return VCELL(r); }
            json_err(j, L("attesa ',' o '}'", "expected ',' or '}'"));
        }
    }
    if (jlit(j, "true")) return vbool(1);
    if (jlit(j, "false")) return vbool(0);
    if (jlit(j, "null")) return VNIL;
    if (c == '-' || isdigit((unsigned char)c)) {
        char *e;
        double d = strtod(j->p, &e);
        if (e == j->p) json_err(j, L("numero non valido", "invalid number"));
        j->p = e;
        return vnum(d);
    }
    json_err(j, L("valore non valido", "invalid value"));
}

NAT(n_from_json) {
    Str *s = need_str(argc, argv, 0, "from_json");
    JP j = { s->chars, s->chars, s->chars + s->len };
    Value v = jval(&j, 0);
    jws(&j);
    if (j.p != j.end) json_err(&j, L("testo in più dopo il valore", "extra text after the value"));
    return v;
}
NAT(n_ord) {
    Str *s = need_str(argc, argv, 0, "ord");
    if (!s->len) return VNIL;
    const unsigned char *p = (const unsigned char *)s->chars;
    int l = u8len(p[0]);
    if ((size_t)l > s->len) l = 1;
    long c = l == 1 ? p[0] : l == 2 ? (p[0] & 31) : l == 3 ? (p[0] & 15) : (p[0] & 7);
    for (int i = 1; i < l; i++) c = (c << 6) | (p[i] & 63);
    return vnum(c);
}
NAT(n_exit) { deny("exit"); fflush(stdout); exit(argc ? (int)need_num(argc, argv, 0, "exit") : 0); }

static const struct { const char *en, *it; NativeFn fn; } NATIVES[] = {
    { "print", "stampa", n_print }, { "write", "scrivi", n_write }, { "input", "chiedi", n_input },
    { "str", "testo", n_str }, { "num", "numero", n_num }, { "type", NULL, n_type }, { "tipo", NULL, n_tipo }, { "language", "lingua", n_lang },
    { "len", "lunghezza", n_len }, { "push", "aggiungi", n_push }, { "pop", "togli", n_pop },
    { "remove", "rimuovi", n_remove }, { "keys", "chiavi", n_keys }, { "values", "valori", n_values },
    { "has", "ha", n_has }, { "slice", "taglia", n_slice }, { "join", "unisci", n_join },
    { "split", "dividi", n_split }, { "upper", "maiuscolo", n_upper }, { "lower", "minuscolo", n_lower },
    { "trim", "pulisci", n_trim }, { "replace", "sostituisci", n_replace }, { "find", "cerca", n_find },
    { "reverse", "inverti", n_reverse }, { "sort", "ordina", n_sort }, { "copy", "copia", n_copy },
    { "floor", "intero", n_floor }, { "ceil", NULL, n_ceil }, { "round", "arrotonda", n_round },
    { "abs", "assoluto", n_abs }, { "sqrt", "radice", n_sqrt }, { "sin", NULL, n_sin },
    { "cos", NULL, n_cos }, { "tan", NULL, n_tan }, { "log", NULL, n_log }, { "exp", NULL, n_exp },
    { "pow", "potenza", n_pow }, { "min", "minimo", n_min }, { "max", "massimo", n_max },
    { "random", "caso", n_random }, { "clock", "orologio", n_clock }, { "time", "ora", n_time },
    { "error", "errore", n_error }, { "try", "prova", n_try }, { "assert", "verifica", n_assert },
    { "read_file", "leggi_file", n_read_file }, { "write_file", "scrivi_file", n_write_file },
    { "import", "importa", n_import }, { "eval", "esegui", n_eval },
    { "chr", "carattere", n_chr }, { "to_json", "in_json", n_to_json }, { "from_json", "da_json", n_from_json }, { "ord", "codice", n_ord }, { "exit", "esci", n_exit },
};

/* La libreria standard scritta in UniCo stesso. */
static const char *PRELUDE =
    "fn map(xs, f) { r = []; for x in xs { push(r, f(x)) }; r }\n"
    "fn filter(xs, f) { r = []; for x in xs { if f(x) { push(r, x) } }; r }\n"
    "fn reduce(xs, f, acc) { for x in xs { acc = f(acc, x) }; acc }\n"
    "fn each(xs, f) { for x in xs { f(x) }; xs }\n"
    "fn sum(xs) { reduce(xs, |a, b| a + b, 0) }\n"
    "fn any(xs, f) { for x in xs { if f(x) { return true } }; false }\n"
    "fn all(xs, f) { for x in xs { if !f(x) { return false } }; true }\n"
    "fn count(xs, f) { n = 0; for x in xs { if f(x) { n += 1 } }; n }\n"
    "fn zip(a, b) { r = []; for i in 0..<min(len(a), len(b)) { push(r, [a[i], b[i]]) }; r }\n"
    "fn times(n, f) { r = []; for i in n { push(r, f(i)) }; r }\n"
    "mappa = map; filtra = filter; riduci = reduce; ognuno = each; somma = sum\n"
    "qualcuno = any; tutti = all; conta = count; accoppia = zip; volte = times\n";

/* ================================================================== */
/*  API pubblica                                                       */
/* ================================================================== */

void uc_def(const char *name, NativeFn fn) {
    Native *nf = gc_alloc(sizeof(Native), O_NATIVE);
    nf->h.perm = 1; nf->fn = fn; nf->name = name;
    setv(vm.globals, str_intern(name, strlen(name)), vobj(T_NATIVE, nf));
}

void uc_set(const char *name, Value v) { setv(vm.globals, str_intern(name, strlen(name)), v); }

/* Limiti per codice non fidato (0 = nessun limite). Valgono per ogni uc_run. */
void uc_limits(long max_steps, double max_seconds, size_t max_bytes) {
    vm.max_steps = max_steps; vm.max_time = max_seconds; vm.mem_limit = max_bytes;
    if (max_bytes && vm.next_bytes > max_bytes) vm.next_bytes = max_bytes;
}

/* Modalità protetta: niente file, importazioni né esci(). */
void uc_sandbox(int on) { vm.sandbox = on; }

/* Esegue codice sorgente; stampa l'errore (con la catena di chiamate)
   e restituisce 0 se fallisce. "name" è il nome del file, o NULL. */
int uc_run(const char *src, size_t len, const char *name, Value *out) {
    jmp_buf jb; jmp_buf *prev = vm.err;
    int sp = vm.sp, esp = vm.esp, depth = vm.depth;
    vm.err = &jb;
    vm.steps = 0; vm.t0 = clock(); vm.fatal = 0;
    if (setjmp(jb)) {
        vm.err = prev; vm.sp = sp; vm.esp = esp; vm.depth = depth;
        vm.unwind = U_NONE; vm.ret = VNIL; vm.fatal = 0;
        Buf b = { 0 }; to_buf(&b, vm.errval, 0, 0);
        fprintf(stderr, "%s: %s\n%s", L("errore", "error"), b.s ? b.s : "", vm.trace.s ? vm.trace.s : "");
        free(b.s);
        vm.errval = VNIL;
        return 0;
    }
    Value v = exec_src(src, len, vm.globals, name ? xstrdup(name) : NULL);
    vm.err = prev;
    if (out) *out = v;
    return 1;
}

/* Lingua dei messaggi: "it" o "en". Restituisce 0 se non è disponibile. */
int uc_lang(const char *name) {
    int l = lang_from(name);
    if (l < 0) return 0;
    vm.lang = l;
    return 1;
}

void uc_init(void) {
    /* lingua: UNICO_LANG, altrimenti quella del sistema; inglese di base */
    const char *env = getenv("UNICO_LANG");
    if (lang_from(env) < 0) {
        env = getenv("LC_ALL");
        if (!env || !*env) env = getenv("LC_MESSAGES");
        if (!env || !*env) env = getenv("LANG");
    }
    vm.lang = lang_from(env) == LANG_IT ? LANG_IT : LANG_EN;
    vm.next_gc = 50000;
    vm.rng = (uint64_t)time(NULL) * 2654435761u + 88172645463325252ULL;
    vm.globals = env_new(NULL);
    vm.globals->h.perm = 1; vm.globals->vars->h.perm = 1;
    vm.modules = cell_new(); vm.modules->h.perm = 1;
    vm.s_self = str_intern("@", 1);
    for (size_t i = 0; i < sizeof NATIVES / sizeof NATIVES[0]; i++) {
        uc_def(NATIVES[i].en, NATIVES[i].fn);
        if (NATIVES[i].it) uc_def(NATIVES[i].it, NATIVES[i].fn);
    }
    uc_set("PI", vnum(3.14159265358979323846));
    uc_set("VERSIONE", vstr(UC_VERSION));
    uc_run(PRELUDE, strlen(PRELUDE), "prelude", NULL);
}

/* ================================================================== */
/*  Riga di comando e REPL                                             */
/* ================================================================== */

#ifndef UNICO_NO_MAIN

static int open_brackets(const char *s) {
    int d = 0;
    for (; *s; s++) {
        if (*s == '#') { while (*s && *s != '\n') s++; if (!*s) break; continue; }
        if (*s == '"' || *s == '\'') {
            char q = *s++;
            while (*s && *s != q) { if (*s == '\\' && s[1]) s++; s++; }
            if (!*s) return 1;
            continue;
        }
        if (*s == '(' || *s == '[' || *s == '{') d++;
        else if (*s == ')' || *s == ']' || *s == '}') d--;
    }
    return d > 0;
}

static void repl(void) {
    printf(L("UniCo %s — un codice unico. Scrivi esci() per uscire.\n", "UniCo %s — one code, unique. Type exit() to quit.\n"), UC_VERSION);
    Buf code = { 0 }; char line[4096];
    for (;;) {
        fputs(code.len ? "... " : "uc> ", stdout); fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        buf_puts(&code, line);
        if (open_brackets(code.s)) continue;
        Value v;
        if (uc_run(code.s, code.len, NULL, &v) && v.t != T_NIL) {
            Buf b = { 0 }; to_buf(&b, v, 1, 0);
            printf("= %s\n", b.s);
            free(b.s);
        }
        code.len = 0; code.s[0] = 0;
    }
    putchar('\n');
}

static void usage(void) {
    puts(L("uso: unico [opzioni] [file.uc [argomenti...]]\n"
           "     senza file parte la modalità interattiva; '-' legge il programma dallo standard input\n"
           "\n"
           "  -e CODICE                esegue il codice indicato\n"
           "  --protetto, --sandbox    niente file, importa() ed esci()\n"
           "  --passi N, --steps N     al massimo N istruzioni eseguite\n"
           "  --tempo S, --time S      al massimo S secondi di calcolo\n"
           "  --memoria M, --memory M  al massimo M megabyte di memoria (circa)\n"
           "  --lingua L, --lang L     lingua dei messaggi: it o en\n"
           "  -v, --version            mostra la versione\n"
           "  -h, --help               mostra questo aiuto",
           "usage: unico [options] [file.uc [args...]]\n"
           "       without a file it starts interactive mode; '-' reads the program from standard input\n"
           "\n"
           "  -e CODE                  run CODE\n"
           "  --sandbox, --protetto    no files, import() or exit()\n"
           "  --steps N, --passi N     run at most N statements\n"
           "  --time S, --tempo S      use at most S seconds of CPU time\n"
           "  --memory M, --memoria M  use at most about M megabytes of memory\n"
           "  --lang L, --lingua L     language of messages: en or it\n"
           "  -v, --version            show the version\n"
           "  -h, --help               show this help"));
}

static int opt_is(const char *a, const char *it, const char *en) { return !strcmp(a, it) || !strcmp(a, en); }

int main(int argc, char **argv) {
    uc_init();
    const char *code = NULL, *file = NULL;
    long steps = 0; double secs = 0, mb = 0;
    int i = 1;
    for (; i < argc && !file; i++) {
        const char *a = argv[i];
        int has_val = i + 1 < argc;
        if (opt_is(a, "-v", "--version")) { printf("UniCo %s\n", UC_VERSION); return 0; }
        else if (opt_is(a, "-h", "--help")) { usage(); return 0; }
        else if (opt_is(a, "--protetto", "--sandbox")) uc_sandbox(1);
        else if (opt_is(a, "--passi", "--steps") && has_val) steps = atol(argv[++i]);
        else if (opt_is(a, "--tempo", "--time") && has_val) secs = atof(argv[++i]);
        else if (opt_is(a, "--memoria", "--memory") && has_val) mb = atof(argv[++i]);
        else if (opt_is(a, "--lingua", "--lang") && has_val) {
            if (!uc_lang(argv[++i])) {
                fprintf(stderr, L("unico: lingua non disponibile '%s' (disponibili: it, en)\n",
                                  "unico: language not available '%s' (available: en, it)\n"), argv[i]);
                return 2;
            }
        }
        else if (!strcmp(a, "-e") && has_val) { code = argv[++i]; break; }
        else if (a[0] == '-' && a[1]) {
            fprintf(stderr, L("unico: opzione sconosciuta '%s'\n", "unico: unknown option '%s'\n"), a);
            usage(); return 2;
        }
        else file = a;
    }
    if (!file && !code) i = argc;   /* modalità interattiva: niente argomenti */
    else if (code) i++;
    Cell *args = cell_new();
    for (; i < argc; i++) cell_push(args, vstr(argv[i]));
    uc_set("ARGS", VCELL(args));
    uc_set("ARGOMENTI", VCELL(args));
    uc_limits(steps, secs, (size_t)(mb * 1024 * 1024));
    if (code) return uc_run(code, strlen(code), NULL, NULL) ? 0 : 1;
    if (!file) { repl(); return 0; }
    size_t n;
    char *src;
    if (!strcmp(file, "-")) {
        Buf b = { 0 }; char tmp[8192]; size_t k;
        while ((k = fread(tmp, 1, sizeof tmp, stdin)) > 0) buf_put(&b, tmp, k);
        src = b.s ? b.s : calloc(1, 1); n = b.len; file = "<stdin>";
    } else src = slurp(file, &n);
    if (!src) { fprintf(stderr, L("unico: impossibile aprire '%s'\n", "unico: cannot open '%s'\n"), file); return 1; }
    int ok = uc_run(src, n, file, NULL);
    free(src);
    fflush(stdout);
    return ok ? 0 : 1;
}

#endif
