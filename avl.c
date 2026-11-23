/* avl.c - order-statistic AVL tree over Player nodes.
 * KEY  = (score DESC, ts ASC, id ASC)  -> in-order traversal == leaderboard.
 * WHY AVL + subtree size:
 *   - height <= 1.44 log2 n  => insert/delete/update O(log n) worst case
 *   - size[] lets us answer "how many players are before X" (rank) and
 *     "who is k-th" (select) in O(log n); a plain BST/heap/hash cannot.
 * Nodes are the Player structs themselves (no malloc per update).      */
#include "arena.h"

int avl_cmp(const Player *a, const Player *b) {
    if (a->score != b->score) return a->score > b->score ? -1 : 1;   /* higher first   */
    if (a->ts    != b->ts)    return a->ts    < b->ts    ? -1 : 1;   /* earlier first  */
    return (a->id > b->id) - (a->id < b->id);                        /* total order    */
}

int avl_size(const Player *n)   { return n ? n->size : 0; }
int avl_height(const Player *n) { return n ? n->height : 0; }
static int mx(int a, int b) { return a > b ? a : b; }
static void upd(Player *n) {
    n->height = 1 + mx(avl_height(n->left), avl_height(n->right));
    n->size   = 1 + avl_size(n->left) + avl_size(n->right);
}
static Player *rot_r(Player *y) { Player *x = y->left;  y->left  = x->right; x->right = y; upd(y); upd(x); return x; }
static Player *rot_l(Player *x) { Player *y = x->right; x->right = y->left;  y->left  = x; upd(x); upd(y); return y; }

static Player *balance(Player *n) {
    upd(n);
    int b = avl_height(n->left) - avl_height(n->right);
    if (b > 1) {
        if (avl_height(n->left->left) < avl_height(n->left->right)) n->left = rot_l(n->left);
        return rot_r(n);
    }
    if (b < -1) {
        if (avl_height(n->right->right) < avl_height(n->right->left)) n->right = rot_r(n->right);
        return rot_l(n);
    }
    return n;
}

Player *avl_insert(Player *n, Player *p) {
    if (!n) { p->left = p->right = NULL; p->height = p->size = 1; return p; }
    if (avl_cmp(p, n) < 0) n->left = avl_insert(n->left, p);
    else                   n->right = avl_insert(n->right, p);
    return balance(n);
}

static Player *remove_min(Player *n, Player **out) {
    if (!n->left) { *out = n; return n->right; }
    n->left = remove_min(n->left, out);
    return balance(n);
}

Player *avl_delete(Player *n, const Player *key) {
    if (!n) return NULL;
    int c = avl_cmp(key, n);
    if (c < 0) n->left  = avl_delete(n->left, key);
    else if (c > 0) n->right = avl_delete(n->right, key);
    else {
        if (!n->left)  return n->right;
        if (!n->right) return n->left;
        Player *m; Player *r = remove_min(n->right, &m);   /* in-order successor */
        m->left = n->left; m->right = r;                   /* relink, no copying */
        return balance(m);
    }
    return balance(n);
}

Player *avl_select(Player *n, int k) {                     /* k-th best, 1-based */
    while (n) {
        int ls = avl_size(n->left);
        if (k <= ls) n = n->left;
        else if (k == ls + 1) return n;
        else { k -= ls + 1; n = n->right; }
    }
    return NULL;
}

int avl_position(Player *n, const Player *p) {             /* ordinal position  */
    int pos = 0;
    while (n) {
        int c = avl_cmp(p, n);
        if (c < 0) n = n->left;
        else { pos += avl_size(n->left) + 1; if (c == 0) return pos; n = n->right; }
    }
    return 0;
}

int avl_rank_comp(Player *n, long score) {                 /* 1 + #strictly higher */
    int above = 0;
    while (n) {
        if (n->score > score) { above += avl_size(n->left) + 1; n = n->right; }
        else n = n->left;
    }
    return above + 1;
}

static int chk(Player *n, Player *lo, Player *hi, int *ok) {
    if (!n) return 0;
    if ((lo && avl_cmp(lo, n) >= 0) || (hi && avl_cmp(n, hi) >= 0)) *ok = 0;
    int hl = chk(n->left, lo, n, ok), hr = chk(n->right, n, hi, ok);
    if (n->height != 1 + mx(hl, hr) || hl - hr > 1 || hr - hl > 1) *ok = 0;
    if (n->size != 1 + avl_size(n->left) + avl_size(n->right)) *ok = 0;
    return n->height;
}
int avl_check(Player *root) { int ok = 1; chk(root, NULL, NULL, &ok); return ok; }
