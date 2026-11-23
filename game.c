/* game.c - the leaderboard engine: glues hash + AVL + heap + queue + stack
 * and implements Time Machine, Rivalry Radar, Momentum, Anti-Cheat, Badges. */
#include "arena.h"
#include <math.h>

#define RST "\033[0m"
#define BLD "\033[1m"
#define DIM "\033[2m"
#define RED "\033[31m"
#define GRN "\033[32m"
#define YEL "\033[33m"
#define CYN "\033[36m"

static void log_add(Game *g, char kind, int id, long score, long ts) {
    if (g->nlog == g->caplog) {
        g->caplog = g->caplog ? g->caplog * 2 : 4096;
        g->log = xrealloc(g->log, g->caplog * sizeof *g->log);
    }
    g->log[g->nlog++] = (LogEntry){ .score = score, .ts = ts, .id = id, .kind = kind };
}

void game_init(Game *g, int record) {
    memset(g, 0, sizeof *g);
    g->record = record;
    ht_init(&g->ht, 1024); heap_init(&g->heap);
    q_init(&g->q, 1024); st_init(&g->undo, record ? UNDO_CAP : 1);
}

void game_free(Game *g) {
    ht_free(&g->ht); q_free(&g->q); st_free(&g->undo);
    free(g->log); free(g->quar);
    memset(g, 0, sizeof *g);
}

static int reg_at(Game *g, int id, const char *name, long ts) {
    Player *p = xcalloc(1, sizeof *p);
    p->id = id; p->ts = ts; p->hpos = -1;
    snprintf(p->name, NAME_LEN, "%s", name);
    if (ht_insert(&g->ht, p) != 0) { free(p); return -1; }
    g->root = avl_insert(g->root, p);
    g->nplayers++;
    heap_offer(&g->heap, p);
    if (g->record) log_add(g, 'R', id, 0, ts);
    return 0;
}
int game_register(Game *g, int id, const char *name) { return reg_at(g, id, name, ++g->clock); }

/* Re-key a player in the tree and keep the Top-10 heap consistent. */
static void do_set(Game *g, Player *p, long ns, long nts) {
    long os = p->score, ots = p->ts;
    g->root = avl_delete(g->root, p);              /* must use the OLD key */
    p->score = ns; p->ts = nts;
    g->root = avl_insert(g->root, p);
    if ((ns > os) || (ns == os && nts < ots)) heap_offer(&g->heap, p);   /* improved */
    else if (p->hpos >= 0) heap_rebuild(&g->heap, g->root);             /* member got worse */
}

static void hist_push(Player *p, int r) {
    if (p->hist_n < MOM_N) p->hist[(p->hist_head + p->hist_n++) % MOM_N] = r;
    else { p->hist[p->hist_head] = r; p->hist_head = (p->hist_head + 1) % MOM_N; }
}

static void quarantine(Game *g, Player *p, long delta) {
    if (g->nquar == g->capquar) {
        g->capquar = g->capquar ? g->capquar * 2 : 64;
        g->quar = xrealloc(g->quar, g->capquar * sizeof *g->quar);
    }
    g->quar[g->nquar++] = (Quar){ p->id, delta };
    g->total_flagged++; g->flag_seq++;
    snprintf(g->last_flag, sizeof g->last_flag,
             "ANTI-CHEAT: %s +%ld (avg gain %ld) quarantined", p->name, delta, p->gain_sum / p->gain_cnt);
}

