/* heap.c - fixed-size (K=10) MIN-heap of the current Top-10.
 * Root = the *weakest* of the top 10, so a newcomer only has to beat the
 * root (O(1) test, O(log 10) replace). Because a player's score normally
 * only rises, the heap is maintained incrementally without touching the
 * tree. If a heap member's score DROPS (penalty/undo) the heap may be
 * missing the true 11th player, so we refill from the tree (O(K log n)). */
#include "arena.h"

static void swp(MinHeap *h, int i, int j) {
    Player *t = h->a[i]; h->a[i] = h->a[j]; h->a[j] = t;
    h->a[i]->hpos = i; h->a[j]->hpos = j;
}
static void up(MinHeap *h, int i) {
    while (i > 0) {
        int pa = (i - 1) / 2;
        if (avl_cmp(h->a[i], h->a[pa]) > 0) { swp(h, i, pa); i = pa; } else break;
    }
}
static void down(MinHeap *h, int i) {
    for (;;) {
        int l = 2 * i + 1, r = l + 1, w = i;      /* w = "worst" of i,l,r */
        if (l < h->n && avl_cmp(h->a[l], h->a[w]) > 0) w = l;
        if (r < h->n && avl_cmp(h->a[r], h->a[w]) > 0) w = r;
        if (w == i) break;
        swp(h, i, w); i = w;
    }
}

void heap_init(MinHeap *h) { h->n = 0; }

void heap_offer(MinHeap *h, Player *p) {
    if (p->hpos >= 0) { up(h, p->hpos); down(h, p->hpos); return; }  /* key changed */
    if (h->n < TOP_K) { h->a[h->n] = p; p->hpos = h->n++; up(h, p->hpos); }
    else if (avl_cmp(p, h->a[0]) < 0) {                              /* beats weakest */
        h->a[0]->hpos = -1; h->a[0] = p; p->hpos = 0; down(h, 0);
    }
}

void heap_rebuild(MinHeap *h, Player *root) {
    for (int i = 0; i < h->n; i++) h->a[i]->hpos = -1;
    int n = avl_size(root) < TOP_K ? avl_size(root) : TOP_K;
    h->n = n;
    for (int i = 0; i < n; i++) {            /* worst-first array is a valid heap */
        h->a[i] = avl_select(root, n - i); h->a[i]->hpos = i;
    }
}

int heap_sorted(const MinHeap *h, Player **out) {
    for (int i = 0; i < h->n; i++) {
        Player *p = h->a[i]; int j = i - 1;   /* insertion sort, n <= 10 */
        while (j >= 0 && avl_cmp(out[j], p) > 0) { out[j + 1] = out[j]; j--; }
        out[j + 1] = p;
    }
    return h->n;
}
