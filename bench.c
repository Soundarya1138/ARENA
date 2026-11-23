/* bench.c - Benchmark Mode (AVL vs naive sorted arrays) and a randomised self-test. */
#include "arena.h"
#include <time.h>

typedef struct { int idx; long delta; } Op;

static double cpu_ms(void) { return 1000.0 * (double)clock() / CLOCKS_PER_SEC; }

static Player *mk(int P) {
    Player *a = xcalloc((size_t)P, sizeof *a);
    for (int i = 0; i < P; i++) { a[i].id = i + 1; a[i].ts = i + 1; a[i].hpos = -1; }
    return a;
}

/* (1) Order-statistic AVL: delete + insert + rank, all O(log n) */
static double run_tree(int P, const Op *ops, int N, long *sum) {
    Player *a = mk(P), *root = NULL; long clk = P; *sum = 0;
    for (int i = 0; i < P; i++) root = avl_insert(root, &a[i]);
    double t0 = cpu_ms();
    for (int i = 0; i < N; i++) {
        Player *p = &a[ops[i].idx];
        root = avl_delete(root, p); p->score += ops[i].delta; p->ts = ++clk; root = avl_insert(root, p);
        *sum += avl_rank_comp(root, p->score);
    }
    double t = cpu_ms() - t0; free(a); return t;
}

/* (2) Naive sorted array: linear locate, shift, linear rank scan => O(n) per update */
static double run_naive(int P, const Op *ops, int N, long *sum) {
    Player *a = mk(P); Player **arr = xcalloc((size_t)P, sizeof *arr); long clk = P; *sum = 0;
    for (int i = 0; i < P; i++) arr[i] = &a[i];
    double t0 = cpu_ms();
    for (int i = 0; i < N; i++) {
        Player *p = &a[ops[i].idx]; int pos = 0;
        while (arr[pos] != p) pos++;                              /* O(n) find */
        p->score += ops[i].delta; p->ts = ++clk;
        while (pos > 0 && avl_cmp(p, arr[pos - 1]) < 0) { arr[pos] = arr[pos - 1]; pos--; }   /* O(n) shift */
        arr[pos] = p;
        int r = 0; while (arr[r]->score > p->score) r++;          /* O(n) rank */
        *sum += r + 1;
    }
    double t = cpu_ms() - t0; free(arr); free(a); return t;
}

/* (3) Smarter sorted array: binary search + memmove. Still O(n) worst-case shift. */
static double run_bsearch(int P, const Op *ops, int N, long *sum) {
    Player *a = mk(P); Player **arr = xcalloc((size_t)P, sizeof *arr); long clk = P; *sum = 0;
    for (int i = 0; i < P; i++) arr[i] = &a[i];
    double t0 = cpu_ms();
    for (int i = 0; i < N; i++) {
        Player *p = &a[ops[i].idx]; int lo = 0, hi = P;
        while (lo < hi) { int m = (lo + hi) / 2; if (avl_cmp(p, arr[m]) > 0) lo = m + 1; else hi = m; }
        int old = lo;
        p->score += ops[i].delta; p->ts = ++clk;
        lo = 0; hi = old;
        while (lo < hi) { int m = (lo + hi) / 2; if (avl_cmp(p, arr[m]) < 0) hi = m; else lo = m + 1; }
        memmove(&arr[lo + 1], &arr[lo], (size_t)(old - lo) * sizeof *arr); arr[lo] = p;
        lo = 0; hi = P;                                           /* first index with score <= p->score */
        while (lo < hi) { int m = (lo + hi) / 2; if (arr[m]->score > p->score) lo = m + 1; else hi = m; }
        *sum += lo + 1;
    }
    double t = cpu_ms() - t0; free(arr); free(a); return t;
}

void bench_run(void) {
    const int P = 20000, sizes[3] = { 1000, 10000, 100000 };
    printf("\n  BENCHMARK  (%d players, each update = re-position + rank query)\n", P);
    printf("  %-9s | %11s | %13s | %15s | %-9s | %s\n", "updates", "AVL (ms)", "naive arr (ms)", "bsearch arr (ms)", "speedup", "ranks agree");
    printf("  ----------+-------------+---------------+-----------------+-----------+------------\n");
    srand(2024);
    for (int s = 0; s < 3; s++) {
        int N = sizes[s]; Op *ops = xcalloc((size_t)N, sizeof *ops);
        for (int i = 0; i < N; i++) { ops[i].idx = rand() % P; ops[i].delta = 1 + rand() % 100; }
        long c1, c2, c3;
        double t1 = run_tree(P, ops, N, &c1), t2 = run_naive(P, ops, N, &c2), t3 = run_bsearch(P, ops, N, &c3);
        printf("  %-9d | %11.2f | %13.2f | %15.2f | %7.1fx  | %s\n", N, t1, t2, t3, t1 > 0 ? t2 / t1 : 0.0,
               (c1 == c2 && c2 == c3) ? "yes (checksum)" : "MISMATCH");
        fflush(stdout); free(ops);
    }
    puts("  naive arr  = linear find + shift + linear rank  -> O(n)/update");
    puts("  bsearch arr= binary search + memmove            -> O(log n) search but O(n) shift");
    puts("  AVL        = order-statistic tree               -> O(log n) everything");
}

