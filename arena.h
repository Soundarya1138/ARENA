/* arena.h - shared types and prototypes for "Arena Ascent" */
#ifndef ARENA_H
#define ARENA_H
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE            /* usleep / clock_gettime under -std=c11 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NAME_LEN          24
#define TOP_K             10
#define MOM_N             5        /* rank-momentum window (last N updates)  */
#define CHEAT_FACTOR      5.0      /* flag if gain > 5x player's average gain */
#define CHEAT_MIN_GAIN    50       /* ignore tiny absolute jumps              */
#define CHEAT_MIN_HISTORY 3        /* need >=3 past gains to have an average  */
#define UNDO_CAP          100000
#define SIM_BASE_ID       10000

static inline void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n, s);
    if (!p) { fputs("out of memory\n", stderr); exit(1); }
    return p;
}
static inline void *xrealloc(void *q, size_t s) {
    void *p = realloc(q, s);
    if (!p) { fputs("out of memory\n", stderr); exit(1); }
    return p;
}

/* ------------------------------------------------------------------ */
/* One Player record. It is *intrusive*: the same struct is a hash-chain
 * node, an AVL node and a heap slot, so an update never allocates.     */
typedef struct Player {
    int    id;
    char   name[NAME_LEN];
    long   score;
    long   ts;                       /* logical time the current score was reached */
    struct Player *hnext;            /* hash chain                                  */
    struct Player *left, *right;     /* AVL links                                   */
    int    height, size;             /* AVL height, subtree size (order-statistic)  */
    int    hpos;                     /* index in Top-10 heap, -1 if absent          */
    long   gain_sum; int gain_cnt;   /* anti-cheat running average                  */
    int    hist[MOM_N]; int hist_n, hist_head; /* last N ranks (momentum ring)      */
    double disp;                     /* animated (eased) score shown in the bar     */
    int    shown_pos;                /* position on last drawn board (0 = off board)*/
} Player;

/* ---------------- hash.c ---------------- */
typedef struct { Player **b; size_t nb, n; } HashTable;
void    ht_init(HashTable *h, size_t nb);
int     ht_insert(HashTable *h, Player *p);        /* 0 ok, -1 duplicate id */
Player *ht_find(const HashTable *h, int id);
size_t  ht_max_chain(const HashTable *h);
void    ht_free(HashTable *h);                     /* also frees all Players */

/* ---------------- avl.c ----------------- */
int     avl_cmp(const Player *a, const Player *b); /* score desc, ts asc, id asc */
Player *avl_insert(Player *root, Player *p);
Player *avl_delete(Player *root, const Player *key);
Player *avl_select(Player *root, int k);           /* k-th best, 1-based         */
int     avl_position(Player *root, const Player *p);   /* ordinal position 1..n  */
int     avl_rank_comp(Player *root, long score);   /* competition rank 1+#higher */
int     avl_size(const Player *n);
int     avl_height(const Player *n);
int     avl_check(Player *root);                   /* 1 if all invariants hold   */

/* ---------------- heap.c ---------------- */
typedef struct { Player *a[TOP_K]; int n; } MinHeap;
void heap_init(MinHeap *h);
void heap_offer(MinHeap *h, Player *p);
void heap_rebuild(MinHeap *h, Player *root);
int  heap_sorted(const MinHeap *h, Player **out);  /* best first, returns count */

/* ---------------- queue.c --------------- */
typedef struct { int id; long delta; } Update;
typedef struct { Update *buf; size_t cap, head, n, peak; } Queue;
void   q_init(Queue *q, size_t cap);
void   q_push(Queue *q, Update u);
int    q_pop(Queue *q, Update *out);
void   q_free(Queue *q);

/* ---------------- stack.c --------------- */
typedef struct { int id; long delta, old_score, old_ts, new_score, new_ts; } UndoRec;
typedef struct { UndoRec *a; size_t cap, start, n; } UndoStack;
void st_init(UndoStack *s, size_t cap);
void st_push(UndoStack *s, UndoRec r);
int  st_pop(UndoStack *s, UndoRec *out);
void st_free(UndoStack *s);

/* ---------------- game.c ---------------- */
typedef struct { long score, ts; int id; char kind; } LogEntry;   /* R/U/D */
typedef struct { int id; long delta; } Quar;

typedef struct Game {
    HashTable ht;  Player *root;  MinHeap heap;  Queue q;  UndoStack undo;
    long clock;    int nplayers;  int record;
    LogEntry *log; size_t nlog, caplog;
    Quar *quar;    size_t nquar, capquar;
    long total_updates, total_flagged, total_undone, total_rejected, total_approved;
    char last_flag[96]; long flag_seq;
} Game;

typedef struct {
    Player *p; int n, comp, ord; double pct; const char *tier, *momentum;
} RankInfo;

void game_init(Game *g, int record);
void game_free(Game *g);
int  game_register(Game *g, int id, const char *name);      /* 0 / -1 dup */
void game_submit(Game *g, int id, long delta);              /* enqueue    */
size_t game_process(Game *g, size_t max);                   /* batch      */
int  game_apply(Game *g, int id, long delta, int force);    /* 0 ok,1 quarantined,-1 unknown */
int  game_undo(Game *g, int n);
void game_quarantine_resolve(Game *g, int approve);
int  game_rank_info(const Game *g, int id, RankInfo *ri);
void game_radar(const Game *g, int id);
void game_print_top(const Game *g, const Game *now, const char *title);
void game_replay(const Game *live, size_t upto, Game *out);
void game_stats(const Game *g);

/* ---------------- anim.c ---------------- */
typedef struct {
    int tick, champ, banner, overtakes; char banner_name[NAME_LEN];
    int prev[TOP_K], nprev; double batch_ms; size_t batch_n; long seen_flag; int flag_age;
} AnimState;
void anim_simulate(Game *g, int bots, int ticks, int per_tick, int fps);

/* ---------------- bench.c --------------- */
void bench_run(void);
int  selftest(void);

/* ---------------- platform.c ------------ */
void   term_init(void);
void   term_raw(int on);
int    key_poll(void);
void   sleep_ms(int ms);
double now_ms(void);
#endif
