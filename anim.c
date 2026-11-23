/* anim.c - ANSI terminal renderer + bot simulator (the "game" layer). */
#include "arena.h"
#include <math.h>
#include <signal.h>

#define RST "\033[0m"
#define BLD "\033[1m"
#define DIM "\033[2m"
static volatile sig_atomic_t g_stop;
static void on_sigint(int s) { (void)s; g_stop = 1; }

static void print_bar(double v, double scale, int width) {       /* sub-cell smooth bars */
    static const char *part[] = { "", "\xE2\x96\x8F", "\xE2\x96\x8E", "\xE2\x96\x8D", "\xE2\x96\x8C",
                                  "\xE2\x96\x8B", "\xE2\x96\x8A", "\xE2\x96\x89" };
    int e = (int)(v / scale * width * 8 + 0.5);
    if (e > width * 8) e = width * 8;
    int full = e / 8, rem = e % 8, used = full + (rem ? 1 : 0);
    for (int i = 0; i < full; i++) fputs("\xE2\x96\x88", stdout);
    fputs(part[rem], stdout);
    for (; used < width; used++) putchar(' ');
}

static void draw_frame(Game *g, AnimState *a, int animate) {
    Player *top[TOP_K]; int n = heap_sorted(&g->heap, top);        /* Top-10 from the min-heap */
    if (n > 0) {
        if (a->champ && top[0]->id != a->champ) {                  /* new #1 */
            a->banner = 8; a->overtakes++;
            snprintf(a->banner_name, NAME_LEN, "%s", top[0]->name);
        }
        a->champ = top[0]->id;
    }
    printf("\033[H" BLD "\033[96m ARENA ASCENT" RST DIM "  real-time leaderboard   " RST
           "tick %-5d players %-6d queue peak %-6zu overtakes %-3d\033[K\n", a->tick, g->nplayers, g->q.peak, a->overtakes);
    if (a->banner > 0) {
        printf(a->banner % 2 ? "\033[1;97;41m" : "\033[1;30;103m");
        printf("  \xE2\x9A\xA1 OVERTAKE!  %-12s takes #1  \xE2\x9A\xA1  " RST "\033[K\n", a->banner_name);
        a->banner--;
    } else printf("\033[K\n");
    printf(DIM "  RANK  MOVE  PLAYER        TOP%%     SCORE  " RST "\033[K\n");
    double mx = 1; 
    for (int i = 0; i < n; i++) {
        Player *p = top[i];
        if (animate) { p->disp += (p->score - p->disp) * 0.25; if (fabs(p->score - p->disp) < 0.5) p->disp = (double)p->score; }
        else p->disp = (double)p->score;
        if (p->disp > mx) mx = p->disp;
    }
    static const char *col[] = { "\033[1;93m", "\033[1;97m", "\033[33m" };
    for (int i = 0; i < TOP_K; i++) {
        if (i >= n) { printf("\033[K\n"); continue; }
        Player *p = top[i]; char mv[48]; int d = p->shown_pos ? p->shown_pos - (i + 1) : 0;
        if (!p->shown_pos) snprintf(mv, sizeof mv, "\033[32m\xE2\x96\xB2NEW" RST);
        else if (d > 0)    snprintf(mv, sizeof mv, "\033[1;32m\xE2\x96\xB2%-3d" RST, d);
        else if (d < 0)    snprintf(mv, sizeof mv, "\033[1;31m\xE2\x96\xBC%-3d" RST, -d);
        else               snprintf(mv, sizeof mv, DIM "\xC2\xB7   " RST);
        const char *c = i < 3 ? col[i] : "\033[36m";
        printf("  %s%2d" RST "    %s  %-12s %5.2f%%  %8ld  %s", c, avl_rank_comp(g->root, p->score), mv, p->name,
               100.0 * avl_rank_comp(g->root, p->score) / g->nplayers, p->score, c);
        print_bar(p->disp, mx, 30);
        printf(RST "\033[K\n");
    }
    for (int i = 0; i < a->nprev; i++) { Player *p = ht_find(&g->ht, a->prev[i]); if (p) p->shown_pos = 0; }
    for (int i = 0; i < n; i++) { top[i]->shown_pos = i + 1; a->prev[i] = top[i]->id; }
    a->nprev = n;
    printf("\033[K\n" DIM "  batch: %zu updates in %.2f ms  (%.1f M upd/s)   applied %ld   undo-stack %zu   quarantined %zu\033[K\n" RST,
           a->batch_n, a->batch_ms, a->batch_ms > 0 ? a->batch_n / a->batch_ms / 1000.0 : 0.0,
           g->total_updates, g->undo.n, g->nquar);
    if (g->flag_seq != a->seen_flag) { a->seen_flag = g->flag_seq; a->flag_age = 40; }
    if (a->flag_age > 0) { printf("  \033[1;31m\xE2\x9A\xA0 %s" RST "\033[K\n", g->last_flag); a->flag_age--; }
    else printf("\033[K\n");
    printf(DIM "  keys: [q] quit  [b] burst 5000 updates  [c] inject cheater" RST "\033[K\n\033[J");
    fflush(stdout);
}