int game_apply(Game *g, int id, long delta, int force) {
    Player *p = ht_find(&g->ht, id);                         /* O(1) */
    if (!p) return -1;
    if (!force && delta >= CHEAT_MIN_GAIN && p->gain_cnt >= CHEAT_MIN_HISTORY &&
        (double)delta > CHEAT_FACTOR * ((double)p->gain_sum / p->gain_cnt)) {
        quarantine(g, p, delta);                             /* held for review */
        return 1;
    }
    long os = p->score, ns = os + delta;
    if (ns < 0) ns = 0;
    long eff = ns - os;
    if (eff == 0) return 0;                                  /* no change => keep old timestamp */
    long ots = p->ts, nts = ++g->clock;
    do_set(g, p, ns, nts);                                   /* O(log n) */
    if (eff > 0) { p->gain_sum += eff; p->gain_cnt++; }
    if (g->record) {
        st_push(&g->undo, (UndoRec){ id, eff, os, ots, ns, nts });
        log_add(g, 'U', id, ns, nts);
    }
    hist_push(p, avl_rank_comp(g->root, ns));                /* O(log n) rank for momentum */
    g->total_updates++;
    return 0;
}

void game_submit(Game *g, int id, long delta) { q_push(&g->q, (Update){ id, delta }); }

size_t game_process(Game *g, size_t max) {
    Update u; size_t done = 0;
    while (done < max && q_pop(&g->q, &u)) { game_apply(g, u.id, u.delta, 0); done++; }
    return done;
}

int game_undo(Game *g, int n) {
    UndoRec r; int done = 0;
    while (done < n && st_pop(&g->undo, &r)) {
        Player *p = ht_find(&g->ht, r.id);
        if (!p) continue;
        do_set(g, p, r.old_score, r.old_ts);
        if (r.delta > 0) { p->gain_sum -= r.delta; p->gain_cnt--; }
        if (p->hist_n > 0) p->hist_n--;
        if (g->record) log_add(g, 'D', r.id, r.old_score, r.old_ts);
        g->total_undone++; done++;
    }
    return done;
}

void game_quarantine_resolve(Game *g, int approve) {
    size_t n = g->nquar; g->nquar = 0;
    Quar *tmp = xcalloc(n ? n : 1, sizeof *tmp);
    memcpy(tmp, g->quar, n * sizeof *tmp);
    for (size_t i = 0; i < n; i++) {
        if (approve) { game_apply(g, tmp[i].id, tmp[i].delta, 1); g->total_approved++; }
        else g->total_rejected++;
    }
    free(tmp);
}

/* ------------------- ranking features ------------------- */
static const char *tier_name(double pct) {
    if (pct >= 99.0) return "Legend \xF0\x9F\x91\x91";
    if (pct >= 90.0) return "Gold \xF0\x9F\xA5\x87";
    if (pct >= 60.0) return "Silver \xF0\x9F\xA5\x88";
    return "Bronze \xF0\x9F\xA5\x89";
}

static const char *momentum_label(const Game *g, const Player *p) {
    if (p->hist_n < 2) return "Steady";
    int oldest = p->hist[p->hist_head], newest = p->hist[(p->hist_head + p->hist_n - 1) % MOM_N];
    int thr = g->nplayers / 100; if (thr < 3) thr = 3;       /* ~1% of the field */
    int d = oldest - newest;                                  /* >0 means climbed */
    if (d >= thr)  return "Rising Star \xF0\x9F\x94\xA5";
    if (d <= -thr) return "Falling";
    return "Steady";
}

int game_rank_info(const Game *g, int id, RankInfo *ri) {
    Player *p = ht_find(&g->ht, id);
    if (!p) return 0;
    ri->p = p; ri->n = g->nplayers;
    ri->comp = avl_rank_comp(g->root, p->score);             /* ties share a rank (1,2,2,4) */
    ri->ord  = avl_position(g->root, p);                     /* strict position (timestamp tie-break) */
    ri->pct  = ri->n > 1 ? 100.0 * (ri->n - ri->comp) / (ri->n - 1) : 100.0;
    ri->tier = tier_name(ri->pct);
    ri->momentum = momentum_label(g, p);
    return 1;
}

