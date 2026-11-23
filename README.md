# Arena Ascent: Real-Time Leaderboard Engine

A terminal-animated leaderboard in C11. It uses an order-statistic AVL tree, a chained hash table, a size-10 min-heap, a queue and a stack to keep 5,000+ simulated players ranked in real time.

```
make            # builds ./arena  (gcc -Wall -Wextra, no warnings)
./arena         # interactive menu
./arena --demo 150   # non-interactive 5000-bot animated run
./arena --selftest   # randomised correctness proof vs brute force
./arena --bench      # 1K / 10K / 100K benchmark table
make memcheck        # valgrind if installed, else AddressSanitizer + LeakSanitizer
```
Windows: `gcc -std=c11 -O2 *.c -o arena.exe -lm` (MinGW). `platform.c` switches to `Sleep()` and `_kbhit()` and enables ANSI/UTF-8 in the console. Use Windows Terminal or Windows 10+ conhost. On Linux and macOS it uses `usleep()` and termios.

## Files

| File | Role |
|---|---|
| `arena.h` | shared types (`Player` is intrusive: one struct is hash node, AVL node and heap slot) and prototypes |
| `hash.c` | chaining hash table: ID to `Player*` |
| `avl.c` | order-statistic AVL: key `(score desc, ts asc, id asc)`, `rank`, `select`, `position`, `check` |
| `heap.c` | size-10 min-heap for the Top 10 |
| `queue.c` | circular FIFO buffering score updates (batch processing) |
| `stack.c` | bounded undo stack |
| `game.c` | engine: apply/undo, Anti-Cheat, Momentum, Percentile badge, Rivalry Radar, Time Machine, stats |
| `anim.c` | ANSI renderer and the 5,000-bot simulator |
| `bench.c` | Benchmark Mode (AVL vs naive arrays) and the randomised self-test |
| `platform.c` | OS guard: `Sleep`/`usleep`, non-blocking key read, ANSI enable |
| `main.c`, `Makefile` | menu and build |

## Data structures and why

| Structure | Job | Why this one |
|---|---|---|
| **Hash table (chaining)** | ID to player | Every update arrives by ID, but the tree is ordered by score. Without the table, finding a player is O(n). Chaining and doubling at load 1.0 give expected O(1). |
| **Order-statistic AVL** | the ranked set | Height is at most 1.44 log n, so insert, delete and re-key are O(log n) worst case. Subtree `size` gives rank and k-th in O(log n). |
| **Min-heap (K=10)** | live Top 10 | A newcomer is compared only against the root (the 10th best): O(1) to reject, O(log 10) to admit. The board redraws from the heap with no tree walk. |
| **Queue** | buffer updates | Producers enqueue in O(1). The engine drains in FIFO batches, which keeps timestamps in arrival order. |
| **Stack** | undo | Undo is last-in-first-out. Each record stores the old `(score, ts)`, so undoing is one O(log n) re-key. |

## Complexity table

n = players, K = 10, v = log entries replayed.

| Operation | Time | Notes |
|---|---|---|
| Register player | O(log n) | O(1) expected hash insert, O(log n) AVL insert, O(log K) heap offer |
| Find player by ID | O(1) expected | hash lookup |
| Update score | O(log n) | AVL delete and insert, heap offer, O(log n) rank for momentum |
| Competition rank of a player | **O(log n)** | one root-to-leaf walk summing left-subtree sizes |
| Strict position (tie-broken) | O(log n) | same walk using the full key |
| k-th player (`select`) | O(log n) | |
| Top 10 display | O(K log K) = O(1) | sort 10 heap items |
| Heap repair when a Top-10 member loses points | O(K log n) | refill from the tree; rare, since scores mostly rise |
| Rivalry Radar (3 above, 3 below, gap) | O(log n) | 7 `select` calls |
| Rank Momentum | O(1) to read, O(log n) to record | ring of last N=5 ranks |
| Percentile badge | O(log n) | derived from the rank |
| Anti-Cheat check | O(1) | running sum and count per player |
| Enqueue / dequeue | O(1) amortised | |
| Process a batch of B updates | O(B log n) | |
| Undo last N | O(N log n) | |
| Time Machine to version v | O(v log n) | rebuild from the log into a temporary board |
| Memory | O(n + log size + undo cap) | no allocation per update |

## Ranking rules

* Order is **score descending, then timestamp ascending, then ID**. The timestamp is a logical clock, so whoever reached a score first sits above players who tie with them later. An update that changes nothing (delta 0) keeps its old timestamp.
* **Competition ranking** (1, 2, 2, 4) is displayed. A player's rank is 1 + the number of players with a strictly higher score, found in O(log n). The strict position (1..n) is shown beside it.
* Dense ranking (1, 2, 2, 3) would need a second order-statistic tree keyed by distinct score, with counts. The design extends to it, but it is not implemented.

