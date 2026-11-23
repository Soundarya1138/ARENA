/* main.c - menu-driven front end for Arena Ascent. */
#include "arena.h"

static int ask(const char *prompt, char *buf, size_t n) {
    printf("%s", prompt); fflush(stdout);
    if (!fgets(buf, (int)n, stdin)) return 0;
    buf[strcspn(buf, "\r\n")] = 0; return 1;
}
static long ask_long(const char *prompt, long def) {
    char b[64]; if (!ask(prompt, b, sizeof b) || !*b) return def;
    return strtol(b, NULL, 10);
}

static void show_rank(Game *g, int id) {
    RankInfo ri;
    if (!game_rank_info(g, id, &ri)) { puts("  No such player."); return; }
    printf("\n  %s (id %d)  score %ld\n", ri.p->name, ri.p->id, ri.p->score);
    printf("  Rank: #%d of %d   (strict position %d, ties broken by who reached the score first)\n", ri.comp, ri.n, ri.ord);
    printf("  Percentile: top %.2f%%   Badge: %s   Momentum: %s\n", 100.0 * ri.comp / ri.n, ri.tier, ri.momentum);
    game_radar(g, id);
}

static void time_machine(Game *g) {
    if (!g->nlog) { puts("  Nothing logged yet."); return; }
    printf("  Log has %zu events (version 0 = empty arena, %zu = now).\n", g->nlog, g->nlog);
    long v = ask_long("  Rewind to version [default: halfway]: ", (long)(g->nlog / 2));
    if (v < 0) v = 0;
    if ((size_t)v > g->nlog) v = (long)g->nlog;
    Game past; game_replay(g, (size_t)v, &past);                    /* rebuild from the log: O(v log n) */
    char title[96]; snprintf(title, sizeof title, "TIME MACHINE - Top 10 at version %ld (%d players)", v, past.nplayers);
    game_print_top(&past, g, title);
    game_free(&past);
}

static void stats_menu(Game *g) {
    game_stats(g);
    if (g->nquar) {
        printf("\n  Quarantined updates awaiting review:\n");
        for (size_t i = 0; i < g->nquar && i < 10; i++) {
            Player *p = ht_find(&g->ht, g->quar[i].id);
            printf("    %s  +%ld\n", p ? p->name : "?", g->quar[i].delta);
        }
        char b[16]; ask("  [a]pprove all / [r]eject all / Enter to skip: ", b, sizeof b);
        if (b[0] == 'a' || b[0] == 'A') game_quarantine_resolve(g, 1);
        else if (b[0] == 'r' || b[0] == 'R') game_quarantine_resolve(g, 0);
    }
}

int main(int argc, char **argv) {
    term_init(); setvbuf(stdout, NULL, _IOFBF, 1 << 16); srand(42);
    if (argc > 1 && !strcmp(argv[1], "--selftest")) return selftest();
    if (argc > 1 && !strcmp(argv[1], "--bench")) { bench_run(); return 0; }
    Game g; game_init(&g, 1);
    if (argc > 1 && !strcmp(argv[1], "--demo")) {                    /* non-interactive showcase */
        anim_simulate(&g, 5000, argc > 2 ? atoi(argv[2]) : 100, 800, 40);
        game_stats(&g); game_free(&g); return 0;
    }
    char line[128], name[64];
    for (;;) {
        printf("\n\033[1;96m  === ARENA ASCENT : Real-Time Leaderboard Engine ===\033[0m\n"
               "   1) Register player        6) Time Machine replay\n"
               "   2) Update score           7) Stats (+ anti-cheat review)\n"
               "   3) Show Top 10            8) Undo last N updates\n"
               "   4) Find rank + radar      9) Benchmark (1K/10K/100K)\n"
               "   5) Start simulation       0) Exit\n");
        if (!ask("  > ", line, sizeof line)) break;
        switch (atoi(line)) {
        case 1: {
            int id = (int)ask_long("  Player ID: ", 0);
            if (id <= 0) { puts("  ID must be a positive integer."); break; }
            ask("  Name: ", name, sizeof name);
            puts(game_register(&g, id, name[0] ? name : "Anon") == 0 ? "  Registered." : "  ID already exists.");
            break; }
        case 2: {
            int id = (int)ask_long("  Player ID: ", 0); long d = ask_long("  Score change (+/-): ", 0);
            game_submit(&g, id, d); game_process(&g, g.q.n);        /* enqueue, then drain the batch */
            if (!ht_find(&g.ht, id)) puts("  No such player.");
            else if (g.nquar && g.flag_seq && g.quar[g.nquar - 1].id == id && g.quar[g.nquar - 1].delta == d)
                printf("  \033[1;31m%s\033[0m\n  (use Stats to approve/reject)\n", g.last_flag);
            else show_rank(&g, id);
            break; }
        case 3: game_print_top(&g, NULL, "TOP 10"); break;
        case 4: show_rank(&g, (int)ask_long("  Player ID: ", 0)); break;
        case 5: {
            int bots = (int)ask_long("  Bots [5000]: ", 5000), ticks = (int)ask_long("  Ticks, 0 = until 'q' [300]: ", 300);
            int per = (int)ask_long("  Updates per tick [800]: ", 800);
            if (bots < 20) bots = 20;
            anim_simulate(&g, bots, ticks, per, 25); break; }
        case 6: time_machine(&g); break;
        case 7: stats_menu(&g); break;
        case 8: { int n = (int)ask_long("  Undo how many updates? ", 1); printf("  Undone: %d\n", game_undo(&g, n)); break; }
        case 9: bench_run(); break;
        case 0: game_free(&g); return 0;
        default: puts("  Unknown option.");
        }
    }
    game_free(&g); return 0;
}