void game_radar(const Game *g, int id) {
    Player *p = ht_find(&g->ht, id);
    if (!p) { puts("Unknown player."); return; }
    int pos = avl_position(g->root, p), n = g->nplayers;
    printf(BLD "\n  RIVALRY RADAR" RST " - %s\n", p->name);
    for (int k = pos - 3; k <= pos + 3; k++) {
        if (k < 1 || k > n) continue;
        Player *q = avl_select(g->root, k);                   /* O(log n) each */
        printf("  %s%s pos %-6d rank %-6d %-12s id %-7d %10ld%s\n", k == pos ? BLD YEL : "",
               k == pos ? ">>" : "  ", k, avl_rank_comp(g->root, q->score), q->name, q->id, q->score, RST);
    }
    if (pos > 1) {
        Player *a = avl_select(g->root, pos - 1);
        printf("  " GRN "+%ld points" RST " to overtake %s (position %d)%s\n", a->score - p->score + 1, a->name, pos - 1,
               a->score == p->score ? "  [tied score, they reached it first]" : "");
    } else if (n > 1) {
        Player *b = avl_select(g->root, 2);
        printf("  " YEL "You are #1." RST " Lead over %s: %ld points.\n", b->name, p->score - b->score);
    }
}

void game_print_top(const Game *g, const Game *now, const char *title) {
    Player *top[TOP_K]; int n = heap_sorted(&g->heap, top);   /* top-10 straight from the heap */
    printf(BLD "\n  %s\n" RST, title);
    printf(DIM "  %-4s %-12s %-7s %10s  %-10s%s\n" RST, "Rank", "Player", "ID", "Score", "Tier", now ? "  Now" : "");
    for (int i = 0; i < n; i++) {
        double pct = g->nplayers > 1 ? 100.0 * (g->nplayers - avl_rank_comp(g->root, top[i]->score)) / (g->nplayers - 1) : 100.0;
        printf("  %-4d %-12s %-7d %10ld  %-10s", avl_rank_comp(g->root, top[i]->score), top[i]->name, top[i]->id, top[i]->score, tier_name(pct));
        if (now) { Player *c = ht_find(&now->ht, top[i]->id); if (c) printf("  #%d", avl_rank_comp(now->root, c->score)); }
        putchar('\n');
    }
    if (!n) puts("  (no players yet)");
}

/* ------------------- Time Machine ------------------- */
void game_replay(const Game *live, size_t upto, Game *out) {
    game_init(out, 0);
    if (upto > live->nlog) upto = live->nlog;
    for (size_t i = 0; i < upto; i++) {
        const LogEntry *e = &live->log[i];
        if (e->kind == 'R') {
            Player *src = ht_find(&live->ht, e->id);
            reg_at(out, e->id, src ? src->name : "?", e->ts);
        } else {
            Player *p = ht_find(&out->ht, e->id);
            if (p) do_set(out, p, e->score, e->ts);          /* log stores absolute state */
        }
    }
}

void game_stats(const Game *g) {
    printf(BLD "\n  ENGINE STATS\n" RST);
    printf("  players            : %d\n", g->nplayers);
    printf("  updates applied    : %ld   (undone: %ld, log entries: %zu)\n", g->total_updates, g->total_undone, g->nlog);
    printf("  anti-cheat flagged : %ld   (pending: %zu, approved: %ld, rejected: %ld)\n", g->total_flagged, g->nquar, g->total_approved, g->total_rejected);
    printf("  AVL height         : %d  (log2 n = %.1f, worst-case bound %.1f)  valid: %s\n", avl_height(g->root),
           g->nplayers ? log2((double)g->nplayers) : 0.0, g->nplayers ? 1.44 * log2((double)g->nplayers + 2) : 0.0,
           avl_check(g->root) ? "yes" : "NO");
    printf("  hash table         : %zu buckets, load %.2f, longest chain %zu\n", g->ht.nb, (double)g->ht.n / g->ht.nb, ht_max_chain(&g->ht));
    printf("  queue              : %zu pending, peak %zu\n", g->q.n, g->q.peak);
    printf("  undo stack         : %zu records (cap %d)\n", g->undo.n, UNDO_CAP);
    printf("  Top-10 heap        : %d members, weakest = %s\n", g->heap.n, g->heap.n ? g->heap.a[0]->name : "-");
}