## Unique features

* **Time Machine:** every register, update and undo is logged as an absolute `(id, score, ts)`. The state at any version v is rebuilt into a temporary `Game` and compared with the live ranks.
* **Rivalry Radar:** shows 3 above and 3 below, plus the exact points to overtake the player above, including `+1` when tied but later by timestamp.
* **Momentum:** a ring of the last 5 ranks per player. The label is "Rising Star 🔥" if the player climbed at least max(3, 1% of n) places, "Falling" if they dropped that many, otherwise "Steady".
* **Anti-Cheat Guard:** a gain of at least 50 and more than 5x the player's average gain (after 3 or more gains) is quarantined. Stats lets you approve or reject.
* **Percentile Badge:** Legend (99th percentile or above), Gold (90th), Silver (60th), Bronze (below that).
* **Benchmark Mode:** a timing table with a checksum proving all three methods produce identical ranks.

## Sample runs

### Self-test (all pass; also clean under AddressSanitizer + LeakSanitizer)
```
  [PASS] 20000 updates, ranks/select/heap
  [PASS] Time Machine replay at version 7601 equals snapshot
  [PASS] undo 3000 updates restores exact state
  [PASS] Anti-Cheat quarantines 12x jump, approval applies it
  ALL TESTS PASSED
```

### Benchmark (`./arena --bench`, 20,000 players, each update = re-position + rank query)
```
  updates   |    AVL (ms) | naive arr (ms) | bsearch arr (ms) | speedup   | ranks agree
  1000      |        0.67 |         15.61 |            1.57 |    23.2x  | yes (checksum)
  10000     |        4.44 |        150.49 |           13.22 |    33.9x  | yes (checksum)
  100000    |       54.09 |       1484.50 |           80.46 |    27.4x  | yes (checksum)
```
The naive array is O(n) per update, so it falls behind steadily as the number of players grows. The binary-search array is a fair challenger. Its search is O(log n) but its `memmove` shift is still O(n). It stays competitive at 20K players with small score gains, but its worst case (a player jumping from last to first) is O(n), while the AVL is O(log n) in every case.

### Live simulation frame (`./arena --demo`)
```
 ARENA ASCENT  real-time leaderboard   tick 58    players 5000   queue peak 802    overtakes 9  
  ⚡ OVERTAKE!  Bot_0902     takes #1  ⚡  
  RANK  MOVE  PLAYER        TOP%     SCORE  
   1    ·     Bot_0902      0.02%       824  ██████████████████████████████
   2    ·     Bot_2348      0.04%       786  █████████████████████████████▍
   3    ·     Bot_1940      0.06%       665  ██████████████████████▏       
   4    ·     Bot_1000      0.08%       645  ██████████████████████▋       
   5    ·     Bot_2805      0.10%       631  ██████████████████▎           
   6    ▲NEW  Bot_0037      0.12%       623  ██████████████████▊           
   7    ▲NEW  Bot_3979      0.14%       614  ████████▋                     
   7    ▲NEW  Bot_2044      0.14%       614  █████▊                        
   9    ▼3    Bot_2566      0.18%       606  ██████████████████████▏       
  10    ▼3    Bot_0167      0.20%       603  ██████████████████████        

  batch: 800 updates in 0.94 ms  (0.9 M upd/s)   applied 47226   undo-stack 47226   quarantined 6
  ⚠ ANTI-CHEAT: Bot_1876 +60 (avg gain 9) quarantined
  keys: [q] quit  [b] burst 5000 updates  [c] inject cheater
```

### Menu session (ties, radar, momentum)
```
  >   Player ID:   Score change (+/-): 
  Asha (id 101)  score 300
  Rank: #1 of 5   (strict position 1, ties broken by who reached the score first)
  Percentile: top 20.00%   Badge: Legend 👑   Momentum: Steady
  RIVALRY RADAR - Asha
  >> pos 1      rank 1      Asha         id 101            300
     pos 2      rank 2      Bilal        id 102              0
     pos 3      rank 2      Chen         id 103              0
     pos 4      rank 2      Diya         id 104              0
  You are #1. Lead over Bilal: 300 points.
  >   Player ID:   Score change (+/-): 
  Bilal (id 102)  score 300
  Rank: #1 of 5   (strict position 2, ties broken by who reached the score first)
  Percentile: top 20.00%   Badge: Legend 👑   Momentum: Steady
  RIVALRY RADAR - Bilal
     pos 1      rank 1      Asha         id 101            300
  >> pos 2      rank 1      Bilal        id 102            300
     pos 3      rank 3      Chen         id 103              0
     pos 4      rank 3      Diya         id 104              0
     pos 5      rank 3      Eli          id 105              0
  +1 points to overtake Asha (position 1)  [tied score, they reached it first]
  >   Player ID:   Score change (+/-): 
  Chen (id 103)  score 450
  Rank: #1 of 5   (strict position 1, ties broken by who reached the score first)
  Percentile: top 20.00%   Badge: Legend 👑   Momentum: Steady
  RIVALRY RADAR - Chen
  >> pos 1      rank 1      Chen         id 103            450
     pos 2      rank 2      Asha         id 101            300
     pos 3      rank 2      Bilal        id 102            300
     pos 4      rank 4      Diya         id 104              0
  You are #1. Lead over Asha: 150 points.
```

