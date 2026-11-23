/* queue.c - growable circular FIFO of pending score updates.
 * WHY: producers (bots/network) just enqueue in O(1); the engine drains it
 * in batches, so bursts of thousands of updates are applied in arrival
 * order (FIFO keeps timestamps fair) without blocking the UI.            */
#include "arena.h"

void q_init(Queue *q, size_t cap) {
    q->buf = xcalloc(cap, sizeof(Update)); q->cap = cap; q->head = q->n = q->peak = 0;
}
void q_push(Queue *q, Update u) {
    if (q->n == q->cap) {                              /* amortised O(1) growth */
        Update *nb = xcalloc(q->cap * 2, sizeof(Update));
        for (size_t i = 0; i < q->n; i++) nb[i] = q->buf[(q->head + i) % q->cap];
        free(q->buf); q->buf = nb; q->cap *= 2; q->head = 0;
    }
    q->buf[(q->head + q->n) % q->cap] = u; q->n++;
    if (q->n > q->peak) q->peak = q->n;
}
int q_pop(Queue *q, Update *out) {
    if (!q->n) return 0;
    *out = q->buf[q->head]; q->head = (q->head + 1) % q->cap; q->n--;
    return 1;
}
void q_free(Queue *q) { free(q->buf); q->buf = NULL; q->n = q->cap = 0; }