void anim_simulate(Game *g, int bots, int ticks, int per_tick, int fps) {
    int *skill = xcalloc((size_t)bots, sizeof(int));
    char name[NAME_LEN];
    for (int i = 0; i < bots; i++) {
        snprintf(name, sizeof name, "Bot_%04d", i + 1);
        game_register(g, SIM_BASE_ID + i, name);                  /* skips if already present */
        skill[i] = 1 + rand() % 5;
    }
    AnimState a; memset(&a, 0, sizeof a);
    a.seen_flag = g->flag_seq;
    int hot_id = 0, hot_left = 0, frame_ms = 1000 / (fps > 0 ? fps : 25);
    g_stop = 0; signal(SIGINT, on_sigint); term_raw(1);
    fputs("\033[?25l\033[2J", stdout);
    for (int t = 0; (ticks <= 0 || t < ticks) && !g_stop; t++) {
        double t0 = now_ms(); a.tick = t;
        int k = key_poll(), burst = 0;
        if (k == 'q' || k == 'Q') break;
        if (k == 'b') burst = 5000;
        if (k == 'c' || (rand() % 100) < 3) {                       /* cheater: huge jump from a top player */
            Player *v = avl_select(g->root, 1 + rand() % 30);
            if (v) game_submit(g, v->id, 20000 + rand() % 5000);
        }
        for (int i = 0; i < per_tick + burst; i++) {                 /* producers only enqueue: O(1) each */
            int b = rand() % bots;
            game_submit(g, SIM_BASE_ID + b, 1 + rand() % (12 * skill[b]));
        }
        if (hot_left > 0) {                                           /* a challenger on a hot streak */
            Player *h = ht_find(&g->ht, hot_id), *t1 = avl_select(g->root, 1);
            long gap = t1->score - h->score; hot_left--;
            int u = (int)((gap * 1.25 / (hot_left + 1)) / 25) + 1; if (u > 600) u = 600;
            for (int j = 0; j < u; j++) game_submit(g, hot_id, 1 + rand() % 50);
        } else if (g->nplayers > 20 && rand() % 100 < 6) {
            Player *h = avl_select(g->root, 2 + rand() % 7); hot_id = h->id; hot_left = 4 + rand() % 6;
        }
        double p0 = now_ms(); size_t done = 0;
        while (g->q.n) done += game_process(g, 4096);                 /* batch processing */
        a.batch_ms = now_ms() - p0; a.batch_n = done;
        draw_frame(g, &a, 1);
        double spent = now_ms() - t0;
        if (spent < frame_ms) sleep_ms((int)(frame_ms - spent));
    }
    signal(SIGINT, SIG_DFL); term_raw(0);
    fputs("\033[?25h\n", stdout); fflush(stdout);
    free(skill);
}
