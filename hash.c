/* hash.c - separate-chaining hash table: player ID -> Player*
 * WHY: every update/lookup arrives with an ID. The tree is keyed by
 * (score,ts,id), so without this table finding a player would need an
 * O(n) scan. Chaining + doubling (load factor <= 1) gives expected O(1). */
#include "arena.h"

static size_t hf(int id, size_t nb) { return ((uint32_t)id * 2654435761u) % nb; }

void ht_init(HashTable *h, size_t nb) {
    h->nb = nb; h->n = 0; h->b = xcalloc(nb, sizeof(Player *));
}

static void ht_resize(HashTable *h) {            /* amortised O(1) per insert */
    size_t nn = h->nb * 2;
    Player **nb = xcalloc(nn, sizeof(Player *));
    for (size_t i = 0; i < h->nb; i++)
        for (Player *p = h->b[i]; p;) {
            Player *next = p->hnext; size_t k = hf(p->id, nn);
            p->hnext = nb[k]; nb[k] = p; p = next;
        }
    free(h->b); h->b = nb; h->nb = nn;
}

Player *ht_find(const HashTable *h, int id) {
    for (Player *p = h->b[hf(id, h->nb)]; p; p = p->hnext)
        if (p->id == id) return p;
    return NULL;
}

int ht_insert(HashTable *h, Player *p) {
    if (ht_find(h, p->id)) return -1;            /* IDs are unique */
    if (h->n >= h->nb) ht_resize(h);
    size_t k = hf(p->id, h->nb);
    p->hnext = h->b[k]; h->b[k] = p; h->n++;
    return 0;
}

size_t ht_max_chain(const HashTable *h) {
    size_t best = 0;
    for (size_t i = 0; i < h->nb; i++) {
        size_t c = 0; for (Player *p = h->b[i]; p; p = p->hnext) c++;
        if (c > best) best = c;
    }
    return best;
}

void ht_free(HashTable *h) {
    for (size_t i = 0; i < h->nb; i++)
        for (Player *p = h->b[i]; p;) { Player *n = p->hnext; free(p); p = n; }
    free(h->b); h->b = NULL; h->nb = h->n = 0;
}