## Viva Q&A

**Q1. Why an AVL tree and not a heap, a sorted array or a skip list?**
A heap cannot answer "what is X's rank" faster than O(n). A sorted array answers rank in O(log n) but needs O(n) shifting per update. An AVL tree with subtree sizes does insert, delete, rank and k-th all in O(log n) worst case. A skip list can do this too, but it is only probabilistic.

**Q2. How do you get rank in O(log n)?**
Every node stores `size`. To count players strictly ahead of score s, walk from the root. If a node's score is greater than s, then it and its whole left subtree are ahead, so add `size(left)+1` and go right. Otherwise go left. The walk is one root-to-leaf path.

**Q3. How are ties handled?**
The tree key is `(score desc, timestamp asc, id)`. The timestamp is a logical clock set when the player reached that score, so the earlier player sits first. Competition rank gives tied players the same displayed rank (1, 2, 2, 4). The ID is the last tie-break, so the order is total and the tree never sees equal keys.

**Q4. Why is there a hash table if the tree can search?**
The tree is ordered by `(score, ts, id)`, and to search it you need the player's current score. An update message carries only an ID, so you need ID to player in O(1) first.

**Q5. Why a size-10 min-heap when you can read the Top 10 from the tree?**
The tree gives it in O(K log n) per frame. The heap holds exactly the 10 best, so a newcomer is rejected with one comparison against the root, and the renderer reads 10 pointers. The tree is the source of truth and the heap is a cache. If a heap member loses points, we rebuild the heap from the tree to avoid missing the true 11th player.

**Q6. Why a queue?**
It decouples producers from the engine and keeps FIFO order, so the logical timestamps reflect arrival order. A burst of 5,000 updates is absorbed in O(1) each and applied in one batch (about 1 ms per 800 updates in the demo).

**Q7. How does undo work, and why a stack?**
Undo must reverse the most recent update first, which is LIFO. Each record stores `(id, old score, old ts)`, and undoing is a re-key (delete, change, insert) in O(log n). It is a bounded ring (100,000), so memory is constant.

**Q8. How does the Time Machine work, and what does it cost?**
The log is append-only and stores the absolute new state of each change, so replay is idempotent and handles undo entries correctly. To view version v we rebuild a fresh board from the first v entries: O(v log n). We trade time for space. Snapshots every k events would make it O(k log n) at the price of O(n) per snapshot.

**Q9. Why store the timestamp when the score is reached, not the last-activity time?**
The spec says ties go to whoever reached the score first. A later no-change update must not move a player down, so a delta of 0 is ignored and keeps the old timestamp.

**Q10. What stops a cheater from distorting the board?**
Anti-Cheat compares each gain with the player's own average (running sum and count, O(1)). A gain of at least 50 that is more than 5x the average is held in a quarantine list and never touches the tree until approved. A limitation: a player with fewer than 3 gains has no baseline, and an adaptive cheater can inflate their own average slowly.

**Q11. Are AVL rotations safe with intrusive nodes?**
Yes. Rotations only relink pointers and recompute `height` and `size` bottom-up. Delete with two children relinks the in-order successor into the node's place instead of copying data, so the `Player*` held by the hash table and the heap stays valid.

**Q12. Why is the benchmark checksum there?**
It sums every rank returned during the run, so identical sums prove the three methods agree. Speed numbers mean nothing without correctness.

**Q13. What are the worst cases?**
The tree is O(log n) always. The hash is O(n) if all keys collide, which is unlikely with multiplicative hashing and doubling. Heap repair after a Top-10 member drops is O(K log n). Time Machine is O(v log n).

**Q14. How is the project leak-free?**
Players are owned by the hash table and freed by `ht_free`. Tree and heap hold non-owning pointers, so there is exactly one owner. `make memcheck` runs valgrind when available, and otherwise AddressSanitizer and LeakSanitizer. The self-test exits clean.