/* ------------------------------ self test ------------------------------ */
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 10) { printf("  FAIL: "); printf(__VA_ARGS__); putchar('\n'); } } } while (0)

static void check_all(Game *g, const long *model, int N, const char *tag) {
    int before = fails;
    for (int i = 1; i <= N; i++) {
        Player *p = ht_find(&g->ht, i);
        CHECK(p && p->score == model[i], "%s: score mismatch id %d", tag, i);
        if (!p) continue;
        int comp = 1, ord = 1;
        for (int j = 1; j <= N; j++) {
            Player *q = ht_find(&g->ht, j);
            if (model[j] > model[i]) comp++;
            if (j != i && avl_cmp(q, p) < 0) ord++;
        }
        CHECK(avl_rank_comp(g->root, p->score) == comp, "%s: competition rank id %d", tag, i);
        CHECK(avl_position(g->root, p) == ord, "%s: ordinal position id %d", tag, i);
        CHECK(avl_select(g->root, ord) == p, "%s: select(k) id %d", tag, i);
    }
    Player *top[TOP_K]; int n = heap_sorted(&g->heap, top);
    CHECK(n == (N < TOP_K ? N : TOP_K), "%s: heap size", tag);
    for (int k = 0; k < n; k++) CHECK(top[k] == avl_select(g->root, k + 1), "%s: heap vs tree at %d", tag, k + 1);
    CHECK(avl_check(g->root), "%s: AVL invariants", tag);
    printf("  [%s] %s\n", fails == before ? "PASS" : "FAIL", tag);
}

int selftest(void) {
    const int N = 600; char nm[NAME_LEN]; fails = 0; srand(12345);
    puts("\n  SELF-TEST (tree/heap/hash vs brute force; small deltas force many ties)");
    Game g; game_init(&g, 1);
    long *model = xcalloc(N + 1, sizeof(long)), *snap = xcalloc(N + 1, sizeof(long)), *snap2 = xcalloc(N + 1, sizeof(long));
    for (int i = 1; i <= N; i++) { snprintf(nm, sizeof nm, "P%d", i); game_register(&g, i, nm); }
    CHECK(game_register(&g, 5, "dup") == -1, "duplicate id must be rejected");
    size_t snapv = 0;
    for (int u = 0; u < 20000; u++) {
        int id = 1 + rand() % N, d = 1 + rand() % 4;
        game_apply(&g, id, d, 1); model[id] += d;
        if (u == 7000) { snapv = g.nlog; memcpy(snap, model, (N + 1) * sizeof(long)); }
    }
    check_all(&g, model, N, "20000 updates, ranks/select/heap");
    Game r; game_replay(&g, snapv, &r);
    int ok = avl_check(r.root);
    for (int i = 1; i <= N; i++) { Player *p = ht_find(&r.ht, i); if (!p || p->score != snap[i]) ok = 0; }
    CHECK(ok, "time machine replay mismatch");
    printf("  [%s] Time Machine replay at version %zu equals snapshot\n", ok ? "PASS" : "FAIL", snapv);
    game_free(&r);
    memcpy(snap2, model, (N + 1) * sizeof(long));
    for (int u = 0; u < 3000; u++) { int id = 1 + rand() % N, d = 1 + rand() % 4; game_apply(&g, id, d, 1); model[id] += d; }
    CHECK(game_undo(&g, 3000) == 3000, "undo count");
    memcpy(model, snap2, (N + 1) * sizeof(long));
    check_all(&g, model, N, "undo 3000 updates restores exact state");
    game_free(&g);
    /* anti-cheat */
    game_init(&g, 1); game_register(&g, 1, "Cheater"); game_register(&g, 2, "Honest");
    for (int i = 0; i < 4; i++) game_apply(&g, 1, 10, 0);
    int fl = game_apply(&g, 1, 500, 0);
    ok = (fl == 1 && ht_find(&g.ht, 1)->score == 40 && g.nquar == 1);
    game_quarantine_resolve(&g, 1);
    ok = ok && ht_find(&g.ht, 1)->score == 540 && g.nquar == 0;
    CHECK(ok, "anti-cheat quarantine/approve");
    printf("  [%s] Anti-Cheat quarantines 12x jump, approval applies it\n", ok ? "PASS" : "FAIL");
    game_free(&g);
    free(model); free(snap); free(snap2);
    printf("  %s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails != 0;
}
