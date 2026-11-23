/* stack.c - bounded LIFO of applied updates, powering "undo last N".
 * WHY: undo is naturally last-in-first-out. Each record stores the old
 * (score,ts) so undo is an O(log n) tree re-key, not a recompute. It is a
 * ring: when full, the oldest record is dropped so memory stays bounded. */
#include "arena.h"

void st_init(UndoStack *s, size_t cap) { s->a = xcalloc(cap, sizeof(UndoRec)); s->cap = cap; s->start = s->n = 0; }
void st_push(UndoStack *s, UndoRec r) {
    if (s->n == s->cap) { s->start = (s->start + 1) % s->cap; s->n--; }
    s->a[(s->start + s->n) % s->cap] = r; s->n++;
}
int st_pop(UndoStack *s, UndoRec *out) {
    if (!s->n) return 0;
    s->n--; *out = s->a[(s->start + s->n) % s->cap]; return 1;
}
void st_free(UndoStack *s) { free(s->a); s->a = NULL; s->n = s->cap = 0; }
