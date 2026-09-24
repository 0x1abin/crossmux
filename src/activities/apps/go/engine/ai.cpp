#include "ai.h"
#define NEIGHBOR_TABLE_OWNER
#include "neighbor_table.h"

// ==================== CrossMux ESP32 port ====================
// The engine was written for the Arduboy (ATmega32U4, 8-bit AVR).
// On ESP32 (Xtensa LX7):
//   - NO_CARRY_TRICKS: use the plain-C pointer forms instead of the
//     AVR carry-free asm fast paths (the #else branches).
//   - SB_BASE: replaces Arduboy2Base::sBuffer (the borrowed screen
//     buffer) with a dedicated static pool. The pool / RAVE tables /
//     NN scratch all time-share it exactly as they did the OLED
//     buffer on the AVR build.
//   - thinkProgressBlit/lcdWin become no-ops (e-ink panel: no
//     SSD1306 register streaming; the UI shows its own thinking
//     frame before calling think()).
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#ifndef NO_CARRY_TRICKS
#define NO_CARRY_TRICKS 1
#endif
static uint8_t engineBuffer[2048];  // replaces Arduboy2Base::sBuffer
#define SB_BASE engineBuffer
static inline void lcdWin(const uint8_t *) {}
static inline void thinkProgressBlit(uint8_t) {}
#else
#define SB_BASE Arduboy2Base::sBuffer
#endif

// Random draws: the device uses the engine's own xorshift (seeded at
// boot) so libc random() never links; the host keeps libc random()
// for per-game srand determinism in the harness. (see rngState)
static uint16_t rnd16();
static uint8_t rnd(uint8_t n);
static uint8_t countStones(Game &game);
#ifdef ARDUINO
#define SYS_RND(n)   rnd((uint8_t)(n))
#define SYS_RNDW(n)  (int16_t)(rnd16() % (uint16_t)(n))
#else
// Host uses the same engine RNG as the device (2026-08): the old
// libc random(n) here was NEVER seeded by the harness's srand --
// on macOS srand() does not seed random() -- so book root picks
// free-ran across a whole batch, silently unpairing every "paired"
// gauntlet and making batch games irreproducible standalone.
#define SYS_RND(n)   rnd((uint8_t)(n))
#define SYS_RNDW(n)  (int16_t)(rnd16() % (uint16_t)(n))
#endif

// ==================== experiment flags ====================
// Every measured experiment stays in the tree behind a flag, with its
// verdict at the definition site. None are set in shipped builds.
//   Strength (paired-gauntlet verdicts):
//     [hand-prior flag arms below were DELETED with the hand engine
//      08-19 (post-2107ccf cleanup); verdicts kept for the record,
//      code in git history: NO_KEIMA, TIGER, LL1X, BLOCKW,
//      LOWLINE_EARLY_X2, EL2X, EL2B*, PRIOR_RACE]
//     NO_KEIMA          remove the cuttable-keima prior  (-26/1000: KEEP prior)
//     TIGER             tiger's-mouth completion prior   (-24/1000 @+4,
//                       -18 @+2, -12 @+4 TIGER_MID: all negative, OFF)
//     NAKADE            prey-inside eyespace vitals      (-1/1000, disc 0/1:
//                       INERT -- fires 0.61% of positions, last 2-4 moves of
//                       decided games; settled nakade is post-hoc. OFF)
//     NET               net/geta capping prior           (-19/1000 @+4,
//                       p=.25, disc 115/134: negative, dose test stopped. OFF)
//     BMARG             komi-dither tree rewards         (-27/1000 flat,
//                       -17 tapered@40: an underdog's best wins are NARROW
//                       wins -- dither devalues our winning moves. OFF)
//     BDEF              playout boundary-defense answer  (refuted at the
//                       ownership gate: p=1 RAISES the inflation -- playouts
//                       never attempt the attack, so defense can't deflate
//                       the belief. OFF; zero gauntlets spent)
//     TREUSE            cross-move subtree reuse          (-3/1000 p=.91,
//                       disc 143/146: NEUTRAL -- opponent picks unexplored
//                       replies ~half the time and matched crowns are thin
//                       at 400 iters; complexity not paid for. OFF)
//     TREEVAL           live-tree integrity validator     (host debug tool;
//                       proved base engine tree-clean during the TREUSE
//                       aliasing hunt)
//     LL1X              midgame line-1 nearEnemy exempt  (-14/1000: junk in,
//                       blocks still don't outrank the field. OFF)
//     BLOCKW            +3 PRIOR_BLOCK on contact answers (-20/1000: fires
//                       board-wide, bonus-prior failure mode. OFF; the
//                       low-line BLOCK EXEMPTION shipped default-on)
//     LOOSE             bounded loose-ladder reader      (premise refuted:
//                       class-A saves reach 4+ libs; loss is strategic. OFF)
//     SQZ34             midgame 3/4 playout squeeze      (-20/1000: distorts
//                       healthy fights; squeeze family fully dead. OFF)
//     LADDER_PRUNE      defender-lookahead chase pick    (199=199, disc 0/0
//                       AND movecmp-identical: the room heuristic already
//                       picks the must-block side in real play. OFF)
//     NO_RESIGN         never resign                     (171=171: zero equity)
//     LOWLINE_EARLY_X2  2x early low-line penalty        (-18/1000)
//     EL2X              line-2 nearEnemy exempt any phase (+4/1000: neutral.
//                       KataGo corpus: admission repaired, eval race binds)
//     EL2B/EL2B1/EL2B2  both-colors low-line exempt (Jay) (pooled -1/2000,
//                       line1-only -15, line2-only -3: all OFF; see the
//                       exemption block for the full KataGo-corpus story)
//     LCB_LEADER_MEAN   leader competes with mean q      (-3/2000 combined)
//     STEAL_VERIFY      +50% budget before a steal pick  (-3/2000 combined)
//     CLUTCH2           second clutch extension          (189=189: saturated)
//     WIDEN_TAPER       tapered widen scan, -2.55% think  (-26/2000: costs strength)
//     FASTPLAY_STOP / CFG_PRIOR / CAPSIZE_PRIOR / ALMOST_VITAL /
//     LD_CLASS / LD_CRIT   earlier arcs, all measured dead
//   Speed (emulator-bench verdicts, all movecmp byte-identical):
//     PACKED_TRIT       base-9 direction stream          (+7.5% mid)
//     (PACKED_NBR / PACKED_PRESCAN: same family, deleted after
//      measuring +4.6% / +1.8% -- see git history 86dfb0a, e827091)
//   Probes (diagnostics, host only):
//     PLAYOUT_STATS PLAYOUT_SNAP DECIDE_PROBE WIDEN_PROBE LATENT_DEBUG

// Dynamic-komi state for graceful losing — adapted at the end of
// each think, applied to playout scoring (see scoreWinner /
// vKomiWinner / the adaptation block in think).
#define VKOMI_STEP2 4   // adaptation step, half-points (= 2 points)
#define VKOMI_MAX2 48   // spot at most 24 points before giving up
                        // (2026-08 blunder-gap hunt: 24 left evals
                        // coin-flip once behind >12 pts and the tree
                        // threw 6-20pt stones; 48 keeps the margin
                        // objective ordered. Two 200-game seed sets:
                        // blunders 35->29, drop mass -15%; paired 1000
                        // 188 vs 189 with only 5 diverged games; game
                        // length +0.7 moves; 96 = byte-identical to 48,
                        // saturation. Resign timing unchanged.)
static uint8_t vKomi2;

#ifdef VKOMI_WIN
// Winning-side dynamic komi (endgame-arc P1, 2026-08-11): when comfortably
// ahead, playouts must win by vKomiWin half-points, so the tree preserves
// margin instead of coasting to the wire (phase-0: ~11 pts shed from peak,
// win or lose). v2 engages on the MARGIN MEAN (playout wr reads 34-74% at
// positions KataGo scores +30..+56 — wr can't see the win; the margin can),
// demands at most half the cushion, releases on virtual-wr collapse.
// MEASURED (MAX=32, human n=1000 paired + gauges): mechanism WORKS — wins
// at-the-wire (0..2.5) 64->43/100 p<.003, comfortable (>6.5) 18->36, median
// final +1.5->+3.3 — but WIN-RATE NEUTRAL (242->248 n.s.) and led-then-lost
// unchanged (57->59%): the close losses die by 10-20pt group deaths the
// playouts APPROVE (L&D blindness) — margin demand can't veto what the eval
// can't price. L0 −0.7pp lean n.s.; +374B flash. OFF — revisit as a stack
// if playout L&D perception ever improves.
#ifndef VKW_STEP
#define VKW_STEP 2      // half-points per think (= 1 point)
#endif
#ifndef VKW_MAX
#define VKW_MAX 16      // demand at most this many half-points
#endif
#ifndef VKW_ENGAGE
#define VKW_ENGAGE 16   // engage when mean playout margin exceeds this
                        // many half-points (16 = 8 points of cushion)
#endif
static uint8_t vKomiWin;
static uint16_t thinkVirtWins; // virtual-komi wins this think (release signal)
#endif

void AI::reset() {
    firstMove = 1;
#ifdef NNOPEN
    nnLast = 0xFF;
#endif
    resigned = 0;
    resignCount = 0;
    resignCount2 = 0;
    vKomi2 = 0;
#ifdef VKOMI_WIN
    vKomiWin = 0;
#endif
}

__attribute__((noinline))
void AI::notifyMove(uint8_t x, uint8_t y) {
    firstMove = 0;
#ifdef NNOPEN
    nnLast = y * 9 + x;
#endif
}

__attribute__((noinline))
void AI::notifyPass() {
    firstMove = 0;
#ifdef NNOPEN
    nnLast = 0xFF;
#endif
}

uint8_t AI::chooseMove(Game &game) {
    uint8_t x, y;
    if(firstMove) {
        // net's own empty-board preference (exact 5-way komoku tie),
        // random member + random symmetry = book-root-like variety
        static const uint8_t KOMOKU[5] PROGMEM = { 5*9+6, 3*9+6, 6*9+5, 2*9+5, 6*9+3 };
        uint8_t mv = pgm_read_byte(KOMOKU + SYS_RND(5));
        x = mv % 9; y = mv / 9;
        uint8_t s2 = SYS_RND(8);
        if(s2 & 1) x = 8 - x;
        if(s2 & 2) y = 8 - y;
        if(s2 & 4) { uint8_t t = x; x = y; y = t; }
        if(game.isValidMove(x, y)) {
            game.playMove(x, y);
            notifyMove(x, y);
            return 1;
        }
    }
#ifdef NNOPEN
    if(nnOpeningMove(game, x, y)) {
        game.playMove(x, y);
        notifyMove(x, y);
        return 1;
    }
#endif
    return 0;
}

// ==================== MCTS + UCB1 ====================

#ifdef NORAVE
#define NODE_POOL_SB 164    // depth-arc arm: RAVE tables surrendered to the pool
                            // (164*6=984 <= 1024; HOST-ONLY experiment flag --
                            // 0x800 would land mid-pool on device)
#else
#define NODE_POOL_SB 137    // nodes in the borrowed screen buffer (137*6=822 keeps
                            // the node region below RAM 0x800; the magic key then
                            // lands in the tolerant raveV table, not a live node)
// Extension nodes in ordinary statics. RAM here trades directly
// against stack headroom: at EXT 33 with UI strings in RAM the
// globals reached 2,383 bytes and think()'s call chain smashed the
// stack into them (the board filled with diagonal garbage). Moving
// the UI strings to PROGMEM bought the budget back — keep total free
// RAM >= ~250 for the stack. Total pool must stay < 255 (8-bit
// links, 0xFF = null).
#endif
#define NODE_POOL_EXT 69
#ifdef POOLEXT2
// arm 3 (PV-longevity program): second extension area in ordinary .bss.
// The linker places it above game (~0x91A+), entirely past the 0x800
// magic key -- no leapfrog logic needed. Cost = a third node() segment.
#ifndef NODE_POOL_EXT2
#define NODE_POOL_EXT2 21
#endif
#define NODE_POOL (NODE_POOL_SB + NODE_POOL_EXT + NODE_POOL_EXT2)
#else
#define NODE_POOL (NODE_POOL_SB + NODE_POOL_EXT) // must stay < 255 (8-bit links)
#endif
#ifndef MCTS_ITERATIONS
#ifndef MCTS_ITERATIONS
#define MCTS_ITERATIONS 1000 // SHIP (2026-08-11, iterations arc): pure-1000 x
                             // stable-stop-512 = human 317/1000 vs base-400's
                             // 242 (+7.5pp, chi2=15.9), L0 401 vs 362; ~18.5s
                             // mean device move. Counters cap ~3400 (12-bit);
                             // winRate6 guard active above ~680.
#endif
#endif
#ifndef RAVE_K
#define RAVE_K 150          // Gelly-Silver beta schedule constant.
                            // SHIP 08-19: 300->150 from the RAVE_K sweep --
                            // -3.7% iterations (stable-stop fires sooner,
                            // ~-0.7 s/move) at L0-neutral (3 paired seed
                            // sets, n=6000) AND human-10k-neutral (n=1000
                            // paired). The strength cliff is at K<=100
                            // (-63 L0 / -33 human); 150 is the last safe
                            // point. K=300/600 tables kept for A/B.
#endif
// Q8 sigma for the root LCB pick. 1.6 sigma (410): measured on a
// 20-seed stability probe of a real misplay position, raising from
// 1.28 sigma removed exactly the one unlucky under-sampled pick (a
// pointless clamp) while leaving all 19 other picks unchanged —
// the "even out the luck" knob. Higher added nothing; raising the
// visit gate instead scattered picks.
#ifndef LCB_Z
#define LCB_Z 410
#endif
// Prior-informed near-tie break (SHIPPED 2026-08-22, Jay verdict):
// within this Q12 LCB band of the pick-time winner, the higher-PRIOR
// root child wins instead. Q12 192 = ~4.7% winrate = the width of the
// measured near-tie class (priordrop: 50% of prior-top-8 blunders are
// coin-flips the prior ranks correctly). L0 +84/6000 p=.008 (3 fresh
// sets all positive), human-10k +37/3289 n.s. non-negative (K150/
// WIDEN_SHAPE precedent frame). EPS swept {82,192,384}: 82 too tight
// for LCB-space (low-visit correct moves carry wide confidence terms),
// margin knobs hurt. 0 disables.
#ifndef PRIORTIE
#define PRIORTIE 192
#endif
// Minimum real visits before a child may win the LCB race: below
// this, prior-seeded win rates are still mostly noise
#ifndef LCB_GATE
#define LCB_GATE 24
#endif
// Relative gate: an LCB candidate also needs visits >= maxVisits /
// LCB_REL_DIV. The absolute gate alone let a 28-visit child with a
// lucky-streak q beat an 84-visit leader and throw away a won game;
// siblings within 2x of the leader are sampled well enough to compare.
#ifndef LCB_REL_DIV
#define LCB_REL_DIV 2
#endif
// Exploration term scaled down by this shift: at 400 iterations over
// ~56 root moves, full UCB1-Tuned exploration (~0.3 at n=10) dwarfs
// the real q spread (~0.1) and the search polls uniformly instead of
// concentrating on the best line.
// SHIP 2 (2026-08-14): the constant was tuned in the 400-iter era and
// never re-swept; at 1000 iters one notch greedier concentrates the
// budget and wins on BOTH referees — L0 1544/3000 vs 1477, human
// 838/2000 vs 774 (+3.2pp, p=.04, two fresh seed sets). shift=3
// measured slightly worse both axes (1530 / 398): the curve peaks here.
#ifndef UCB_EXPLORE_SHIFT
#define UCB_EXPLORE_SHIFT 2
#endif

// Virtual win/visit priors seeded at expansion; real playout results
// accumulate on top and wash these out. The base is deliberately heavy
// (michi's "even prior"): at weight 2 a single lucky playout swung a
// child's q from 0.50 to 0.67 and selection chased noise; at weight 8
// early q is stable and selection follows the bonus ordering until
// real evidence accumulates.
// SHIP V2/W1 (2026-08-15): that stability reasoning dated from the
// hand-prior era. With the learned prior's ordering, the even-prior
// ballast was pure dilution — the sweep is monotone to the floor
// (V24:1446 V16:1505 V8:1544 V4:1552 V2:1598 L0), and the human leg
// confirmed the campaign's best: 461/1000 vs ship's 421 on the same
// block (+replication pending in log). Neutral children become
// first-playout-volatile by design: with a prior this good, neutrals
// are rarely the answer — let them thrash and resolve cheaply.
// V0 impossible (winRate6 0/0); V1 can't express the even prior.
#ifndef PRIOR_BASE_V
#define PRIOR_BASE_V 2
#endif
#ifndef PRIOR_BASE_W
#define PRIOR_BASE_W 1
#endif
// Opening/midgame phase threshold (stones on board). Consumers:
// the sole-connector gate, NN handoff logic, and various phase gates.
// (Named for the old low-line discipline, which lived and died with
// the hand prior — see git history pre-08-19.)
#ifndef EARLY_STONES
#define EARLY_STONES 20
#endif
// Komi-dither half-width in points for BMARG tree rewards, and the
// stone count where dithering stops (endgame half-point precision:
// K=3 dither everywhere measured -27/1000).
#ifndef BMARG_K
#define BMARG_K 3
#endif
#ifndef BMARG_END
#define BMARG_END 40
#endif

// BDEF playout answer rate = 1/(BDEF_MASK+1); mask 1 = 1/2, 3 = 1/4.
#ifndef BDEF_MASK
#define BDEF_MASK 1
#endif

// Playout throw-in exception (the kill-bias fix): normal playouts gate
// self-atari off entirely, so they can NEVER play the sacrifice that
// kills an eyespaced-but-dead group -- dead groups survived every
// rollout and the eval read lost positions at ~50-65% (measured: 66%
// win-rate on a true -16.5 board; scoreDead's self-atari-allowed vote
// reads the same board correctly). A LONE stone in self-atari is by
// construction a throw-in (no friendly neighbour + one liberty => all
// non-empty neighbours are enemy); allow exactly that shape, at rate
// 1/PLAYOUT_THROWIN_RATE (power of 2; 1 = always, 0 = off). Connected
// self-atari -- feeding a group into atari -- stays rejected.
#ifndef PLAYOUT_THROWIN_RATE
#define PLAYOUT_THROWIN_RATE 1
#endif

// Only the first moves of a playout feed RAVE: a point that gets
// filled successfully in the endgame of most playouts says nothing
// about playing it NOW, and those stats were drowning the priors.
#ifndef RAVE_HORIZON
#define RAVE_HORIZON 24
#endif

// Uncertainty extension: if the visit leader has not reached this
// many visits when the budget runs out, the root is FLAT — too many
// live candidates sharing too few visits — and the LCB pick becomes
// a raffle among whichever few crossed the gate by luck. The blunder
// hunt's biggest drops (-14 to -28 pts) all came from flat roots,
// with the winning move sometimes nearly unvisited. One extra
// half-budget, granted once, self-targets exactly those positions.
#ifndef UNCERTAIN_MIN
#define UNCERTAIN_MIN 0 // SHIP: extension off at 1000 base (see boost note)
#endif

// Resignation: real-playout win rate under ~8% (1/12) for this many
// consecutive searches, past the opening. Light playouts keep a
// 5-10% swindle floor even in dead positions, so a reading this low
// means truly hopeless; the streak guards against one noisy search.
#ifndef RESIGN_DENOM
#define RESIGN_DENOM 12
#endif
// Second tier: a milder floor held for LONGER. Some dead positions
// keep a swindle-floor eval above the strict 1/12 forever and the
// AI flails on for 20+ moves — the blunder hunt's largest class by
// count was moves in such games. Five consecutive sub-1/6 reads is
// a corpse, not a fight.
#ifndef RESIGN2_DENOM
#define RESIGN2_DENOM 6
#endif
#ifndef RESIGN2_STREAK
#define RESIGN2_STREAK 4 // was 5; 2026-08 blunder-gap hunt: one think
                         // less of hopeless-game garbage. Blunders -7%
                         // on 400 hunt games with IDENTICAL wins;
                         // paired 1000: 187 vs 188, exactly 1 game
                         // diverged (an earlier resignation of a lost
                         // game). Swindle equity vs L0 measured ZERO.
#endif
#ifndef RESIGN_STREAK
#define RESIGN_STREAK 2
#endif
#ifndef RESIGN_MIN_STONES
#define RESIGN_MIN_STONES 25
#endif

// Settle gate (see settleVote): after the opponent passes in the
// endgame, accept the honest count and pass back once the ownership
// vote reads us behind by at least this many HALF-POINTS-doubled
// (6 = 3 points). Between winning and this, keep playing the close
// ones out.
#ifndef SETTLE_ACCEPT2
#define SETTLE_ACCEPT2 6
#endif

// Progressive widening (non-root): children allowed = 1 + visits/RATE
#ifndef WIDEN_RATE
#define WIDEN_RATE 6
#endif
// Root progressive widening: start with the best ROOT_INIT prior
// candidates (plus pass) and widen by visits. Full root expansion
// diluted 400-600 iterations over 25-40 candidates (~15 visits each)
// — the blunder hunt showed flat roots picking raffle winners while
// the actual best move sat under 10 visits, twice with the winning
// move absent from the >=10-visit table entirely.
#ifndef ROOT_INIT
#define ROOT_INIT 8
#endif
#ifndef ROOT_WIDEN_RATE
#define ROOT_WIDEN_RATE 12
#endif
#ifndef WIDEN_CAP
#define WIDEN_CAP 16
#endif
// Candidates added per widenNode scan (see the batch comment there)
// Net-priored widening: the net makes per-scan cost ~10x the old hand
// prior's, so amortize -- one scan banks 8 candidates (1 active + 7
// latents) instead of 3, cutting scan count ~2.5x. Latent activation
// picks best-by-recovered-prior from the larger remembered ranking.
#define WIDEN_BATCH 8
// A leaf must collect this many visits (prior seed included) before it
// may grow a child: playouts from a cold leaf are as informative as a
// one-child subtree, and each expansion costs a full prior scan.
// Tactically hot leaves carry big seeds, so they still expand at once.
#ifndef EXPAND_VISITS
#define EXPAND_VISITS 8
#endif
// Playouts should run until the position resolves: truncating scores
// every not-yet-dead group as alive. ~90-120 moves settles an early
// position; 160 keeps the runaway guard from bending evaluations
// while staying affordable on the device.
#ifndef PLAYOUT_CAP
#define PLAYOUT_CAP 160
#endif
// Contact-push answer probability mask (3 = 3/4 of the non-local
// coin, stacking with the local-answer step to ~7/8; 0 disables).
#ifndef CONTACT_ANSWER_MASK
#define CONTACT_ANSWER_MASK 3
#endif
// Lone-invader answer probability mask, SEPARATE from the contact
// answer so the invasion knob can be tuned in isolation (the two were
// one gate; a sweep of the combined mask confounded boundary defense
// with invasion optimism).
#ifndef LONE_ANSWER_MASK
#define LONE_ANSWER_MASK 3
#endif
// Extra search on the first out-of-book moves: below this many
// stones, iterations get a +50% boost. The board is at its most open
// exactly where playout evaluation is flattest, and the first search
// move after book exit was a measured, repeated 14-26 point blunder.
// 0 disables.
// Opening boost REMOVED (2026-08-14): the iterations arc measured
// opening-boosted budgets landing in the 1200-2000 dead plateau at the
// 1000 base (L0 five-way: pure 406 > ship 393); the knob sat at 0 since,
// and the dead guard is deleted. Budget is a flat MCTS_ITERATIONS -- the
// stop rules only ever shrink it.
#ifndef MERCY_MARGIN
#define MERCY_MARGIN 20     // capture lead that ends a playout early
                            // (25->20 SHIP 2026-08-24: -7.7% think,
                            // human-10k wash +9/1000, L0 -39 trend)
#endif
#define MOVE_PASS 81
#define NO_KO 0xFF
#define ILLEGAL 0xFE
#define POISONED 0xFF0      // visits sentinel for illegal tree moves
                            // (stats are 12-bit: max real count 4079)

struct Node {
    uint8_t move;        // 0-80 board index, MOVE_PASS
    uint8_t firstChild;  // pool index, 0xFF = none
    uint8_t nextSibling; // pool index, 0xFF = none
    // visits and wins packed 12+12 bits across 3 bytes:
    // s0 = v[7:0], s1 = v[11:8] | w[3:0]<<4, s2 = w[11:4].
    // Wins are from the perspective of the player who made 'move'.
    uint8_t s[3];
};

// The tree spans two regions, routed by index in node():
//   indices 0..NODE_POOL_SB-1 (136)  -> the borrowed 1KB screen buffer
//   indices NODE_POOL_SB..205        -> poolExt in ordinary statics
// The buffer borrow works because the search is blocking: the OLED
// retains the last display()ed frame while we trash the buffer, and
// the next jay.clear() redraw wipes any residue.
// Buffer layout: 137 nodes * 6 = 822, then two 81-byte RAVE tables
// (raveV[0] deliberately sits ON the 0x800 magic key -- see checkmagic).
static Node * const pool = (Node *)SB_BASE;

// Root-only RAVE (AMAF): for each board point, how often the root
// player played it anywhere in a simulation, and how often those
// simulations were won. Pure statistics — never used as indices — so
// a 0x800 magic-key stomp here is harmless noise, no redirect needed.
static uint8_t * const raveV = SB_BASE + NODE_POOL_SB * sizeof(Node);
static uint8_t * const raveW = SB_BASE + NODE_POOL_SB * sizeof(Node) + BOARD_CELLS;

// Single-bit masks: `1 << (i & 7)` compiles to a variable shift LOOP at
// -Os (ror/dec/brpl, ~2-9 cycles) at every bitmap site; an lpm from this
// table is a constant 3 cycles. Same values -- bit-identical results.
PROGMEM const uint8_t BIT_MASK[8] = {1, 2, 4, 8, 16, 32, 64, 128};
static inline __attribute__((always_inline)) uint8_t bitMask(uint8_t i) {
    return pgm_read_byte(BIT_MASK + (i & 7));
}

// Which points the root player touched in the current simulation
static uint8_t raveMask[11];

static inline void raveMark(uint8_t pos) {
    raveMask[pos >> 3] |= bitMask(pos);
}

// Static pool extension
static Node poolExt[NODE_POOL_EXT];
#ifdef POOLEXT2
static Node poolExt2[NODE_POOL_EXT2];
#endif

// Plain pool indexing — no 0x800 redirect. The magic-key bytes at
// RAM 0x800-0x801 land in the RAVE tables above (harmless statistics);
// test/checkmagic.sh asserts at build time that neither node region
// (pool[0..142], poolExt) nor floodScratch spans 0x800, failing the
// build loudly if a RAM-layout change ever pushes them onto it. That
// replaced the old per-access redirect, which cost ~3% of think time
// (floodSlot alone, by the emulator profile) guarding a collision the
// layout already prevents.
static inline Node& node(uint8_t i) {
#ifdef POOLEXT2
    if(i < NODE_POOL_SB) return pool[i];
    if(i < NODE_POOL_SB + NODE_POOL_EXT) return poolExt[i - NODE_POOL_SB];
    return poolExt2[i - (NODE_POOL_SB + NODE_POOL_EXT)];
#else
    return (i < NODE_POOL_SB) ? pool[i] : poolExt[i - NODE_POOL_SB];
#endif
}

// Packed 12-bit stat accessors. The Node& variants skip the pool-split
// resolution when the caller already holds the reference.
static inline uint16_t nRefVisits(Node &n) {
    return n.s[0] | ((uint16_t)(n.s[1] & 0x0F) << 8);
}
static inline uint16_t nRefWins(Node &n) {
    return (n.s[1] >> 4) | ((uint16_t)n.s[2] << 4);
}
static inline uint16_t nVisits(uint8_t i) {
    Node &n = node(i);
    return n.s[0] | ((uint16_t)(n.s[1] & 0x0F) << 8);
}

static inline uint16_t nWins(uint8_t i) {
    Node &n = node(i);
    return (n.s[1] >> 4) | ((uint16_t)n.s[2] << 4);
}

static inline void nSetStatsN(Node &n, uint16_t v, uint16_t w) {
    n.s[0] = v;
    n.s[1] = ((v >> 8) & 0x0F) | ((w & 0x0F) << 4);
    n.s[2] = w >> 4;
}
static inline void nSetStats(uint8_t i, uint16_t v, uint16_t w) {
    nSetStatsN(node(i), v, w);
}

static inline void nBump(uint8_t i, uint8_t win) {
    Node &n = node(i);                 // resolve pool split once, not 3x
    uint16_t v = nRefVisits(n) + 1, w = nRefWins(n) + win;
    n.s[0] = v;
    n.s[1] = ((v >> 8) & 0x0F) | ((w & 0x0F) << 4);
    n.s[2] = w >> 4;
}
static uint8_t poolUsed;
static uint8_t freeHead;   // recycled nodes, threaded via nextSibling
static uint8_t path[32];   // current descent, root first (see reclaim)
static uint8_t pathDepth;
#if !defined(ARDUINO) && defined(RECLAIM_LOG)
// Host-only mechanism instrument: evictions, admissions, node lifetimes,
// descent-depth histogram, PV-depth timeline. Env RECLAIM_LOG_FILE.
static FILE *rlF;
static uint16_t rlBirth[NODE_POOL]; static uint8_t rlBirthD[NODE_POOL];
static uint32_t rlDescHist[32];
extern uint32_t thinkItersRunTotal;
static uint16_t rlIter;
static void rlOpen(void) {
    if(!rlF) { const char *p = getenv("RECLAIM_LOG_FILE"); if(p) rlF = fopen(p, "a"); }
}
static void rlEvict(char type, uint8_t d, uint16_t vis, uint8_t nodeIdx, uint8_t mv) {
    if(rlF) fprintf(rlF, "E %c %u %u %u %u %u %u\n", type, rlIter, d, vis, mv,
                    rlBirth[nodeIdx], rlBirthD[nodeIdx]);
}
#endif
// Any capture along the current descent invalidates the incremental
// near-mask (captures shrink halos; a stale mask would admit different
// widening candidates). Set from simCaptured per descent step.
static uint8_t descentCaptured;
static uint8_t rootTurn;
static uint8_t rootKo;
static uint8_t rootLast; // opponent's actual last move (0xFF = none)
static uint8_t rootStones; // stones on the real board at think() time
static uint8_t simKomi;
// Real-playout tally for the whole search: the seed-free, average
// (not max-biased) evaluation of the root position. Read by the host
// test tools; costs two counters on-device.
uint16_t thinkSims; static uint16_t thinkSimWins;  // thinkSims non-static:
                          // the stats display reads it directly (see ai.h)
#ifndef STABLE_K
#define STABLE_K 0   // RETIRED 2026-08-26 (z-stop arc): replaced by the
                     // posterior-gap stop (ZSTOP below). Historical ship
                     // value 512; the K sweep's law (humans punish
                     // premature stops L0 can't see) now enforced by the
                     // evidence-denominated criterion instead.
#endif
#ifndef ZSTOP
#define ZSTOP 24     // SHIP 2026-08-26: posterior-gap stop, z = 24/16 =
                     // 1.5 sigma over the top-2 root children. -5.4%
                     // iterations (host ITERS n=2000), L0 +18 wash,
                     // human-10k +5 wash; mass-insensitive (invariance
                     // battery: flat under mm48/noti/patrnd where
                     // iteration-denominated K was the tax suspect).
                     // Z=20 (-12.5%) human -25 lean = the frontier edge.
                     // -DZSTOP=0 disables (with STABLE_K=0 -> lead-stop
                     // + flat budget only).
#endif
#if STABLE_K > 0
#define STABLE_STOP 1
static uint16_t ssSince; static uint8_t ssPrev;
#endif
#if !defined(ARDUINO) || defined(VKOMI_WIN)
// Device carries this int32 only with VKOMI_WIN (v2 margin-engage signal).
// NOTE the bias caveat in the host-diagnostic comment below applies here
// too: playouts can't kill eyespaced-dead groups, so the margin mean is
// optimistically biased on exactly those boards.
static int32_t thinkMargin2Sum;
#endif
#ifndef ARDUINO
// Host diagnostic: mean root-relative TRUE margin (doubled points, komi
// applied) across this think's playouts, annotated by play_gui as est=.
// NOT a trustworthy lost-position detector: normal playouts run with
// scoreMode=0 (self-atari gated off), so they can never play the
// throw-in sacrifices that kill an eyespaced-but-dead group — the
// margin inherits that systematic bias wholesale (measured on the
// Jul 30 SGF: est +4.5 at a true −16.5, win-rate just as fooled).
// The only honest ownership read is the scoreMode=1 vote (settleVote /
// scoreDead). Kept as an SGF red-flag diagnostic: est far from the
// eventual scoreDead count marks positions the playout policy misreads.
int16_t thinkAvgMargin2;
// Wait accounting for the FASTPLAY experiment: iterations actually
// run vs budgeted, across all thinks (host tools print the ratio)
uint32_t thinkItersRun, thinkItersBudget;
#endif
// Vital points of small eyespaces on the ROOT board, found once per
// search. Playouts probe these no matter where the last move was —
// otherwise a tenuki from a life-and-death spot is never punished in
// rollouts and scores as well as resolving it.
static uint8_t rootVitals[3];
static uint8_t nRootVitals;
#if defined(EYE_PRIOR2) || defined(EYE_FEATS)
#define EYE_BITMAPS 1
#endif
#if defined(EYE_PRIOR) || defined(EYE_BITMAPS)
// Depth arc (2026-08-14): liberties of WEAK chains (<= EYE_WEAK_MAX
// exact libs, root board) -- the L&D-critical placement class the
// batch ranking measurably buries (0/10 ply-2 containment; the ten
// missed replies were all zero-own-contact cells in weak groups'
// liberty structures). Root-computed once per think; read as a prior
// bonus at every widen depth.
#ifndef EYE_WEAK_MAX
#define EYE_WEAK_MAX 5
#endif
#if defined(EYE_PRIOR) || defined(EYE_PRIOR2) || defined(EYE_INJECT)
static uint8_t weakLibs[11];   // experiment-flag arms only (see EYE_PRIOR)
#endif
#ifdef EYE_BITMAPS
static uint8_t weakLibsC[2][11];   // per-color: [0]=BLACK chains, [1]=WHITE
static uint8_t vitalLibs[11];      // degree-max liberty of each weak chain
#endif
#endif
// Liberty carryover: a gated simPlay floods the placed group anyway;
// the next playout move classifies that same group on an unchanged
// board, so the result is cached instead of re-flooded.
static uint8_t cacheLibsPos; // group's stone position, 0xFF = invalid
static uint8_t cacheLibs, cacheL1, cacheL2;
static uint8_t simCaptured;  // stones captured by the last simPlay
// BENCH-ONLY layout shim (2026-08-15): the bench firmware links its own
// object set, so its RAM layout drifts independently of the device's --
// and boardAt's carry-free trick needs lo8(simBoard) <= 0xAF in EVERY
// binary that runs the engine. The endlist experiment's 12 unconditional
// stats bytes pushed the BENCH build's simBoard to lo8 0xF4 and the
// emulator silently measured an engine whose boardAt read simMark as
// board cells for q >= 12 (device was unaffected -- checkmagic green).
// The pad below is tuned per-layout by the bench driver, which now
// nm-asserts lo8(simBoard) after every build. Not defined on device.
#ifdef BENCH_PAD
__attribute__((used)) static volatile uint8_t benchPad[BENCH_PAD];
#endif
#ifdef ARDUINO
// Page-phase knob (08-29): net-NEW bytes pinned into the plain-.bss
// front group (avr5 script places *(.bss) before all *(.bss.*)) shift
// every .bss.* object's lo8 uniformly, so the carry-free constraints
// (simBoard/simMark/chainId lo8 <= 0xAF, pool clear of 0x800) reduce
// to one tunable number. Window after the prevBoard removal: [17,24]
// (floor: simBoard off lo8 0xEF; ceiling: pool end vs 0x800). 17 =
// simBoard 0x00, chainId 0x67, simMark 0x1E, pool end 0x7F8. Moving
// EXISTING early statics here instead cancels out (their vacated slot
// un-shifts everything after it) -- the pad must be new bytes.
// checkmagic + the bench driver's nm assert gate every rebuild.
__attribute__((section(".bss"), used)) static uint8_t bssPagePad[66];   // retuned 08-30 (front-pin era):
                                 // sims/chainId now sit in the front
                                 // group, so this ONE knob sets their
                                 // lo8 phase for BOTH elfs; iterate
                                 // with checkmagic on game AND bench.
#endif
__attribute__((section(".bss")))   // front-pinned (08-30): the three
                                   // carry-constrained arrays live in
                                   // the stable plain-.bss front group
                                   // beside sBuffer, so LTO reshuffles
                                   // can't move them; ONE pad tunes
                                   // all their lo8s, in BOTH elfs.
uint8_t simBoard[BOARD_CELLS];   // non-static (08-29): game.cpp borrows
                                 // it as its flood `visited` scratch --
                                 // every game flood runs outside the
                                 // search (rootMoveOK is post-search,
                                 // scoreDead's computeScore is post-vote)
#ifdef ARDUINO
// boardAt moved to neighbor_table.h (08-30): game.cpp's rules walks
// read simBoard through the same 3-instruction carry-free trick.
#endif
__attribute__((section(".bss")))
static uint8_t simMark[BOARD_CELLS];   // epoch marks for flood fill
#if defined(ARDUINO) && !defined(NO_CARRY_TRICKS)
// &simMark[q] by the same carry-free trick (checkmagic.sh asserts
// lo8(simMark) <= 0xAF). Returns a pointer: the flood loops both
// read and write the mark.
static inline __attribute__((always_inline)) uint8_t *markPtr(uint8_t q) {
    // carry-FREE form (2026-08-15 asm audit): lo8(simMark) <= 0xAF is
    // now asserted by the bench driver AND checkmagic alongside
    // simBoard's, so the hi byte is a link-time constant — one
    // instruction and one cycle less at eight hot flood sites.
    uint8_t *p;
    asm("mov %A0,%1\n\t"
        "subi %A0,lo8(-(%2))\n\t"
        "ldi %B0,hi8(%2)"
        : "=&d"(p) : "r"(q), "i"(simMark));
    return p;
}
#else
static inline __attribute__((always_inline)) uint8_t *markPtr(uint8_t q) { return &simMark[q]; }
#endif
#if defined(ARDUINO) && !defined(NO_CARRY_TRICKS)
// &simBoard[q], pointer form of boardAt (same carry-free precondition).
// For scattered read+write pairs on ONE cell — NOT for loops (spill law).
static inline __attribute__((always_inline)) uint8_t *boardPtr(uint8_t q) {
    uint8_t *p;
    asm("mov %A0,%1\n\t"
        "subi %A0,lo8(-(%2))\n\t"
        "ldi %B0,hi8(%2)"
        : "=&d"(p) : "r"(q), "i"(simBoard));
    return p;
}
#else
static inline __attribute__((always_inline)) uint8_t *boardPtr(uint8_t q) { return &simBoard[q]; }
#endif
// Chain map, computed once per EXPANSION while the board is frozen.
// One byte per cell: (libs << 6) | id — the capped 1/2/3+ liberty
// class lives in the top 2 bits, the chain id in the low 6 (0 =
// empty cell). Legal positions max out near ~40 chains (the safe
// theoretical bound is ~64); ids saturate at 63, which degrades
// identity precision in impossible positions instead of anything
// worse. Replaces per-candidate chain floods (once ~38% of think
// time). Valid only inside one expandNode/widenNode call.
__attribute__((section(".bss")))
static uint8_t chainId[BOARD_CELLS];
#if defined(ARDUINO) && !defined(NO_CARRY_TRICKS)
// &chainId[q] by the boardAt/markPtr carry-free trick (checkmagic.sh
// asserts lo8(chainId) <= 0xAF). Hot readers (the candidatePrior
// neighbour scan, the regionVital stamp) paid a 16-bit index extend
// per access. NOT used in buildChainMap (spill cascade, see the
// boardAt fences).
static inline __attribute__((always_inline)) uint8_t *chainPtr(uint8_t q) {
    // carry-FREE form (2026-08-15, the markPtr sibling): lo8(chainId)
    // <= 0xAF is asserted by checkmagic and the bench driver.
    uint8_t *p;
    asm("mov %A0,%1\n\t"
        "subi %A0,lo8(-(%2))\n\t"
        "ldi %B0,hi8(%2)"
        : "=&d"(p) : "r"(q), "i"(chainId));
    return p;
}
#else
static inline __attribute__((always_inline)) uint8_t *chainPtr(uint8_t q) { return &chainId[q]; }
#endif
// Runtime RAM-layout guard (see ai.h). The node region must clear the
// 0x800 magic key; simBoard's low byte must stay <= 0xAF for boardAt's
// carry-free trick. checkmagic.sh is the build-time twin, but it can go
// unrun/stale (it did) -- so this halts loud on-device too.
uint8_t AI::layoutHazard() {
#if defined(ARDUINO) && !defined(ESP32) && !defined(ARDUINO_ARCH_ESP32)   // AVR pointers are 16-bit; host has no 0x800 magic key
    if((uint16_t)Arduboy2Base::sBuffer + NODE_POOL_SB * sizeof(Node) > 0x800u)
        return 1;
    if(((uint16_t)&simBoard[0] & 0xFF) > 0xAFu)
        return 2;
#endif
    return 0;
}
// Which empty cells' eyespace code (chainId bits 6-7) is cached this
// widen (see buildChainMap / regionVital's lazy stamp). 81 bits.
static uint8_t regionDone[11];
#define CHAIN_OF(b) ((b) & 0x3F)
#define LIBS_OF(b) ((b) >> 6)
static uint8_t markEpoch;
// Set only inside scoreDead's vote playouts (see playoutTry)
static uint8_t scoreMode;
#ifdef PLAYOUT_STATS
uint32_t spLoneOK, spLoneRej, spFloodOK, spFloodRej, spFloodE2;
#endif

// xorshift16 state; 0 is sticky, so every seeding path must |1.
// On the DEVICE this is seeded once at boot (see setup()) and
// free-runs — routing every random draw through it lets the AVR
// build drop libc random_r (~400 bytes of flash). The host harness
// keeps libc random() so per-game srand determinism is unchanged.
uint16_t rngState = 1;
#ifndef ARDUINO
// Host-only debug hooks for reproducibility (play_gui SGF annotations).
// think() records the seed it started from in lastThinkSeed; setting
// forceThinkSeed nonzero replays that exact seed. Compiled out on device.
uint16_t lastThinkSeed = 0, forceThinkSeed = 0;
#endif
// Host-harness tuning knobs. On the device they compile to constants
// and cost neither RAM nor cycles. CrossMux ESP32 port: keep them as
// variables so difficulty can vary the iteration budget at runtime.
#if defined(ARDUINO) && !defined(ESP32) && !defined(ARDUINO_ARCH_ESP32)
#define mctsIterations MCTS_ITERATIONS
#define reclaimEnabled 1
#define resignStreak RESIGN_STREAK
#else
uint16_t mctsIterations = MCTS_ITERATIONS;
uint8_t reclaimEnabled = 1;
uint8_t resignStreak = RESIGN_STREAK;
#endif
// flood work stack: shared floodScratch[] from game.cpp

static uint16_t rnd16() {
    rngState ^= rngState << 7;
    rngState ^= rngState >> 9;
    rngState ^= rngState << 8;
    return rngState;
}

static uint8_t rnd(uint8_t n) {
    // Lemire multiplicative reduction: map rnd16()'s [0,2^16) uniformly into
    // [0,n) with a 16x8 multiply + high-word take, instead of a ~180-cycle
    // variable __udivmodhi4. NOT bit-identical to %n (a different draw->index
    // map) but the same uniform distribution -- strength validated vs L0.
    return ((uint32_t)rnd16() * n) >> 16;
}

// rnd() for a COMPILE-TIME modulus. At -Os gcc emits a small rcall
// __udivmodhi4 even for a constant divisor; forcing -O2 (and out-of-line so
// the attribute sticks through inlining) turns `% N` into a magic-multiply.
// Bit-identical to rnd(N) -- same rnd16() draw, same remainder.
__attribute__((optimize("O2"), noinline))
// Lemire high-mult reduction (2026-08-15): same uniform family as %N,
// a DIFFERENT draw->index map (trace-changing, so gauntlet-gated:
// L0 3x1000 pooled 1634/3000 vs ship 1582 — at/above band) and no
// __umulhisi3 magic-divide. Matches rnd()'s existing Lemire map.
// Runtime-n (was template<N>): the asm already took N in a register, so
// the two instantiations (<3>, <81>) were identical 72B bodies differing
// only in one ldi -- one shared copy, byte-identical results.
static uint8_t rndMod(uint8_t nn) {
#if defined(ARDUINO) && !defined(ESP32) && !defined(ARDUINO_ARCH_ESP32)
    // (x*N) >> 16 for 8-bit N via two hardware muls: exact because
    // result = (xh*N + (xl*N >> 8)) >> 8 and the dropped low byte can
    // never carry ((u+thi)*256 + tlo >= 65536 needs tlo >= 256). gcc
    // was emitting a ~25-instruction 32-bit shift-add chain here.
    uint16_t x = rnd16();
    uint8_t r, t;
    asm("mul %A[x], %[n]   \n\t"
        "mov %[t], r1      \n\t"
        "mul %B[x], %[n]   \n\t"
        "add r0, %[t]      \n\t"
        "clr %[t]          \n\t"
        "adc r1, %[t]      \n\t"
        "mov %[r], r1      \n\t"
        "clr __zero_reg__  \n\t"
        : [r]"=&r"(r), [t]"=&r"(t)
        : [x]"r"(x), [n]"r"(nn)
        : "r0", "r1");
    return r;
#else
    return (uint8_t)(((uint32_t)rnd16() * nn) >> 16);
#endif
}

// Fused PROGMEM read + post-increment: avr-libc's pgm_read_byte(p++) emits
// `lpm; adiw` (read then a separate 2-cyc increment); `lpm Z+` does both in
// lpmNext/FOR_EACH_NEIGHBOR moved to neighbor_table.h (08-29): shared
// with game.cpp's rules walks. lpmNext aliases nbNext for old call sites.
#define lpmNext nbNext

static inline __attribute__((always_inline)) uint8_t newMark() {
    if(++markEpoch == 0) {
        memset(simMark, 0, sizeof(simMark));
        markEpoch = 1;
    }
    return markEpoch;
}

// Board index -> packed (y<<4)|x. AVR has no hardware divide, so the
// pervasive pos%9 / pos/9 split compiled to __udivmodqi4, a ~40-cycle
// bit loop. The hardware multiplier gives the result in a few cycles:
// y=(pos*57)>>9 is exact for every pos in 0..80 (checked to 0..255),
// and x = pos - y*9. Applied to the hot playout/prior/widen sites this
// is play-identical (verified bit-for-bit) for ~0.4% off the whole
// search — small because the remaining divide time is the 16/32-bit
// UCB win-rate divides, which have variable divisors and stay.
PROGMEM const uint8_t POSXY_TAB[81] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88
};
// High nibble of a packed posXY byte. At -Os GCC lowers `xy >> 4`
// through int promotion into a 4-iteration asr/ror loop (~12 cycles,
// found by the 2026-08 asm audit in isOwnEye); AVR's swap+andi does it
// in 2. Exact for every uint8 input.
static inline __attribute__((always_inline)) uint8_t xyHi(uint8_t v) {
#if defined(ARDUINO) && !defined(ESP32) && !defined(ARDUINO_ARCH_ESP32)
    asm("swap %0" : "+r"(v));
    return v & 0x0F;
#else
    return v >> 4;
#endif
}

static uint8_t posXY(uint8_t pos) {
    // one lpm instead of mul/shift/sub (~7 cycles saved per call on
    // the per-candidate prior path)
    return pgm_read_byte(POSXY_TAB + pos);
}

static uint8_t groupLibsCore(uint8_t start, uint8_t markAll, uint8_t cap);
static uint8_t glcL1, glcL2;   // first two liberties of the last flood


// ============== experimental neighbour walkers (gated) ==============
// Jay's packed-neighbour family, 2026-08. Three encodings were built
// and measured, all provably correct (movecmp byte-identical while
// active), all beaten by the sentinel lpm walk -- lpm Z+ hands over a
// ready absolute index in 3 cycles with zero decode. PACKED_NBR
// (delta-or-zero fields, +4.6% mid) and PACKED_PRESCAN (+1.8% mid)
// were deleted after measurement (git 86dfb0a / e827091); the trit
// stream below is kept as the family's representative.
// Verdict: PACKED_TRIT +7.5% mid (/9 %9 digit split ~10 cy/direction).
// Encoding: see NBR_TRIT in neighbor_table.h.

// ====================================================================


// Does the group at start have any liberty? Dedicated flood: unlike the
// shared core it keeps no liberty list, count, or dedup — it just bails
// on the first empty cell it sees. This is the hottest liberty query
// (simPlay runs it per opponent neighbor and per ungated move), so the
// leaner inner loop earns back its own flash. Bit-identical: a boolean
// "any liberty" is independent of flood order.
// On a full flood (return 0 = no liberty = captured), hasLiberty leaves the
// whole group in floodScratch[0..capturedGroupN-1] so removeGroup can sweep
// it instead of re-flooding (its only caller runs it right after).
uint8_t capturedGroupN;   // non-static 08-29: game's captureGroup
                          // sweeps the same list (see game.cpp)

// (O2 attribute dropped 2026-08-15: it predated the newMark inline and
// the simPlay same-chain dedup, and on the current tree -Os measures
// FASTER (-0.38%) and -26 B. Stale flavor attributes rot — re-probe
// them after structural changes.)
uint8_t hasLiberty(uint8_t start, uint8_t color) {   // non-static 08-29:
    // game.cpp's rules checks sync game.board -> simBoard (memcpy) and
    // reuse this flood instead of owning a second one
    // color = simBoard[start], passed in: every hot caller already
    // holds it in a register, and the reload cost ~6 cycles x 180K
    // calls/think
    // Seed pre-scan (same shape as groupLibsCore's): most queries find an
    // empty neighbour of the seed itself, so scan those four cells before
    // paying for newMark and the flood machinery. Same-colour neighbours
    // are recorded straight into floodScratch[1..] as we go -- dead
    // scratch if an empty settles it, and exactly the stack the old
    // first-iteration scan would have pushed (same table order) if not.
    uint8_t *rp = &floodSlot(0);
    uint8_t *wp = rp + 1;
    {
        // Unrolled sentinel walk (Jay): same lpm Z+ mechanism, no
        // loop-back branch. Every cell has AT LEAST two neighbours
        // (corners have exactly two), so the first two steps skip the
        // sentinel test entirely; at most 4 neighbours means the 5th
        // byte is never read, and the last read drops the pointer
        // post-increment (dead afterwards).
        const uint8_t *e = NEIGHBOR_TABLE + start * 5;
        uint8_t q, s;
#define PS_BODY \
        s = boardAt(q); \
        if(s == EMPTY) return 1; \
        if(s == color) *wp++ = q;
        q = lpmNext(e); PS_BODY
        q = lpmNext(e); PS_BODY
        q = lpmNext(e); if(q & 0x80) goto prescanDone; PS_BODY
        q = pgm_read_byte(e); if(q & 0x80) goto prescanDone; PS_BODY
#undef PS_BODY
prescanDone:;
    }
    uint8_t em = newMark();
    // BFS with chasing read/write POINTERS into floodScratch (indexed
    // access paid a 16-bit extend per push/pop; floodScratch's address
    // carries, so the boardAt trick can't apply -- pointers can):
    // stones are appended (wp) and consumed front-to-back (rp), so the
    // group accumulates append-only and is still there on a full
    // flood. Done when rp meets wp. The seed's scan already happened
    // above, so slot 0 is stamped for the group list and the loop
    // starts from its recorded neighbours. A doubled seed neighbour
    // (two list entries of one chain) dedups here at mark time exactly
    // as the old push-time check did.
    *rp++ = start;
    *markPtr(start) = em;
    for(uint8_t *dp = rp; dp != wp; dp++) *markPtr(*dp) = em;
    while(rp != wp) {
        uint8_t p = *rp++;
        // NOT unrolled: +0.88% pre-always_inline, retested +3.3%
        // WITH inlining enforced -- so unlike the seed scan this is
        // genuine allocation cost (markPtr body x4 with rp/wp live
        // across every copy), not the outlining trap. The rolled loop
        // shares its join points; this body is the wrong side of the
        // unroll boundary on its own merits.
        const uint8_t *e = NEIGHBOR_TABLE + p * 5;
        uint8_t q;
        while(!((q = lpmNext(e)) & 0x80)) {   // sign-bit sentinel
            uint8_t s = boardAt(q);
            if(s == EMPTY) return 1;
            if(s == color) {
                uint8_t *mp = markPtr(q);
                if(*mp != markEpoch) {
                    *mp = markEpoch;
                    *wp++ = q;
                }
            }
        }
    }
    capturedGroupN = (uint8_t)(wp - &floodSlot(0));
    return 0;
}

// Remove the group hasLiberty just found to be captured. It left the whole
// group in floodScratch[0..capturedGroupN-1] (its sole caller, simPlay, runs
// hasLiberty(start)==0 immediately before this), so sweep the list -- no
// second flood. INVARIANT: only valid right after hasLiberty(start) == 0.
static uint8_t removeGroup(uint8_t start) {
    (void)start;
    uint8_t n = capturedGroupN;
    for(uint8_t i = 0; i < n; i++) simBoard[floodSlot(i)] = EMPTY;
    return n;
}

static uint8_t groupLibsFind(uint8_t start);

// If the group at start has exactly one liberty, return it; else 0xFF.
// (Thin wrapper: groupLibsFind's early-exit-at-3 does the same flood
// with marginally more work than a dedicated exit-at-2 — the ~150
// bytes of flash matter more than those cycles.)
static uint8_t soleLiberty(uint8_t start) {
    return groupLibsCore(start, 0, 2) == 1 ? glcL1 : 0xFF;
}

// Shared flood core for ALL the liberty finders: counts distinct
// liberties, early-exiting once `cap` are found (1 = hasLiberty,
// 2 = soleLiberty, 3 = full find — profiling showed hasLiberty
// routed through a fixed cap-3 version was HALF of all search time).
// With markAll it floods the WHOLE group into the CURRENT mark epoch
// (membership tests need complete marking; cap is ignored).
// Returns the count; the first two liberties land in glcL1/glcL2
// statics. (History: pointer out-params lost to a packed uint32
// return; the 2026-08 asm audit then showed -Os builds that pack
// with zeroed-register ORs and every caller pays shift chains to
// unpack -- two sts here + two lds at the caller beat both.)
static uint8_t groupLibsCore(uint8_t start, uint8_t markAll, uint8_t cap) {
    uint8_t color = boardAt(start);
    uint8_t lib1 = 0xFF, lib2 = 0xFF;
    uint8_t count = 0;
    uint8_t sp = 0;

    if(!markAll) {
        // Seed fast path: scan the seed's neighbours once -- count its
        // empty liberties and push its same-colour neighbours STRAIGHT
        // onto the flood stack (no same[4] buffer: the entries are dead
        // scratch if enough empties settle it early, and pushing here
        // keeps table order for the flood). Enough empties settles it
        // before any flood setup (the common case); otherwise marks are
        // stamped from the stack once newMark has run, and the flood
        // continues from those neighbours -- the seed is never
        // re-scanned.
        // Unrolled with a SINGLE exit (the allocator workaround): the
        // in-loop cap-return gave the unrolled DAG four early exits
        // and -Os answered with three extra saved register pairs per
        // call (+8.75%!). Deferring the cap test to one join point
        // keeps each copy tiny. Value-identical: lib1/lib2 only fill
        // empty slots so later empties can't disturb them, the extra
        // scratch pushes are dead on the return path, and the return
        // clamps to cap exactly as the early exit did.
        const uint8_t *e = NEIGHBOR_TABLE + start * 5;
        uint8_t q, s;
        // Cap exits restored per step (Jay): with always_inline
        // enforced the outlining trap is gone, so the early return
        // pays only its own branch. Step 1 needs no check (count <= 1
        // is below every cap); in the hot cap=3 clone GCC's value
        // propagation deletes step 2's check the same way it deleted
        // the dead lib1/lib2 tests.
#define GL_SEED_BODY(CAPCHK) \
        s = boardAt(q); \
        if(s == EMPTY) { \
            if(lib1 & 0x80) lib1 = q; \
            else if(lib2 & 0x80) lib2 = q; \
            count++; \
            CAPCHK \
        } else if(s == color) floodSlot(sp++) = q;
#define GL_CAP \
        if(count >= cap) { \
            glcL1 = lib1; \
            glcL2 = lib2; \
            return count; \
        }
        q = lpmNext(e); GL_SEED_BODY()
        q = lpmNext(e); GL_SEED_BODY(GL_CAP)
        q = lpmNext(e); if(q & 0x80) goto glcSeedTally; GL_SEED_BODY(GL_CAP)
        q = pgm_read_byte(e); if(q & 0x80) goto glcSeedTally; GL_SEED_BODY(GL_CAP)
#undef GL_SEED_BODY
#undef GL_CAP
glcSeedTally:;
        { uint8_t em = newMark();
          *markPtr(start) = em;
          for(uint8_t k = 0; k < sp; k++)
              *markPtr(floodSlot(k)) = em; }
    } else {
        // markAll floods into the CURRENT epoch (see header) -- no newMark
        floodSlot(sp++) = start;
        *markPtr(start) = markEpoch;
    }

    while(sp && (markAll || count < cap)) {
        uint8_t p = floodSlot(--sp);
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, p) {
            uint8_t s = boardAt(q);
            if(s == EMPTY) {
                if(count < 3 && q != lib1 && q != lib2) {
                    if(lib1 & 0x80) lib1 = q;          // sign-bit sentinel
                    else if(lib2 & 0x80) lib2 = q;
                    count++;
                    if(!markAll && count >= cap) break;
                }
            } else if(s == color) {
                uint8_t *mp = markPtr(q);
                if(*mp != markEpoch) {
                    *mp = markEpoch;
                    floodSlot(sp++) = q;
                }
            }
        }
    }
    glcL1 = lib1;
    glcL2 = lib2;
    return count;
}

// Find a group's distinct liberties, early-exiting at 3. Fills l1/l2
// with the first two (0xFF if fewer). Returns the count, 0-3.
static inline __attribute__((always_inline)) uint8_t groupLibsFind(uint8_t start) {
    return groupLibsCore(start, 0, 3);
}

static inline __attribute__((always_inline)) uint8_t groupLibsMax3(uint8_t start) {
    uint8_t n = groupLibsFind(start);
    return n ? n : 1; // preserve old behavior: 0 liberties reads as 1
}

// Full-flood variant of groupLibsFind (see groupLibsCore's markAll)
static uint8_t groupLibsMark(uint8_t start) {
    return groupLibsCore(start, 1, 3);
}

#define SETTLED_REGION_MAX 8
// Vital point of a small marked empty region: the UNIQUE cell of max
// degree within it (region[0..cnt-1], all in the current mark epoch),
// or 0xFF if none. Shared by regionVital and its lazy cache stamp;
// bestDeg/ties feed the square-four gate.
__attribute__((optimize("O2")))
static uint8_t regionVitalCell(const uint8_t *region, uint8_t cnt,
                               uint8_t *bestDeg, uint8_t *ties) {
    uint8_t bd = 1, bc = 0xFF, t = 0;
    for(uint8_t j = 0; j < cnt; j++) {
        uint8_t deg = 0, q;
        FOR_EACH_NEIGHBOR(q, region[j])
            if(simBoard[q] == EMPTY && simMark[q] == markEpoch) deg++;
        if(deg > bd) { bd = deg; bc = region[j]; t = 1; }
        else if(deg == bd) t++;
    }
    *bestDeg = bd; *ties = t;
    return bc;
}
// Build the chain map: flood each chain to assign ids and count
// liberties, then a second cheap walk stamps the libs bits. A lazy
// eyespace map also lives in the empty cells' top bits, filled on
// demand by regionVital; buildChainMap just clears its cache flags.
__attribute__((optimize("O2")))
static void buildChainMap() {
    memset(chainId, 0, sizeof(chainId));
#ifdef EYE_BITMAPS
#if defined(EYE_PRIOR) || defined(EYE_PRIOR2) || defined(EYE_INJECT)
    memset(weakLibs, 0, sizeof(weakLibs));   // experiment flags only
#endif
    memset(weakLibsC, 0, sizeof(weakLibsC));
    memset(vitalLibs, 0, sizeof(vitalLibs));
#endif
    uint8_t nextId = 0;
    for(uint8_t s = 0; s < BOARD_CELLS; s++) {
        if(simBoard[s] == EMPTY || chainId[s]) continue;
        uint8_t color = simBoard[s];
        uint8_t id = nextId < 63 ? ++nextId : 63;
        uint8_t lib1 = 0xFF, lib2 = 0xFF, count = 0;
#ifdef EYE_BITMAPS
        uint8_t exLibs[6], exN = 0, chSz = 0;
#endif
        // Append-only flood (the hasLiberty pattern): members accumulate
        // in floodScratch as the BFS consumes them front-to-back (rp reads,
        // wp appends), so the whole chain is still listed when rp meets wp
        // -- no second flood to stamp the capped lib class, just a flat
        // walk of that list. Chain size <= board < 81 = floodScratch cap.
        uint8_t *base = &floodSlot(0), *rp = base, *wp = base;
        *wp++ = s;
        chainId[s] = id;
        while(rp != wp) {
            uint8_t p = *rp++;
#ifdef EYE_BITMAPS
            chSz++;
#endif
            uint8_t q;
            FOR_EACH_NEIGHBOR(q, p) {
                if(boardAt(q) == EMPTY) {
                    if(count < 3 && q != lib1 && q != lib2) {
                        if(lib1 == 0xFF) lib1 = q;
                        else if(lib2 == 0xFF) lib2 = q;
                        count++;
                    }
#ifdef EYE_BITMAPS
                    { uint8_t j = 0;
                      while(j < exN && exLibs[j] != q) j++;
                      if(j == exN && exN < 6) exLibs[exN++] = q; }
#endif
                } else if(boardAt(q) == color && !chainId[q]) {
                    chainId[q] = id;
                    *wp++ = q;
                }
            }
        }
#ifdef EYE_BITMAPS
        // weak = <=4 exact libs, or 5 with real size (lone 5-lib fresh
        // stones are not fights); 6-dedupe cap means exN==6 reads safe.
        if(exN <= 4 || (exN == 5 && chSz >= 2)) {
            uint8_t bestDeg = 0, bestJ = 0;
            for(uint8_t j = 0; j < exN; j++) {
                uint8_t d = 0, q;
                FOR_EACH_NEIGHBOR(q, exLibs[j]) {
                    for(uint8_t m = 0; m < exN; m++)
                        if(exLibs[m] == q) { d++; break; }
                }
                if(d > bestDeg) { bestDeg = d; bestJ = j; }
            }
            for(uint8_t j = 0; j < exN; j++) {
                uint8_t lb = exLibs[j];
#if !defined(EYE_VITALONLY) && (defined(EYE_PRIOR) || defined(EYE_PRIOR2) || defined(EYE_INJECT))
                weakLibs[lb >> 3] |= bitMask(lb);
#endif
                weakLibsC[color - 1][lb >> 3] |= bitMask(lb);
            }
#ifdef EYE_VITALONLY
            { uint8_t lb = exLibs[bestJ];
              weakLibs[lb >> 3] |= bitMask(lb); }
#endif
            if(bestDeg) {
                uint8_t lb = exLibs[bestJ];
                vitalLibs[lb >> 3] |= bitMask(lb);
            }
        }
#endif
        // Stamp the capped lib class (count << 6) into every member from
        // the accumulated list. count==0 (a 0-liberty chain, only on an
        // illegal board) leaves bits==0 -> the OR is a no-op, so the guard
        // just skips a dead pass.
        uint8_t bits = count << 6;
        if(bits)
            for(uint8_t *dp = base; dp != wp; dp++)
                chainId[*dp] |= bits;
    }
    // Fresh eyespace cache for this node's board (see regionVital).
    memset(regionDone, 0, sizeof(regionDone));
}

// pos touches two DISTINCT friendly chains. Is it their ONLY connecting
// point? A connector is an empty cell adjacent to both chains, i.e. a
// liberty of chain A that also touches chain B — so flood A from a seed
// stone and check its empty neighbours, instead of rescanning the whole
// board. Same-colour distinct chains are never adjacent, so the flood
// stays within A. Exactly one connector (pos itself) means the opponent
// playing here splits us for real; two or more is miai (no urgency —
// bonusing those over-concentrated and measurably tanked an earlier
// connect prior).
__attribute__((optimize("O2")))
static uint8_t soleConnector(uint8_t seedA, uint8_t idB) {
    uint8_t colorA = boardAt(seedA);
    uint8_t connectors = 0;
    newMark();
    uint8_t sp = 0;
    floodSlot(sp++) = seedA;
    simMark[seedA] = markEpoch;
    while(sp) {
        uint8_t p = floodSlot(--sp);
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, p) {
            if(simMark[q] == markEpoch) continue;
            if(simBoard[q] == colorA) {         // extend the flood over chain A
                simMark[q] = markEpoch;
                floodSlot(sp++) = q;
            } else if(simBoard[q] == EMPTY) {   // a liberty of A — connector?
                simMark[q] = markEpoch;         // dedup this empty cell
                uint8_t r, nearB = 0;
                FOR_EACH_NEIGHBOR(r, q)
                    if(CHAIN_OF(chainId[r]) == idB) { nearB = 1; break; }
                if(nearB && ++connectors > 1) return 0;
            }
        }
    }
    return connectors == 1;
}

// Flood the small empty region containing `seed` (a big region is
// neither settled territory nor an eyespace). Returns the color of
// the stones bordering it — 0 if it touches both colors or is too
// open — and sets *vital to the region's vital point, 0xFF if none.
//
// The vital point is the unique cell with the most neighbors inside
// the region (degree >= 2): center of a straight/bent three, center
// of a T/pyramid four, center of a bulky/cross five. Uniqueness
// makes the classics come out right by itself — line four has no
// single deciding point and correctly yields none; square four
// (dead from any entry) is special-cased below.
// Whoever plays the vital point decides the region's life: the owner
// splits it into two eyes, the opponent reduces it to one.
//
// Non-vital moves inside a settled region are pointless-to-harmful
// under the Japanese rules the game scores by (own fill: -1 point;
// hopeless invasion: gifts a prisoner), but the area-scoring
// playouts think they are free.
// Returns (vital<<8) | ownerCode: low byte 0 = unsettled/open (as the
// old uint8_t return), high byte = the vital cell or 0xFF. Packed so no
// caller needs an out-param (the pointer plumbing showed in the
// profile; one hot caller only wants the cache side-effect).
// STAMP selects the lazy-cache write-back at compile time: the widen
// scan and the root passes need it, but the PLAYOUT eyespace hook
// never reads the cache (chainId/regionDone are rebuilt by the next
// buildChainMap -- the stamps were pure wasted stores there, ~2% of
// think). Template clone: the stampless copy also gets leaner register
// allocation, worth ~0.5% over a runtime flag; the +304B clone cost is
// paid for by the renderScoring table shrink (F1, -294B).
template<uint8_t STAMP>
static uint16_t regionVitalT(uint8_t seed) {
    uint8_t region[SETTLED_REGION_MAX];
    uint8_t cnt = 0, head = 0;
    uint8_t owner = 0, unsettled = 0;
    uint8_t vit = 0xFF;
    newMark();
    region[cnt++] = seed;
    simMark[seed] = markEpoch;
    while(head < cnt && !unsettled) {
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, region[head++]) {
            uint8_t s = boardAt(q);
            if(s != EMPTY) {
                if(!owner) owner = s;
                else if(owner != s) { unsettled = 1; break; } // both colors
                continue;
            }
            if(simMark[q] == markEpoch) continue;
            if(cnt >= SETTLED_REGION_MAX) { unsettled = 1; break; } // too open
            simMark[q] = markEpoch;
            region[cnt++] = q;
        }
    }
    if(!unsettled && owner && cnt >= 3 && cnt <= 6) {
        uint8_t bestDeg, ties;
        uint8_t bestCell = regionVitalCell(region, cnt, &bestDeg, &ties);
        if(bestCell != 0xFF && ties == 1) vit = bestCell;
        // Square four: the one shape the unique-max rule misses.
        // All four cells tie at degree 2 (a straight four has two
        // degree-1 ends, so this signature is unambiguous). It is a
        // DEAD shape — any cell starts the kill, and what remains
        // is a bent three whose center the unique-max rule then
        // finds. SCORING ONLY: in live play a 2x2 pocket usually
        // belongs to a group with eyes elsewhere, and treating it
        // as vital invited gift-stone invasions and own-eye fills
        // (160-game referee dropped 26 -> 17 before this gate).
        else if(scoreMode && cnt == 4 && bestDeg == 2 && ties == 4)
            vit = bestCell;
    }
    // Lazy eyespace cache: stamp the flooded cells (<=8) with the region
    // code (bits 6-7: 1=black, 2=white, 0=open; the vital cell = 3) and
    // flag them done, so other candidates in this region skip the flood.
    // Compiled out for STAMP=0 callers (see the template comment).
    if(STAMP) {
        uint8_t code6 = (uint8_t)(((unsettled || !owner) ? 0 : owner) << 6);
        for(uint8_t j = 0; j < cnt; j++) {
            uint8_t c = region[j];
            uint8_t *cp = chainPtr(c);  // carry-free RMW, no 16-bit extend
            *cp = (*cp & 0x3F) | code6;
            uint8_t cb = c >> 3;   // 8-bit shift (promoted index = 16-bit loop)
            regionDone[cb] |= bitMask(c);
        }
        if(vit != 0xFF) {
            uint8_t *vp2 = chainPtr(vit);
            *vp2 = (*vp2 & 0x3F) | (3 << 6);
        }
    }
    return ((uint16_t)vit << 8) | (unsettled ? 0 : owner);
}
// Stamped variant = the historical regionVital; the playout call site
// uses regionVitalT<0> directly.
__attribute__((noinline)) static inline uint16_t regionVital(uint8_t seed) { return regionVitalT<1>(seed); }



// Bit pos set iff cell pos is >=2 from every board edge (the interior
// 5x5): the "big open point" edge test (widenNode) and, complemented,
// the line-1/2 test (the BDEF playout step).
static const uint8_t PROGMEM FAR_BITMAP[11] = {
    0x00, 0x00, 0xF0, 0xE1, 0xC3, 0x87, 0x0F, 0x1F, 0x00, 0x00, 0x00
};

// All orthogonal neighbors own color (or edge)
static uint8_t isOwnEye(uint8_t pos, uint8_t color) {
    // Unrolled sentinel walk (the pre-scan recipe): min degree 2 makes
    // the first two sentinel checks dead, the 5th byte is never read,
    // and the last read drops its dead post-increment. Smallest body
    // in the engine -- one probe, one compare.
    const uint8_t *e = NEIGHBOR_TABLE + pos * 5;
    uint8_t q;
    q = lpmNext(e);
    if(boardAt(q) != color) return 0;
    q = lpmNext(e);
    if(boardAt(q) != color) return 0;
    q = lpmNext(e);
    if(!(q & 0x80)) {
        if(boardAt(q) != color) return 0;
        q = pgm_read_byte(e);
        if(!(q & 0x80) && boardAt(q) != color) return 0;
    }
    // False-eye test (michi's is_eye): enemy-held diagonals make this
    // a connection point, not an eye -- an "eye" whose diagonals the
    // enemy controls must eventually be filled to connect, and the
    // crude all-orthogonals rule made such points unplayable for both
    // the playouts AND widen admission (hunt game 5110: the four-way
    // connection E5 held a +19 prior and never entered the tree).
    // Interior: false iff 2+ enemy diagonals; edge/corner: the
    // off-board side counts as one false, so 1 enemy diagonal kills it.
    {
        uint8_t xy = posXY(pos);
        uint8_t x = xy & 0x0F, y = xyHi(xy);
        uint8_t opp = 3 - color;
        uint8_t falses =
            (x == 0 || x == BOARD_SIZE - 1 ||
             y == 0 || y == BOARD_SIZE - 1) ? 1 : 0;
        for(int8_t dy = -1; dy <= 1; dy += 2) {
            if((uint8_t)(y + dy) >= BOARD_SIZE) continue;
            for(int8_t dx = -1; dx <= 1; dx += 2) {
                if((uint8_t)(x + dx) >= BOARD_SIZE) continue;
                if(simBoard[pos + dy * BOARD_SIZE + dx] == opp) falses++;
            }
        }
        if(falses >= 2) return 0;
    }
    return 1;
}

// ---- Ladder-read journal undo (2026-08-15) ----
// ladderEscapes' 81-byte pre-read snapshot memcpy was ~its whole cost
// (per call, even instant-dead reads). The chase mutates the board two
// ways only: stones played on pre-read-EMPTY cells (journaled below),
// and captures. Captures are handled by a LAZY snapshot taken inside
// simPlay at the instant the first capture is about to run: the board
// there equals pre-read + journaled plays (the capturing stone is
// journaled by the caller BEFORE its simPlay call), so copying the
// board and re-emptying the journaled cells reconstructs the exact
// pre-read state. Restore: full memcpy back if snapshotted, else just
// re-empty the journaled plays. Value-identical by construction: both
// paths restore the exact pre-read board; no draw or verdict changes.
// The journal is a small static (stack is high-water-critical on the
// ladder path); 2*maxSteps <= 40 entries by the callers' caps.
static uint8_t ladderBoard[BOARD_CELLS];   // lazy snapshot (pre-read image)
static uint8_t ladderJournal[40];
static uint8_t ladderJMode;      // 1 = journal active (inside a read)
static uint8_t ladderSnapped;    // 1 = ladderBoard holds the snapshot
static uint8_t ladderJN;         // journaled play count
__attribute__((noinline)) static void ladderSnapNow(void) {
    memcpy(ladderBoard, simBoard, BOARD_CELLS);
    for(uint8_t i = 0; i < ladderJN; i++)
        ladderBoard[ladderJournal[i]] = EMPTY;
    ladderSnapped = 1;
}
// Play on simBoard. Returns new ko point, NO_KO, or ILLEGAL.
// noSelfAtari additionally rejects non-capturing moves that leave the
// placed group at one liberty (the playout policy). Cheap by
// construction: when nothing was captured the only mutation is the
// placed stone, so rejection is a one-byte undo — no snapshot needed.
__attribute__((noinline)) static uint8_t simPlay(uint8_t pos, uint8_t color, uint8_t ko,
                       uint8_t noSelfAtari = 0) {
    if(pos == MOVE_PASS) return NO_KO;
    uint8_t *bp = boardPtr(pos);   // one address calc for the read AND write
    if(pos == ko || *bp != EMPTY) return ILLEGAL;

    uint8_t opp = 3 - color;
    *bp = color;

    // ONE fused neighbour walk (was three): the capture check, the
    // fast-path classification (empties a/b/e, connectivity) and the
    // material for ko detection all come from this pass.
    // - The a/b/e/connected tallies are consumed only when captured==0,
    //   exactly when the board was unchanged during the walk, so they
    //   equal what a second walk would see. (When a capture DID happen
    //   mid-walk the tallies can be stale -- and are unused.)
    // - Ko (captured==1, single stone off): post-capture liberties of
    //   the placed lone stone = e + 1 (the captured cell), no other
    //   neighbour changed; lone = !connected. So the old third walk is
    //   `!connected && e == 0`.
    // Working-set pack (2026-08-15): e (bits 0-3), noSelfAtari (bit 6)
    // and connected (bit 7) share one byte, and capPos is recomputed in
    // the rare ko branch instead of held across the flood calls — three
    // fewer callee-saved registers on a ~130k-calls/think function
    // (prologue/epilogue was 7.7% of think). e <= 4 so the low nibble
    // never carries into the flag bits.
    uint8_t captured = 0;
    uint8_t a = 0xFF, b = 0xFF;
    uint8_t ec = noSelfAtari ? 0x40 : 0;
    uint8_t q;
    // Same-chain dedup (2026-08-15): see comment in the eyefold variant.
    uint8_t e0 = markEpoch;
    FOR_EACH_NEIGHBOR(q, pos) {
        uint8_t s = boardAt(q);
        if(s == opp) {
            if(markEpoch != e0 && *markPtr(q) == markEpoch)
                ;   // same chain as an already-flooded neighbour: alive
            else if(!hasLiberty(q, opp)) {
                // ladder-journal lazy snapshot: must run before the
                // FIRST capture of a journaled read (see ladderSnapNow)
                if(ladderJMode && !ladderSnapped) ladderSnapNow();
                captured += removeGroup(q);
            }
        } else if(s == EMPTY) {
            // sign-bit test for the 0xFF sentinel (form probe)
            if(a & 0x80) a = q; else if(b & 0x80) b = q;
            ec++;
        } else
            ec |= 0x80;
    }

    if(!captured) {
        if(ec & 0x40) {
            // Immediate-liberty fast-path (Pachi): a placement with no
            // same-colour neighbour is its own group, so its liberties
            // are exactly its empty neighbours -- in the same order
            // groupLibsFind reports them -- and no flood is needed. Only
            // a connected placement can borrow liberties and still floods.
            // One flood/scan covers both suicide (0 libs) and self-atari
            // (1); the result is cached for the next playout move, which
            // classifies this same group on an unchanged board.
            uint8_t libs;
            if(ec & 0x80) {
#ifdef PLAYOUT_STATS
                if((ec & 0x0F) >= 2) spFloodE2++;
#endif
                libs = groupLibsFind(pos);
                a = glcL1;
                b = glcL2;
            } else
                libs = ec & 0x0F;
#ifdef PLAYOUT_STATS
            if(libs >= 2) { if(ec & 0x80) spFloodOK++; else spLoneOK++; }
            else          { if(ec & 0x80) spFloodRej++; else spLoneRej++; }
#endif
            if(libs < 2) {
#if PLAYOUT_THROWIN_RATE
                // throw-in exception (see PLAYOUT_THROWIN_RATE above)
                if(!(!(ec & 0x80) && libs == 1 &&
                     (PLAYOUT_THROWIN_RATE == 1 ||
                      (rnd16() & (PLAYOUT_THROWIN_RATE - 1)) == 0)))
#endif
                {
                    // barrier: recompute &simBoard[pos] here (4 cycles,
                    // reject path) instead of holding it in a saved
                    // register pair across every call (8 cycles, always)
                    asm volatile("" : "+r"(pos));
                    simBoard[pos] = EMPTY;
                    return ILLEGAL;
                }
            }
            cacheLibsPos = pos;
            cacheLibs = libs;
            cacheL1 = a;
            cacheL2 = b;
        } else {
            if(!hasLiberty(pos, color)) { // suicide
                asm volatile("" : "+r"(pos));  // barrier: see above
                simBoard[pos] = EMPTY;
                return ILLEGAL;
            }
            // Ungated success: no flood ran, so no cache. Invalidate —
            // a capture may even have re-emptied the cached position.
            cacheLibsPos = 0xFF;
        }
    } else {
        cacheLibsPos = 0xFF;
    }
    simCaptured = captured;

    // Simple ko (see the fused-walk comment above). capPos recompute:
    // with e==0 there were NO pre-capture empty neighbours, so after a
    // single-stone capture the ONLY empty neighbour IS the captured
    // cell — a 4-cell rescan recovers it exactly, in the rare branch,
    // instead of a callee-saved register on every call.
    if(captured == 1 && !(ec & 0x8F)) {
        FOR_EACH_NEIGHBOR(q, pos)
            if(boardAt(q) == EMPTY) return q;
    }
    return NO_KO;
}

// (ladderBoard is declared beside the journal machinery above)

#ifdef NNOPEN
// ==================== TINY OPENING NET ====================
// Distilled KataGo opening policy (Jay's idea, 2026-08): int8 net over
// hand features, trained on the expected-winrate-loss ("competitive")
// objective. Picks the opening move instantly while the position is
// quiet, superseding think() below NN_MAX_STONES. Borrows think-scoped
// scratch (simBoard/simMark/ladderBoard) — zero standing RAM beyond
// the 1-byte last-move tracker. Weights: nn_open_weights.h (export:
// scratchpad nn_export.py). Feature layout MUST mirror nn_train3d.py;
// parity-checked by test/nnprobe.cpp against the Python int emulation.
#include "nn_open_weights.h"
// ---- two-net selection (opening net + DAGGER cost-net prior) --------------
// NN_TWONET adds a SECOND weight set (nn_prior_weights.h, NN_P_*) used ONLY for
// the net-priored root scores (nnPriorMode); the shipped opening net still
// drives opening-direct (NN_MAX_STONES). Without NN_TWONET everything below
// resolves to the single shipped net (NN_*) and is byte-identical to the ship
// build. A_* pick the active weight set; A_H/A_KS the active shape+scale.
#ifdef NN_TWONET
#include "nn_prior_weights.h"
static const uint8_t *nnA_B  = (const uint8_t *)NN_B,  *nnA_W  = (const uint8_t *)NN_W,
                     *nnA_E  = (const uint8_t *)NN_E,  *nnA_EB = (const uint8_t *)NN_EB,
                     *nnA_B1 = (const uint8_t *)NN_B1, *nnA_V  = (const uint8_t *)NN_V,
                     *nnA_F  = (const uint8_t *)NN_F,  *nnA_LF = (const uint8_t *)NN_LF;
static uint8_t nnA_H  = NN_H;
static int32_t nnA_KS = NN_KSCALE;
#define A_B  nnA_B
#define A_W  nnA_W
#define A_E  nnA_E
#define A_EB nnA_EB
#define A_B1 nnA_B1
#define A_V  nnA_V
#define A_F  nnA_F
#define A_LF nnA_LF
#define A_H  nnA_H
#define A_KS nnA_KS
#define NNA_MAXH ((NN_H) > (NN_P_H) ? (NN_H) : (NN_P_H))
#else
#define A_B  ((const uint8_t *)NN_B)
#define A_W  ((const uint8_t *)NN_W)
#define A_E  ((const uint8_t *)NN_E)
#define A_EB ((const uint8_t *)NN_EB)
#define A_B1 ((const uint8_t *)NN_B1)
#define A_V  ((const uint8_t *)NN_V)
#define A_F  ((const uint8_t *)NN_F)
#define A_LF ((const uint8_t *)NN_LF)
#define A_H  NN_H
#define A_KS NN_KSCALE
#define NNA_MAXH NN_H
#endif
static int8_t patternBonus(int8_t cx, int8_t cy, uint8_t color); // defined below
#if NN_BITS <= 4
__attribute__((noinline)) static int8_t nnRd(const uint8_t *t, uint16_t i) {   // signed nibble unpack
    uint8_t b = pgm_read_byte(t + (i >> 1));
    uint8_t v = (i & 1) ? (b >> 4) : (b & 0xF);
    return (int8_t)((v ^ 8) - 8);
}
#define NNRD(t, i) nnRd((const uint8_t *)(t), (uint16_t)(i))
#else
#define NNRD(t, i) ((int8_t)pgm_read_byte((const uint8_t *)(t) + (i)))
#endif

// add one H-wide weight row (row*NN_H..+NN_H-1) into the pre[] accumulators
__attribute__((noinline)) static void nnAddRow(int16_t *pre, const uint8_t *t, uint16_t row) {
    uint16_t i = row * A_H;
    for(uint8_t h = 0; h < A_H; h++) pre[h] += NNRD(t, i + h);
}
// 4-neighbor of p in direction d (0..3), 0xFF off-board
__attribute__((noinline)) static uint8_t nnNb(uint8_t p, uint8_t d) {
    uint8_t x = p % 9, y = p / 9;
    int8_t nx = x + (d == 0) - (d == 1);
    int8_t ny = y + (d == 2) - (d == 3);
    if((uint8_t)nx > 8 || (uint8_t)ny > 8) return 0xFF;
    return (uint8_t)(ny * 9 + nx);
}
static uint8_t nnCheby(uint8_t p, uint8_t cx, uint8_t cy) {
    uint8_t px = p % 9, py = p / 9;
    uint8_t dx = px > cx ? px - cx : cx - px;
    uint8_t dy = py > cy ? py - cy : cy - py;
    return dx > dy ? dx : dy;
}

// factored micro-idioms (NN opening; speed irrelevant, force one copy)
static uint8_t nnEdge(uint8_t v) { return v < (uint8_t)(8 - v) ? v : (uint8_t)(8 - v); }
static uint8_t nnAbs(int8_t v) { return v < 0 ? -v : v; }
static uint8_t nnSat(uint8_t v, uint8_t n) { return v > n ? n : v; }
#ifndef NN_QUIET_MAXTEMP
#define NN_QUIET_MAXTEMP 3  // ship gate: let the NN (which beats search vs L0)
                            // handle more 2-lib-chain positions. 2->3 measured
                            // +76/2000 (33.0->36.8% vs L0); "looser is better"
                            // since the NN out-plays MCTS in its range. Ataris
                            // (1-lib) still hand off to search unconditionally.
#endif

__attribute__((noinline)) static uint8_t nnSymPos(uint8_t x, uint8_t y, uint8_t s, uint8_t &ox, uint8_t &oy) {
    if(s & 1) x = 8 - x;
    if(s & 2) y = 8 - y;
    if(s & 4) { uint8_t t = x; x = y; y = t; }
    ox = x; oy = y;
    return (uint8_t)(y * 9 + x);
}

// canon tie-break: is sym s lexicographically better than bestSym?
static uint8_t nnCanonBetter(uint8_t s, uint8_t bestSym) {
    uint8_t sI = (s & 4) ? (4 | ((s & 1) << 1) | ((s >> 1) & 1)) : s;
    uint8_t bI = (bestSym & 4) ? (4 | ((bestSym & 1) << 1) | ((bestSym >> 1) & 1)) : bestSym;
    for(uint8_t q = 0; q < 81; q++) {
        uint8_t px, py, qy = q / 9, qx = q % 9;
        uint8_t va = simBoard[nnSymPos(qy, qx, sI, px, py)];
        uint8_t vb = simBoard[nnSymPos(qy, qx, bI, px, py)];
        if(va == vb) continue;
        return vb == EMPTY || (va != EMPTY && va != WHITE);
    }
    return 0;
}

// threshold-ladder bucketizer: b = count of thresholds v exceeds.
// Replaces the ternary compare ladders (each ~30-60B at -Os).
static const int8_t NN_TH_GAIN[] PROGMEM = {0, 2, 5};    // dil gain / oppDilGain
static const int8_t NN_TH_PAT[]  PROGMEM = {-5, -1, 0, 2, 4}; // pattern buckets
static const int8_t NN_TH_GD[]   PROGMEM = {-8, -2, 1, 7};    // globDiff
static const int8_t NN_TH_LB[]   PROGMEM = {2, 3, 4};    // census lib bucket
static const int8_t NN_TH_12[]   PROGMEM = {1, 2};       // ela b2 / crossLD line
__attribute__((noinline)) static uint8_t nnBucket(int8_t v, const int8_t *thrs, uint8_t n) {
    uint8_t b = 0;
    while(b < n && v > (int8_t)pgm_read_byte(thrs + b)) b++;
    return b;
}

// nnOpeningMove runs before think() builds its node pool in sBuffer, so
// the screen buffer is free scratch here (same borrow as the pool).
// 81-byte maps replace bitsets: no 1<<(n&7) shift loops at -Os.
static uint8_t * const nnMapA = SB_BASE;        // weakMap
static uint8_t * const nnMapB = SB_BASE + 81;   // doneMap / deadMap
static uint8_t * const nnMapC = SB_BASE + 162;  // nnChain libSeen
static uint8_t * const nnMapD = SB_BASE + 243;  // nnChain seen
static uint8_t * const nnMapId = SB_BASE + 324; // census chain id

// flood a chain on simBoard from p; returns liberty count treating
// cells set in deadMap (byte map over 81) as EMPTY; chain cells -> cells[]
__attribute__((noinline)) static uint8_t nnChain(uint8_t p, const uint8_t *deadMap, uint8_t *cells, uint8_t &ncells) {
    uint8_t col = simBoard[p];
    uint8_t libs = 0;
    uint8_t *libSeen = nnMapC, *seen = nnMapD;
    memset(libSeen, 0, 162);          // both maps, contiguous
    uint8_t st[40]; uint8_t sp = 0;
    st[sp++] = p; seen[p] = 1;
    ncells = 0;
    while(sp) {
        uint8_t q = st[--sp];
        cells[ncells++] = q;
        uint8_t n;
        FOR_EACH_NEIGHBOR(n, q) {
            uint8_t empty = simBoard[n] == EMPTY || (deadMap && deadMap[n]);
            if(empty) {
                if(!libSeen[n]) {
                    libSeen[n] = 1;
                    libs++;
                }
            } else if(simBoard[n] == col && !seen[n]) {
                seen[n] = 1;
                st[sp++] = n;
            }
        }
    }
    return libs;
}

// manhattan-dilate a color's stones into an 81-bitset (radius r)
// Manhattan-distance map from a color's stones (generation BFS, cap 3).
// dist[p] = 0 for the stones, 1..3 rings, 0xFF beyond. (Jay's byte-board
// dilation: state@r == dist<=r, gain@r tests dist>r — replaces 6 bitsets.)
__attribute__((noinline)) static void nnDistMap(uint8_t col, uint8_t *dist) {
    memset(dist, 0xFF, 81);
    for(uint8_t p = 0; p < 81; p++)
        if(simBoard[p] == col) dist[p] = 0;
    for(uint8_t g = 0; g < 3; g++)
        for(uint8_t p = 0; p < 81; p++) {
            if(dist[p] != g) continue;
            uint8_t nd;
            FOR_EACH_NEIGHBOR(nd, p) {
                if(dist[nd] > g + 1) dist[nd] = g + 1;
            }
        }
}
// Liberty family for a candidate: captures, post-capture resulting-lib
// bucket (suicide-clamped), enemy-libs-after (with the trainer's
// captured-chain quirk). Fills deadMask; returns packed rlb | ela<<4 | cap<<7.
static const int8_t OD4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};

// One 81-cell pass gathering every per-stone statistic the features
// need: out[0]=no out[1]=ne out[2]=dens2 out[3]=dens4 out[4]=bro
// out[5]=bre out[6]=wd out[7]=ring(ro|re<<1)
static void nnStoneScan(uint8_t cx, uint8_t cy,
        uint8_t toMove, const uint8_t *cellLb, const uint8_t *weakMask,
        uint8_t *out) {
    uint8_t no = 9, ne = 9, dens2 = 0, dens4 = 0;
    uint8_t blo = 99, bro = 3, ble = 99, bre = 3;
    uint8_t wd = 4, ro = 0, re = 0;
    uint8_t cpos = cy * 9 + cx;
    for(uint8_t p = 0; p < 81; p++) {
        uint8_t v = simBoard[p];
        if(v == EMPTY || p == cpos) continue;
        uint8_t ch = nnCheby(p, cx, cy);
        if(v == toMove) {
            if(ch < no) no = ch;
            if(ch < blo || (ch == blo && cellLb[p] < bro)) { blo = ch; bro = cellLb[p]; }
#ifndef NN_DEVICE_TIER
            if(ch == 2) ro = 1;
#endif
        } else {
#ifndef NN_CORE_TIER
            if(ch < ne) ne = ch;
#endif
            if(ch < ble || (ch == ble && cellLb[p] < bre)) { ble = ch; bre = cellLb[p]; }
#ifndef NN_DEVICE_TIER
            if(ch == 2) re = 1;
#endif
        }
#ifndef NN_CORE_TIER
        if(ch <= 4) dens4++;
        if(weakMask[p] && ch < wd) wd = ch;
#endif
    }
    (void)dens2;
    out[0] = no; out[1] = ne; out[2] = dens2; out[3] = dens4;
    out[4] = bro; out[5] = bre; out[6] = wd; out[7] = ro | (re << 1);
}



// dilation state + area-gain classes for 3 radii (6 fx entries).
// compile-time compact->dense feature-class remap (ship tier drops the
// gaps 11-24, 74-82, 91-103); folds at each emission so no runtime shift.
#if defined(NN_CORE_TIER) && defined(NN_DEVICE_TIER)
#define FXD(c) ((c) >= 104 ? (c) - 36 : (c) >= 83 ? (c) - 23 : (c) >= 25 ? (c) - 14 : (c))
#else
#define FXD(c) (c)
#endif
static uint8_t nnDilFeats(uint8_t cx, uint8_t cy,
        const uint8_t *ODS, const uint8_t *EDS,
        uint8_t *fx, uint8_t nf) {
    uint8_t cpos = cy * 9 + cx;
    // dilation states + gains (board coords)
    for(uint8_t di = 0; di < 3; di++) {
        uint8_t r = di + 1;
        uint8_t st2 = (ODS[cpos] <= r) + 2 * (EDS[cpos] <= r);
        uint8_t gain = 0;
        for(int8_t dx = -(int8_t)r; dx <= (int8_t)r; dx++) {
        int8_t rem = r - (nnAbs(dx));
        for(int8_t dy = -rem; dy <= rem; dy++) {
            int8_t nx = cx + dx, ny = cy + dy;
            if((uint8_t)nx > 8 || (uint8_t)ny > 8) continue;
            uint8_t n = ny * 9 + nx;
            if(ODS[n] > r) gain++;
        }
        }
        uint8_t gb = nnBucket(gain, NN_TH_GAIN, 3);
        fx[nf++] = FXD(25) + di * 8 + st2;
        fx[nf++] = FXD(25) + di * 8 + 4 + gb;
    }
    return nf;
}


// 48 rules, 169 bytes
PROGMEM static const uint8_t NN_SHAPES[] = { // 48 rules
    0x10,0xD1,0x31,0x10,0xD7,0x31,0x12,0xD1,0x31,0x12,0xD9,0x31,0x1E,0xDF,0x31,0x1E,
    0xD7,0x31,0x20,0xDF,0x31,0x20,0xD9,0x31,0x50,0x34,0x52,0x34,0x5E,0x34,0x60,0x34,
    0x26,0x9F,0x32,0x66,0x9F,0x35,0x0A,0x91,0x32,0x4A,0x91,0x35,0x1A,0x99,0x32,0x5A,
    0x99,0x35,0x16,0x97,0x32,0x56,0x97,0x35,0x21,0x99,0xA0,0x33,0x61,0x99,0xA0,0x36,
    0x1D,0x97,0x9E,0x33,0x5D,0x97,0x9E,0x36,0x13,0x99,0x92,0x33,0x53,0x99,0x92,0x36,
    0x0F,0x97,0x90,0x33,0x4F,0x97,0x90,0x36,0x27,0x9F,0xA0,0x33,0x67,0x9F,0xA0,0x36,
    0x25,0x9F,0x9E,0x33,0x65,0x9F,0x9E,0x36,0x0B,0x91,0x92,0x33,0x4B,0x91,0x92,0x36,
    0x09,0x91,0x90,0x33,0x49,0x91,0x90,0x36,0x11,0x17,0x90,0x37,0x11,0x19,0x92,0x37,
    0x1F,0x17,0x9E,0x37,0x1F,0x19,0xA0,0x37,0x5F,0x66,0x20,0x38,0x5F,0x66,0x1E,0x38,
    0x51,0x4A,0x10,0x38,0x51,0x4A,0x12,0x38,0x59,0x5A,0x20,0x38,0x59,0x5A,0x12,0x38,
    0x57,0x56,0x10,0x38,0x57,0x56,0x1E,0x38,0x3C,
};

// Table-driven shape walker (Jay's time-for-flash rule): each rule is
// (offset|cond) cells then 0x80|flagbit; conds: 0=own 1=opp 2=empty
// 3=not-opp; off-board fails the rule. Emits the same 4 classes.
__attribute__((noinline)) static uint8_t nnRelShapes(uint8_t cx, uint8_t cy,
        uint8_t toMove, uint8_t opp, uint8_t *fx, uint8_t nf) {
    uint8_t flags = 0, ok = 1;
    const uint8_t *tp = NN_SHAPES;
    for(;;) {
        uint8_t b = pgm_read_byte(tp++);
        uint8_t v = b & 0x3F;
        if(v > 48) {
            if(v == 60) break;              // table end
            if(ok) flags |= 1 << (v - 49);  // rule end
            ok = 1;
            continue;
        }
        if(!ok) continue;
        int8_t qx = cx + (int8_t)(v / 7) - 3;
        int8_t qy = cy + (int8_t)(v % 7) - 3;
        if((uint8_t)qx > 8 || (uint8_t)qy > 8) { ok = 0; continue; }
        uint8_t c = simBoard[qy * 9 + qx];
        uint8_t cond = b >> 6;
        if(cond == 0) ok = c == toMove;
        else if(cond == 1) ok = c == opp;
        else if(cond == 2) ok = c == EMPTY;
        else ok = c != opp;
    }
    uint8_t fOwn = flags & 7, fEnm = (flags >> 3) & 7;
    uint8_t nOwnRel = fOwn == 0 ? 0 : (fOwn & (fOwn - 1)) ? 4 :
              fOwn == 1 ? 1 : fOwn == 2 ? 2 : 3;
    uint8_t nEnmRel = fEnm == 0 ? 0 : (fEnm & 1) ? 1 : (fEnm & 2) ? 2 : 3;
    fx[nf++] = 91 + nOwnRel;
    fx[nf++] = 96 + nEnmRel;
    fx[nf++] = 100 + ((flags >> 6) & 1);
    fx[nf++] = 102 + (flags >> 7);
    return nf;
}

static uint8_t nnLibFam(uint8_t cpos, uint8_t toMove,
        uint8_t opp, uint8_t *deadMask, uint8_t *cells) {
    uint8_t nc;
#ifdef NN_ATARI_EXIT
    // gate invariant: no chain is in atari, so no candidate can capture --
    // the capture flood is structurally dead and deadMask stays all-zero
    // (callees take 0 = no dead map).
    (void)deadMask;
    const uint8_t cap = 0;
    simBoard[cpos] = toMove;
#else
    memset(deadMask, 0, 81);
    uint8_t cap = 0;
    simBoard[cpos] = toMove;
    uint8_t nca;
    FOR_EACH_NEIGHBOR(nca, cpos) {
        if(simBoard[nca] == opp && !deadMask[nca]) {
            if(nnChain(nca, 0, cells, nc) == 0) {
                cap = 1;
                for(uint8_t i = 0; i < nc; i++)
                    deadMask[cells[i]] = 1;
            }
        }
    }
#endif
#ifdef NN_ATARI_EXIT
    uint8_t rl = nnChain(cpos, 0, cells, nc);
#else
    uint8_t rl = nnChain(cpos, deadMask, cells, nc);
#endif
    uint8_t rlb = rl == 0 ? 0 : (nnSat(rl, 4)) - 1;
    uint8_t ela = 3;
    uint8_t nel;
    FOR_EACH_NEIGHBOR(nel, cpos) {
        if(simBoard[nel] == opp) {
#ifndef NN_ATARI_EXIT
            if(deadMask[nel]) {
                if(2 < ela) ela = 2;
            } else
#endif
            {
#ifdef NN_ATARI_EXIT
                uint8_t l = nnChain(nel, 0, cells, nc);
#else
                uint8_t l = nnChain(nel, deadMask, cells, nc);
#endif
                uint8_t b2 = nnBucket(l, NN_TH_12, 2);
                if(b2 < ela) ela = b2;
            }
        }
    }
    simBoard[cpos] = EMPTY;
    return rlb | (ela << 4) | (cap << 7);
}

// Net-priored MCTS: with nnPriorMode set, nnOpeningMove runs UNGATED, fills
// nnTop[] with the net's top-3 root candidates, returns 0. candidatePrior gives
// those a tiered bonus so the net guides while MCTS decides + stays grounded.
static uint8_t nnPriorMode = 0;
static uint8_t nnTop[3] = {0xFF, 0xFF, 0xFF};
static int32_t nnTopSc[3];
#ifndef NN_PRIOR_TOP
#define NN_PRIOR_TOP 0
#endif

uint8_t AI::nnOpeningMove(Game &game, uint8_t &ox, uint8_t &oy) {
    if(nnLast > 80) return 0;                    // need a last move
    if(nnPriorMode) { nnTop[0]=nnTop[1]=nnTop[2]=0xFF; }
#ifdef NN_TWONET
    // select the active weight set: prior net for root priors, opening net otherwise
    if(nnPriorMode) {
        nnA_B=(const uint8_t*)NN_P_B; nnA_W=(const uint8_t*)NN_P_W; nnA_E=(const uint8_t*)NN_P_E;
        nnA_EB=(const uint8_t*)NN_P_EB; nnA_B1=(const uint8_t*)NN_P_B1; nnA_V=(const uint8_t*)NN_P_V;
        nnA_F=(const uint8_t*)NN_P_F; nnA_LF=(const uint8_t*)NN_P_LF; nnA_H=NN_P_H; nnA_KS=NN_P_KSCALE;
    } else {
        nnA_B=(const uint8_t*)NN_B; nnA_W=(const uint8_t*)NN_W; nnA_E=(const uint8_t*)NN_E;
        nnA_EB=(const uint8_t*)NN_EB; nnA_B1=(const uint8_t*)NN_B1; nnA_V=(const uint8_t*)NN_V;
        nnA_F=(const uint8_t*)NN_F; nnA_LF=(const uint8_t*)NN_LF; nnA_H=NN_H; nnA_KS=NN_KSCALE;
    }
#endif
#ifndef ARDUINO
    // Feature-dump tooling (host-only): FORCE_NN bypasses the handoff gate so
    // the NN computes features for ANY position (incl. midgame); NN_DUMP prints
    // the exact per-candidate features for whole-game-net training. Neither
    // compiles on device (ARDUINO); engine behavior is unchanged.
    const uint8_t forceNN = getenv("FORCE_NN") != 0;
#else
    const uint8_t forceNN = 0;
#endif
    // unpack + census
    uint8_t nstones = 0;
    for(uint8_t p = 0; p < 81; p++) {
        simBoard[p] = packedGet(game.board, p);   // p == (p/9)*9 + p%9
        if(simBoard[p] != EMPTY) nstones++;
    }
    if(nstones == 0 || (nstones > NN_MAX_STONES && !forceNN && !nnPriorMode)) return 0;
    uint8_t toMove = game.turn;
    uint8_t opp = 3 - toMove;
    // quiet gate + fight temperature: chains at <= 2 libs
    uint8_t *cells = nnMapId + 81; uint8_t nc;   // borrow sBuffer+405..445 (frame<64 win)
    uint8_t temp = 0;
    uint8_t *cellLb = simMark;        // borrow think scratch (free in chooseMove)
#ifndef NN_CORE_TIER
    uint8_t *weakMask = nnMapA; memset(weakMask, 0, 81);
#else
    uint8_t *weakMask = nnMapA;   // passed but unread in core (wd guarded)
#endif
    {
        uint8_t cmId = 0; memset(nnMapId, 0, 81);   // nnMapId != 0 marks visited
#ifndef NN_CORE_TIER
        uint8_t wmin = 255;
#endif
        for(uint8_t p = 0; p < 81; p++) {
            if(simBoard[p] == EMPTY || nnMapId[p]) continue;
            uint8_t libs = nnChain(p, 0, cells, nc);
            uint8_t lb = nnBucket(libs, NN_TH_LB, 3);
            ++cmId;
            for(uint8_t i = 0; i < nc; i++) {
                cellLb[cells[i]] = lb;
                nnMapId[cells[i]] = cmId;
            }
            if(libs <= 2) temp++;
#ifdef NN_ATARI_EXIT
            // Jay 2026-08: any chain in atari = a capture is on the
            // board -- tactics have started, hand over to MCTS.
            if(libs == 1) return 0;
#endif
#ifndef NN_CORE_TIER
            if(simBoard[p] == game.turn) {          // weakMask: only nnStoneScan's
                if(libs < wmin) {                    // wd reads it (core-dead)
                    wmin = libs; memset(weakMask, 0, 81);
                }
                if(libs == wmin)
                    for(uint8_t i = 0; i < nc; i++)
                        weakMask[cells[i]] = 1;
            }
#endif
        }
#ifdef NN_DEBUG
        fprintf(stderr, "NNDBG nstones=%u temp=%u\n", (unsigned)nstones, (unsigned)temp);
#endif
        if(temp > NN_QUIET_MAXTEMP && !forceNN && !nnPriorMode) return 0;
    }
    if(temp > 3) temp = 3;
    // dilations (board coords): ladderBoard hosts 6 bitsets of 11B
#ifndef MEAS_NODIL
    uint8_t *distOwn = ladderBoard, *distEnm = chainId;   // think scratch
    nnDistMap(toMove, distOwn);
    nnDistMap(opp, distEnm);
#endif
    uint8_t lx = nnLast % 9, ly = nnLast / 9;
    uint8_t lastContact = 0;                     // last (opp) touches our chain
    {
        uint8_t nlc;
        FOR_EACH_NEIGHBOR(nlc, nnLast) {
            if(simBoard[nlc] == toMove) lastContact = 1;
        }
    }
    int32_t bestScore = (int32_t)-2147483647;
    uint8_t bestPos = 0xFF;
    for(uint8_t cx = 0; cx < 9; cx++) for(uint8_t cy = 0; cy < 9; cy++) {
        uint8_t cpos = cy * 9 + cx;
        if(simBoard[cpos] != EMPTY) continue;
        // ---- canonical sym: min (a,b), tie-break lexicographic ----
        uint8_t a0 = nnEdge(cx);
        uint8_t b0 = nnEdge(cy);
        if(a0 > b0) { uint8_t t = a0; a0 = b0; b0 = t; }
        uint8_t bestSym = 0xFF;
        for(uint8_t s = 0; s < 8; s++) {
            uint8_t tx, ty;
            nnSymPos(cx, cy, s, tx, ty);
            uint8_t a = nnEdge(tx);
            uint8_t bb = nnEdge(ty);
            if(a > bb) continue;                 // not in triangle
            if(a != a0 || bb != b0) continue;    // not minimal (a0,b0 is min by construction)
            if(bestSym == 0xFF) { bestSym = s; continue; }
            if(nnCanonBetter(s, bestSym)) bestSym = s;
        }
        uint8_t tcx, tcy;
        nnSymPos(cx, cy, bestSym, tcx, tcy);
        // the sym loop accepted only a==a0 && bb==b0, so nnEdge(tcx)==a0 and
        // nnEdge(tcy)==b0 (already min-ordered): reuse them, skip recompute+swap
        uint8_t bcls = a0 * 5 + b0 - (a0 * (a0 + 1)) / 2;
        // ---- accumulators ----
        int16_t lin = NNRD(A_B, bcls);
        int16_t pre[NNA_MAXH] = {0};
        nnAddRow(pre, A_B1, 0);
        nnAddRow(pre, A_EB, bcls);
#ifndef ARDUINO
        {
            const char *dbg = getenv("NN_DBG3");
            if(dbg && (uint8_t)atoi(dbg) == cpos)
                fprintf(stderr, "SYM cpos=%d bestSym=%d bcls=%d\n", cpos, bestSym, bcls);
        }
#endif
#ifndef MEAS_NOWE
        // ---- coarse offsets per stone (canonical frame) ----
        for(uint8_t p = 0; p < 81; p++) {
            if(simBoard[p] == EMPTY) continue;
            uint8_t sx, sy;
            nnSymPos(p % 9, p / 9, bestSym, sx, sy);
            int8_t dx = (int8_t)sx - (int8_t)tcx, dy = (int8_t)sy - (int8_t)tcy;
            // dense signed offset class: clamp(dx,+-3) preserves both
            // min(|dx|,3) and sign, so each offset keeps its trained NN_E
            // weight -- 49 offsets*2col = 98 rows vs the old sparse 128.
            int8_t sdx = dx < -3 ? -3 : (dx > 3 ? 3 : dx);
            int8_t sdy = dy < -3 ? -3 : (dy > 3 ? 3 : dy);
            uint8_t col = simBoard[p] == toMove ? 0 : 1;
            uint16_t cls = ((uint16_t)(sdx + 3) * 7 + (sdy + 3)) * 2 + col;
#ifndef ARDUINO
            {
                const char *dbg = getenv("NN_DBG3");
                if(dbg && (uint8_t)atoi(dbg) == cpos)
                    fprintf(stderr, "  stone p=%d cls=%d woff=%d wv=%d\n", p, (int)cls,
                            (int)(((uint16_t)(dx + 8) * 17 + (dy + 8)) * 2 + col),
                            (int)NNRD(A_W, ((uint16_t)(dx + 8) * 17 + (dy + 8)) * 2 + col));
            }
#endif
            nnAddRow(pre, A_E, cls);
            // exact-pairwise linear path (uncapped offsets, output scale)
            lin += NNRD(A_W, ((uint16_t)(dx + 8) * 17 + (dy + 8)) * 2 + col);
        }
#endif
        // ---- fx features (board coords), EXACT trainer layout ----
        uint8_t *fx = nnMapId + 121; uint8_t nf = 0;   // borrow sBuffer+445..477
#ifndef MEAS_NOLIB
        uint8_t *deadMask = nnMapB;   // census done-map is dead by now
        uint8_t lf = nnLibFam(cpos, toMove, opp, deadMask, cells);
        uint8_t rlb = lf & 0xF ? lf & 0xF : 0;
        uint8_t rl = 1;                       // clamp applied inside helper
        uint8_t ela = (lf >> 4) & 7;
        uint8_t cap = lf >> 7;
#endif
        uint8_t ss[8];
        nnStoneScan(cx, cy, toMove, cellLb, weakMask, ss);
        uint8_t no = ss[0], ne = ss[1], dens2 = ss[2], dens4 = ss[3];
        uint8_t ld = nnCheby(nnLast, cx, cy);   // ly*9+lx == nnLast
        fx[nf++] = lf & 0xF;            // resLibs, suicide-clamped in helper
        fx[nf++] = 4 + cap;
        fx[nf++] = 6 + (nnSat(no, 5)) - 1;
#ifndef NN_CORE_TIER
        fx[nf++] = 11 + (nnSat(ne, 5)) - 1;
#endif
#ifndef NN_CORE_TIER
        fx[nf++] = 16 + ((dens4 / 2) > 3 ? 3 : dens4 / 2);
#endif
#ifndef NN_CORE_TIER
        fx[nf++] = 20 + (nnSat(ld, 5)) - 1;
#endif
        (void)dens2;
#ifndef MEAS_NODILF
        nf = nnDilFeats(cx, cy, distOwn, distEnm, fx, nf);
#endif
        // fight block
        {
            // distinct adjacent own / enemy chains (dedupe by census chain id)
            uint8_t ownIds[4], enmIds[4], nOwn = 0, nEnm = 0;
            uint8_t n;
            FOR_EACH_NEIGHBOR(n, cpos) {
                if(simBoard[n] == EMPTY) continue;
                uint8_t mn = nnMapId[n];
                uint8_t *ids = simBoard[n] == toMove ? ownIds : enmIds;
                uint8_t *cnt = simBoard[n] == toMove ? &nOwn : &nEnm;
                uint8_t dup = 0;
                for(uint8_t i = 0; i < *cnt; i++) if(ids[i] == mn) dup = 1;
                if(!dup && *cnt < 4) ids[(*cnt)++] = mn;
            }
            uint8_t rel = 0;
            if(lastContact) {
                if(ld == 1 && (lx == cx || ly == cy)) rel = 2;
                else if(ld <= 1 && nOwn) rel = 1;   // nOwn != 0 == touchOwn
                else if(ld <= 2) rel = 3;
                else rel = 4;
            }
            uint8_t xcut = 0;
            for(int8_t dx = -1; dx <= 1; dx += 2) for(int8_t dy = -1; dy <= 1; dy += 2) {
                int8_t qx = cx + dx, qy = cy + dy;
                if((uint8_t)qx > 8 || (uint8_t)qy > 8) continue;
                if(simBoard[qy * 9 + qx] == toMove &&
                   simBoard[cy * 9 + qx] == opp && simBoard[qy * 9 + cx] == opp)
                    xcut = 1;
            }
            uint8_t oo = 0, oe = 0;
            for(int8_t dx = -1; dx <= 1; dx++) for(int8_t dy = -1; dy <= 1; dy++) {
                if(!dx && !dy) continue;
                int8_t qx = cx + dx, qy = cy + dy;
                if((uint8_t)qx > 8 || (uint8_t)qy > 8) continue;
                uint8_t c3 = simBoard[qy * 9 + qx];
                if(c3 == toMove) oo = 1;
                else if(c3 != EMPTY) oe = 1;
            }
            fx[nf++] = FXD(49) + ela;
            fx[nf++] = FXD(53) + rel;
            fx[nf++] = FXD(58) + (nnSat(nOwn, 2));
            fx[nf++] = FXD(61) + (nnSat(nEnm, 2));
            (void)xcut;
            fx[nf++] = FXD(64) + oo + 2 * oe;
        }
        // ---- 3e: pattern / corner / global diff / nearest-chain libs ----
        {
#ifndef MEAS_NOPAT
            int8_t ps = patternBonus(cx, cy, toMove);
            uint8_t pb = nnBucket(ps, NN_TH_PAT, 5);
            fx[nf++] = FXD(68) + pb;
            int8_t po = patternBonus(cx, cy, opp);       // r3: opponent swap
            uint8_t pb2 = nnBucket(po, NN_TH_PAT, 5);
            uint8_t fxOppPat = FXD(104) + pb2;
#endif
            // nearest corner state at r2 (corners: 3-3 points)
            static const uint8_t CPTS[4] = { 2*9+2, 2*9+6, 6*9+2, 6*9+6 };
#ifndef NN_CORE_TIER
            // frame-independent: min state among nearest-tied corners
            uint8_t bd2 = 255, cst = 3;
            for(uint8_t i = 0; i < 4; i++) {
                uint8_t d = nnCheby(CPTS[i], cx, cy);
                uint8_t cp = CPTS[i];
                uint8_t st3 = (distOwn[cp] <= 2) + 2 * (distEnm[cp] <= 2);
                if(d < bd2) { bd2 = d; cst = st3; }
                else if(d == bd2 && st3 < cst) cst = st3;
            }
            fx[nf++] = 74 + cst;
#endif
#ifndef NN_CORE_TIER
            int8_t gd = 0;
            for(uint8_t p = 0; p < 81; p++) {
                if(distOwn[p] <= 3) gd++;
                if(distEnm[p] <= 3) gd--;
            }
            uint8_t db = nnBucket(gd, NN_TH_GD, 4);
            fx[nf++] = 78 + db;
#endif
            // nearest own / enemy chain lib-buckets (order-free: min dist,
            // then min bucket among equidistant) via per-cell cellLb
            uint8_t bro = ss[4], bre = ss[5];
            fx[nf++] = FXD(83) + bro;
            fx[nf++] = FXD(87) + bre;
#ifndef NN_DEVICE_TIER
            nf = nnRelShapes(cx, cy, toMove, opp, fx, nf);
#endif
            fx[nf++] = fxOppPat;
#ifndef NN_DEVICE_TIER
            // r3: opponent dilation gain at r2 (manhattan ball vs enmD2)
            uint8_t og = 0;
            for(int8_t dxx = -2; dxx <= 2; dxx++) {
                int8_t rem = 2 - (nnAbs(dxx));
                for(int8_t dyy = -rem; dyy <= rem; dyy++) {
                    int8_t qx = cx + dxx, qy = cy + dyy;
                    if((uint8_t)qx > 8 || (uint8_t)qy > 8) continue;
                    uint8_t q = qy * 9 + qx;
                    if(distEnm[q] > 2) og++;
                }
            }
            fx[nf++] = 110 + nnBucket(og, NN_TH_GAIN, 3);
#endif
#ifndef NN_DEVICE_TIER
            // r3: one-ply liberty lookahead on the placed chain
            simBoard[cpos] = toMove;
            uint8_t ll = 3;
            for(uint8_t j = 0; j < 4; j++) {
                int8_t qx = cx + OD4[j][0], qy = cy + OD4[j][1];
                if((uint8_t)qx > 8 || (uint8_t)qy > 8) continue;
                uint8_t q = qy * 9 + qx;
                if(simBoard[q] != EMPTY || deadMask[q]) continue;
                simBoard[q] = opp;
                uint8_t l2 = nnChain(cpos, deadMask, cells, nc);
                simBoard[q] = EMPTY;
                if(l2 < ll) ll = l2;
            }
            simBoard[cpos] = EMPTY;
            fx[nf++] = 114 + ll;
#endif
#ifndef NN_CORE_TIER
            // r3: distance to weakest own chain
            uint8_t wd = ss[6];
            fx[nf++] = 118 + wd;
#endif
#ifndef NN_DEVICE_TIER
            // r3: 5x5 ring presence
            uint8_t ro = ss[7] & 1, re = ss[7] >> 1;
            fx[nf++] = 123 + ro + 2 * re;
#endif
#ifndef NN_CORE_TIER
            // r3: line-class x nearestOwn cross
            uint8_t lc = nnEdge(cx);
            uint8_t lc2 = nnEdge(cy);
            if(lc2 < lc) lc = lc2;
            uint8_t lcc = nnBucket(lc, NN_TH_12, 2);
            uint8_t nod = no >= 1 ? (nnSat(no, 4)) - 1 : 0;
            uint8_t cr = lcc * 4 + nod;
            fx[nf++] = 127 + (nnSat(cr, 11));
#endif
        }
#ifndef ARDUINO
        {
            const char *dbg = getenv("NN_DBG");
            if(dbg && (uint8_t)atoi(dbg) == cpos) {
                fprintf(stderr, "FX cpos=%d:", cpos);
                for(uint8_t i = 0; i < nf; i++) fprintf(stderr, " %d", fx[i]);
                fprintf(stderr, "\n");
            }
        }
        // NN_DUMP: exact per-candidate features for whole-game-net training.
        // "CAND <cpos> B <bcls> E <cls:woff> ... F <fx> ..." — cls/woff recomputed
        // host-side (cheap) to match the coarse-offset loop above bit-for-bit.
        if(getenv("NN_DUMP")) {
            printf("CAND %d B %d E", cpos, (int)bcls);
            for(uint8_t p = 0; p < 81; p++) {
                if(simBoard[p] == EMPTY) continue;
                uint8_t sx, sy; nnSymPos(p % 9, p / 9, bestSym, sx, sy);
                int8_t dx = (int8_t)sx - (int8_t)tcx, dy = (int8_t)sy - (int8_t)tcy;
                int8_t sdx = dx < -3 ? -3 : (dx > 3 ? 3 : dx);
                int8_t sdy = dy < -3 ? -3 : (dy > 3 ? 3 : dy);
                uint8_t col = simBoard[p] == toMove ? 0 : 1;
                uint16_t cls = ((uint16_t)(sdx + 3) * 7 + (sdy + 3)) * 2 + col;
                uint16_t woff = ((uint16_t)(dx + 8) * 17 + (dy + 8)) * 2 + col;
                printf(" %d:%d", (int)cls, (int)woff);
            }
            printf(" F");
            for(uint8_t i = 0; i < nf; i++) printf(" %d", fx[i]);
            printf("\n");
        }
#endif
        // ---- score ----
        for(uint8_t i = 0; i < nf; i++) {
            lin += NNRD(A_LF, fx[i]);
            nnAddRow(pre, A_F, fx[i]);
        }
        int32_t score = (int32_t)lin * A_KS;
        for(uint8_t h = 0; h < A_H; h++)
            if(pre[h] > 0)
                score += (int32_t)NNRD(A_V, h) * pre[h];
#ifndef ARDUINO
        {
            const char *dbg = getenv("NN_DBG");
            if(dbg && atoi(dbg) == -1)
                fprintf(stderr, "SC %d %ld %d\n", cpos, (long)score, (int)bcls);
        }
#endif
#if NN_PRIOR_TOP
        uint8_t validc = game.isValidMove(cx, cy);
        if(score > bestScore && validc) { bestScore = score; bestPos = cpos; }
        if(nnPriorMode && validc) {          // maintain descending top-3
            for(uint8_t r = 0; r < 3; r++) {
                if(nnTop[r] == 0xFF || score > nnTopSc[r]) {
                    for(uint8_t s = 2; s > r; s--) { nnTop[s] = nnTop[s-1]; nnTopSc[s] = nnTopSc[s-1]; }
                    nnTop[r] = cpos; nnTopSc[r] = score;
                    break;
                }
            }
        }
#else
        if(score > bestScore && game.isValidMove(cx, cy)) {
            bestScore = score;
            bestPos = cpos;
        }
#endif
    }
#if NN_PRIOR_TOP
    if(nnPriorMode) return 0;
#endif
    if(bestPos > 80) return 0;
    ox = bestPos % 9;
    oy = bestPos / 9;
    // NB (2026-08): an anti-crawl handoff here -- past ~10 stones, defer a
    // 1st/2nd-line NN pick to MCTS search -- LOST -8.0% vs L0 (paired 1547
    // games, 351 vs 474 wins). KataGo flags those low moves as -5..-7 pt,
    // but the NN's deep edge play still beats handing off to our own search
    // vs L0. Reverted. See memory ardu-go-nn-crawl-probe.
    return 1;
}
#endif // NNOPEN


// Light ladder reader. The group containing defStart is in atari with
// sole liberty esc; run the forced chase and return 1 if it escapes.
// Every ambiguity (depth cap, odd shapes, no working chase) resolves
// toward "escape", so a wrong read just reproduces the old behavior.
// Ko is ignored while reading. NOT reentrant (single snapshot).
static uint8_t ladderEscapes(uint8_t defStart, uint8_t esc,
                             uint8_t maxSteps = 20) {
    uint8_t defColor = boardAt(defStart);
    uint8_t atkColor = 3 - defColor;
    uint8_t escaped = 1;
    // Journal-undo protocol (see ladderSnapNow above): no snapshot up
    // front. Every play is journaled BEFORE its simPlay call (so the
    // lazy in-simPlay snapshot can re-empty it); ILLEGAL un-journals.
    ladderJMode = 1; ladderSnapped = 0; ladderJN = 0;

    for(uint8_t step = 0; step < maxSteps; step++) {
        // Defender extends at the sole liberty
        ladderJournal[ladderJN++] = esc;
        if(simPlay(esc, defColor, NO_KO) == ILLEGAL) {
            ladderJN--;
            escaped = 0;
            break;
        }
        uint8_t libs = groupLibsFind(esc);
        uint8_t l1 = glcL1, l2 = glcL2;
        if(libs >= 3) break;               // clear escape
        if(libs <= 1) { escaped = 0; break; } // attacker just takes

        // Attacker chases at whichever liberty keeps his stone alive,
        // preferring the tighter side (less room around the defender's
        // remaining liberty)
        uint8_t chase = 0xFF, next = 0xFF;
        uint8_t bestRoom = 0xFF;
        for(uint8_t t = 0; t < 2; t++) {
            uint8_t cand = t ? l2 : l1;
            uint8_t other = t ? l1 : l2;
            simBoard[cand] = atkColor; // tentative, no captures
            uint8_t alibs = groupLibsMax3(cand);
            simBoard[cand] = EMPTY;
            if(alibs < 2) continue; // defender would just capture it

            uint8_t room = 0;
            uint8_t q;
            FOR_EACH_NEIGHBOR(q, other)
                if(simBoard[q] == EMPTY) room++;
            if(room < bestRoom) {
                bestRoom = room;
                chase = cand;
                next = other;
            }
        }
        if(chase == 0xFF) break; // no working chase: escape
        ladderJournal[ladderJN++] = chase;
        if(simPlay(chase, atkColor, NO_KO) == ILLEGAL) { ladderJN--; break; }
        esc = next;
    }

    ladderJMode = 0;
    if(ladderSnapped)
        memcpy(simBoard, ladderBoard, BOARD_CELLS);
    else
        for(uint8_t i = 0; i < ladderJN; i++)
            simBoard[ladderJournal[i]] = EMPTY;
    return escaped;
}

#ifdef LOOSE
// ===== Bounded loose-ladder reader (midgame fighting hunt, 2026-08) =====
// PREMISE REFUTED AT THE VALIDATION GATE: every class-A exemplar's
// save reaches 4+ liberties immediately (the earlier "3" was
// groupLibsFind's cap), so no bounded chase read can see the loss --
// and the game continuations show the mechanism is STRATEGIC, not
// tactical: the engine tenukis from live boundary exchanges while its
// playouts score the position 75-80% against gnugo's near-even margin
// (unpriced crawls/pushes = the eval-calibration domain). The reader
// is kept flag-gated as correct bounded-semeai machinery for possible
// future use; it never reached a gauntlet. AND-OR search, attacker
// tries to capture the defender group, defender tries to reach
// LL_ALIVE_LIBS or survive to the caps:
//   - ITERATIVE (explicit line/index arrays): 12-level recursion would
//     blow the AVR stack (high-water ~200B against ~230 slack).
//   - Replay-based undo: one snapshot in ladderBoard (never used
//     concurrently with ladderEscapes) + full-line replay on backtrack.
//   - Cap-out or depth-out returns ALIVE: false doom is the only
//     unsafe direction (same convention as ladderEscapes).
//   - Move ordering = the room heuristic (attack the open side first,
//     run toward the open side first) so real kills land inside the
//     node budget.
// Verdict is for the chain containing defStart on the CURRENT board.
#ifndef LL_DEPTH
#define LL_DEPTH 12
#endif
#ifndef LL_NODES
#define LL_NODES 60
#endif
#ifndef LL_ALIVE_LIBS
#define LL_ALIVE_LIBS 4
#endif

// Distinct liberties of the chain at start, up to cap(<=4), into out[].
// Fresh mark epoch; returns the count.
static uint8_t groupLibsList(uint8_t start, uint8_t *out, uint8_t cap) {
    uint8_t color = boardAt(start);
    uint8_t n = 0;
    newMark();
    uint8_t sp = 0;
    floodSlot(sp++) = start;
    simMark[start] = markEpoch;
    while(sp) {
        uint8_t p = floodSlot(--sp);
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, p) {
            uint8_t s = boardAt(q);
            uint8_t *mp = markPtr(q);
            if(*mp == markEpoch) continue;
            if(s == color) { *mp = markEpoch; floodSlot(sp++) = q; }
            else if(s == EMPTY) {
                *mp = markEpoch;
                out[n++] = q;
                if(n >= cap) return n;
            }
        }
    }
    return n;
}

// Empty-neighbour count (the room heuristic's currency)
static uint8_t emptyDeg(uint8_t pos) {
    uint8_t d = 0, q;
    FOR_EACH_NEIGHBOR(q, pos) if(simBoard[q] == EMPTY) d++;
    return d;
}

// k-th candidate move at the current position for the side at `depth`
// (even depth = attacker). Deterministic enumeration; 0xFF = none.
// Defender moves: extends (open side first), then counter-captures of
// adjacent 1-lib enemy chains. Attacker moves: liberty fills, open
// side first (block the escape direction).
static uint8_t llDefColor, llDefStart;
static uint8_t llMoveAt(uint8_t depth, uint8_t k) {
    uint8_t libs[4];
    uint8_t nl = groupLibsList(llDefStart, libs, 4);
    // order libs by descending emptyDeg (insertion, n<=4)
    for(uint8_t i = 1; i < nl; i++)
        for(uint8_t j = i; j; j--)
            if(emptyDeg(libs[j]) > emptyDeg(libs[j - 1])) {
                uint8_t t = libs[j]; libs[j] = libs[j - 1]; libs[j - 1] = t;
            } else break;
    if(!(depth & 1)) {      // ATTACKER: fill a liberty
        return k < nl ? libs[k] : 0xFF;
    }
    // DEFENDER: extend, then counter-capture
    if(k < nl) return libs[k];
    // counter-captures: adjacent enemy chains at 1 lib -- walk the
    // group, note distinct enemy chains in atari, capture at their lib
    uint8_t want = k - nl, seen = 0;
    uint8_t atk = 3 - llDefColor;
    newMark();
    uint8_t sp = 0;
    floodSlot(sp++) = llDefStart;
    simMark[llDefStart] = markEpoch;
    while(sp) {
        uint8_t p = floodSlot(--sp);
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, p) {
            uint8_t s = boardAt(q);
            uint8_t *mp = markPtr(q);
            if(*mp == markEpoch) continue;
            if(s == llDefColor) { *mp = markEpoch; floodSlot(sp++) = q; }
            else if(s == atk) {
                *mp = markEpoch;               // dedupe by first stone touch
                uint8_t sl = soleLiberty(q);   // clobbers glc statics: fine
                if(sl != 0xFF) {
                    if(seen == want) return sl;
                    seen++;
                }
            }
        }
    }
    return 0xFF;
}

// 1 = the defender chain survives the bounded chase, 0 = it dies.
static uint8_t looseLadderAlive(uint8_t defStart) {
    llDefColor = simBoard[defStart];
    llDefStart = defStart;
    memcpy(ladderBoard, simBoard, BOARD_CELLS);
    uint8_t line[LL_DEPTH];
    uint8_t idx[LL_DEPTH];
    uint8_t depth = 0;
    idx[0] = 0;
    uint8_t nodes = 0;
    uint8_t verdict;
    // AND-OR DFS: attacker (even depths) needs ONE line to DEAD;
    // defender needs ONE reply to ALIVE. `verdict` bubbles up as we
    // backtrack; sentinel 2 = still descending.
    for(;;) {
        // ---- evaluate current position (before this level branches)
        uint8_t v = 2;
        if(simBoard[llDefStart] != llDefColor) v = 0;        // captured
        else {
            uint8_t libs[4];
            uint8_t nl = groupLibsList(llDefStart, libs, LL_ALIVE_LIBS);
            if(nl >= LL_ALIVE_LIBS) v = 1;                   // ran free
            else if(depth >= LL_DEPTH || nodes >= LL_NODES) v = 1; // cap: optimistic
        }
        if(v == 2) {
            // ---- try the next candidate at this level
            uint8_t mv = llMoveAt(depth, idx[depth]);
            uint8_t side = (depth & 1) ? llDefColor : (uint8_t)(3 - llDefColor);
            uint8_t played = 0;
            while(mv != 0xFF && !played) {
                idx[depth]++;
                if(simPlay(mv, side, NO_KO) != ILLEGAL) {
                    // attacker fill must not be a trivial sacrifice;
                    // defender extend must not be pointless self-atari
                    uint8_t ok = 1;
                    if(!simCaptured) {
                        uint8_t pl = groupLibsCore(mv, 0, 2);
                        if(pl <= 1) ok = 0;
                    }
                    if(ok) played = 1;
                    else {
                        // undo the probe: restore + replay the line
                        memcpy(simBoard, ladderBoard, BOARD_CELLS);
                        for(uint8_t i = 0; i < depth; i++) {
                            uint8_t s2 = (i & 1) ? llDefColor
                                                 : (uint8_t)(3 - llDefColor);
                            simPlay(line[i], s2, NO_KO);
                        }
                    }
                }
                if(!played) mv = llMoveAt(depth, idx[depth]);
            }
            if(played) {
                line[depth] = mv;
                nodes++;
                depth++;
                idx[depth] = 0;
                continue;
            }
            // no candidate worked: attacker out of tries = ALIVE,
            // defender out of tries = DEAD
            v = (depth & 1) ? 0 : 1;
        }
        // ---- backtrack with verdict v
        for(;;) {
            if(depth == 0) { verdict = v; goto done; }
            depth--;
            // undo to this depth: restore + replay prefix
            memcpy(simBoard, ladderBoard, BOARD_CELLS);
            for(uint8_t i = 0; i < depth; i++) {
                uint8_t s2 = (i & 1) ? llDefColor
                                     : (uint8_t)(3 - llDefColor);
                simPlay(line[i], s2, NO_KO);
            }
            uint8_t attacker = !(depth & 1);
            if(attacker ? (v == 0) : (v == 1)) {
                // cut: this side got what it wanted
                continue;   // bubble v one level further up... loop
            }
            break;          // other branches of this level remain
        }
        if(depth == 0 && idx[0] == 0) { /* unreachable */ }
    }
done:
    memcpy(simBoard, ladderBoard, BOARD_CELLS);
    return verdict;
}
#endif // LOOSE

// Does the 3x3 neighborhood of (cx,cy) match the MoGo pattern library
// for `color` to move? The library is precompiled into truth-table
// bitmaps per position class, so a query is a base-3 index over the
// on-board neighbors (colors relative to the mover; the set is
// color-swap closed, so this is exact) plus one bit test.
// Shared base-3 index over the on-board neighbors; sets *pcls, *pstones.
// Edge-class half of pattern3Index, kept out of line so the interior
// majority of patternBonus calls doesn't inherit this path's register
// frame (the x/y counters + 16-bit mult/idx pairs drove a 9-register
// prologue on every call). Packed return: idx | stones<<16 | cls<<24.
// Per-class on-board neighbour deltas (dy*9+dx as two's-complement
// bytes) in the EXACT dy,dx scan order the old bounds-checked 3x3 loop
// visited them; 0 terminates (the centre delta can never appear). The
// on-board subset is a static function of the position class, so the
// per-cell row/column bounds tests collapse into one lpm-driven walk.
// Row 4 (interior) is unused -- pattern3Index handles interior cells.
static const uint8_t PROGMEM EDGE_OFFS[9 * 6] = {
    0x01, 0x09, 0x0A, 0x00, 0x00, 0x00,  // cls 0: NW corner
    0xFF, 0x01, 0x08, 0x09, 0x0A, 0x00,  // cls 1: N edge
    0xFF, 0x08, 0x09, 0x00, 0x00, 0x00,  // cls 2: NE corner
    0xF7, 0xF8, 0x01, 0x09, 0x0A, 0x00,  // cls 3: W edge
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // cls 4: interior (unused)
    0xF6, 0xF7, 0xFF, 0x08, 0x09, 0x00,  // cls 5: E edge
    0xF7, 0xF8, 0x01, 0x00, 0x00, 0x00,  // cls 6: SW corner
    0xF6, 0xF7, 0xF8, 0xFF, 0x01, 0x00,  // cls 7: S edge
    0xF6, 0xF7, 0xFF, 0x00, 0x00, 0x00,  // cls 8: SE corner
};
static uint8_t p3eCls, p3eStones;   // edge-path out-params (the packed
                                    // uint32 return cost a 4-register
                                    // marshal both sides — glcL1/L2
                                    // precedent: statics beat packing)
__attribute__((noinline)) static uint16_t pattern3Edge(int8_t cx, int8_t cy, uint8_t color) {
    uint8_t clsx = (cx == 0) ? 0 : (cx == BOARD_SIZE - 1) ? 2 : 1;
    uint8_t clsy = (cy == 0) ? 0 : (cy == BOARD_SIZE - 1) ? 2 : 1;
    uint8_t cls = (uint8_t)(clsy * 3 + clsx);
    // An edge/corner cell has at most 5 on-board neighbours, so idx
    // <= 3^5-1 = 242 and mult tops out at 243: the whole base-3
    // accumulation fits in 8 bits (see the mult comment history).
    // Same digits in the same order as the bounds-checked loop --
    // identical values, minus ~8 per-cell row/column tests.
    uint8_t idx = 0;
    uint8_t stones = 0;
    uint8_t mult = 1;
    const uint8_t *b = simBoard + cy * BOARD_SIZE + cx;   // 3x3 centre
    const uint8_t *op = EDGE_OFFS + cls * 6;
    int8_t d;
    while((d = (int8_t)lpmNext(op)) != 0) {
        uint8_t s = b[d];
        uint8_t v = (s == EMPTY) ? 0 : (s == color) ? 1 : 2;
        if(v) stones++;
        idx += v * mult;
        mult = (uint8_t)(mult + (mult << 1));   // mult *= 3, 8-bit
    }
    p3eCls = cls;
    p3eStones = stones;
    return idx;
}

static uint16_t pattern3Index(int8_t cx, int8_t cy, uint8_t color,
                              uint8_t *pcls, uint8_t *pstones) {
    uint16_t idx = 0;
    uint8_t stones = 0;
    if((uint8_t)(cx - 1) < BOARD_SIZE - 2 && (uint8_t)(cy - 1) < BOARD_SIZE - 2) {
        // Left-right mirror fold: read the 8 neighbours as own(1)/opp(2)/
        // empty(0), then index the interior by (middle spine, unordered pair
        // of {left,right} columns) with a dense triangular index. Halves the
        // interior table; mirror-equivalent shapes share a slot.
        // Sequential pointer walk: row-adjacent cells load with 2-cycle
        // ld Z+ instead of per-read 16-bit address arithmetic.
        const uint8_t *p = simBoard + cy * BOARD_SIZE + cx - 10;
#define VC(s) ((s) ? (((s) == color) ? 1 : 2) : 0)
        uint8_t vNW = VC(*p); p++;
        uint8_t vN  = VC(*p); p++;
        uint8_t vNE = VC(*p); p += 7;
        uint8_t vW  = VC(*p); p += 2;
        uint8_t vE  = VC(*p); p += 7;
        uint8_t vSW = VC(*p); p++;
        uint8_t vS  = VC(*p); p++;
        uint8_t vSE = VC(*p);
#undef VC
        stones = (vNW>0)+(vN>0)+(vNE>0)+(vW>0)+(vE>0)+(vSW>0)+(vS>0)+(vSE>0);
        uint8_t L = vNW + 3*vW + 9*vSW;   // left column  0..26
        uint8_t R = vNE + 3*vE + 9*vSE;   // right column 0..26
        uint8_t M = vN + 3*vS;            // middle spine 0..8
        if(L > R) { uint8_t t = L; L = R; R = t; }
        // M*378 + L*27 - L*(L-1)/2 by table: the combine's three 16-bit
        // multiplies have tiny domains (M<=8, L<=26), so two PROGMEM
        // word loads replace them. Same values by construction.
        static const uint16_t PROGMEM FOLD_M[9] = {0, 378, 756, 1134, 1512, 1890, 2268, 2646, 3024};
        static const uint16_t PROGMEM FOLD_TRI[27] = {0, 27, 53, 78, 102, 125, 147, 168, 188, 207, 225, 242, 258, 273, 287, 300, 312, 323, 333, 342, 350, 357, 363, 368, 372, 375, 377};
        idx = pgm_read_word(FOLD_M + M) + pgm_read_word(FOLD_TRI + L) +
              (uint8_t)(R - L);
    } else {
        uint16_t r = pattern3Edge(cx, cy, color);
        *pcls = p3eCls;
        *pstones = p3eStones;
        return r;
    }
    *pcls = 4;   // interior: clsy*3+clsx == 4 by construction
    *pstones = stones;
    return idx;
}


#include "pattern_weights.h"
// Signed data-learned 3x3 pattern weight (replaces the MoGo bit).
__attribute__((optimize("O2")))
__attribute__((noinline))
static int8_t patternBonus(int8_t cx, int8_t cy, uint8_t color) {
    uint8_t cls, stones;
    uint16_t idx = pattern3Index(cx, cy, color, &cls, &stones);
    if(stones < 2) return 0;
    uint16_t n = pgm_read_word(PAT3W_BASE + cls) + idx;
    uint8_t bits = pgm_read_byte(PAT3W_BITS + (n >> 2));
    // >> ((n&3)*2) is a variable shift = a cycle-per-step loop on AVR;
    // two conditional constant shifts (swap+andi / 2x lsr) are fixed cost
    if(n & 2) bits >>= 4;
    if(n & 1) bits >>= 2;
    return (int8_t)pgm_read_byte(PAT3W_LEVEL + (bits & 3));
}

// Dynamic komi for graceful losing (see think): when behind, the
// tree learns from playouts scored with this many HALF-POINTS
// spotted to the root player, so "lose by less" reads as winning
// and the search plays normal margin-preserving moves instead of
// maximizing the tiny chance of an opponent collapse (the flail).
// True-komi results still drive resignation, passing, and the eval.

// Area scoring; returns winning color and leaves the raw doubled
// margin (black - white, komi not applied) for the virtual verdict
static int16_t lastMargin2;
static uint8_t mercyHit;    // set by the capture sites, read at playout loop top
static uint8_t scoreWinner() {
    int8_t diff = 0;   // black minus white; margin = 2*diff exactly
#ifndef SW_LEGACY
    // SHIP 2026-08-26 (micro-opt round): anchored-pointer scan. The
    // sequential scan knows its position, so the PROGMEM neighbour
    // table (lpm 3cyc + x5 pointer setup + sentinel tests) is replaced
    // by direct SRAM reads off ONE walking pointer with positive
    // constant offsets (cell = q[9]) -- -Os emits single-instruction
    // ldd Z+d loads instead of X-register adiw/ld/sbiw round-trips.
    // -0.50% think for +14B flash; byte-identical games vs the table
    // version (2x60 identical-seed check). Peeled/pipelined variants
    // reached -0.78..-0.94% but cost ~+400B -- see roadmap memory.
    {
        const uint8_t *q = simBoard - 9;   // cell = q[9]
        for(uint8_t row = 0; row < BOARD_SIZE; row++) {
            const uint8_t notTop = row != 0, notBot = row != BOARD_SIZE - 1;
            for(uint8_t col = 0; col < BOARD_SIZE; col++, q++
                ) {
                uint8_t c = q[9];
                if(c == BLACK) { diff++; continue; }
                if(c == WHITE) { diff--; continue; }
                // OR-fold: cells are 0/1/2, so the OR of the neighbours
                // is BLACK iff only-black borders, WHITE iff only-white,
                // 3 iff mixed.
                uint8_t mm = 0;
                if(col)                  mm |= q[8];
                if(col != BOARD_SIZE-1)  mm |= q[10];
                if(notTop)               mm |= q[0];
                if(notBot)               mm |= q[18];
                if(mm == BLACK) diff++;
                else if(mm == WHITE) diff--;
            }
        }
    }
#else
    // Pre-08-26 neighbour-table version, kept for reference.
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        if(simBoard[i] == BLACK) { diff++; continue; }
        if(simBoard[i] == WHITE) { diff--; continue; }
        uint8_t mm = 0, q;
        FOR_EACH_NEIGHBOR(q, i) mm |= simBoard[q];
        if(mm == BLACK) diff++;
        else if(mm == WHITE) diff--;
    }
#endif
    lastMargin2 = (int16_t)diff * 2;
#if !defined(ARDUINO) && defined(SCOREPROBE)
    // terminal-evaluation audit (Jay, 2026-08-23): sample playout end
    // boards + the engine's verdict for KataGo cross-checking
    {
        static uint32_t scpN = 0;
        if((++scpN % 50) == 0) {
            char b[82];
            for(uint8_t i = 0; i < BOARD_CELLS; i++) b[i] = '0' + simBoard[i];
            b[81] = 0;
            fprintf(stderr, "SCP %s %d %d %d\n", b, (int)lastMargin2,
                    (int)simKomi, (int)mercyHit);
        }
    }
#endif
    return lastMargin2 > (int16_t)simKomi ? BLACK : WHITE;
}

// The same playout under the virtual komi: the root player wins if
// within vKomi2 half-points of the real bar
static uint8_t vKomiWinner() {
    int16_t bar = (int16_t)simKomi;
    if(rootTurn == BLACK) bar -= vKomi2;
    else bar += vKomi2;
#ifdef VKOMI_WIN
    // Winning side: raise the bar against the root player (must win by more)
    if(rootTurn == BLACK) bar += vKomiWin;
    else bar -= vKomiWin;
#endif
    return lastMargin2 > bar ? BLACK : WHITE;
}

// Playout capture tallies (statics so the shared move helper can
// update them; reset at each playout start)
static uint8_t capArr[3];   // captures by color: [_, BLACK, WHITE] (mercy rule)
// (declaration moved above scoreWinner for the SCOREPROBE mercy tag)
#if !defined(ARDUINO) && defined(GROWPRE_STATS)
uint32_t gpMoves, gpPlayouts;
#endif
#if !defined(ARDUINO) && defined(PT_STATS)
uint8_t ptTried[11];              // cells attempted THIS ply (census)
uint32_t ptDupPly;                // intra-ply duplicate attempts
#endif
#if PT_BANCACHE > 0
// Reject cache (comb find #1, 08-27): 76% of playoutTry* attempts are
// rejects (census: 33.5M calls, 10.7M eye + 14.7M simPlay rejects per
// 30 games) and every reject reason is CAPTURE-MONOTONE — placing
// stones only removes liberties, so suicide stays suicide, self-atari
// stays self-atari, own-eye only turns on, until a capture re-adds
// liberties. Ko is the sole non-monotone reason and every try* caller
// pre-filters it. Per-color ban bitmaps: set on reject, tested at try*
// entry + the scan/pattern collectors, cleared at playout start and on
// every capture (mercyCheck = the universal capture hook). Per-playout
// lifetime also makes scoreMode's different self-atari policy safe
// (a playout never mixes modes). NOT in widenNode: tree-side scans run
// outside playouts where this state is stale.
static uint8_t ptBan[2][11];
#endif
__attribute__((noinline)) static void mercyCheck(uint8_t mover) {
    // absorbs the caller-side capArr add (was duplicated at every site);
    // reuses the post-add value instead of re-loading it. simCaptured=0
    // sites are unchanged (+=0 no-op).
    uint8_t mine = (capArr[mover] += simCaptured);
    if(mine > capArr[3 - mover] + MERCY_MARGIN) mercyHit = 1;
#if PT_BANCACHE > 0
    if(simCaptured) memset(ptBan, 0, sizeof(ptBan));
#endif
}

// Attempt one playout move at pos (0xFF = none): the legality gate,
// gated play, and bookkeeping shared by every playout heuristic.
// Returns 1 when the move was played; caller flips toMove.
// Scoring playouts (see scoreDead) allow self-atari: killing an
// eyespaced-but-dead group needs throw-in sacrifices, and with the
// gate on, playouts defended square four successfully (3/64 kills)
// while any correct line captures the sacrifices right back — so
// ownership tallies stay honest. (scoreMode is declared up top.)
__attribute__((noinline))
// (O2 dropped 2026-08-15: -Os is +0.06% (noise) for -44 B on the
// current tree — the flash funds banked speed wins.)
// Returns 0 on failure, else (0x100 | newKo): ko travels by VALUE and
// comes back in the result, `last` is the caller's own pos -- the two
// pointer out-params this replaces cost two derefs on entry and two
// indirect stores on success, on a ~100K-calls-per-think path.
// Entry for callers that GUARANTEE pos is on-board, empty and not ko
// (the global probe pre-filters exactly these) -- skips re-checking.
static uint16_t playoutTryOpen(uint8_t pos, uint8_t toMove, uint8_t ko,
                               uint8_t m);

static inline __attribute__((always_inline)) uint16_t playoutTry(uint8_t pos, uint8_t toMove, uint8_t ko,
                           uint8_t m) {
    if(pos >= BOARD_CELLS || pos == ko || boardAt(pos) != EMPTY)
        return 0;
    return playoutTryOpen(pos, toMove, ko, m);
}

#if !defined(ARDUINO) && defined(PT_STATS)
uint32_t ptCalls, ptEyeRej, ptSimRej, ptOk, ptPatCalls, ptPatRej;
#endif
static uint16_t playoutTryOpen(uint8_t pos, uint8_t toMove, uint8_t ko,
                               uint8_t m) {
#if !defined(ARDUINO) && defined(PT_STATS)
    ptCalls++;
    if(ptTried[pos >> 3] & bitMask(pos)) ptDupPly++;
    ptTried[pos >> 3] |= bitMask(pos);
#endif
#if PT_BANCACHE > 0
    if(ptBan[toMove - 1][pos >> 3] & bitMask(pos)) return 0;
#endif
    if(isOwnEye(pos, toMove)) {
#if !defined(ARDUINO) && defined(PT_STATS)
        ptEyeRej++;
#endif
#if PT_BANCACHE > 0
        ptBan[toMove - 1][pos >> 3] |= bitMask(pos);
#endif
        return 0;
    }
    uint8_t nk = simPlay(pos, toMove, ko, !scoreMode);
    if(nk == ILLEGAL) {
#if !defined(ARDUINO) && defined(PT_STATS)
        ptSimRej++;
#endif
#if PT_BANCACHE > 0
        ptBan[toMove - 1][pos >> 3] |= bitMask(pos);
#endif
        return 0;
    }
#if !defined(ARDUINO) && defined(PT_STATS)
    ptOk++;
#endif
    // barrier: raveMark below re-derives its bitmap address from this
    // one register instead of GCC also keeping a zero-extended copy of
    // pos in a second call-saved pair across the simPlay call
    asm volatile("" : "+r"(pos));
    if(toMove == rootTurn && m < RAVE_HORIZON) raveMark(pos);
    if(simCaptured) {
        mercyCheck(toMove);   // capArr add absorbed inside
    }
    return 0x100 | nk;
}

// Same, for callers whose candidate is ALREADY proven empty, non-ko
// and non-eye (the pattern scan verifies all three per candidate;
// board state cannot change between its scan and the try within one
// cascade pass, so the isOwnEye recheck is dead work). Own body --
// chaining through playoutTryOpen taxed the global probe's entry.
static uint16_t playoutTryPat(uint8_t pos, uint8_t toMove, uint8_t ko,
                              uint8_t m) {
#if !defined(ARDUINO) && defined(PT_STATS)
    ptPatCalls++;
    if(ptTried[pos >> 3] & bitMask(pos)) ptDupPly++;
    ptTried[pos >> 3] |= bitMask(pos);
#endif
#if PT_BANCACHE > 0
    if(ptBan[toMove - 1][pos >> 3] & bitMask(pos)) return 0;
#endif
    uint8_t nk = simPlay(pos, toMove, ko, !scoreMode);
    if(nk == ILLEGAL) {
#if !defined(ARDUINO) && defined(PT_STATS)
        ptPatRej++;
#endif
#if PT_BANCACHE > 0
        ptBan[toMove - 1][pos >> 3] |= bitMask(pos);
#endif
        return 0;
    }
    asm volatile("" : "+r"(pos));   // same barrier as playoutTryOpen
    if(toMove == rootTurn && m < RAVE_HORIZON) raveMark(pos);
    if(simCaptured) {
        mercyCheck(toMove);   // capArr add absorbed inside
    }
    return 0x100 | nk;
}


#ifdef SRC_STATS
// per-source playout move counts: tac,vit,rvit,pat,ans,scan,pass,growskip
uint32_t srcCnt[10];
uint32_t qeCapMoves, qeCapPlayouts;
uint32_t qeMMercy;
#endif

#ifdef PLAYOUT_STATS
uint32_t plN, plMoves, plEndCap, plEndPass, plEndMercy;
#endif
#ifdef DECIDE_PROBE
// probe-only: iteration at which the visit-argmax last changed
uint16_t dpLastChange; uint8_t dpPrevBest;
#endif
#ifdef WIDEN_PROBE
// probe-only: widening-waste statistics
uint16_t wpCalls, wpAdded, wpEmpty, wpAllocFail;
#endif
// Random playout from current simBoard; returns winning color.
// `last` = the previous move (0xFF/pass = none).
// FLASH TRADE (2026-08-06): -O2 dropped here -- measured -470 B for +1.64%
// think (287 B per 1% think, better than the RECIP trade). Strength-neutral
// (fixed iteration count; slower playout = same rollouts). Re-add the
// attribute to buy the 1.64% back if the flash is ever wanted for speed.

// Playout-gate engagement probe (roadmap #5 opening, 2026-08-23):
// counts, at the playout squeeze/escape decision sites, how often the
// gate conditions occur. Host-only instrumentation; DEADTAG law says
// measure engagement before building any gate.
#ifndef TACT_MASK
#define TACT_MASK 7        // tactical gate: 7 = 7/8, 3 = 3/4
#endif
#ifndef SQZ_MASK
#define SQZ_MASK 8         // squeeze: 8 = 1/2, 24 = 3/4
#endif
#ifndef VIT_MASK
#define VIT_MASK 3         // dead-shape vital: 3 = 1/4, 1 = 1/2, 7 = 1/8
#endif
#ifndef RVIT_MASK
#define RVIT_MASK 7        // root-vitals: 7 = 1/8, 3 = 1/4
#endif
// RVIT_OFF compiles the root-vitals playout gate out entirely (arm for
// the 08-23 removal gauntlet: behaviorally dead at 0.1% 5k compliance,
// mini-neutral +7/2000, ties unchanged 225v222 -> inert; removal buys
// flash + complexity).

#ifndef PAT_MASK
#define PAT_MASK 3         // 3x3 patterns: 3 = 3/4, 15 = 15/16
#endif

#if !defined(ARDUINO) && defined(FLIPTRACE)
uint8_t ftMoves[220]; uint16_t ftN; uint8_t ftOn;
#define FT_REC do { if(ftOn && ftN < 220) ftMoves[ftN++] = last; } while(0);
#else
#define FT_REC
#endif
#if GROWPRE > 0
// Growth preamble (Jay's design, 08-27): before random play begins,
// each side in turn scans rings 5->2 (ring = line from nearest edge;
// ring 1 excluded) and plays the first empty point adjacent to 1-2 own
// stones and ZERO enemy stones -- a boundary-extension sketch that
// gives territory frameworks their natural growth before the random
// phase splits open areas 50/50. Alternates until the mover finds no
// qualifying point (or a try is rejected), then normal playout resumes
// with that mover. Real moves: playoutTry legality/policy, RAVE-marked.
// Balanced by construction (identical heuristic both colors).
static const uint8_t GROWPRE_SCAN[49] PROGMEM = {40,30,31,32,39,41,48,49,50,20,21,22,23,24,29,33,38,42,47,51,56,57,58,59,60,10,11,12,13,14,15,16,19,25,28,34,37,43,46,52,55,61,64,65,66,67,68,69,70};
#if GROWPRE == 6
static uint8_t gpDead[2];   // per-color latch: scan found nothing
#endif
#endif
static uint8_t playout(uint8_t toMove, uint8_t ko, uint8_t last) {
#if !defined(ARDUINO) && defined(FLIPTRACE)
    ftN = 0;
#endif
#if PT_BANCACHE > 0
    memset(ptBan, 0, sizeof(ptBan));
#endif
    uint8_t passes = 0;
#if GROWPRE > 0
#ifndef GP_MINSTONES
#define GP_MINSTONES 0
#endif
#if GROWPRE == 6
    gpDead[0] = gpDead[1] = 0;
#endif
    // Phase gate (v4): early/mid, own-adjacent enemy-free points are
    // FRONTIER, and auto-claiming them pre-decides contested space
    // (poeval flips +6.9pp at 25-34 stones, +2.4 at 35-44). From ~45
    // stones they are genuine dame and the preamble is eval-neutral
    // (-0.7/+0.4pp). rootStones + pathDepth ~ leaf stone count
    // (captures rare on the descent; poeval has pathDepth = 0).
#if GROWPRE < 6
    if(!scoreMode && rootStones + pathDepth >= GP_MINSTONES) {
#if !defined(ARDUINO) && defined(GROWPRE_STATS)
        extern uint32_t gpMoves, gpPlayouts; gpPlayouts++;
#endif
        for(;;) {
            uint8_t found = 0xFF;
#if GROWPRE >= 3
            // v3: rotate the scan start WITHIN each ring per pick --
            // ring priority (5->2) kept, but playouts from one leaf no
            // longer share identical preambles (v1's fixed order made
            // every playout open with the same ~6 moves; correlated
            // trajectories are the diversity-collapse suspect for the
            // poeval flip rise).
            uint8_t rot = (uint8_t)rnd16();
            static const uint8_t GP_SEG[5] = {0, 1, 9, 25, 49};
            for(uint8_t seg = 0; seg < 4 && found == 0xFF; seg++) {
                uint8_t s0 = GP_SEG[seg], sl = GP_SEG[seg+1] - s0;
                for(uint8_t j = 0; j < sl; j++) {
                    uint8_t k = s0 + (uint8_t)((j + rot) % sl);
                    uint8_t p2 = pgm_read_byte(GROWPRE_SCAN + k);
#else
            for(uint8_t k = 0; k < 49; k++) {
                uint8_t p2 = pgm_read_byte(GROWPRE_SCAN + k);
#endif
                if(simBoard[p2] != EMPTY || p2 == ko) continue;
                uint8_t q2, own = 0, enemy = 0;
                FOR_EACH_NEIGHBOR(q2, p2) {
                    uint8_t s2 = simBoard[q2];
                    if(s2 == toMove) own++;
                    else if(s2 != EMPTY) { enemy = 1; break; }
                }
                if(!enemy && own >= 1 && own <= 2) {
#ifndef GP_GUARDL
#define GP_GUARDL 3
#endif
#if GROWPRE >= 2 && !defined(GP_NOGUARD)
                    // v2 semeai guard: never fill a liberty of an own
                    // chain that is short of liberties itself -- outside
                    // liberties of race groups are own-adjacent, enemy-
                    // free points and v1 threw races by self-filling
                    // them (poeval flips 18.7 -> 25.3%). Capped flood.
                    uint8_t q3, weak = 0;
                    FOR_EACH_NEIGHBOR(q3, p2)
                        if(simBoard[q3] == toMove &&
                           groupLibsCore(q3, 0, GP_GUARDL + 1) <= GP_GUARDL)
                            { weak = 1; break; }
                    if(weak) continue;
#endif
                    found = p2; break;
                }
#if GROWPRE >= 3
                }
            }
#else
            }
#endif
            if(found == 0xFF) break;
#ifdef GP_NORAVE
            // RAVE-poisoning decomposition arm: identical fills, but
            // m >= RAVE_HORIZON suppresses raveMark — every playout
            // from a leaf opens with near-identical fills, so marked
            // preamble moves flood the root player's RAVE stats with
            // generic outcomes.
            uint16_t r2 = playoutTry(found, toMove, ko, 255);
#else
            uint16_t r2 = playoutTry(found, toMove, ko, 0);
#endif
            if(!r2) break;
#if !defined(ARDUINO) && defined(GROWPRE_STATS)
            gpMoves++;
#endif
#if !defined(ARDUINO) && defined(GP_DUMP)
            printf("GPF %u %u\n", found, toMove);
#endif
            ko = (uint8_t)r2;
            last = found;
            toMove ^= 3;
        }
    }
#endif  // GROWPRE < 6
#endif
    capArr[BLACK] = capArr[WHITE] = 0;
    mercyHit = 0;
#ifdef PLAYOUT_STATS
    uint8_t psM = 0, psMercy = 0;
#define PS_TICK psM = m + 1;
#else
#define PS_TICK
#endif
    for(uint8_t m = 0; m < PLAYOUT_CAP && passes < 2; m++) {
#if !defined(ARDUINO) && defined(PT_STATS)
        memset(ptTried, 0, sizeof(ptTried));
#endif
#ifdef PL_MAXLEN
        // truncation sweep (Jay 08-24): hard-stop the playout at N
        // moves and score the open board with the current scorer
        if(m >= PL_MAXLEN) break;
#endif
        PS_TICK
        // Mercy rule: a lopsided capture balance has decided the game;
        // the area score already reflects it, skip the remaining fill.
        // The balance only moves on captures, so the (rare) capture
        // sites keep a one-byte flag via an out-of-line check and this
        // per-move test is a load+branch.
        if(mercyHit) {
#ifdef PLAYOUT_STATS
            psMercy = 1;
#endif
            break;
        }

#ifndef MMERCY
#define MMERCY 48    // SHIP 2026-08-26: margin-checkpoint mercy — every
                     // 16th playout move, exact area count; |margin| >=
                     // 24 pts over the bar ends the playout (capture-
                     // free blowouts; sibling of the capture mercy rule).
                     // -1.5% think (v2 bench), L0 -8 / human-10k -3
                     // washes, +56B (37B per 1%). Threshold ladder:
                     // 16pt misfires (unrealized-stone counts), 24pt
                     // screen-identical at 12% fires, 32pt only 3.4%.
                     // 0 disables. scoreMode-gated (votes need full
                     // boards); mass-insensitive under ZSTOP.
#endif
#if MMERCY > 0
        // Margin-checkpoint mercy (Jay's fast-forward, 08-26): every
        // 16th move run the exact area count; a blowout-grade margin
        // (|d| >= MMERCY half-points over the bar) ends the playout —
        // the mercy rule's sibling for capture-free blowouts. Gated
        // out of scoring playouts (votes need full-board ownership).
        // Threshold must stay blowout-grade: mid-playout counts
        // underread unrealized territory (the truncation-law boundary).
        // (m & 15) tested first: fails 15/16 times and m is already in
        // a register, so the scoreMode SRAM load is skipped on the
        // common path. Same predicate, reordered.
        if((m & 15) == 0 && m && !scoreMode) {
            (void)scoreWinner();               // sets lastMargin2 exactly
            int16_t md = lastMargin2 - (int16_t)simKomi;
            if(md >= MMERCY || md <= -(int16_t)MMERCY) {
#ifdef SRC_STATS
                qeMMercy++;
#endif
                break;
            }
        }
#endif


        // Tactical: if the opponent's just-moved group is in atari,
        // capture it. Uniform-random playouts ignore atari, which
        // wrecks every life-and-death estimate.
        // Heuristics fire probabilistically (michi-style): applied
        // every time, all playouts make the SAME systematic errors and
        // those don't average out. Tactical ~7/8, patterns ~15/16.
        // ONE 16-bit draw feeds every gate this move (Jay's idea,
        // 2026-08): disjoint bit slices are distribution-exact for
        // mask tests, and the 3-5 separate xorshift draws they replace
        // were ~2% of think. Modulo sites keep full draws (narrow
        // slices bias the remainder).
        uint16_t rb = rnd16();
        // (boardAt(last) != EMPTY dropped 2026-08-15: a just-played
        // stone cannot be captured before the next move -- no path
        // leaves `last` on an empty cell; pool-hash-verified.)
        if(last < BOARD_CELLS &&
           (rb & TACT_MASK)
          ) {
            uint8_t tac = 0xFF, isSave = 0;

            // Classify the opponent's just-moved group — free when
            // their gated simPlay already flooded it (board unchanged)
            uint8_t l1, l2, libs;
            if(cacheLibsPos == last) {
                libs = cacheLibs;
                l1 = cacheL1;
                l2 = cacheL2;
            } else {
                libs = groupLibsFind(last);
                l1 = glcL1;
                l2 = glcL2;
            }
            if(libs == 1 && l1 != ko) {
                // Capture the atari'd group
                tac = l1;
            } else if(libs == 2 && ((
                     rb & SQZ_MASK
                     )
                     )) {
                // Squeeze a 2-liberty group: fill one of its
                // liberties. (A 3/4 rate was tried with the race
                // reader and made opening rollouts contact-crazy.)
                // This is what actually kills disconnected stones in
                // playouts — without it, cut-off groups survive by
                // randomness and thin extensions look safe.
                // XOR swap-select: cand = l1 or l2 by coin; the ko
                // fixup flips to the other via one XOR chain (probe)
                uint8_t cand = (rb & 16) ? l1 : l2;
                if(cand == ko) cand = (uint8_t)(cand ^ l1 ^ l2);
                tac = cand;
                isSave = 1; // route through the self-atari gate
            } else {
                // Else escape: their move may have put an own group
                // next to it into atari — extend at its last liberty
                uint8_t q;
                FOR_EACH_NEIGHBOR(q, last) {
                    if(boardAt(q) != toMove) continue;
                    uint8_t sl = soleLiberty(q);
                    // Short read: playouts rarely need long ladders,
                    // and a truncated read defaults to "escape"
                    if(sl != 0xFF && sl != ko &&
                       ladderEscapes(q, sl, 8)) {
                        tac = sl;
                        isSave = 1;
                        break;
                    }
                }
            }

            if(tac != 0xFF) {
                // Captures go direct (snapback reading allowed); a save
                // that self-ataris is a refused ladder step
                uint8_t nk = simPlay(tac, toMove, ko, isSave);
                if(nk != ILLEGAL) {
                    if(toMove == rootTurn && m < RAVE_HORIZON) raveMark(tac);
                    if(simCaptured) {
                        mercyCheck(toMove);   // capArr add absorbed inside
                    }
                    ko = nk;
#ifdef SRC_STATS
                srcCnt[0]++;
#endif
                    last = tac;
                    passes = 0;
                    FT_REC toMove ^= 3;
                    continue;
                }
            }
        }

#if GROWPRE == 6
        // v6 (Jay): the preamble's growth-fill logic as an in-cascade
        // gate AFTER the tactical stage, 7/8 dice (rb bits 13-15,
        // previously unused). Fights get answered first; fills
        // interleave with normal play instead of front-loading. Per-
        // color latch = the preamble's "stop when one side can't
        // fulfill", bounding the ring scans.
        if((rb >> 13) && !scoreMode && !gpDead[toMove - 1]
           && rootStones + pathDepth >= GP_MINSTONES) {
            uint8_t gfound = 0xFF;
            for(uint8_t k = 0; k < 49; k++) {
                uint8_t p2 = pgm_read_byte(GROWPRE_SCAN + k);
                if(simBoard[p2] != EMPTY || p2 == ko) continue;
                uint8_t q2, own = 0, enemy = 0;
                FOR_EACH_NEIGHBOR(q2, p2) {
                    uint8_t s2 = simBoard[q2];
                    if(s2 == toMove) own++;
                    else if(s2 != EMPTY) { enemy = 1; break; }
                }
                if(!enemy && own >= 1 && own <= 2) { gfound = p2; break; }
            }
            if(gfound == 0xFF) gpDead[toMove - 1] = 1;
            else {
#ifdef GP_NORAVE
                uint16_t r2 = playoutTry(gfound, toMove, ko, 255);
#else
                uint16_t r2 = playoutTry(gfound, toMove, ko, m);
#endif
                if(r2) {
#if !defined(ARDUINO) && defined(GROWPRE_STATS)
                    { extern uint32_t gpMoves; gpMoves++; }
#endif
                    ko = (uint8_t)r2;
                    last = gfound;
                    passes = 0;
                    FT_REC toMove ^= 3;
                    continue;
                }
            }
        }
#endif

        // Simple life & death: if the last move borders a small
        // one-color eyespace, its vital point decides life — take it
        // (deny the second eye, or split own space into two). This
        // is what makes dead shapes actually die in playouts instead
        // of surviving on randomness.
        // Gated 1/4 (michi-style probabilistic gating; was 3/4). This
        // block is ~2.7% of think and pure overhead -- unlike the
        // capture/save heuristics it does NOT shorten playouts, so
        // firing it less is a near-free speed win. 1/4 measured
        // strength-neutral vs 3/4 (136 vs 125 / 1000 @ L0, z=+0.73);
        // 25% firing still catches dead shapes across a playout.
#ifndef VIT_OFF
        if(last < BOARD_CELLS &&
           !((rb >> 5) & VIT_MASK)
          ) {
            uint8_t vcand = 0xFF;
            uint8_t q;
            FOR_EACH_NEIGHBOR(q, last) {
                if(boardAt(q) != EMPTY) continue;
                // stampless clone: playouts never read the lazy cache
                uint16_t rv = regionVitalT<0>(q);
                if((uint8_t)rv) vcand = rv >> 8;
                break; // only the first adjacent region
            }
            uint16_t r = playoutTry(vcand, toMove, ko, m);
            if(r) {
                ko = (uint8_t)r;
#ifdef SRC_STATS
                srcCnt[1]++;
#endif
                last = vcand;
                passes = 0;
                FT_REC toMove ^= 3;
                continue;
            }
        }
#endif  // VIT_OFF

        // Root-board vital points: grab one if still open (1/8 per
        // move — hotter distorts ordinary endgame playouts). The
        // local hook above only reacts when the last move touches
        // the eyespace — this is what makes tenuki from a
        // life-and-death spot actually lose rollouts.
#ifndef RVIT_OFF
        if((nRootVitals
           ) &&
           !((rb >> 7) & RVIT_MASK)
          ) {
            uint8_t vp = 0xFF;
            for(uint8_t i = 0; i < nRootVitals; i++)
                if(simBoard[rootVitals[i]] == EMPTY) {
                    vp = rootVitals[i];
                    break;
                }
            uint16_t r = playoutTry(vp, toMove, ko, m);
            if(r) {
                ko = (uint8_t)r;
#ifdef SRC_STATS
                srcCnt[2]++;
#endif
                last = vp;
                passes = 0;
                FT_REC toMove ^= 3;
                continue;
            }
        }
#endif // RVIT_OFF

        // 3x3 patterns at the 8 points around the last move. Rate 3/4
        // (michi-style probabilistic gating): measured strength-neutral
        // vs firing 15/16 (125 vs 127 / 1000 games @ L0, z=-0.13) while
        // skipping the ~8-point pattern scan more often. 1/2 overshot
        // (-2.1pp), so 3/4 is the elbow.
        if(last < BOARD_CELLS &&
           ((rb >> 10) & PAT_MASK)
          ) {
            uint8_t lpxy = posXY(last);
            int8_t lpx = lpxy & 0x0F, lpy = lpxy >> 4;
            uint8_t matches[8];
            uint8_t nMatches = 0;
            if(pgm_read_byte(NEIGHBOR_TABLE + last * 5 + 3) != 0xFF) {
                // Interior last (4th neighbour exists): the whole 3x3 is
                // on-board, so drop the bounds checks and the rowBase
                // multiply -- the cell index just walks +1 across rows
                // stepped by +9. Same visit order, same candidates.
                uint8_t p0 = last - BOARD_SIZE - 1;
                for(int8_t dy = -1; dy <= 1; dy++, p0 += BOARD_SIZE) {
                    int8_t cy = lpy + dy;
                    uint8_t pp = p0;
                    for(int8_t dx = -1; dx <= 1; dx++, pp++) {
                        // no (0,0) skip: the centre is `last`, always
                        // occupied, so the EMPTY check rejects it.
                        // Filter ORDER (2026-08-15): pattern first, eye
                        // second -- the predicates commute (both pure
                        // reads of the current board, same match set),
                        // but bonus>0 passes ~2/8 while isOwnEye rejects
                        // only ~1/20, so the cheap-to-fail test should
                        // gate the other, not vice versa.
                        if(boardAt(pp) != EMPTY || pp == ko) continue;
                        if(patternBonus(lpx + dx, cy, toMove) <= 0) continue;
                        if(isOwnEye(pp, toMove)) continue;
                        matches[nMatches++] = pp;
                    }
                }
            } else
            for(int8_t dy = -1; dy <= 1; dy++) {
                int8_t cy = lpy + dy;
                if((uint8_t)cy >= BOARD_SIZE) continue;  // hoisted row check
                uint8_t rowBase = (uint8_t)cy * BOARD_SIZE;
                for(int8_t dx = -1; dx <= 1; dx++) {
                    // no (0,0) skip: the centre is `last` itself, always
                    // occupied by the stone just played, so the EMPTY
                    // check below rejects it -- same matches. (A D8
                    // PROGMEM-table flatten of this loop measured +0.16%
                    // SLOWER: 3 lpm/point beats the loop machinery here,
                    // unlike keima where most iterations were filtered.)
                    int8_t cx = lpx + dx;
                    if((uint8_t)cx >= BOARD_SIZE) continue;
                    uint8_t pos = rowBase + (uint8_t)cx;
                    if(boardAt(pos) != EMPTY || pos == ko) continue;
#if PT_BANCACHE > 0
                    if(ptBan[toMove - 1][pos >> 3] & bitMask(pos)) continue;
#endif
                    if(patternBonus(cx, cy, toMove) <= 0) continue;
                    if(isOwnEye(pos, toMove)) continue;   // (order: see above)
                    matches[nMatches++] = pos;
                }
            }
            if(nMatches) {
                uint8_t mp = matches[rnd(nMatches)];
                uint16_t r = playoutTryPat(mp, toMove, ko, m);
                if(r) {
                    ko = (uint8_t)r;
#ifdef SRC_STATS
                srcCnt[3]++;
#endif
                    last = mp;
                    passes = 0;
                    FT_REC toMove ^= 3;
                    continue;
                }
            }
        }


        // Local answer: half the time, try one random point around
        // the last move before the global probe — plain contact
        // resistance the patterns can't express. A LONE last stone
        // (no support within distance 2 — an invasion or deep
        // reduction) additionally gets a contact reply on 3/4 of the
        // remaining coin, ~7/8 total: without that, random invasions
        // of settled territory live far too often, the single
        // biggest playout evaluation bias.
        if(last < BOARD_CELLS) {   // (dead EMPTY guard dropped, see above)
            // NOT rb's spare bits: xorshift16 outputs carry strong
            // intra-word bit correlations (the <<7/>>9/<<8 taps copy
            // bit patterns around), and feeding the local answer from
            // the same word as the gates shifted playout dynamics
            // wholesale (open bench -18%!). Fresh draw = independent.
            uint16_t p = rnd16();
            uint8_t pos = 0xFF;
            // posXY sunk into the branches that read lx/ly (2026-08-15):
            // the contact-answer path picks nbl[rnd(nl)] and never needs
            // the coordinates.
            if(p & 1) {
                uint8_t lxy = posXY(last);
                int8_t lx = lxy & 0x0F, ly = lxy >> 4;
                int8_t cx = lx + (int8_t)rndMod(3) - 1;
                int8_t cy = ly + (int8_t)rndMod(3) - 1;
                if(cx >= 0 && cx < BOARD_SIZE && cy >= 0 && cy < BOARD_SIZE)
                    pos = cy * BOARD_SIZE + cx;
            } else if(rootStones + m >= EARLY_STONES &&
                      (p & ((CONTACT_ANSWER_MASK | LONE_ANSWER_MASK) << 1))
                          != 0) {
                // union pre-gate: if neither case's coin could pass,
                // skip the classification scan (with equal masks this
                // is exactly the pre-split gate — no extra work)
                // (gated: in the opening EVERY stone is "lone" and
                // every attachment is normal — these answer rules
                // only mean something once territory has shape)
                // Answer a stone that CONTACTS us (a boundary push)
                // or one with no support within 2 (an invasion);
                // either way the reply is a contact move. The two
                // cases carry SEPARATE probability masks.
                uint8_t lastColor = boardAt(last);
                // One pass over last's neighbours: collect them into nbl[]
                // (needed for the random pick nbl[rnd(nl)] below), count nl,
                // and flag a contact push -- no break, nl must count all.
                uint8_t nbl[4], nl = 0, contact = 0, answer = 0, q;
                uint8_t *nw = nbl;   // pointer store beats indexed (probe)
                FOR_EACH_NEIGHBOR(q, last) {
                    *nw++ = q;
                    if(boardAt(q) == toMove) contact = 1; // contact push
                }
                nl = (uint8_t)(nw - nbl);
                if(contact) {
                    answer = (p & (CONTACT_ANSWER_MASK << 1)) != 0;
                } else if((p & (LONE_ANSWER_MASK << 1)) != 0) {
                    // 5x5 support scan only after the coin passes
                    uint8_t lxy = posXY(last);
                    int8_t lx = lxy & 0x0F, ly = lxy >> 4;
                    answer = 1; // lone unless support found
                    const uint8_t *rp = simBoard + last - 2 * BOARD_SIZE;
                    for(int8_t dy = -2; dy <= 2 && answer;
                        dy++, rp += BOARD_SIZE) {
                        if((uint8_t)(ly + dy) >= BOARD_SIZE) continue;
                        for(int8_t dx = -2; dx <= 2; dx++) {
                            if(!dx && !dy) continue;
                            if((uint8_t)(lx + dx) >= BOARD_SIZE) continue;
                            if(rp[dx] == lastColor) {
                                answer = 0;
                                break;
                            }
                        }
                    }
                }
                if(answer) pos = nbl[rnd(nl)];
            }
            if(pos != 0xFF) {
                uint16_t r = playoutTry(pos, toMove, ko, m);
                if(r) {
#ifdef SRC_STATS
                srcCnt[4]++;
#endif
                    ko = (uint8_t)r;
                    last = pos;
                    passes = 0;
                    FT_REC toMove ^= 3;
                    continue;
                }
            }
        }

        uint8_t start = rndMod(BOARD_CELLS);
        uint8_t played = 0;
        // Two-phase circular scan (start..80, then 0..start-1): identical
        // visit order to the old start+i-with-wrap, but the per-cell wrap
        // check -- the hottest inlined line in the engine -- is gone; the
        // phase switch runs once. continue still steps pos++ naturally.
        uint8_t pos = start, scanEnd = BOARD_CELLS;
        for(;; pos++) {
            if(pos >= scanEnd) {
                if(scanEnd != BOARD_CELLS || start == 0) break;
                pos = 0; scanEnd = start;   // phase 2: 0..start-1
            }
            if(boardAt(pos) != EMPTY || pos == ko) continue;
#if PT_BANCACHE > 0
            if(ptBan[toMove - 1][pos >> 3] & bitMask(pos)) continue;
#endif

            // Lonely first-line moves are pure noise: skip unless the
            // point touches a stone. Slot 3 == 0xFF <=> <4 neighbours
            // <=> first line.
            const uint8_t *ne = NEIGHBOR_TABLE + pos * 5;
            uint8_t fourth = pgm_read_byte(ne + 3);  // 0xFF iff <4 neighbours
            if(fourth == 0xFF) {
                uint8_t contact = 0, q;
                while(!((q = pgm_read_byte(ne++)) & 0x80)) {   // sign-bit
                    if(boardAt(q) != EMPTY) { contact = 1; break; }
                }
                if(!contact) continue;
            }

            // Territory bias: filling deep empty space at random
            // splits open areas 50/50 no matter who holds the
            // boundary, hiding the value of quiet boundary moves
            // from the eval. Past the opening, 3/4 of the time defer
            // isolated points (no stone in the 8-neighborhood) and
            // grow from existing structure instead.
#ifndef PLAYOUT_GROW_MASK
#define PLAYOUT_GROW_MASK 3
#endif
            if(fourth != 0xFF && rootStones + m >= EARLY_STONES &&
               (rnd16() & PLAYOUT_GROW_MASK)) {
                // fourth != 0xFF already proved pos interior, so the
                // whole 3x3 is on-board: no posXY, no bounds checks.
                // EMPTY == 0, so "any stone in the 8-neighbourhood" is
                // an OR over eight ldd loads off one pointer.
                const uint8_t *tp = simBoard + pos - BOARD_SIZE - 1;
                uint8_t touch = tp[0] | tp[1] | tp[2] |
                                tp[9] | tp[11] |
                                tp[18] | tp[19] | tp[20];
                if(!touch) {
#ifdef SRC_STATS
                    srcCnt[7]++;
#endif
                    continue;
                }
            }

            uint16_t r = playoutTryOpen(pos, toMove, ko, m);
            if(!r) continue;
#ifdef SRC_STATS
                srcCnt[5]++;
#endif
            ko = (uint8_t)r;
            last = pos;
            played = 1;
            passes = 0;
            break;
        }
        if(!played) {
#ifdef SRC_STATS
            srcCnt[6]++;
#endif
            passes++;
            last = 0xFF;
            ko = NO_KO;
        }
        FT_REC toMove ^= 3;
    }
#ifdef PLAYOUT_STATS
    plN++; plMoves += psM;
    if(psMercy) plEndMercy++;
    else if(passes >= 2) plEndPass++;
    else plEndCap++;
#endif
#undef PS_TICK
    return scoreWinner();
}

// ==================== Dead-stone scoring ====================
// The static scorer counts any one-color-bordered region as
// territory, so a dead group whose eyespace can never be two eyes
// (a square four in a real game) scores as alive — flipping the
// game result. At the end, vote with light playouts: a stone whose
// cell finishes opponent-owned in most of them is dead; remove it
// as a capture, then score the cleaned board statically. The
// scoring screen also gets the cleaned board, so dead stones
// visibly come off at the count.
#ifndef SCORE_PLAYOUTS
#define SCORE_PLAYOUTS 64
#endif

#ifdef LD_CLASS
// ---- Stage-1 life-and-death classifier (design study 2026-08-01) ----
// Static group status, computed once per think on the root board.
// Chains sharing an eyespace wall are unioned into a COMPLEX (they
// live or die together, to first order); each complex accumulates
// eye value in HALF-EYE units from its adjacent regions:
//   size 1: 2 if a real eye (the shipped diagonal test), else 0
//   size 2: 2 (one eye, throw-in proof)
//   size 3-6, unique vital: secure 2 / max 4 -- CRITICAL, vital = move
//     (square four: secure 0 / max 2, the known dead shape)
//   size >= 7: 4 (two eyes)
//   poked wall (1-2 enemy stones, majority >= 3): secure 0 / max 4,
//     CRITICAL, the gate cell = move (the almostVital lesson)
//   contested / oversize: no value; wall chains get an open liberty
// Status: ALIVE (secure >= 4); DEAD (max < 4, no open liberties);
// CRITICAL (secure < 4 <= max, settling move recorded); else untagged.
// Consumers must treat DEAD as the least trusted tag (static analysis
// cannot see seki or ko) -- stage 2 gives it only the weakest role.
#define LD_NONE 0
#define LD_ALIVE 1
#define LD_DEAD 2
#define LD_CRIT 3
static uint8_t ldStatus[64];   // per complex root id
static uint8_t ldMove[64];     // settling move for LD_CRIT
static uint8_t ldParent[64];
static uint8_t ldFind(uint8_t x) {
    while(ldParent[x] != x) { ldParent[x] = ldParent[ldParent[x]]; x = ldParent[x]; }
    return x;
}
static void ldUnion(uint8_t a, uint8_t b) {
    a = ldFind(a); b = ldFind(b);
    if(a != b) ldParent[b] = a;
}
// status of the chain occupying `cell` (0xFF cell / empty = LD_NONE)
static uint8_t ldCellStatus(uint8_t cell) {
    if(cell >= BOARD_CELLS || simBoard[cell] == EMPTY) return LD_NONE;
    uint8_t id = CHAIN_OF(chainId[cell]);
    if(ldStatus[id] != LD_NONE) return ldStatus[id];  // hostage override
    return ldStatus[ldFind(id)];
}
// Exact liberty count of the chain at `start` (cap 12). Uses newMark,
// so ONLY callable outside the region sweep (which owns the epoch).
static uint8_t ldChainLibs(uint8_t start) {
    uint8_t color = boardAt(start);
    uint8_t libs = 0, sp = 0;
    newMark();
    simMark[start] = markEpoch;
    floodScratch[sp++] = start;
    while(sp) {
        uint8_t p = floodScratch[--sp], q;
        FOR_EACH_NEIGHBOR(q, p) {
            if(simMark[q] == markEpoch) continue;
            simMark[q] = markEpoch;
            if(simBoard[q] == EMPTY) { if(++libs >= 12) return 12; }
            else if(simBoard[q] == color) floodScratch[sp++] = q;
        }
    }
    return libs;
}

static void ldClassify() {
    uint8_t eyes2s[64], eyes2m[64], openL[64], critMv[64];
    for(uint8_t i = 0; i < 64; i++) {
        ldParent[i] = i;
        eyes2s[i] = eyes2m[i] = openL[i] = 0;
        critMv[i] = ldMove[i] = 0xFF;
        ldStatus[i] = LD_NONE;
    }
    buildChainMap();
    uint8_t cellOf[64];
    for(uint8_t i = 0; i < 64; i++) cellOf[i] = 0xFF;
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        if(simBoard[i] != EMPTY) cellOf[CHAIN_OF(chainId[i])] = i;
    // Hostage candidates found during the sweep, verified after it
    // (exact liberty counting needs the mark epoch the sweep owns):
    // a majority-colour wall chain whose in-region liberty count may
    // equal its total. If the poked region falls, the hostage falls
    // -- per-chain CRITICAL at the gate, overriding complex eyes
    // (hunt 5328: A8's liberties are all inside the invaded space;
    // the ALIVE complex verdict is true for the wall, not for A8).
    struct { uint8_t id, libsIn, gate; } pend[16];
    uint8_t nPend = 0;
    newMark();
    for(uint8_t seed = 0; seed < BOARD_CELLS; seed++) {
        if(simBoard[seed] != EMPTY || simMark[seed] == markEpoch) continue;
        // flood this empty region (cap 10: bigger = open space)
        uint8_t region[10];
        uint8_t cnt = 0, head = 0, over = 0;
        uint8_t nB = 0, nW = 0;
        uint8_t wall[8]; uint8_t nWall = 0; // distinct wall chain ids
        region[cnt++] = seed;
        simMark[seed] = markEpoch;
        while(head < cnt) {
            uint8_t q;
            FOR_EACH_NEIGHBOR(q, region[head]) {
                uint8_t st = simBoard[q];
                if(st != EMPTY) {
                    if(st == BLACK) nB++; else nW++; // stone-adjacency count
                    uint8_t id = CHAIN_OF(chainId[q]);
                    uint8_t known = 0;
                    for(uint8_t k = 0; k < nWall; k++)
                        if(wall[k] == id) { known = 1; break; }
                    if(!known && nWall < 8) wall[nWall++] = id;
                    continue;
                }
                if(simMark[q] == markEpoch) continue;
                if(cnt >= 10) { over = 1; continue; }
                simMark[q] = markEpoch;
                region[cnt++] = q;
            }
            head++;
        }
        // classify the wall (adjacency counts, not distinct stones:
        // cheaper, and a 2-cell poke still reads as small)
        uint8_t minC = nB < nW ? nB : nW;
        uint8_t majColor = nB < nW ? WHITE : BLACK;
        uint8_t pokeColor = 3 - majColor;
        if(over || (minC > 2 && nB >= 3 && nW >= 3)) {
            // open space / genuinely contested: escape route for all
            for(uint8_t k = 0; k < nWall; k++) openL[ldFind(wall[k])] = 1;
            continue;
        }
        // union the majority-colour wall chains through this region
        uint8_t root = 0xFF;
        for(uint8_t k = 0; k < nWall; k++) {
            // wall[] ids belong to either colour; union majority only
            uint8_t cell = 0xFF, q2;
            // find a wall stone of this id to check its colour
            for(uint8_t j = 0; j < cnt && cell == 0xFF; j++)
                FOR_EACH_NEIGHBOR(q2, region[j])
                    if(simBoard[q2] != EMPTY &&
                       CHAIN_OF(chainId[q2]) == wall[k]) { cell = q2; break; }
            if(cell == 0xFF || simBoard[cell] != majColor) continue;
            if(root == 0xFF) root = ldFind(wall[k]);
            else ldUnion(root, wall[k]);
        }
        if(root == 0xFF) continue;
        root = ldFind(root);
        uint8_t s2 = 0, m2 = 0, cm = 0xFF;
        if(minC == 0) {                       // enclosed, single colour
            if(cnt == 1) {
                // real single-point eye? (isOwnEye includes the shipped
                // false-eye diagonal test; colour = wall colour)
                s2 = m2 = isOwnEye(region[0], majColor) ? 2 : 0;
            } else if(cnt == 2) { s2 = m2 = 2; }
            else if(cnt >= 7)   { s2 = m2 = 4; }
            else {
                uint8_t bd, ties;
                uint8_t vit = regionVitalCell(region, cnt, &bd, &ties);
                if(ties == 1 && vit != 0xFF) { s2 = 2; m2 = 4; cm = vit; }
                else if(cnt == 4 && bd == 2 && ties == 4)
                    { s2 = 0; m2 = 2; cm = vit; }  // square four
                else { s2 = 2; m2 = 2; }
            }
        } else {                              // poked wall: gate cell
            uint8_t gate = 0xFF, multi = 0;
            for(uint8_t j = 0; j < cnt && !multi; j++) {
                uint8_t q3;
                FOR_EACH_NEIGHBOR(q3, region[j])
                    if(simBoard[q3] == pokeColor) {
                        if(gate != 0xFF && gate != region[j]) { multi = 1; break; }
                        gate = region[j];
                    }
            }
            s2 = 0;
            m2 = (cnt >= 3) ? 4 : 2;
            if(!multi) cm = gate;
            // hostage candidates: majority-colour wall chains with
            // in-region liberties (region cells adjacent to them)
            if(!multi && gate != 0xFF) {
                for(uint8_t k = 0; k < nWall && nPend < 16; k++) {
                    uint8_t c0 = cellOf[wall[k]];
                    if(c0 == 0xFF || simBoard[c0] != majColor) continue;
                    uint8_t li = 0;
                    for(uint8_t j = 0; j < cnt; j++) {
                        uint8_t q4, hit = 0;
                        FOR_EACH_NEIGHBOR(q4, region[j])
                            if(simBoard[q4] != EMPTY &&
                               CHAIN_OF(chainId[q4]) == wall[k]) { hit = 1; break; }
                        li += hit;
                    }
                    if(li)
                        pend[nPend++] = { wall[k], li, gate };
                }
            }
        }
        eyes2s[root] = (eyes2s[root] + s2 > 8) ? 8 : eyes2s[root] + s2;
        eyes2m[root] = (eyes2m[root] + m2 > 8) ? 8 : eyes2m[root] + m2;
        if(cm != 0xFF && critMv[root] == 0xFF) critMv[root] = cm;
    }
    // fold accumulators onto union roots and tag
    for(uint8_t i = 1; i < 64; i++) {
        uint8_t r = ldFind(i);
        if(r != i) {
            eyes2s[r] = (eyes2s[r] + eyes2s[i] > 8) ? 8 : eyes2s[r] + eyes2s[i];
            eyes2m[r] = (eyes2m[r] + eyes2m[i] > 8) ? 8 : eyes2m[r] + eyes2m[i];
            openL[r] |= openL[i];
            if(critMv[r] == 0xFF) critMv[r] = critMv[i];
        }
    }
    for(uint8_t i = 1; i < 64; i++) {
        if(ldFind(i) != i) continue;
        if(cellOf[i] == 0xFF && eyes2s[i] == 0 && eyes2m[i] == 0)
            continue;                                 // no such chain
        if(eyes2s[i] >= 4) ldStatus[i] = LD_ALIVE;
        else if(openL[i]) ldStatus[i] = LD_NONE;      // can run: no tag
        else if(eyes2m[i] < 4) ldStatus[i] = LD_DEAD;
        else { ldStatus[i] = LD_CRIT; ldMove[i] = critMv[i]; }
    }
    // Hostage verification (exact liberties == in-region liberties):
    // per-CHAIN critical tag at the region's gate, overriding the
    // complex verdict for that chain only. ldCellStatus reads the
    // chain's own slot before the complex root's, so hostages shine
    // through an ALIVE complex.
    for(uint8_t k = 0; k < nPend; k++) {
        uint8_t id = pend[k].id;
        if(ldChainLibs(cellOf[id]) == pend[k].libsIn) {
            ldStatus[id] = LD_CRIT;
            ldMove[id] = pend[k].gate;
        }
    }
}
#endif


#ifdef OWL_LITE
// ---- OWL-LITE (endgame-arc, 2026-08-11) -------------------------------
// Bounded local life-status solver for ONE own group at the root. The
// question is NOT full L&D but the anti-feeding question: "can this group
// reach two eyes (or escape: 5+ libs, or capture an adjacent atari chain)
// within OWL_DEPTH plies against best local resistance?" No line found =
// HOPELESS -> the root pick vetoes moves that feed the group (the
// rootSelfAtari veto pattern). Conservative by construction; unlike random
// playouts this search EXECUTES ordered kills/saves.
// MEASURED (2026-08-11): solver validated on the death census — 7/32
// true-dead recall (incl. the −47.7/−37.8 monsters), 6/93 false-DEAD on
// survivors, sub-ms, worst 164 nodes. THE VETO CONSUMER IS HARMFUL:
// human n=1000 paired winrate 242->228 n.s. but scoreLead −2.05 t=−2.51
// SIGNIFICANT — close losses 148->124 by sliding into BIGGER losses.
// LESSON: true deaths are once-a-game events but the veto fires every
// think, so a ~6% false-DEAD rate compounds (~3 vetoes/game) and each
// false fire abandons a defensible group. A status verdict needs ~10x
// that precision before ANY per-think consumer can profit. OFF; the
// solver + census harness (test/owlcensus.cpp, death/control_events.h)
// are the reusable parts.
#define OWL_DEPTH 6
#define OWL_MAXCAND 6
#define OWL_BUDGET 800
#define OWL_MAXCAP 20
static uint8_t owlArena[15][OWL_MAXCAP];            // per-depth undo cells (v2 depth 14)
static uint16_t owlNodes;
static uint8_t owlSeed, owlColor;
static uint8_t owlVeto[11];        // cells of the hopeless group (bitmap)
static uint8_t owlVetoActive;

// Remove the chain at p (color c); append its cells to undo, return count
// (0xFE = overflow -> caller treats the position as unresolvable/alive).
static uint8_t owlRemove(uint8_t p, uint8_t c, uint8_t *undo) {
    uint8_t st[24]; uint8_t sp = 0, n = 0;
    st[sp++] = p; simBoard[p] = EMPTY;
    undo[n++] = p;
    while(sp) {
        uint8_t u = st[--sp], q;
        FOR_EACH_NEIGHBOR(q, u)
            if(simBoard[q] == c) {
                if(n >= OWL_MAXCAP || sp >= 24) return 0xFE;
                simBoard[q] = EMPTY;
                undo[n++] = q;
                st[sp++] = q;
            }
    }
    return n;
}

// Place, resolve captures. Returns capture count, 0xFF suicide/illegal,
// 0xFE overflow. *koCell = the cell of a single-stone capture (simple-ko
// guard: the opponent may not immediately retake there).
static uint8_t owlPlay(uint8_t pos, uint8_t c, uint8_t *undo, uint8_t *koCell) {
    simBoard[pos] = c;
    uint8_t n = 0, q;
    *koCell = 0xFF;
    FOR_EACH_NEIGHBOR(q, pos)
        if(simBoard[q] == (uint8_t)(3 - c) && !hasLiberty(q, (uint8_t)(3 - c))) {
            uint8_t r = owlRemove(q, (uint8_t)(3 - c), undo + n);
            if(r == 0xFE || n + r > OWL_MAXCAP) { // restore what we took
                for(uint8_t i = 0; i < n; i++) simBoard[undo[i]] = (uint8_t)(3 - c);
                simBoard[pos] = EMPTY;
                return 0xFE;
            }
            n += r;
        }
    if(!n && !hasLiberty(pos, c)) { simBoard[pos] = EMPTY; return 0xFF; }
    if(n == 1) *koCell = undo[0];
    return n;
}
static void owlUndoPlay(uint8_t pos, uint8_t c, const uint8_t *undo, uint8_t n) {
    simBoard[pos] = EMPTY;
    for(uint8_t i = 0; i < n; i++) simBoard[undo[i]] = (uint8_t)(3 - c);
}

// ---- v2: EXACT SYSTEM SOLVER (Jay's segmentation, 2026-08-11) --------
// Segment: the group's cavity flooded through empties, where 1-SPACE-JUMP
// gaps (attacker both sides on an axis) and STONE-TO-EDGE gaps (attacker
// one side, edge the other; >=1 real stone) SEAL propagation but join the
// system as contested cells. Census: closes 12/32 death events carrying
// 375/490 bleed points; the two extra events blanket 1-dilation closed
// were exactly the unsound lone-stone seals. Wall chains: 0 outside libs
// = in-system (semeai, capturable); exactly 1 = NO VERDICT (globally
// ambiguous); >=2 = locally immortal (outside libs are unfillable by
// local play). Complete search on the system (defender first, node cap,
// in-system simple ko); defender escaping through a gap to >=2 outside
// liberties is ALIVE unconditionally. DEAD verdicts are proofs modulo
// the immortal-wall and static-outside conventions.
// FINAL VERDICT (2026-08-11, pristine-board census at depth 30 / 2M
// nodes): DEAD-recall 0/32, false-DEAD 0/93. The solver is CORRECT and
// the result is the finding: the census groups genuinely LIVE in their
// local systems (1996@44: 40k nodes -> alive; the 2-cell-cavity monsters
// hold their cavities in seki/small life) — KataGo kills them through
// GLOBAL play: the "locally immortal wall" convention is empirically
// false on 9x9, where mid-game death is a whole-board phenomenon. The
// class of locally-decidable deaths is EMPTY on this board size. Exact
// local L&D solving is therefore a dead end for THIS engine — kept for
// the machinery (segmentation, jump/edge seals, complete cavity search)
// and for the measurement method. KNOWN BUG if ever revived: the
// loadRootBoard scan's solve calls leak board mutations (census results
// differed pristine vs post-scan) — audit owlPlay/owlUndoPlay pairing.
#ifndef OWL_SYS_MAX
#define OWL_SYS_MAX 9
#endif
#ifndef OWL_V2_BUDGET
#define OWL_V2_BUDGET 4000
#endif
#ifndef OWL_V2_DEPTH
#define OWL_V2_DEPTH 14
#endif
static uint8_t owlSys[OWL_SYS_MAX];    // system cells (cavity + gaps)
static uint8_t owlNSys;
static uint8_t owlGap[OWL_SYS_MAX];    // 1 = sealed gap cell (borders outside)

static uint8_t owlSealedJE(uint8_t q, uint8_t atk) {
    uint8_t x = q % 9, y = q / 9;
    // horizontal axis
    uint8_t la = (x > 0) ? simBoard[q - 1] : 0xFF;   // 0xFF = edge
    uint8_t ra = (x < 8) ? simBoard[q + 1] : 0xFF;
    if((la == atk && (ra == atk || ra == 0xFF)) ||
       (ra == atk && la == 0xFF)) return 1;
    uint8_t ua = (y > 0) ? simBoard[q - 9] : 0xFF;
    uint8_t da = (y < 8) ? simBoard[q + 9] : 0xFF;
    if((ua == atk && (da == atk || da == 0xFF)) ||
       (da == atk && ua == 0xFF)) return 1;
    return 0;
}

// Build the system for the group at owlSeed. Returns 0 = no verdict
// possible (too big / ambiguous wall), 1 = system ready.
static uint8_t owlSegment(void) {
    uint8_t atk = (uint8_t)(3 - owlColor);
    owlNSys = 0;
    // flood the group, collect liberty cells as flood seeds
    uint8_t st[28]; uint8_t sp = 0;
    uint8_t seeds[OWL_SYS_MAX]; uint8_t nseeds = 0;
    newMark();
    st[sp++] = owlSeed; *markPtr(owlSeed) = markEpoch;
    while(sp) {
        uint8_t u = st[--sp], q;
        FOR_EACH_NEIGHBOR(q, u) {
            if(*markPtr(q) == markEpoch) continue;
            if(simBoard[q] == owlColor) {
                if(sp >= 28) return 0;
                *markPtr(q) = markEpoch;
                st[sp++] = q;
            } else if(simBoard[q] == EMPTY) {
                *markPtr(q) = markEpoch;
                if(nseeds >= OWL_SYS_MAX) return 0;
                seeds[nseeds++] = q;
            }
        }
    }
    if(!nseeds) return 0;
    // cavity flood: sealed cells join but do not propagate
    uint8_t cq[OWL_SYS_MAX]; uint8_t head = 0, tail = 0;
    newMark();
    for(uint8_t i = 0; i < nseeds; i++) {
        owlSys[owlNSys] = seeds[i];
        owlGap[owlNSys] = owlSealedJE(seeds[i], atk);
        *markPtr(seeds[i]) = markEpoch;
        if(!owlGap[owlNSys]) cq[tail++] = seeds[i];
        owlNSys++;
        if(owlNSys > OWL_SYS_MAX) return 0;
    }
    while(head < tail) {
        uint8_t u = cq[head++], q;
        FOR_EACH_NEIGHBOR(q, u) {
            if(simBoard[q] != EMPTY || *markPtr(q) == markEpoch) continue;
            *markPtr(q) = markEpoch;
            if(owlNSys >= OWL_SYS_MAX) return 0;
            uint8_t sealed = owlSealedJE(q, atk);
            owlSys[owlNSys] = q;
            owlGap[owlNSys] = sealed;
            owlNSys++;
            if(!sealed) { if(tail >= OWL_SYS_MAX) return 0; cq[tail++] = q; }
        }
    }
    // wall audit: every attacker chain adjacent to a system cell must have
    // 0 outside libs (in-system) or >=2 (immortal); exactly 1 = no verdict.
    // system cells carry markEpoch; walk walls with a second scratch mark
    // impossible (one epoch) -> audit via direct lib counting per chain,
    // deduped by lowest-cell tag stored in a tiny list.
    uint8_t seenWall[6]; uint8_t nw = 0;
    for(uint8_t i = 0; i < owlNSys; i++) {
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, owlSys[i]) {
            if(simBoard[q] != atk) continue;
            // chain id = its lowest cell: flood once to find it
            uint8_t wst[24]; uint8_t wsp = 0, lowest = q, outl = 0;
            uint8_t wseen[11]; memset(wseen, 0, sizeof(wseen));
            wst[wsp++] = q; wseen[q >> 3] |= bitMask(q);
            while(wsp) {
                uint8_t w = wst[--wsp], r;
                if(w < lowest) lowest = w;
                FOR_EACH_NEIGHBOR(r, w) {
                    if(simBoard[r] == atk && !(wseen[r >> 3] & bitMask(r))) {
                        if(wsp >= 24) return 0;
                        wseen[r >> 3] |= bitMask(r);
                        wst[wsp++] = r;
                    } else if(simBoard[r] == EMPTY && *markPtr(r) != markEpoch) {
                        outl++;   // liberty outside the system
                    }
                }
            }
            uint8_t dup = 0;
            for(uint8_t j = 0; j < nw; j++) if(seenWall[j] == lowest) dup = 1;
            if(dup) continue;
            if(nw >= 6) return 0;
            seenWall[nw++] = lowest;
            // >=1 outside lib = locally immortal (standard convention:
            // outside libs are unfillable by local play). The stricter
            // outl==1 abort was measured to kill segmentation on most
            // census events (walls in live fights have exactly 1); the
            // control set arbitrates the precision cost empirically.
        }
    }
    return 1;
}

// Complete search over the system. Returns 1 = defender lives/escapes/
// unresolvable, 0 = proven dead. `passes` = consecutive passes.
static uint8_t owlSolve(uint8_t depth, uint8_t toMove, uint8_t koCell,
                        uint8_t passes) {
    if(++owlNodes > OWL_V2_BUDGET) return 1;
    if(simBoard[owlSeed] != owlColor) return 0;         // captured
    // eye count: liberties of the group that are true own eyes
    uint8_t st[28]; uint8_t sp = 0, eyes = 0;
    newMark();
    st[sp++] = owlSeed; *markPtr(owlSeed) = markEpoch;
    while(sp) {
        uint8_t u = st[--sp], q;
        FOR_EACH_NEIGHBOR(q, u) {
            if(*markPtr(q) == markEpoch) continue;
            if(simBoard[q] == owlColor) {
                if(sp >= 28) return 1;                  // grew out: alive
                *markPtr(q) = markEpoch;
                st[sp++] = q;
            } else if(simBoard[q] == EMPTY) {
                *markPtr(q) = markEpoch;
                if(isOwnEye(q, owlColor)) eyes++;
            }
        }
    }
    if(eyes >= 2) return 1;
    if(passes >= 2) return 1;       // attacker cannot improve: seki/alive
    if(!depth) return 1;            // depth cap: NO VERDICT (conservative)
    uint8_t defender = (toMove == owlColor);
    uint8_t *undo = owlArena[depth];    // depth <= 14 by construction
    uint8_t anyMove = 0;
    for(uint8_t i = 0; i < owlNSys; i++) {
        uint8_t m = owlSys[i];
        if(simBoard[m] != EMPTY || m == koCell) continue;
        uint8_t ko2;
        uint8_t n = owlPlay(m, toMove, undo, &ko2);
        if(n == 0xFF) continue;
        if(n == 0xFE) return 1;
        anyMove = 1;
        uint8_t r;
        if(defender && owlGap[i]) {
            // escape check: the placed stone's chain liberties OUTSIDE
            // the system (system empties are marked... marks were churned
            // by owlPlay's hasLiberty; recount directly against owlSys)
            uint8_t est[24]; uint8_t esp = 0, outs = 0;
            uint8_t eseen[11]; memset(eseen, 0, sizeof(eseen));
            est[esp++] = m; eseen[m >> 3] |= bitMask(m);
            while(esp && outs < 2) {
                uint8_t u = est[--esp], q;
                FOR_EACH_NEIGHBOR(q, u) {
                    if(simBoard[q] == toMove && !(eseen[q >> 3] & bitMask(q))) {
                        if(esp >= 24) { outs = 2; break; }
                        eseen[q >> 3] |= bitMask(q);
                        est[esp++] = q;
                    } else if(simBoard[q] == EMPTY) {
                        uint8_t inSys = 0;
                        for(uint8_t k2 = 0; k2 < owlNSys; k2++)
                            if(owlSys[k2] == q) { inSys = 1; break; }
                        if(!inSys && !(eseen[q >> 3] & bitMask(q))) {
                            eseen[q >> 3] |= bitMask(q);
                            outs++;
                        }
                    }
                }
            }
            if(outs >= 2) {         // broke through the seal
                owlUndoPlay(m, toMove, undo, n);
                return 1;
            }
        }
        r = owlSolve((uint8_t)(depth - 1), (uint8_t)(3 - toMove), ko2, 0);
        owlUndoPlay(m, toMove, undo, n);
        if(defender && r) return 1;
        if(!defender && !r) return 0;
    }
    // pass move (both sides may pass; two passes = settled)
    {
        uint8_t r = owlSolve((uint8_t)(depth - 1), (uint8_t)(3 - toMove),
                             0xFF, (uint8_t)(passes + 1));
        if(defender && r) return 1;
        if(!defender && !r) return 0;
    }
    (void)anyMove;
    return (uint8_t)!defender;
}

// Entry: proven hopeless? 1 = DEAD (a proof, modulo wall conventions).
static uint8_t owlHopeless(uint8_t seed) {
    owlSeed = seed;
    owlColor = simBoard[seed];
    owlNodes = 0;
    if(!owlSegment()) {
#if defined(OWL_DEBUG) && !defined(ARDUINO)
        fprintf(stderr, "OWLDBG segfail\n");
#endif
        return 0;
    }
#if defined(OWL_DEBUG) && !defined(ARDUINO)
    fprintf(stderr, "OWLDBG sys=%u\n", owlNSys);
#endif
    return (uint8_t)!owlSolve(OWL_V2_DEPTH, owlColor, 0xFF, 0);
}
#endif

#if defined(PRIOR_DUMP) && !defined(ARDUINO)
static uint8_t priorDumpOn;
uint32_t priorDumpSeq;   // non-static: dump drivers reset it per GAME so
                         // the 1-in-4 widen sampling phase is invariant
                         // to worker slice boundaries (cross-machine
                         // byte-identity requires it)
static uint32_t priorRootHash;   // FNV-1a of the root board, set per think
#endif
#if defined(PRIORNN) || defined(PRIOR_DUMP)
#include "priornet_weights.h"   /* PRIOR_DUMP needs PN_FM_* to un-fold */
#endif
#ifdef PRIORLIN
#include "priorlin_weights.h"
#endif
#ifdef PRIOR2PASS
#include "priorcut_weights.h"
#endif

// Fill simBoard from the game and collect the eyespace vital points.
// Shared by think() and scoreDead().
void unpackBoard(Game &game);
static void loadRootBoard(Game &game) {
    unpackBoard(game);
#if defined(PRIOR_DUMP) && !defined(ARDUINO)
    { uint32_t h = 2166136261u;
      for(uint8_t i = 0; i < BOARD_CELLS; i++)
          h = (h ^ simBoard[i]) * 16777619u;
      priorRootHash = h ^ ((uint32_t)game.turn << 24); }
#endif
#ifndef RVIT_OFF
    nRootVitals = 0;
    for(uint8_t i = 0; i < BOARD_CELLS && nRootVitals < 3; i++) {
        if(simBoard[i] != EMPTY) continue;
        uint16_t rv = regionVital(i);
        if((uint8_t)rv && (rv >> 8) == i)
            rootVitals[nRootVitals++] = i;
    }
#endif
#ifdef EYE_PRIOR
    memset(weakLibs, 0, sizeof(weakLibs));
    newMark();
    for(uint8_t s0 = 0; s0 < BOARD_CELLS; s0++) {
        if(simBoard[s0] == EMPTY || simMark[s0] == markEpoch) continue;
        uint8_t col = simBoard[s0];
        uint8_t members[81], mc = 0, head = 0;
        uint8_t libs[8], lc = 0;
        members[mc++] = s0; simMark[s0] = markEpoch;
        while(head < mc) {
            uint8_t q;
            FOR_EACH_NEIGHBOR(q, members[head]) {
                uint8_t v = simBoard[q];
                if(v == EMPTY) {
                    uint8_t j = 0;
                    while(j < lc && libs[j] != q) j++;
                    if(j == lc && lc < 8) libs[lc++] = q;
                } else if(v == col && simMark[q] != markEpoch) {
                    simMark[q] = markEpoch; members[mc++] = q;
                }
            }
            head++;
        }
        if(lc <= EYE_WEAK_MAX)
            for(uint8_t j = 0; j < lc; j++)
                weakLibs[libs[j] >> 3] |= bitMask(libs[j]);
    }
#endif
#ifdef OWL_LITE
    // Owl pass (endgame-arc): find the LARGEST own group in the target
    // class (>=3 stones, <2 point-eyes, <=4 libs), solve it; if hopeless,
    // mark its cells -- rootMoveOK vetoes non-capturing moves adjacent to
    // them (the anti-feeding veto, rootSelfAtari pattern).
    owlVetoActive = 0;
    {
        uint8_t mine = game.turn;
        uint8_t seen[11]; memset(seen, 0, sizeof(seen));
        uint8_t bestSeed = 0xFF, bestSize = 0;
        for(uint8_t i = 0; i < BOARD_CELLS; i++) {
            if(simBoard[i] != mine || (seen[i >> 3] & bitMask(i))) continue;
            uint8_t st[28]; uint8_t sp = 0, sz = 0, eyes = 0, libs = 0;
            uint8_t cells[28];
            newMark();
            st[sp++] = i; *markPtr(i) = markEpoch;
            seen[i >> 3] |= bitMask(i);
            uint8_t big = 0;
            while(sp) {
                uint8_t u = st[--sp], q;
                if(sz < 28) cells[sz] = u;
                sz++;
                FOR_EACH_NEIGHBOR(q, u) {
                    if(*markPtr(q) == markEpoch) continue;
                    if(simBoard[q] == mine) {
                        if(sp >= 28) { big = 1; break; }
                        *markPtr(q) = markEpoch;
                        seen[q >> 3] |= bitMask(q);
                        st[sp++] = q;
                    } else if(simBoard[q] == EMPTY) {
                        *markPtr(q) = markEpoch;
                        libs++;
                        if(isOwnEye(q, mine)) eyes++;
                    }
                }
                if(big) break;
            }
            if(big || sz < 3 || sz > 27 || eyes >= 2 || libs > 4) continue;
            if(sz > bestSize) { bestSize = sz; bestSeed = i; }
        }
        if(bestSeed != 0xFF && owlHopeless(bestSeed)) {
            // re-flood the (unchanged) group into the veto bitmap
            memset(owlVeto, 0, sizeof(owlVeto));
            uint8_t st[28]; uint8_t sp = 0;
            newMark();
            st[sp++] = bestSeed; *markPtr(bestSeed) = markEpoch;
            owlVeto[bestSeed >> 3] |= bitMask(bestSeed);
            while(sp) {
                uint8_t u = st[--sp], q;
                FOR_EACH_NEIGHBOR(q, u)
                    if(simBoard[q] == game.turn && *markPtr(q) != markEpoch) {
                        *markPtr(q) = markEpoch;
                        owlVeto[q >> 3] |= bitMask(q);
                        st[sp++] = q;
                    }
            }
            owlVetoActive = 1;
#if defined(OWL_DEBUG) && !defined(ARDUINO)
            fprintf(stderr, "OWL hopeless group sz=%u seed=%u\n", bestSize, bestSeed);
#endif
        }
    }
#endif
}

// The ownership vote itself, shared by scoreDead and the settle gate
// below: SCORE_PLAYOUTS light scoring playouts from the real board,
// counting per cell how often it finishes black-owned (black stone, or
// empty bordered only by black — same rules as scoreWinner).
static void ownVote(Game &game, uint8_t *own) {
    for(uint8_t i = 0; i < BOARD_CELLS; i++) own[i] = 0;

    rootTurn = game.turn;
    simKomi = game.kpieces;
    rootLast = 0xFF;
    rootKo = NO_KO;
    scoreMode = 1; // before loadRootBoard: square-four vitals apply
    loadRootBoard(game);
    rootStones = countStones(game);   // simBoard == game.board here
                                      // (loadRootBoard just synced)

    for(uint8_t p = 0; p < SCORE_PLAYOUTS; p++) {
        unpackBoard(game);
        playout(game.turn, NO_KO, 0xFF);
        // Per-cell black ownership, same rules as scoreWinner
        for(uint8_t i = 0; i < BOARD_CELLS; i++) {
            uint8_t s = simBoard[i];
            if(s == WHITE) continue;
            if(s == BLACK) { own[i]++; continue; }
            uint8_t tb = 0, tw = 0;
            uint8_t q;
            FOR_EACH_NEIGHBOR(q, i) {
                if(simBoard[q] == BLACK) tb = 1;
                if(simBoard[q] == WHITE) tw = 1;
            }
            if(tb && !tw) own[i]++;
        }
    }
    scoreMode = 0;
}

// Ownership-corrected settle margin for the opponent-just-passed
// decision: the honest area margin (doubled, komi applied, positive =
// side to move wins) of the board as the scoreDead vote reads it. This
// is the SAME vote game-over scoring applies, so a pass taken on this
// verdict scores the way the verdict says. SETTLE_NONE before the
// endgame — with open space the vote is coin flips, not a count.
#define SETTLE_NONE (-32768)
// Premature-pass guard (2026-08-13, Jay's device game): the ownership
// vote can read a wide-open NN-phase board as decisively won (small
// playout sample -> lucky unanimity; measured und=2 at 22 stones) and
// answer a premature pass by passing into adjudication scoring. A pass
// answer is only trustworthy once territory is CLOSED: any empty region
// touching both colors and bigger than this means play on. Calibrated
// over test/saved_games first-pass points: every legit settle pass
// measures <= 26 (08-01 anchor: 24); the premature class measures
// 53-73. The und guard cannot separate them (0 vs 2).
#define OPEN_REGION_MAX 32
static uint8_t boardOpen(Game &game) {
    newMark();     // simMark is free pre-search (loadRootBoard runs later)
    for(uint8_t s = 0; s < BOARD_CELLS; s++) {
        if(simMark[s] == markEpoch || game.board[s] != EMPTY)
            continue;
        uint8_t head = 0, cnt = 0, colors = 0;
        floodScratch[cnt++] = s;
        simMark[s] = markEpoch;
        while(head < cnt) {
            uint8_t nb;
            FOR_EACH_NEIGHBOR(nb, floodScratch[head]) {
                uint8_t v = game.board[nb];
                if(v != EMPTY) colors |= v;
                else if(simMark[nb] != markEpoch) {
                    simMark[nb] = markEpoch;
                    floodScratch[cnt++] = nb;
                }
            }
            head++;
        }
        if(colors == (BLACK | WHITE) && cnt > OPEN_REGION_MAX) return 1;
    }
    return 0;
}
static uint8_t countStones(Game &game) {
    // THE total-count (08-30): stonePair died with the areas rework,
    // and this loop now also serves both former inline rootStones
    // counts. Total stones only -- nobody wants the per-color split.
    uint8_t st = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++)
        if(game.board[i] != EMPTY) st++;
    return st;
}
static int16_t settleVote(Game &game) {
    // Pre-endgame (<45 stones) the vote USED to be switched off
    // entirely ("open space = coin flips"). Jay's game 2026-08-01: he
    // passed at 31 stones with the territory effectively decided, and
    // the disabled gate let the engine fill its own territory and
    // throw dead stones into his for eleven moves - the playout eval
    // applauding (+2 -> +13) while converting a won game into a loss
    // at the real count. The honest reliability test is the vote's own
    // DECISIVENESS: when nearly every cell reads owned (<=16 or >=48
    // of 64), the count is trustworthy at any stone count - and it is
    // the SAME vote game-over scoring applies, so passing on it is
    // self-consistent. Coin-flip cells only appear over genuinely
    // open space, which is what the old guard was protecting against.
    // NOT sBuffer[0]: the first 81 bytes of the screen buffer are pool
    // nodes 0..13, and on the passToWin early-return think() never
    // rebuilds the pool -- borrowing them left the stale tree with
    // vote tallies for sibling links, and play_gui froze walking the
    // resulting cycle (reproduced bit-exact from Jay's 2026-08-01 SGF
    // via forceThinkSeed=C772; test/freezeprobe.cpp). The RAVE half of
    // the buffer (past the node pool) IS free here: stale raveV/raveW
    // are read by nothing after an early return and cleared by the
    // next real think. scoreDead may still use sBuffer[0] -- it only
    // runs at game end, after which the tree is never walked again.
    uint8_t *own = SB_BASE + NODE_POOL_SB * sizeof(Node);
    ownVote(game, own);
    uint8_t b = 0, und = 0;
    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        if(own[i] >= SCORE_PLAYOUTS / 2) b++;
        if(own[i] > SCORE_PLAYOUTS / 4 && own[i] < 3 * SCORE_PLAYOUTS / 4)
            und++;
    }
    if(countStones(game) < 45 && und > 8) return SETTLE_NONE;
    int16_t m2 = (int16_t)b * 4 - BOARD_CELLS * 2; // 2*(black - white) area
    return (game.turn == BLACK) ? m2 - (int16_t)simKomi
                                : (int16_t)simKomi - m2;
}

void AI::scoreDead(Game &game) {
    uint8_t *own = SB_BASE; // free once the game is over (CrossMux: engineBuffer)
    ownVote(game, own);

    for(uint8_t i = 0; i < BOARD_CELLS; i++) {
        uint8_t s = game.board[i];
        uint8_t blackOwned = own[i] >= SCORE_PLAYOUTS / 2;
        if(s == BLACK && !blackOwned) {
            packedSet(game.board, i, EMPTY);   // (i/9)*9 + i%9 == i
            game.captures[1]++;
        } else if(s == WHITE && blackOwned) {
            packedSet(game.board, i, EMPTY);   // (i/9)*9 + i%9 == i
            game.captures[0]++;
        }
    }
    game.computeScore();
}

#ifdef LATENT_DEBUG
uint8_t dbgWatch = 0xFF;
#endif
static uint8_t newNode(uint8_t move) {
    uint8_t i;
    if(freeHead != 0xFF) {
        i = freeHead;
        freeHead = node(i).nextSibling;
    } else if(poolUsed < NODE_POOL) {
        i = poolUsed++;
    } else {
        return 0xFF;
    }
    Node &n = node(i);
    n.move = move;
    n.firstChild = 0xFF;
    n.nextSibling = 0xFF;
    n.s[0] = n.s[1] = n.s[2] = 0;
    return i;
}

// Return every descendant of v (not v itself) to the free list. The
// sibling links double as the traversal worklist, so no stack is
// needed: a node's children are spliced into the list ahead of its
// remaining siblings before it is freed.
// REVERTED 2026-08-18 (Jay): chunk-20 briefly shipped as the default on
// a -26.6% think claim that a 20-seed paired calibration exposed as
// single-trace luck (true iteration delta -2.8% +-4.5%, ~= neutral).
// Its L0 +54 was also a single seed set (the >=3-fresh-sets protocol
// was not applied). The classic engine is the default again; chunk-20
// remains available via -DWIDEN_CHUNK=20 pending proper multi-set
// strength evaluation. LAW: single-trace bench deltas between TRACE-
// CHANGED builds carry +-10-20% noise; only value-identical (hash-
// proven) comparisons are exact on one trace.

static void freeSubtree(uint8_t v) {
    uint8_t work = node(v).firstChild;
    node(v).firstChild = 0xFF;
    while(work != 0xFF) {
        uint8_t nxt = node(work).nextSibling;
        uint8_t fc = node(work).firstChild;
        if(fc != 0xFF) {
            uint8_t t = fc;
            while(node(t).nextSibling != 0xFF) t = node(t).nextSibling;
            node(t).nextSibling = nxt;
            nxt = fc;
        }
#if !defined(ARDUINO) && defined(RECLAIM_LOG)
        rlEvict('s', rlBirthD[work], nVisits(work), work, node(work).move & 0x7F);
#endif
        node(work).nextSibling = freeHead;
        freeHead = work;
        work = nxt;
    }
}

// Pool dry: recycle the subtree under the least-visited off-path
// sibling of the descent path. The victim keeps its own stats — root
// moves stay choosable by bestMove, and a revisit re-widens it from
// scratch — only its descendants go back to the free list. Skipping
// path[] members keeps the active descent's indices valid. Returns 1
// if anything was freed.
static uint8_t reclaim() {
#ifdef LATENT_DEBUG
    if(path[0] != 0 || pathDepth > 31) {
        fprintf(stderr, "PATH CORRUPT at reclaim: path[0]=%u pathDepth=%u poolUsed=%u\n",
                path[0], pathDepth, poolUsed);
        abort();
    }
#endif
    // Latents first: a pending pre-scanned candidate (move bit7) is
    // pure cache -- one node, no subtree, no information loss; a
    // future scan simply recreates it. Freeing one costs nothing,
    // unlike evicting a real subtree. Latents are never on the path
    // (path nodes were selected, which latents cannot be).
    for(uint8_t d = 0; d < pathDepth; d++) {
        uint8_t prev = 0xFF;
        for(uint8_t c = node(path[d]).firstChild; c != 0xFF;
            prev = c, c = node(c).nextSibling) {
            if(!(node(c).move & 0x80)) continue;
#ifdef LATENT_DEBUG
            fprintf(stderr, "RECLAIM latent c=%u move=%u parent=%u d=%u pathDepth=%u\n",
                    c, node(c).move & 0x7F, path[d], d, pathDepth);
#endif
            if(prev == 0xFF)
                node(path[d]).firstChild = node(c).nextSibling;
            else
                node(prev).nextSibling = node(c).nextSibling;
            node(c).nextSibling = freeHead;
            freeHead = c;
#if !defined(ARDUINO) && defined(RECLAIM_LOG)
            rlEvict('l', d, nVisits(c), c, node(c).move & 0x7F);
#endif
            return 1;
        }
    }
#ifndef NO_RECLAIM_FLAT
    // SHIP (2026-08-18 flatten arc, arm 5 -- Powley tree-flattening,
    // off-path form; default engine, -DNO_RECLAIM_FLAT = escape hatch
    // back to pure least-visited-victim reclaim): raze EVERY root
    // child's subtree except the one holding the current descent. The
    // ply-1 nodes and their aggregated counters survive -- the candidate
    // reservoir (the program's measured load-bearing asset) is immortal
    // by construction, and deep material regrows regeneratively. One
    // flatten harvests ~100+ nodes, so eviction decisions drop from
    // ~700/think (leaf arms) to a handful. Falls through to the classic
    // victim stage when the tree is already flat.
    {
        uint8_t keep = pathDepth > 1 ? path[1] : 0xFF;
        uint8_t freed = 0;
        for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
            if(c == keep || (node(c).move & 0x80)) continue;
            if(node(c).firstChild == 0xFF) continue;
#if !defined(ARDUINO) && defined(RECLAIM_LOG)
            rlEvict('t', 0, nVisits(c), c, node(c).move & 0x7F);
#endif
            freeSubtree(c);
            freed = 1;
        }
        if(freed) return 1;
    }
#endif
#ifdef NO_SUBTREE_EVICT
    // Depth arc: subtree eviction under pool pressure CHURNS -- the
    // least-visited victim is disproportionately the critical-but-
    // disliked line (measured: kataBest children with 36-81 visits and
    // ZERO surviving children). Freeze instead: latents were already
    // freed above; with none left the tree keeps its shape and visits
    // keep refining what exists.
    return 0;
#endif
    uint8_t victim = 0xFF;
    uint16_t worst = 0xFFFF;
    for(uint8_t d = 0; d < pathDepth; d++) {
        uint8_t next = (d + 1 < pathDepth) ? path[d + 1] : 0xFF;
        for(uint8_t c = node(path[d]).firstChild; c != 0xFF;
            c = node(c).nextSibling) {
            if(c == next || node(c).firstChild == 0xFF) continue;
            uint16_t v = nVisits(c);
            if(v < worst) {
                worst = v;
                victim = c;
            }
        }
    }
    if(victim == 0xFF) return 0;
#if !defined(ARDUINO) && defined(RECLAIM_LOG)
    { uint8_t vd = 0;
      for(uint8_t d = 0; d < pathDepth; d++)
          for(uint8_t c = node(path[d]).firstChild; c != 0xFF; c = node(c).nextSibling)
              if(c == victim) vd = d;
      rlEvict('t', vd, nVisits(victim), victim, node(victim).move & 0x7F); }
#endif
    freeSubtree(victim);
    return 1;
}

// A node can be allocated now, or after recycling a dead subtree.
// Checked BEFORE the prior scans in expand/widen, so a hopeless scan
// is never paid for.
static uint8_t allocReady() {
    if(freeHead != 0xFF || poolUsed < NODE_POOL) return 1;
    return reclaimEnabled && reclaim();
}

// Corner quadrants (4x4 regions) with no stones, as bits TL,TR,BL,BR.
// Filled by buildNearMask, consumed by candidatePrior.
static uint8_t emptyCorners;
// Root near-mask cache: the root board is frozen during a think, so its
// buildNearMask result is computed once and replayed for every later
// root widen (~20% of all widen calls) instead of re-stamped per stone.
static uint8_t rootNear[12];
static uint8_t rootNearAny, rootEmptyCorners, rootNearValid;

// Stamp one stone's distance-2 halo (and its corner claim) into near[].
// Shared by the full board scan and the incremental descent path.
__attribute__((optimize("O2"), noinline))
static void stampNear(uint8_t *near, uint8_t p) {
    uint8_t sxy = posXY(p);                       // one lpm, not two magic-muls
    uint8_t sx = sxy & 0x0F, sy = xyHi(sxy);
    // computed quadrant mask: under the outer guard sx,sy ∈ {0..3,5..8},
    // so (sx>=5) + 2*(sy>=5) picks exactly the branch the old 4-way
    // else-if cascade took (bits TL=1,TR=2,BL=4,BR=8).
    if(sx != 4 && sy != 4)
        emptyCorners &= ~(1 << ((sx >= 5) + 2 * (sy >= 5)));
    uint8_t x0 = sx > 2 ? sx - 2 : 0, x1 = sx < 6 ? sx + 2 : 8;
    uint8_t y0 = sy > 2 ? sy - 2 : 0, y1 = sy < 6 ? sy + 2 : 8;
    uint8_t run = (uint8_t)((1u << (x1 - x0 + 1)) - 1);
    uint8_t b = y0 * BOARD_SIZE + x0;
    uint8_t sh = b & 7;
    uint16_t m = (uint16_t)run << sh;
    for(uint8_t yy = y0; yy <= y1; yy++, b += BOARD_SIZE) {
        near[b >> 3]       |= (uint8_t)m;
        near[(b >> 3) + 1] |= (uint8_t)(m >> 8);
        if((b & 7) == 7) m = run;   // offset wraps to 0 next row
        else m <<= 1;
    }
}

// 81-bit bitmap of points within distance 2 of any stone.
// Returns 0 if the board has no stones (then allow everything).
__attribute__((optimize("O2")))
static uint8_t buildNearMask(uint8_t *near) {
    memset(near, 0, 12);
    uint8_t anyStone = 0;
    emptyCorners = 0x0F;
    for(uint8_t p = 0; p < BOARD_CELLS; p++) {
        if(simBoard[p] == EMPTY) continue;
        anyStone = 1;
        stampNear(near, p);
    }
    return anyStone;
}

// The 8 knight (keima) offsets, in the exact order the old
// a=-2..2,b=-2..2 / a*a+b*b==5 filter produced them, so the keima loop's
// first-match break stays identical (iterate 8, not 25 with 17 discarded).
// entries 0-7: knight offsets; entries 8-11: straight one-point jumps
// Companion tables: KEIMA_L[ki] = a + 9b (partner as a linear offset;
// L/2 is also the jump midpoint for ki>=8, and L-a recovers the b-row
// corner), KEIMA_W1/W2[ki] = the two waist offsets as constants --
// replaces the b*9 multiply and the waist-selection branch per entry.
static const int8_t PROGMEM KEIMA_L[12] = {-11, 7, -19, 17, -17, 19, -7, 11, -18, 18, -2, 2};
static const int8_t PROGMEM KEIMA_W1[8] = {-1, -1, -9, 9, -9, 9, 1, 1};
static const int8_t PROGMEM KEIMA_W2[8] = {-10, 8, -10, 8, -8, 10, -8, 10};

static const int8_t PROGMEM KEIMA_A[12] = {-2, -2, -1, -1,  1,  1,  2,  2,
                                            0,  0, -2,  2};
static const int8_t PROGMEM KEIMA_B[12] = {-1,  1, -2,  2, -2,  2, -1,  1,
                                           -2,  2,  0,  0};
// Per-coordinate keima bounds masks: bit ki of KEIMA_MX[x] is set iff
// x + KEIMA_A[ki] is on-board (KEIMA_MY likewise for y + KEIMA_B[ki]).
// Their AND decides all 12 bounds tests at once; KEIMA_B itself is now
// used only by the table generator above.
static const uint16_t PROGMEM KEIMA_MX[9] = {0xBF0, 0xBFC, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0x73F, 0x70F};
static const uint16_t PROGMEM KEIMA_MY[9] = {0xEAA, 0xEEB, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xFFF, 0xDD7, 0xD55};


// Prior for one candidate: tactics + center + locality + shape, minus
// early-game low-line penalties. Negative = virtual losses. isFar
// marks a big open point, which can never earn tactical or local
// credit and gets its own bonus instead.
__attribute__((noinline, optimize("O2")))
#if defined(PRIOR_DUMP) && !defined(ARDUINO)
// ---- Midgame feature-arc detectors (2026-08-21), DUMP-ONLY ----------------
// Corpora carry pf[29..34]; nothing on the play path reads them. Clarity
// over speed: host-only, recomputed per candidate.

// CFG (common-fate-graph) distance from `from` to `to` on simBoard: stones
// of one chain collapse to a single node, every edge costs 1. Cap 7;
// 7 also = unreachable/no-last. Pachi's fight-locality metric.
static uint8_t dumpCfgDist(uint8_t from, uint8_t to) {
    if(from >= BOARD_CELLS || to >= BOARD_CELLS) return 7;
    // Fixed-point relaxation (host-only; correctness over speed): stones
    // of one chain share a distance (edge cost 0 inside a chain, 1
    // otherwise). Sweep until no distance improves.
    uint8_t dist[BOARD_CELLS]; memset(dist, 0x3F, sizeof(dist));
    dist[from] = 0;
    for(uint8_t changed = 1; changed;) {
        changed = 0;
        for(uint8_t p = 0; p < BOARD_CELLS; p++) {
            uint8_t n;
            FOR_EACH_NEIGHBOR(n, p) {
                uint8_t step = (simBoard[n] != EMPTY &&
                                simBoard[n] == simBoard[p]) ? 0 : 1;
                if(dist[p] + step < dist[n]) { dist[n] = dist[p] + step; changed = 1; }
                if(dist[n] + step < dist[p]) { dist[p] = dist[n] + step; changed = 1; }
            }
        }
    }
    return dist[to] > 7 ? 7 : dist[to];
}

// Chain flood on simBoard from seed: fills cells[] mask and libs[] mask
// (11-byte bitsets), returns lib count.
static uint8_t dumpChainLibs(uint8_t seed, uint8_t *cells, uint8_t *libs) {
    memset(cells, 0, 11); memset(libs, 0, 11);
    uint8_t col = simBoard[seed];
    uint8_t st[BOARD_CELLS]; uint8_t sp = 0, nl = 0;
    st[sp++] = seed; cells[seed >> 3] |= bitMask(seed);
    while(sp) {
        uint8_t p = st[--sp], n;
        FOR_EACH_NEIGHBOR(n, p) {
            if(simBoard[n] == col) {
                if(!(cells[n >> 3] & bitMask(n))) {
                    cells[n >> 3] |= bitMask(n); st[sp++] = n;
                }
            } else if(simBoard[n] == EMPTY) {
                if(!(libs[n >> 3] & bitMask(n))) {
                    libs[n >> 3] |= bitMask(n); nl++;
                }
            }
        }
    }
    return nl;
}

// Semeai trio for a candidate cell: among adjacent friendly/enemy chains,
// find the pair sharing >=1 liberty with the fewest total libs (the
// tightest race touching this move). Outputs: eyeDelta in {-1,0,1}
// (my-eye minus their-eye, me-ari-me-nashi), exclDelta = |A-only| -
// |B-only| clamped +-7, shared = |libs(A) & libs(B)| (0 = no race).
static void dumpSemeai(uint8_t pos, uint8_t toMove,
                       int8_t *eyeDelta, int8_t *exclDelta, int8_t *shared) {
    *eyeDelta = *exclDelta = *shared = 0;
    uint8_t seedsF[4], seedsE[4]; uint8_t nF = 0, nE = 0, n;
    FOR_EACH_NEIGHBOR(n, pos) {
        if(simBoard[n] == toMove) { if(nF < 4) seedsF[nF++] = n; }
        else if(simBoard[n] == 3 - toMove) { if(nE < 4) seedsE[nE++] = n; }
    }
    uint8_t bestTot = 0xFF;
    for(uint8_t a = 0; a < nF; a++) {
        uint8_t cA[11], lA[11]; uint8_t la = dumpChainLibs(seedsF[a], cA, lA);
        for(uint8_t b = 0; b < nE; b++) {
            uint8_t cB[11], lB[11]; uint8_t lb = dumpChainLibs(seedsE[b], cB, lB);
            uint8_t sh = 0;
            for(uint8_t i = 0; i < 11; i++)
                for(uint8_t m = lA[i] & lB[i]; m; m &= m - 1) sh++;
            if(!sh || la + lb >= bestTot) continue;
            bestTot = la + lb;
            int8_t ed = (int8_t)(la - sh) - (int8_t)(lb - sh);
            if(ed > 7) ed = 7; if(ed < -7) ed = -7;
            *exclDelta = ed;
            *shared = (int8_t)(sh > 7 ? 7 : sh);
            // eye flags: any exclusive liberty that is an eye for its owner
            uint8_t eyeA = 0, eyeB = 0;
            for(uint8_t p2 = 0; p2 < BOARD_CELLS; p2++) {
                if((lA[p2 >> 3] & bitMask(p2)) && !(lB[p2 >> 3] & bitMask(p2))
                   && isOwnEye(p2, toMove)) eyeA = 1;
                if((lB[p2 >> 3] & bitMask(p2)) && !(lA[p2 >> 3] & bitMask(p2))
                   && isOwnEye(p2, 3 - toMove)) eyeB = 1;
            }
            *eyeDelta = (int8_t)eyeA - (int8_t)eyeB;
        }
    }
}

// Encirclement octant count around the candidate: 8 compass rays; the
// FIRST stone hit decides (enemy -> 1, own shields -> 0, edge -> 0).
static uint8_t dumpOctants(uint8_t pos, uint8_t toMove) {
    static const int8_t OD[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
    uint8_t x0 = pos % 9, y0 = pos / 9, cnt = 0;
    for(uint8_t d = 0; d < 8; d++) {
        int8_t x = x0, y = y0;
        for(;;) {
            x += OD[d][0]; y += OD[d][1];
            if(x < 0 || x > 8 || y < 0 || y > 8) break;
            uint8_t b = simBoard[(uint8_t)(y * 9 + x)];
            if(b != EMPTY) { if(b == 3 - toMove) cnt++; break; }
        }
    }
    return cnt;
}
#endif // PRIOR_DUMP midgame detectors


__attribute__((noinline))   // its own register file (the H8 kernel keeps
                            // 8 int16 accumulators live); inlining into
                            // widenNode measured +1.9KB of spill bloat.
static int8_t candidatePrior(uint8_t pos, uint8_t toMove, uint8_t last,
                             uint8_t isFar) {
    uint8_t opp = 3 - toMove;
    uint8_t sawCapture = 0, sawSave = 0, sawAtari = 0, sawDoomed = 0;
    uint8_t sawWeakFriend = 0;
    uint8_t hasOrthFriend = 0;

    // Group-aware neighbor scan over the precomputed chain map
    // (buildChainMap runs once per expansion): distinct chains per
    // color and their weakest liberty classes — pure array reads,
    // no floods (the per-candidate chain floods here were the
    // biggest single cost in the whole search).
    uint8_t fGroups = 0, eGroups = 0, emptyN = 0;
    uint8_t fMinLibs = 0xFF, eMinLibs = 0xFF;
    uint8_t doomCand[4];
    uint8_t nDoom = 0;
    uint8_t fIds[4];  // ids of the distinct friendly chains seen
    uint8_t fSeed[4]; // a stone of each (soleConnector floods from it)
    uint8_t seen[4];
    uint8_t nSeen = 0;
    uint8_t q;
    FOR_EACH_NEIGHBOR(q, pos) {
        uint8_t cq = *chainPtr(q);  // one carry-free read serves id AND libs
        uint8_t id = CHAIN_OF(cq);
        if(!id) { emptyN++; continue; }   // empty neighbour = a liberty
        uint8_t dup = 0;
        for(uint8_t k = 0; k < nSeen; k++)
            if(seen[k] == id) { dup = 1; break; }
        if(dup) continue;
        seen[nSeen++] = id;
        uint8_t l = LIBS_OF(cq);
        if(boardAt(q) == opp) {
            eGroups++;
            if(l < eMinLibs) eMinLibs = l;
            if(l == 1) {
                sawCapture = 1;             // pos is its last liberty
            }
            else if(l == 2) sawAtari = 1;
        } else {
            hasOrthFriend = 1;
            if(fGroups < 4) { fIds[fGroups] = id; fSeed[fGroups] = q; }
            fGroups++;
            if(l < fMinLibs) fMinLibs = l;
            if(l == 1) doomCand[nDoom++] = q;
            else if(l == 2) sawWeakFriend = 1;
        }
    }
    // Only credit a save if the ladder actually works — extending a
    // ladder-dead group just feeds stones
    for(uint8_t j = 0; j < nDoom; j++) {
        if(ladderEscapes(doomCand[j], pos)) {
            // one escape decides everything downstream: the bonus takes
            // sawSave first, and sawDoomed's only consumer is gated on
            // !sawSave -- the remaining ladder reads (162 B snapshot +
            // forced chase each) cannot change any output. Break.
            sawSave = 1;
            break;
        }
        sawDoomed = 1;
    }

    // The net IS the prior (PRIORNN_V2 ship, 2107ccf): the hand-rule
    // arithmetic that used to accumulate here was deleted 08-19 — the
    // detectors above/below survive purely as the net's feature vector.
    // The full hand engine lives in git history (pre-2107ccf).
    int8_t bonus = 0;

    // Connection: this point joins a 2-liberty chain to another
    // friendly chain — the rescue move for a group about to be split
    // off and killed. Cutting is the mirror image. A hopeless
    // connection into self-atari still gets sunk by the thin-stretch
    // penalty below.
    // connHere also exempts the point from the settled-territory
    // penalty below: a sole connector often sits inside what reads
    // as own territory, and -6 was cancelling the bonus.
    uint8_t connHere = 0;
    if(fGroups >= 2) {
        if(fMinLibs == 2) {
            connHere = 1;
        } else if(rootStones >= EARLY_STONES &&
                  soleConnector(fSeed[0], fIds[1])) {
            connHere = 1;
        }
    }

    // Eyespaces and settled territory. The vital point of a small
    // one-color region is simple life and death — make the second
    // eye or deny it — and gets a strong bonus for either side. Any
    // OTHER move inside a settled region is an own fill or a
    // hopeless invasion: invisible costs to the area-scoring
    // playouts.
    // vitalHere also exempts the move from the thin-stretch and
    // low-line penalties below: vital points are inherently thin,
    // low-line moves inside eyespaces, and the generic penalties
    // were burying the bonus.
    uint8_t vitalHere = 0;
    {
        // Eyespace lookup: pos is empty, so its chainId top bits hold the
        // region code — 0=open, 1=black, 2=white, 3=vital. Filled lazily
        // by regionVital (first candidate in a region floods it; the rest
        // read the cache), so this is O(1) per candidate.
        uint8_t rb = pos >> 3;  // 8-bit shift (see widenNode scan)
        if(!(regionDone[rb] & bitMask(pos)))
            regionVital(pos); // cache side-effect only
        uint8_t rc = *chainPtr(pos) >> 6;
        if(rc == 3) {
            vitalHere = 1;
        }
#if defined(EYE_PRIOR) || defined(EYE_PRIOR2)
        // Weak-group liberty structure (root-board bitmap): the cell is
        // a weak chain's liberty, or adjacent to two of them (the
        // placement/vital class open regionVital cannot see mid-game).
#ifdef EYE_PRIOR2
#define EYE_PRIOR EYE_PRIOR2
#endif
        if(weakLibs[rb] & bitMask(pos)) bonus += EYE_PRIOR;
        else {
            uint8_t wq, wn = 0;
            FOR_EACH_NEIGHBOR(wq, pos)
                if(weakLibs[wq >> 3] & bitMask(wq)) wn++;
            if(wn >= 2) bonus += EYE_PRIOR - 4;
        }
#endif
    }

    // pos coords (x,y) and |offset| to last (adx,ady): decoded once here
    // and reused by the urgent-defense / contact-push / locality blocks.
    uint8_t xy = posXY(pos);
    uint8_t x = xy & 0x0F, y = xyHi(xy);
    uint8_t adx = 0, ady = 0;
    if(last < BOARD_CELLS) {
        uint8_t lxy = posXY(last);
        uint8_t lx = lxy & 0x0F, ly = xyHi(lxy);
        adx = x > lx ? x - lx : lx - x;
        ady = y > ly ? y - ly : ly - y;
    }

    // Thin-stretch penalty: tentatively place the stone and count the
    // merged group's liberties. Tactical moves are exempt (a capture
    // would raise the count anyway; a crosscut fight is legitimately
    // sharp). Patterned blocks at 2 libs still net above quiet moves;
    // outright self-atari sinks far below anything playable.
    // Immediate-liberty fast-path (Pachi). mlibs' ONLY consumer is the
    // dump-only pf[26] slot (no kernel reads past pf[23] in any build),
    // so the producer -- including the occasional groupLibsFind flood --
    // is dump-gated: the device build was paying it on every widen for
    // a value nothing read.
#ifdef PRIOR_DUMP
    uint8_t mlibs;
    if(fGroups == 0)     mlibs = emptyN;
    else if(emptyN >= 3) mlibs = 3;
    else {
        simBoard[pos] = toMove;
        mlibs = (uint8_t)groupLibsFind(pos);
        simBoard[pos] = EMPTY;
    }
#endif


    // Cuttable-keima check. The knight's-move partner lies OUTSIDE the
    // candidate's 3x3, so neither patterns nor the merged-libs class
    // can see this shape. Applies only when the keima is the sole link
    // (no orthogonal contact — checked above — and no diagonal one).
    // (Hand-prior era: the penalty measured -26/1000 to remove; that
    // arithmetic died with the hand engine — the detector survives
    // dump-only so training corpora carry the shape as pf[28].)
#ifdef PRIOR_DUMP   // keimaCut feeds pf[28] in training dumps only
    // keimaCut: rc3 feature candidate — 1 = cuttable one-point jump,
    // 2 = cuttable keima (waist-supported). Dump-only: the net owns the
    // prior; this detector exists so training corpora can carry pf[28].
    uint8_t keimaCut = 0; (void)keimaCut;
    if(!hasOrthFriend && !isFar && !sawCapture && !sawSave && !sawAtari) {
        // (guard order: hasOrthFriend disqualifies most candidates)
        // (isFar: no stone within Chebyshev 2, and every cell this
        // block reads -- diagonals, keima partners, jump midpoints --
        // lies within that radius, so all 16 probes are guaranteed
        // misses; skipping is byte-identical)
        // All board reads here are cells of a fixed shape around the
        // candidate, so they're pos + a linear offset -- no Y*9 multiply.
        // Bounds are still checked on the (x,y) coords before each read.
        uint8_t hasDiagFriend = 0;
        for(int8_t dy = -1; dy <= 1; dy += 2)
            for(int8_t dx = -1; dx <= 1; dx += 2) {
                int8_t fx = x + dx, fy = y + dy;
                if(fx < 0 || fx >= BOARD_SIZE || fy < 0 || fy >= BOARD_SIZE)
                    continue;
                if(simBoard[pos + dx + dy * BOARD_SIZE] == toMove)
                    hasDiagFriend = 1;
            }
        if(!hasDiagFriend) {
            uint8_t penalized = 0;
            uint16_t kmask = pgm_read_word(KEIMA_MX + x) &
                             pgm_read_word(KEIMA_MY + y);
            // terminate at the mask's last set bit: edge candidates
            // have sparse masks and the tail iterations were all
            // continue-spins (interior = all 12 bits, identical work)
            for(uint8_t ki = 0; kmask && !penalized; ki++, kmask >>= 1) {
                if(!(kmask & 1)) continue;  // partner off-board
                {
                    int8_t L = (int8_t)pgm_read_byte(KEIMA_L + ki);
                    if(simBoard[pos + L] != toMove) continue;
                    // a is only needed on a partner HIT: loading it
                    // after the test drops one lpm from every miss
                    int8_t a = (int8_t)pgm_read_byte(KEIMA_A + ki);
                    if(ki >= 8) {
                        // one-point jump: single midpoint, must be empty,
                        // enemy on a SIDE of it (the push-in cut). Sides
                        // are perpendicular to the jump line.
                        int8_t m = L / 2;
                        if(simBoard[pos + m] != EMPTY) continue;
                        uint8_t hit = 0;
                        if(a) { // horizontal jump: sides above/below midpoint
                            int8_t my = y;
                            if(my > 0 && simBoard[pos + m - BOARD_SIZE] == opp) hit = 1;
                            if(my < BOARD_SIZE - 1 &&
                               simBoard[pos + m + BOARD_SIZE] == opp) hit = 1;
                        } else { // vertical jump: sides left/right of midpoint
                            if(x > 0 && simBoard[pos + m - 1] == opp) hit = 1;
                            if(x < BOARD_SIZE - 1 &&
                               simBoard[pos + m + 1] == opp) hit = 1;
                        }
                        if(hit) {
                            keimaCut = 1;
                            penalized = 1;
                        }
                        continue;
                    }
                    // Back corners of the keima box, (kx,y) and (x,ky), must
                    // not be mine — else the pair links down the outside line
                    // and it isn't a lone keima.
                    if(simBoard[pos + a] == toMove ||
                       simBoard[pos + (int8_t)(L - a)] == toMove) continue;
                    // The two waist points between candidate and partner,
                    // constants per entry from the companion tables.
                    int8_t w1 = (int8_t)pgm_read_byte(KEIMA_W1 + ki);
                    int8_t w2 = (int8_t)pgm_read_byte(KEIMA_W2 + ki);
                    // Enemy exactly ON a waist = a supported cut. (The old
                    // oppNear fired on any enemy in the waist's 3x3 — ~5x more
                    // firings, mostly false; strength-equal at 1000 games L0.)
                    if(simBoard[pos + w1] == opp ||
                       simBoard[pos + w2] == opp) {
                        keimaCut = 2;
                        penalized = 1;
                    }
                }
            }
        }
    }

#endif // PRIOR_DUMP (keima)


    // Low-line discipline (see the defines): opening low-line moves
    // are penalized unconditionally; later, second-line moves near an
    // enemy stone are legitimate boundary plays. Penalized moves also
    // lose their shape/locality bonuses: a correct-LOOKING contact
    // answer down there is still usually wrong, and pattern+local
    // (+5) was overpowering the line penalty.

#if NN_PRIOR_TOP
    // Net-priored MCTS: tiered bonus for the whole-game net's top-3 root moves.
    if(pos == nnTop[0]) bonus += NN_PRIOR_TOP;
    else if(pos == nnTop[1]) bonus += NN_PRIOR_TOP - 2;
    else if(pos == nnTop[2]) bonus += NN_PRIOR_TOP - 4;
#endif

#if defined(PRIOR_DUMP) || defined(PRIORNN)
    // Learned-prior arc: the 24-feature vector, assembled ONCE and shared
    // by the training dump and the in-tree net -- parity by construction.
    // ORDER IS THE TRAINING CONTRACT (f1..f24); never reorder.
    int8_t pf[35];   // [29..34] = midgame-arc dump features
    {
        uint8_t rc2 = *chainPtr(pos) >> 6;
        uint8_t wOwn = 0, wEnm = 0, wAdj = 0, vFlag = 0;
        // PN_NF==19 (r1b packed) parks these as dead slots — skip the
        // per-candidate bitmap reads except in dump builds, where the
        // parked raw values still flow into corpora.
#if defined(EYE_BITMAPS) && (PN_NF != 19 || defined(PRIOR_DUMP))
        { uint8_t own = toMove - 1, enm = 2 - toMove;
          wOwn = (weakLibsC[own][pos >> 3] & bitMask(pos)) ? 1 : 0;
          wEnm = (weakLibsC[enm][pos >> 3] & bitMask(pos)) ? 1 : 0;
          vFlag = (vitalLibs[pos >> 3] & bitMask(pos)) ? 1 : 0;
          uint8_t wq;
          FOR_EACH_NEIGHBOR(wq, pos)
              if((weakLibsC[0][wq >> 3] | weakLibsC[1][wq >> 3]) & bitMask(wq)) wAdj++;
        }
#endif
        uint8_t line = x < 8 - x ? x : 8 - x;
        { uint8_t ly2 = y < 8 - y ? y : 8 - y; if(ly2 < line) line = ly2; }
        uint8_t dl = adx > ady ? adx : ady;
        // rc2 mover-relativization (2026-08-14): raw region code
        // 1=black/2=white is color-ALIASED for the net (toMove is not a
        // feature). The recode (0=open,1=own,2=enemy,3=vital) ships ONLY
        // with a net trained on it — the exporter emits PN_RC_RELATIVE
        // for such nets; feeding a raw-trained net recoded input is
        // train/inference skew. Training-data remapping happens offline
        // (records carry toMove), so the dump may emit either encoding.
#ifdef PN_RC_RELATIVE
        uint8_t rcv = rc2;
        if(rc2 == 1 || rc2 == 2) rcv = (rc2 == toMove) ? 1 : 2;
#else
        uint8_t rcv = rc2;
#endif
#if   PN_NF == 19
        // F19 PACKED LAYOUT v2 (r1b, 2026-08-21): live set = 24-vector
        // minus the five measured-constant features (isFar, wEnm, wOwn,
        // wAdj, vFlag — all-zero in 106,716 sampled widen contexts, so
        // dropping them is value-identical). Kernel iterates 0..18.
        // The dead five park at slots 19-23, raw, dump-visible so
        // future corpora (e.g. opening-window dumps where isFar DOES
        // vary) can revive them. Export contract: PACK19=1 emits
        // weights/FMID in this order (KEEP map in export_prior_weights).
        pf[0]  = (int8_t)(patternBonus(x, y, toMove) - PN_FM_0);
        pf[1]  = (int8_t)(line - PN_FM_1);
        pf[2]  = (int8_t)(dl - PN_FM_2);
        pf[3]  = (int8_t)(pathDepth - PN_FM_3);
        pf[4]  = (int8_t)(emptyN - PN_FM_4);
        pf[5]  = (int8_t)(fGroups - PN_FM_5);
        pf[6]  = (int8_t)(eGroups - PN_FM_6);
        pf[7]  = (int8_t)((fMinLibs == 0xFF ? 7 : fMinLibs) - PN_FM_7);
        pf[8]  = (int8_t)((eMinLibs == 0xFF ? 7 : eMinLibs) - PN_FM_8);
        pf[9]  = (int8_t)(sawCapture - PN_FM_9);
        pf[10] = (int8_t)(sawSave - PN_FM_10);
        pf[11] = (int8_t)(sawAtari - PN_FM_11);
        pf[12] = (int8_t)(sawDoomed - PN_FM_12);
        pf[13] = (int8_t)(connHere - PN_FM_13);
        pf[14] = (int8_t)(rcv - PN_FM_14);
        pf[15] = (int8_t)(sawWeakFriend - PN_FM_15);
        pf[16] = (int8_t)(hasOrthFriend - PN_FM_16);
        pf[17] = (int8_t)(rootStones - PN_FM_17);
        pf[18] = (int8_t)(vitalHere - PN_FM_18);
        pf[19] = (int8_t)isFar;
        pf[20] = (int8_t)wEnm;
        pf[21] = (int8_t)wOwn;
        pf[22] = (int8_t)wAdj;
        pf[23] = (int8_t)vFlag;
#else
        pf[0]  = (int8_t)(patternBonus(x, y, toMove) - PN_FM_0);
        pf[1]  = (int8_t)(line - PN_FM_1);
        // 7 features carry a nonzero FMID: fold the subtraction into the
        // write (compile-time immediates) so the kernel's d IS pf[i] and
        // the per-feature lpm+sub disappears from the 24-wide loop.
        pf[2]  = (int8_t)(dl - PN_FM_2);
        pf[3]  = (int8_t)(isFar - PN_FM_3);
        pf[4]  = (int8_t)(pathDepth - PN_FM_4);
        pf[5]  = (int8_t)(emptyN - PN_FM_5);
        pf[6]  = (int8_t)(fGroups - PN_FM_6);
        pf[7]  = (int8_t)(eGroups - PN_FM_7);
        pf[8]  = (int8_t)((fMinLibs == 0xFF ? 7 : fMinLibs) - PN_FM_8);
        pf[9]  = (int8_t)((eMinLibs == 0xFF ? 7 : eMinLibs) - PN_FM_9);
        pf[10] = (int8_t)(sawCapture - PN_FM_10);
        pf[11] = (int8_t)(sawSave - PN_FM_11);
        pf[12] = (int8_t)(sawAtari - PN_FM_12);
        pf[13] = (int8_t)(sawDoomed - PN_FM_13);
        pf[14] = (int8_t)(connHere - PN_FM_14);
        pf[15] = (int8_t)(wEnm - PN_FM_15);
        pf[16] = (int8_t)(wOwn - PN_FM_16);
        pf[17] = (int8_t)(wAdj - PN_FM_17);
        pf[18] = (int8_t)(vFlag - PN_FM_18);
        pf[19] = (int8_t)(rootStones - PN_FM_19);
        pf[20] = (int8_t)(rcv - PN_FM_20);
        pf[21] = (int8_t)(sawWeakFriend - PN_FM_21);
        pf[22] = (int8_t)(hasOrthFriend - PN_FM_22);
        pf[23] = (int8_t)(vitalHere - PN_FM_23);
#endif
#ifdef PRIOR_DUMP
        // Dump-only slots: no kernel in any build reads past pf[23]
        // (max PN_NF is 24). The oc corner-band test ran on every
        // device widen for a value nothing consumed.
        pf[24] = (int8_t)adx;
        pf[25] = (int8_t)ady;
        pf[26] = (int8_t)mlibs;
        { uint8_t ex2 = x < 8 - x ? x : 8 - x;
          uint8_t ey2 = y < 8 - y ? y : 8 - y;
          uint8_t oc = 0;
          if(ex2 >= 2 && ex2 <= 3 && ey2 >= 2 && ey2 <= 3) {
              uint8_t cq = (y <= 3 ? 0 : 2) + (x <= 3 ? 0 : 1);
              if(emptyCorners & (1 << cq)) oc = 1;
          }
          pf[27] = (int8_t)oc; }
        pf[28] = (int8_t)keimaCut;   // rc3 candidate (dump-only for now)
        // ---- midgame feature arc (2026-08-21), pf[29..34] ----
        // last2: the move before `last` at this node = two plies up the
        // descent, falling to the real-game last move at pathDepth==2.
        // Root expansion (pathDepth<2) has no last2 -> d2 sentinel 8
        // (mirrors dl's no-last convention); those rows are outside the
        // dump gate anyway.
        {
            pf[29] = (int8_t)dumpCfgDist(last, pos);          // CFG hops, cap 7
            int8_t sEye, sExcl, sShared;
            dumpSemeai(pos, toMove, &sEye, &sExcl, &sShared);
            pf[30] = sEye;                                    // me-ari-me-nashi
            pf[31] = sExcl;                                   // excl-lib delta
            pf[32] = sShared;                                 // shared libs (0=no race)
            uint8_t last2 = 0xFF;
            if(pathDepth >= 3)      last2 = node(path[pathDepth-2]).move & 0x7F;
            else if(pathDepth == 2) last2 = rootLast;
            uint8_t d2 = 8;
            if(last2 < BOARD_CELLS) {
                uint8_t l2xy = posXY(last2);
                uint8_t adx2 = (uint8_t)((x > (l2xy & 0x0F)) ? x - (l2xy & 0x0F)
                                                             : (l2xy & 0x0F) - x);
                uint8_t ady2 = (uint8_t)((y > xyHi(l2xy)) ? y - xyHi(l2xy)
                                                          : xyHi(l2xy) - y);
                d2 = adx2 > ady2 ? adx2 : ady2;               // chebyshev, = dl metric
            }
            pf[33] = (int8_t)d2;
            pf[34] = (int8_t)dumpOctants(pos, toMove);        // encirclement 0-8
        }
#endif
    }
#endif
#ifdef PRIORNN
    // v1 ADDITIVE: int8 MLP (PN_W1/PN_B1/PN_V), int32 accumulation,
    // arithmetic >>8 lands p5..p95 on ~[-31,+11] (bonus scale); clamp.
#ifndef PN_STUB
    {
        // int16 kernel: real accumulator range measured +-189 over 788k
        // candidate rows (17x under int16 even before headroom) -- full
        // int8 weights, native 8x8->16 muls, no saturation guard needed.
#if PN_H == 4
        // 4-wide sibling of the PN_H==8 unroll below (H4 ships the same
        // strength at −110 B tables; gauntleted 08-15: L0 1593 / human
        // 462 vs the H8 champion's 1598/461). Four accumulator pairs =
        // half the register pressure; same lpm Z+ asm idiom.
#ifdef __AVR__
        int16_t a0, a1, a2, a3;
        {
            const uint8_t *bp = (const uint8_t *)PN_B1;
            asm(
                "lpm %A[a0], Z+ \n\t" "lpm %B[a0], Z+ \n\t"
                "lpm %A[a1], Z+ \n\t" "lpm %B[a1], Z+ \n\t"
                "lpm %A[a2], Z+ \n\t" "lpm %B[a2], Z+ \n\t"
                "lpm %A[a3], Z+ \n\t" "lpm %B[a3], Z+ \n\t"
                : [a0]"=r"(a0), [a1]"=r"(a1), [a2]"=r"(a2), [a3]"=r"(a3),
                  "+z"(bp));
        }
#else
        int16_t a0 = (int16_t)pgm_read_word(&PN_B1[0]),
                a1 = (int16_t)pgm_read_word(&PN_B1[1]),
                a2 = (int16_t)pgm_read_word(&PN_B1[2]),
                a3 = (int16_t)pgm_read_word(&PN_B1[3]);
#endif
        const uint8_t *wp = (const uint8_t *)PN_W1;
        for(uint8_t i = 0; i < PN_NF; i++) {
            int8_t d = (int8_t)pf[i];   // FMID folded at the write sites
            if(!d) { wp += 4; continue; }
#ifdef __AVR__
            // lpm Z+ x4 advances wp itself: no per-iteration Z copy and
            // no loop-header wp += 4 on the taken path -- gcc keeps wp
            // pinned in Z across the whole feature loop
            uint8_t wt;
            asm(
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a0], r0     \n\t" "adc %B[a0], r1    \n\t"
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a1], r0     \n\t" "adc %B[a1], r1    \n\t"
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a2], r0     \n\t" "adc %B[a2], r1    \n\t"
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a3], r0     \n\t" "adc %B[a3], r1    \n\t"
                "clr __zero_reg__   \n\t"
                : [a0]"+r"(a0), [a1]"+r"(a1), [a2]"+r"(a2), [a3]"+r"(a3),
                  [wt]"=&d"(wt), "+z"(wp)
                : [d]"d"(d)
                : "r0", "r1");
#else
            const uint8_t *w = wp; wp += 4;
            a0 += (int16_t)(d * (int8_t)pgm_read_byte(w)); w++;
            a1 += (int16_t)(d * (int8_t)pgm_read_byte(w)); w++;
            a2 += (int16_t)(d * (int8_t)pgm_read_byte(w)); w++;
            a3 += (int16_t)(d * (int8_t)pgm_read_byte(w));
#endif
        }
        int32_t out = 0;
        if(a0 > 0) out += (int32_t)a0 * (int8_t)pgm_read_byte(&PN_V[0]);
        if(a1 > 0) out += (int32_t)a1 * (int8_t)pgm_read_byte(&PN_V[1]);
        if(a2 > 0) out += (int32_t)a2 * (int8_t)pgm_read_byte(&PN_V[2]);
        if(a3 > 0) out += (int32_t)a3 * (int8_t)pgm_read_byte(&PN_V[3]);
#elif PN_H == 8

        // h-UNROLLED: all 8 accumulators live in registers across the
        // feature loop (candidatePrior is noinline = its own register
        // file; 8x int16 = 16 GPRs, fits). The rolled loop bounced every
        // pre[h] through the stack -- 8 ld/st pairs per nonzero feature,
        // ~144 cyc vs ~66 unrolled. The !d skip stays: features are 70%
        // zero vs FMID (cache-measured nz mean 7.3/24). w++ reads keep
        // the weight walk on lpm Z+ (no displacement mode on AVR).
#ifdef __AVR__
        // B1 is one contiguous 16-byte PROGMEM row: walk it with lpm Z+
        // once instead of 8 pgm_read_word address rebuilds. No upper-reg
        // temps involved, so this stays clear of the reload wall the
        // V-pass rewrite hit.
        int16_t a0, a1, a2, a3, a4, a5, a6, a7;
        {
            const uint8_t *bp = (const uint8_t *)PN_B1;
            asm(
                "lpm %A[a0], Z+ \n\t" "lpm %B[a0], Z+ \n\t"
                "lpm %A[a1], Z+ \n\t" "lpm %B[a1], Z+ \n\t"
                "lpm %A[a2], Z+ \n\t" "lpm %B[a2], Z+ \n\t"
                "lpm %A[a3], Z+ \n\t" "lpm %B[a3], Z+ \n\t"
                "lpm %A[a4], Z+ \n\t" "lpm %B[a4], Z+ \n\t"
                "lpm %A[a5], Z+ \n\t" "lpm %B[a5], Z+ \n\t"
                "lpm %A[a6], Z+ \n\t" "lpm %B[a6], Z+ \n\t"
                "lpm %A[a7], Z+ \n\t" "lpm %B[a7], Z+ \n\t"
                : [a0]"=r"(a0), [a1]"=r"(a1), [a2]"=r"(a2), [a3]"=r"(a3),
                  [a4]"=r"(a4), [a5]"=r"(a5), [a6]"=r"(a6), [a7]"=r"(a7),
                  "+z"(bp));
        }
#else
        int16_t a0 = (int16_t)pgm_read_word(&PN_B1[0]),
                a1 = (int16_t)pgm_read_word(&PN_B1[1]),
                a2 = (int16_t)pgm_read_word(&PN_B1[2]),
                a3 = (int16_t)pgm_read_word(&PN_B1[3]),
                a4 = (int16_t)pgm_read_word(&PN_B1[4]),
                a5 = (int16_t)pgm_read_word(&PN_B1[5]),
                a6 = (int16_t)pgm_read_word(&PN_B1[6]),
                a7 = (int16_t)pgm_read_word(&PN_B1[7]);
#endif
        const uint8_t *wp = (const uint8_t *)PN_W1;
        for(uint8_t i = 0; i < PN_NF; i++, wp += 8) {
            int8_t d = (int8_t)pf[i];   // FMID folded at the write sites
            if(!d) continue;
#ifdef __AVR__
            // gcc can't keep Z alive here: with 16 accumulator regs held
            // it loads the weight byte into r30 itself and rebuilds Z per
            // element (movw+adiw, ~11 cyc). Hand asm with a dedicated
            // upper-reg temp keeps the walk on lpm Z+ (7 cyc/element) and
            // clears __zero_reg__ once instead of per-muls. Same adds in
            // the same order — value-identical (pool-hash checked).
            const uint8_t *w = wp;
            uint8_t wt;
            asm(
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a0], r0     \n\t" "adc %B[a0], r1    \n\t"
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a1], r0     \n\t" "adc %B[a1], r1    \n\t"
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a2], r0     \n\t" "adc %B[a2], r1    \n\t"
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a3], r0     \n\t" "adc %B[a3], r1    \n\t"
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a4], r0     \n\t" "adc %B[a4], r1    \n\t"
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a5], r0     \n\t" "adc %B[a5], r1    \n\t"
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a6], r0     \n\t" "adc %B[a6], r1    \n\t"
                "lpm %[wt], Z+      \n\t" "muls %[wt], %[d]  \n\t"
                "add %A[a7], r0     \n\t" "adc %B[a7], r1    \n\t"
                "clr __zero_reg__   \n\t"
                : [a0]"+r"(a0), [a1]"+r"(a1), [a2]"+r"(a2), [a3]"+r"(a3),
                  [a4]"+r"(a4), [a5]"+r"(a5), [a6]"+r"(a6), [a7]"+r"(a7),
                  [wt]"=&d"(wt), "+z"(w)
                : [d]"d"(d)
                : "r0", "r1");
#else
            const uint8_t *w = wp;
            a0 += (int16_t)(d * (int8_t)pgm_read_byte(w)); w++;
            a1 += (int16_t)(d * (int8_t)pgm_read_byte(w)); w++;
            a2 += (int16_t)(d * (int8_t)pgm_read_byte(w)); w++;
            a3 += (int16_t)(d * (int8_t)pgm_read_byte(w)); w++;
            a4 += (int16_t)(d * (int8_t)pgm_read_byte(w)); w++;
            a5 += (int16_t)(d * (int8_t)pgm_read_byte(w)); w++;
            a6 += (int16_t)(d * (int8_t)pgm_read_byte(w)); w++;
            a7 += (int16_t)(d * (int8_t)pgm_read_byte(w));
#endif
        }
        int32_t out = 0;
        if(a0 > 0) out += (int32_t)a0 * (int8_t)pgm_read_byte(&PN_V[0]);
        if(a1 > 0) out += (int32_t)a1 * (int8_t)pgm_read_byte(&PN_V[1]);
        if(a2 > 0) out += (int32_t)a2 * (int8_t)pgm_read_byte(&PN_V[2]);
        if(a3 > 0) out += (int32_t)a3 * (int8_t)pgm_read_byte(&PN_V[3]);
        if(a4 > 0) out += (int32_t)a4 * (int8_t)pgm_read_byte(&PN_V[4]);
        if(a5 > 0) out += (int32_t)a5 * (int8_t)pgm_read_byte(&PN_V[5]);
        if(a6 > 0) out += (int32_t)a6 * (int8_t)pgm_read_byte(&PN_V[6]);
        if(a7 > 0) out += (int32_t)a7 * (int8_t)pgm_read_byte(&PN_V[7]);
#else
        int16_t pre[PN_H];
        for(uint8_t h = 0; h < PN_H; h++)
            pre[h] = (int16_t)pgm_read_word(&PN_B1[h]);
        const uint8_t *wp = (const uint8_t *)PN_W1;
        for(uint8_t i = 0; i < PN_NF; i++, wp += PN_H) {
            int8_t d = (int8_t)pf[i];   // FMID folded at the write sites
            if(!d) continue;
            for(uint8_t h = 0; h < PN_H; h++)
                pre[h] += (int16_t)(d * (int8_t)pgm_read_byte(wp + h));
        }
        int32_t out = 0;
        for(uint8_t h = 0; h < PN_H; h++)
            if(pre[h] > 0) out += (int32_t)pre[h] * (int8_t)pgm_read_byte(&PN_V[h]);
#endif
        // PN_OUT_SHIFT: the net-score -> bonus exchange rate. 8 was
        // inherited from the hand prior's output range (p5-p95 lands on
        // ~[-31,+11]); 7 doubles the prior's voltage. Swept 08-14 with
        // the seed-base factorial (scale x ballast = one ratio).
#ifndef PN_OUT_SHIFT
#define PN_OUT_SHIFT 8
#endif
        int16_t nb = (int16_t)(out >> PN_OUT_SHIFT);
        if(nb > 48) nb = 48;
        if(nb < -48) nb = -48;
        // v2: the net IS the prior (hand arithmetic deleted 08-19).
        bonus = (int8_t)nb;
    }
#endif  /* PN_STUB */
#endif  /* PRIORNN */
#ifdef PN_STUB
    bonus = (int8_t)(pf[0] >> 2);   // stub: pattern-only stand-in, no MAC
#endif
#ifdef PRIORLIN
    // Cut-only prior: the linear distillation of the net, no MLP at all.
    // Answers "is ordering-precision or mere candidacy the value?"
    {
        int16_t ls = 0;
        for(uint8_t i = 0; i < PL_NF; i++) {
            int8_t d = (int8_t)(pf[i] - (int8_t)pgm_read_byte(&PL_FMID[i]));
            if(d) ls += (int16_t)(d * (int8_t)pgm_read_byte(&PL_W[i]));
        }
        int16_t nb2 = ls >> 6;
        if(nb2 > 48) nb2 = 48;
        if(nb2 < -48) nb2 = -48;
        bonus = (int8_t)nb2;
    }
#endif
#if defined(PRIOR_DUMP) && !defined(ARDUINO)
    if(priorDumpOn) {
        // un-fold the write-site FMID subtraction: the dump contract is
        // RAW features (what the trainer/extractor expect)
#if PN_NF == 23
        // MIDF lean layout: 19 core + mlibs + semeai trio; the dump's
        // raw columns beyond the fold are zero-offset.
        static const int8_t pnFm[35] = {PN_FM_0,PN_FM_1,PN_FM_2,PN_FM_3,
            PN_FM_4,PN_FM_5,PN_FM_6,PN_FM_7,PN_FM_8,PN_FM_9,PN_FM_10,PN_FM_11,
            PN_FM_12,PN_FM_13,PN_FM_14,PN_FM_15,PN_FM_16,PN_FM_17,PN_FM_18,
            PN_FM_19,PN_FM_20,PN_FM_21,PN_FM_22,
            0,0,0,0,0,0,0,0,0,0,0,0};
#elif PN_NF == 19
        static const int8_t pnFm[35] = {PN_FM_0,PN_FM_1,PN_FM_2,PN_FM_3,
            PN_FM_4,PN_FM_5,PN_FM_6,PN_FM_7,PN_FM_8,PN_FM_9,PN_FM_10,PN_FM_11,
            PN_FM_12,PN_FM_13,PN_FM_14,PN_FM_15,PN_FM_16,PN_FM_17,PN_FM_18,
            0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0};
#else
        static const int8_t pnFm[35] = {PN_FM_0,PN_FM_1,PN_FM_2,PN_FM_3,
            PN_FM_4,PN_FM_5,PN_FM_6,PN_FM_7,PN_FM_8,PN_FM_9,PN_FM_10,PN_FM_11,
            PN_FM_12,PN_FM_13,PN_FM_14,PN_FM_15,PN_FM_16,PN_FM_17,PN_FM_18,
            PN_FM_19,PN_FM_20,PN_FM_21,PN_FM_22,PN_FM_23,0,0,0,0,0, 0,0,0,0,0,0};
#endif
        fprintf(stderr, "WC %u", pos);
        for(uint8_t i = 0; i < 35; i++) fprintf(stderr, " %d", pf[i] + pnFm[i]);
        fprintf(stderr, " %d\n", (int)bonus);
    }
#endif
    return bonus;
}

// Push-front: child order carries no meaning beyond tie-breaking, and
// random scan starts keep the ties fair. A negative bonus becomes
// virtual losses: extra visits with no wins.

static uint8_t addChild(uint8_t nodeIdx, uint8_t move, int8_t bonus) {
    uint8_t c = newNode(move);
    if(c == 0xFF) return 0xFF;
#if !defined(ARDUINO) && defined(RECLAIM_LOG)
    rlBirth[c] = rlIter; rlBirthD[c] = pathDepth;
    if(rlF) fprintf(rlF, "A %u %u %u\n", rlIter, pathDepth, move & 0x7F);
#endif
    Node &nc = node(c);                // resolve once (was 2x: setstats + sibling)
    if(bonus >= 0)
        nSetStatsN(nc, PRIOR_BASE_V + bonus, PRIOR_BASE_W + bonus);
    else
        nSetStatsN(nc, PRIOR_BASE_V - bonus, PRIOR_BASE_W);
    Node &np = node(nodeIdx);          // resolve once (was 2x: read + write)
    nc.nextSibling = np.firstChild;
    np.firstChild = c;
    return c;
}

// Best pending latent of a node by RECOVERED prior: addChild seeded
// (V+p, W+p) for p>=0 and (V-p, W) for p<0, so p = (w-W == v-V) ?
// v-V : -(v-V). Order-independent, so partial batches and free-list
// reuse cannot skew activation order.
static uint8_t latentBest(uint8_t nodeIdx) {
    uint8_t best = 0xFF;
    int8_t bestP = -128;
    for(uint8_t c = node(nodeIdx).firstChild; c != 0xFF;
        c = node(c).nextSibling) {
        if(!(node(c).move & 0x80)) continue;
        int8_t dv = (int8_t)(nVisits(c) - PRIOR_BASE_V);
        int8_t p = ((int8_t)(nWins(c) - PRIOR_BASE_W) == dv) ? dv : -dv;
        if(p > bestP) { bestP = p; best = c; }
    }
    return best;
}

static uint8_t childCount(uint8_t nodeIdx) {
    // ACTIVE children only: latents (move bit7) hold a scanned-ahead
    // candidate but do not occupy a widening slot until activated
    uint8_t n = 0;
    for(uint8_t c = node(nodeIdx).firstChild; c != 0xFF; c = node(c).nextSibling)
        if(!(node(c).move & 0x80)) n++;
    return n;
}

#ifdef PRIOR2PASS
// Two-pass widening, pass 1: LITE score for the cut -- chain-map reads
// only (no ladder, no connector flood), pattern, coords. int8 weights
// distilled from the judge net (PC_W; zero slots = expensive features
// excluded from the cut by design). Offline: K=12 costs +0.117pp vs
// judging every cell.
__attribute__((noinline))
static int16_t cutScore(uint8_t pos, uint8_t toMove, uint8_t last,
                        uint8_t isFar) {
    uint8_t opp = 3 - toMove;
    uint8_t fGroups = 0, eGroups = 0, emptyN = 0;
    uint8_t fMin = 7, eMin = 7;
    uint8_t cap = 0, sav = 0, ata = 0, weakF = 0, orthF = 0;
    uint8_t q;
    FOR_EACH_NEIGHBOR(q, pos) {
        uint8_t b = simBoard[q];
        if(b == EMPTY) { emptyN++; continue; }
        uint8_t cls = LIBS_OF(*chainPtr(q));
        if(b == toMove) {
            orthF = 1; fGroups++;
            if(cls < fMin) fMin = cls;
            if(cls == 1) sav = 1;
            if(cls == 2) weakF = 1;
        } else {
            eGroups++;
            if(cls < eMin) eMin = cls;
            if(cls == 1) cap = 1;
            if(cls == 2) ata = 1;
        }
    }
    uint8_t xy = posXY(pos);
    uint8_t x = xy & 0x0F, y = xyHi(xy);
    uint8_t line = x < 8 - x ? x : 8 - x;
    { uint8_t ly = y < 8 - y ? y : 8 - y; if(ly < line) line = ly; }
    uint8_t dl = 8;
    if(last < BOARD_CELLS) {
        uint8_t lxy = posXY(last);
        uint8_t lx = lxy & 0x0F, ly2 = xyHi(lxy);
        uint8_t ax = x > lx ? x - lx : lx - x;
        uint8_t ay = y > ly2 ? y - ly2 : ly2 - y;
        dl = ax > ay ? ax : ay;
    }
    int8_t f[24] = {0};
    f[0] = isFar ? 0 : patternBonus(x, y, toMove);   // far: 3x3 empty, exact 0
    f[1] = (int8_t)line;  f[2] = (int8_t)dl;   f[3] = (int8_t)isFar;
    f[5] = (int8_t)emptyN; f[6] = (int8_t)fGroups; f[7] = (int8_t)eGroups;
    f[8] = (int8_t)fMin;  f[9] = (int8_t)eMin;
    f[10] = (int8_t)cap;  f[11] = (int8_t)sav;  f[12] = (int8_t)ata;
    f[21] = (int8_t)weakF; f[22] = (int8_t)orthF;
    int16_t sc = 0;
    for(uint8_t i = 0; i < 24; i++) {
        int8_t wv = (int8_t)pgm_read_byte(&PC_W[i]);
        if(wv && f[i]) sc += (int16_t)(f[i] * wv);
    }
    return sc;
}
#endif

// Root expansion, PROGRESSIVE (see ROOT_INIT): pass plus the top
// prior candidates; the rest arrive via widening as visits grow, so
// early visits concentrate instead of spreading over everything.
static uint8_t widenNode(uint8_t nodeIdx, uint8_t toMove, uint8_t ko, uint8_t last);

static void expandNode(uint8_t nodeIdx, uint8_t toMove, uint8_t ko, uint8_t last) {
    if(poolUsed < NODE_POOL) {
        uint8_t c = addChild(nodeIdx, MOVE_PASS, 0);
        nSetStats(c, PRIOR_BASE_V, 0); // passing is a last resort
    }
    for(uint8_t k = 0; k < ROOT_INIT; k++) {
        // burst-fill: drain stored latents before scanning again, so
        // the active set is the true prior top-N exactly as the
        // one-at-a-time schedule would build it
        uint8_t lat = latentBest(nodeIdx);
        if(lat != 0xFF) {
            node(lat).move &= 0x7F;
            continue;
        }
        if(!widenNode(nodeIdx, toMove, ko, last)) break;
    }
}

// Progressive widening: add the single best not-yet-present candidate.
// Poisoned children stay in the have-bitmap, so an illegal move is
// never re-added. Returns 1 if a child was added.
// (O2 attribute removed 2026-08-13: traded its ~426 B back to fit the
// r2s0 opening-net header; the speed cost is re-benched in the commit.)
static uint8_t widenNode(uint8_t nodeIdx, uint8_t toMove, uint8_t ko, uint8_t last) {
    uint8_t have[11];
    memset(have, 0, sizeof(have));
    for(uint8_t c = node(nodeIdx).firstChild; c != 0xFF;) {
        Node &n = node(c);
        uint8_t m = n.move & 0x7F;
        if(m < BOARD_CELLS) {
            uint8_t mb = m >> 3;  // 8-bit shift: a promoted index shifts
                                  // in a 16-bit asr/ror loop at -Os
            have[mb] |= bitMask(m);
        }
        c = n.nextSibling;
    }

    uint8_t near[12];  // 12 not 11: buildNearMask's run write touches byte 11
    // The root board is frozen for the whole think, so its near mask
    // (and corner set) is computed once and replayed for every later
    // root widen -- a 12-byte copy instead of the full per-stone stamp.
    // Deeper nodes see a different simBoard and keep the live scan.
    // rootNearValid is cleared at think() entry.
    uint8_t anyStone;
    uint8_t atRoot = (nodeIdx == 0);
    if(atRoot && !rootNearValid) {
        rootNearAny = buildNearMask(rootNear);
        rootEmptyCorners = emptyCorners;
        rootNearValid = 1;
    }
    if(atRoot || (rootNearValid && !descentCaptured)) {
        // Replay the cached root mask (the 12-byte copy the compiler
        // was emitting twice, once per branch). Non-root: this node's
        // position = root + the descent moves applied so far
        // (path[1..pathDepth-1] == exactly the stones added to
        // simBoard) -- stamp only those; any capture on the path
        // falls back to the full rebuild below.
        memcpy(near, rootNear, sizeof(rootNear));
        emptyCorners = rootEmptyCorners;
        anyStone = rootNearAny;
        if(!atRoot)
            for(uint8_t d = 1; d < pathDepth; d++) {
                uint8_t mv = node(path[d]).move;
                if(mv >= BOARD_CELLS) continue;   // pass adds no stone
                stampNear(near, mv);
                anyStone = 1;
            }
    } else
        anyStone = buildNearMask(near);
    buildChainMap();

    // Batch widening: one scan yields the top WIDEN_BATCH candidates
    // (the scan already ranks everything to find #1; tracking three is
    // ~free). Callers gate on childCount < maxKids, so a node that
    // received a batch simply skips its next widen triggers until the
    // visit schedule catches up -- same candidate sets, ~3x fewer
    // 81-cell scans (widenNode was 17.7% of think).
    int8_t bP[WIDEN_BATCH];
    uint8_t bPos[WIDEN_BATCH];
    for(uint8_t k = 0; k < WIDEN_BATCH; k++) { bP[k] = -128; bPos[k] = 0xFF; }
    uint8_t startPos = rndMod(BOARD_CELLS);
    // Tapered scan budget (Jay, 2026-08): cover at least 60 cells,
    // then stop at a uniform point in [60, 81] -- one draw, rising
    // stop odds toward the end of the circle. The random start makes
    // the skipped tail a uniform sample; stragglers wait one trigger.
    // MEASURED: -2.55% think, but paired 2000 vs L0 = -26 (17.9 vs
    // 19.1), BOTH samples negative with adverse discordants -- the
    // skipped prior evaluations lean into real strength cost, and at
    // fixed iterations the speed is latency-only. Off by default.
    // Same two-phase circular scan as playout's global probe (see there).
    // The bitmap byte/mask pair (pb, pm) walks incrementally with pos --
    // shift the mask, step the byte on wrap -- instead of a >>3 and a
    // bitMask lpm per cell. Same values at every cell.
    uint8_t pos = startPos, scanEnd = BOARD_CELLS;
    uint8_t pb = pos >> 3;
#if defined(PRIOR_DUMP) && !defined(ARDUINO)
    // Sampling gate: midgame boards, tree plies 2-6, 1-in-4 widens.
    // PRIOR_DUMP_OPEN flips the stone window to 1-13 (opening boards the
    // standard gate never covers) for whole-game prior training.
    priorDumpOn = 0;
    uint8_t dumpLo = getenv("PRIOR_DUMP_OPEN") ? 1 : 14;
    uint8_t dumpHi = getenv("PRIOR_DUMP_OPEN") ? 13 : 45;
    if(getenv("PRIOR_DUMP_ON") && rootStones >= dumpLo && rootStones <= dumpHi &&
       pathDepth >= 2 && pathDepth <= 6 && ((priorDumpSeq++) & 3) == 0) {
        priorDumpOn = 1;
        fprintf(stderr, "WP %lu %08lx %u %u %u", (unsigned long)priorDumpSeq,
                (unsigned long)priorRootHash,
                rootStones, pathDepth, toMove);
        for(uint8_t d = 1; d < pathDepth; d++)
            fprintf(stderr, " %u", node(path[d]).move & 0x7F);
        fprintf(stderr, "\n");
    }
#endif
#ifdef PRIOR2PASS
    int16_t ckSc[PRIOR2PASS];
    uint8_t ckPos[PRIOR2PASS], ckFar[PRIOR2PASS];
    for(uint8_t k = 0; k < PRIOR2PASS; k++) { ckSc[k] = -32768; ckPos[k] = 0xFF; }
#endif
#ifdef EYE_INJECT
    uint8_t bwPos = 0xFF; int8_t bwPrio = -128;
#endif
    uint8_t pm = bitMask(pos);
    for(;; pos++,
           pm = (uint8_t)(pm << 1), pm || (pm = 1, ++pb)) {
        if(pos >= scanEnd) {
            if(scanEnd != BOARD_CELLS || startPos == 0) break;
            pos = 0; scanEnd = startPos;    // phase 2: 0..startPos-1
            pb = 0; pm = 1;
        }
        if(boardAt(pos) != EMPTY || pos == ko) continue;
        if(have[pb] & pm) continue;
#ifdef DEEP_LOCAL
        // Depth arc arm (c): deep in the tree a fight continuation stays
        // LOCAL -- candidates restricted to Chebyshev<=2 of the last move
        // once pathDepth >= DEEP_LOCAL (the pass child stays as the
        // tenuki escape; candidacy is local, evaluation stays global).
        if(pathDepth >= DEEP_LOCAL && last < BOARD_CELLS) {
            int8_t dx = (int8_t)(pos % 9) - (int8_t)(last % 9);
            int8_t dy = (int8_t)(pos / 9) - (int8_t)(last / 9);
            if(dx < -2 || dx > 2 || dy < -2 || dy > 2) continue;
        }
#endif
        uint8_t isFar = 0;
        if(anyStone && !(near[pb] & pm)) {
            // "Big open point" = >=2 from every edge AND not near a
            // stone. The edge-distance test is a static property of the
            // cell (interior 5x5), so read it from FAR_BITMAP instead of
            // recomputing posXY + four min ops per far candidate -- pb/pm
            // are already in hand. Byte-identical to the min-distance form.
            if(!(pgm_read_byte(FAR_BITMAP + pb) & pm)) continue;
            isFar = 1;
        }
        if(isOwnEye(pos, toMove)) continue;
#ifdef PRIOR2PASS
        // pass 1: cheap cut only; the judge runs on the top-K after
        {
            int16_t cs = cutScore(pos, toMove, last, isFar);
            if(cs > ckSc[PRIOR2PASS - 1]) {
                uint8_t k = PRIOR2PASS - 1;
                while(k > 0 && cs > ckSc[k - 1]) {
                    ckSc[k] = ckSc[k - 1]; ckPos[k] = ckPos[k - 1];
                    ckFar[k] = ckFar[k - 1]; k--;
                }
                ckSc[k] = cs; ckPos[k] = pos; ckFar[k] = isFar;
            }
        }
#else
        int8_t p = candidatePrior(pos, toMove, last, isFar);
        if(p > bP[WIDEN_BATCH - 1]) {
            uint8_t k = WIDEN_BATCH - 1;
            while(k > 0 && p > bP[k - 1]) {
                bP[k] = bP[k - 1];
                bPos[k] = bPos[k - 1];
                k--;
            }
            bP[k] = p;
            bPos[k] = pos;
        }
#endif
#ifdef EYE_INJECT
        // Depth arc: track the best WEAK-LIBERTY candidate separately --
        // the L&D placement class the ranking buries (see weakLibs).
        if((weakLibs[pb] & pm) && p > bwPrio) { bwPrio = p; bwPos = pos; }
#endif
    }
#ifdef EYE_INJECT
    // Guaranteed candidacy: one batch slot for the weak-lib class. The
    // injected candidate keeps its true prior (latent activation picks
    // by recovered prior, so it queues fairly behind the naturals).
    if(bwPos != 0xFF && bwPos != bPos[0] && bwPos != bPos[1] &&
       bwPos != bPos[WIDEN_BATCH - 1]) {
        bP[WIDEN_BATCH - 1] = bwPrio;
        bPos[WIDEN_BATCH - 1] = bwPos;
    }
#endif
#ifdef PRIOR2PASS
    // pass 2: full featurization + judge net on the cut survivors
    for(uint8_t k = 0; k < PRIOR2PASS && ckPos[k] != 0xFF; k++) {
        int8_t p = candidatePrior(ckPos[k], toMove, last, ckFar[k]);
        if(p > bP[WIDEN_BATCH - 1]) {
            uint8_t j = WIDEN_BATCH - 1;
            while(j > 0 && p > bP[j - 1]) {
                bP[j] = bP[j - 1];
                bPos[j] = bPos[j - 1];
                j--;
            }
            bP[j] = p;
            bPos[j] = ckPos[k];
        }
    }
#endif
#if defined(PRIOR_DUMP) && !defined(ARDUINO)
    if(priorDumpOn) { fprintf(stderr, "WEND\n"); priorDumpOn = 0; }
#endif
    if(bPos[0] == 0xFF) {
#ifdef WIDEN_PROBE
        wpCalls++; wpEmpty++;
#endif
        return 0;
    }
#ifdef WIDEN_PROBE
    wpCalls++;
#endif
    // LATENT batch: #1 activates now; #2/#3 join the child list with
    // bit7 of move set (latent: invisible to selection until the widen
    // schedule reaches their slot and flips the flag -- see the
    // trigger in mctsIterate). The ACTIVE child allocates FIRST: under
    // pool pressure a partial batch must yield the active child, never
    // a latent-only node (selectChild's fallback once returned such a
    // latent and simPlay indexed simBoard[move|0x80] out of bounds --
    // the memory-corruption bug this ordering fixes). Activation picks
    // the best latent by RECOVERED PRIOR, so list order is free.
    uint8_t any = 0;
    if(addChild(nodeIdx, bPos[0], bP[0]) != 0xFF) {
        any = 1;
#ifdef WIDEN_PROBE
        wpAdded++;
#endif
        for(uint8_t k = 1; k < WIDEN_BATCH; k++) {
            // SHIP (2026-08-18 latstats arc): the root banks ONE latent
            // runner-up instead of seven. Root latents activate at only
            // 7% (vs 29% at ply 2) and reclaim eats the rest before use,
            // so the full batch is churn; one slot keeps most of the
            // activation value (8 of 13.4/think at 21% yield) while the
            // freed pool lets deeper latents survive -- widen scans/think
            // 744 -> 681 (-8.5%), device think -3..-6%, L0 paired +31/2000
            // (n.s.). Gauntleted as LAT_SHAPE={7,1,7,7,7,7,7,7}; pathDepth
            // counts nodes on the path, so ==1 is exactly "at the root".
            if(pathDepth == 1 && k > 1) break;
            if(bPos[k] == 0xFF) continue;
            if(addChild(nodeIdx, bPos[k] | 0x80, bP[k]) == 0xFF) break;
#ifdef WIDEN_PROBE
            wpAdded++;
#endif
        }
    }
    return any;
}



// Q12 natural-log fractional part: log2(1 + i/16) already scaled by
// ln2 (2839 in Q12). Folding ln2 into the table lets lnQ12 avoid a
// 32-bit multiply (see below).
PROGMEM const uint16_t LN_FRAC[16] = {
    0, 248, 482, 704, 914, 1113, 1304, 1486,
    1660, 1827, 1988, 2143, 2292, 2435, 2574, 2708
};

// Q12 natural log for x >= 1: integer log2 (bit position + 4-bit
// mantissa table), scaled by ln2 (2839 in Q12)
__attribute__((optimize("O2")))
static uint16_t lnQ12(uint16_t x) {
    if(x < 2) return 0;
    uint8_t k = 0;
    uint16_t t = x;
    while(t >>= 1) k++;
    uint8_t frac = (k >= 4) ? (x >> (k - 4)) & 0x0F
                            : (x << (4 - k)) & 0x0F;
    // ln x = k*ln2 + frac; bit-identical to the old (log2 << 12) * ln2
    // >> 12 (the k<<12 term times ln2 >> 12 is exactly k*2839, and the
    // fraction floors independently), with the 32-bit multiply gone.
    return (uint16_t)k * 2839 + pgm_read_word(LN_FRAC + frac);
}

// (w<<6)/v for a win/visit pair, i.e. the Q6 win rate. Because w<=v the
// quotient is <=64 (7 bits), so a 7-step restoring divide replaces libgcc's
// full 16-bit __udivmodhi4 (~2x cheaper) and is bit-exact. Valid while
// v<<6 fits u16 (v<=1023); real visit counts stay far under that at the
// shipped 400-iteration budget, and POISONED children have w==0 -> 0.
__attribute__((optimize("O2")))
static inline uint16_t winRate6(uint16_t w, uint16_t v) {
    // 7-step restoring divide floor(w*64/v), quotient in [0,64]. Hand-
    // unrolled: no bit counter / loop branch (out-of-line, one copy).
#if !defined(ARDUINO) || (MCTS_ITERATIONS * 3 / 2) > 1023
    // High-budget guard (iterations-arc, 2026-08-11): v<<6 wraps uint16 at
    // v>=1024 — a dominant child past 1023 visits reads a garbage (tiny)
    // win rate, inverting the pick. Reachable once budget*1.5 > 1023
    // (extended thinks); pre-scale preserves the ratio, Q6 error <=1/256.
    if(v >= 1024) { w >>= 2; v >>= 2; }
#endif
    uint16_t num = w << 6, d = v << 6, q = 0;
    for(uint8_t bit = 64; bit; bit >>= 1) {   // re-rolled 2026-08-21 (flash arc)
        if(num >= d) { num -= d; q |= bit; }
        d >>= 1;
    }
    return q;
}

// Gelly-Silver RAVE ratio (RAVE_K<<12)/(3nv+RAVE_K). Factor exactly to
// 409600/(nv+100) -- 1228800 = 3*409600, so the floors match -- whose
// quotient is <=4055 (12-bit). A 12-step restoring divide then replaces
// libgcc's full 32-bit __udivmodsi4. (O2-unroll dropped: measured -36 B,
// think-neutral -- raveRatio is root-only so the unroll's speed is noise.)
static inline uint16_t raveRatio(uint16_t nv) {
#if RAVE_K == 300
    uint32_t num = 409600UL, dd = (uint32_t)(nv + 100) << 11;
#elif RAVE_K == 150
    // Same exact x3 factoring as the 300 path: (150<<12)/((3nv+150)<<11)
    // = 204800/((nv+50)<<11) -- both operands scale by exactly 3, so
    // every compare in the restoring loop (hence the quotient) is
    // bit-identical to the general form.
    uint32_t num = 204800UL, dd = (uint32_t)(nv + 50) << 11;
#else
    // sweep form: general K (the factorings above are per-K)
    uint32_t num = (uint32_t)RAVE_K << 12, dd = (uint32_t)(3UL * nv + RAVE_K) << 11;
#endif
    uint16_t ratio = 0;
    for(uint16_t bit = 0x800; bit; bit >>= 1) {
        if(num >= dd) { num -= dd; ratio |= bit; }
        dd >>= 1;
    }
    return ratio;
}

// Root RAVE beta by table for the hot visit range: beta(nv) =
// isqrt32(raveRatio(nv) << 12) is a pure function of nv, and the
// blend recomputed a 12-step restoring divide plus an isqrt for
// every root child on every selection. Exact values (host-verified
// against the integer pipeline); nv >= 64 falls back to computing.
// Reciprocals 2^16/nv for the lnN/nv divide (below): for nv < 64,
// q0 = (lnN * RECIP_TAB[nv]) >> 16 is within one of lnN/nv, and a
// single correction step makes it exact - the same floor as the
// divide, ~70 cycles cheaper, on the same hot per-child path the
// beta table just left. (RECIP_TAB[1] saturates to 65535; the
// correction absorbs it.)
PROGMEM const uint16_t RECIP_TAB[64] = {
        0, 65535, 32768, 21845, 16384, 13107, 10922,  9362,
     8192,  7281,  6553,  5957,  5461,  5041,  4681,  4369,
     4096,  3855,  3640,  3449,  3276,  3120,  2978,  2849,
     2730,  2621,  2520,  2427,  2340,  2259,  2184,  2114,
     2048,  1985,  1927,  1872,  1820,  1771,  1724,  1680,
     1638,  1598,  1560,  1524,  1489,  1456,  1424,  1394,
     1365,  1337,  1310,  1285,  1260,  1236,  1213,  1191,
     1170,  1149,  1129,  1110,  1092,  1074,  1057,  1040
};

// Root RAVE beta for the hot visit range: beta(nv) = isqrt32(raveRatio(nv)<<12)
// is a pure function of nv, and the blend recomputed a raveRatio (shift loop)
// plus an isqrt for every rave-eligible root child on every root selection.
// At 400 iters those children cluster at 16-31 visits, so a 32-entry table
// (nv<32; nv>=32 falls back to computing) catches ~all of them. Dropping this
// table cost +7.9% mid think (measured on the emulator) for 164 B flash -- a
// bad trade; kept. 64 B PROGMEM. host-verified against the integer pipeline.
#if RAVE_K == 100
// K=100 table: same construction as the other sweep Ks.
PROGMEM const uint16_t BETA_TAB[64] = {
     4080,  4032,  3968,  3920,  3872,  3808,  3760,  3712,
     3680,  3632,  3584,  3552,  3504,  3472,  3440,  3392,
     3360,  3328,  3296,  3264,  3232,  3200,  3184,  3152,
     3120,  3088,  3056,  3040,  3024,  2992,  2960,  2944,
     2912,  2896,  2880,  2848,  2832,  2816,  2800,  2768,
     2752,  2736,  2720,  2704,  2688,  2656,  2656,  2640,
     2608,  2592,  2592,  2576,  2560,  2528,  2528,  2512,
     2496,  2480,  2464,  2448,  2448,  2432,  2416,  2400,
};
#elif RAVE_K == 200
// K=200/250 tables: same construction as 150/600 — all 64 entries =
// isqrtLUT(raveRatio(nv)<<12), exactly what the nv>=64 fallback
// computes at this K, so table path == compute path.
PROGMEM const uint16_t BETA_TAB[64] = {
     4080,  4064,  4032,  4000,  3968,  3952,  3920,  3888,
     3872,  3840,  3808,  3792,  3760,  3744,  3712,  3696,
     3680,  3648,  3632,  3616,  3584,  3568,  3552,  3536,
     3504,  3488,  3472,  3456,  3440,  3408,  3392,  3376,
     3360,  3344,  3328,  3312,  3296,  3280,  3264,  3248,
     3232,  3216,  3200,  3184,  3184,  3152,  3152,  3136,
     3120,  3104,  3088,  3088,  3056,  3056,  3040,  3024,
     3024,  3008,  2992,  2976,  2960,  2960,  2944,  2928,
};
#elif RAVE_K == 250
PROGMEM const uint16_t BETA_TAB[64] = {
     4080,  4064,  4048,  4016,  4000,  3968,  3952,  3936,
     3904,  3888,  3872,  3856,  3824,  3808,  3792,  3760,
     3744,  3728,  3712,  3696,  3680,  3664,  3632,  3616,
     3600,  3584,  3568,  3552,  3536,  3520,  3504,  3488,
     3472,  3456,  3440,  3440,  3408,  3408,  3392,  3376,
     3360,  3344,  3344,  3312,  3312,  3296,  3280,  3264,
     3264,  3248,  3232,  3216,  3200,  3200,  3184,  3184,
     3168,  3152,  3136,  3120,  3120,  3104,  3088,  3088,
};
#elif RAVE_K == 150
// K=150 table: all 64 entries = isqrtLUT(raveRatio(nv)<<12), the EXACT
// quantity the nv>=64 fallback computes (generated by the same integer
// pipeline compiled at -DRAVE_K=150, so table path == compute path).
PROGMEM const uint16_t BETA_TAB[64] = {
     4080,  4048,  4016,  3968,  3936,  3904,  3872,  3824,
     3792,  3760,  3744,  3696,  3680,  3648,  3616,  3584,
     3552,  3536,  3504,  3488,  3456,  3440,  3408,  3392,
     3360,  3344,  3312,  3296,  3280,  3264,  3232,  3216,
     3200,  3184,  3152,  3136,  3120,  3104,  3088,  3056,
     3056,  3024,  3024,  2992,  2992,  2960,  2960,  2928,
     2912,  2912,  2896,  2880,  2864,  2848,  2832,  2816,
     2800,  2800,  2784,  2768,  2752,  2752,  2736,  2720,
};
#elif RAVE_K == 600
// K=600 table: all 64 entries = isqrtLUT(raveRatio(nv)<<12), the EXACT
// quantity the nv>=64 fallback computes (generated by the same integer
// pipeline compiled at -DRAVE_K=600, so table path == compute path).
PROGMEM const uint16_t BETA_TAB[64] = {
     4080,  4080,  4064,  4064,  4048,  4032,  4032,  4016,
     4016,  4000,  3984,  3984,  3968,  3968,  3952,  3952,
     3936,  3920,  3920,  3904,  3904,  3888,  3888,  3872,
     3872,  3856,  3856,  3840,  3824,  3824,  3808,  3808,
     3792,  3792,  3776,  3776,  3760,  3760,  3760,  3744,
     3744,  3728,  3712,  3712,  3696,  3696,  3696,  3680,
     3680,  3664,  3664,  3648,  3648,  3632,  3632,  3616,
     3616,  3616,  3600,  3600,  3584,  3584,  3568,  3568,
};
#else
PROGMEM const uint16_t BETA_TAB[64] = {
     4096,  4075,  4055,  4035,  4016,  3996,  3978,  3959,
     3941,  3922,  3905,  3887,  3870,  3852,  3835,  3819,
     3803,  3786,  3770,  3754,  3738,  3723,  3708,  3693,
     3678,  3663,  3648,  3634,  3620,  3606,  3591,  3578,
    // 32..127 (2026-08-15): values = isqrtLUT(raveRatio(nv)<<12), the
    // EXACT quantity the fallback computes (the 0..31 entries predate
    // isqrtLUT and carry the old exact-isqrt values -- kept as-is).
    // At the 1000-iteration budget many root children sit at 32..127
    // visits, each paying the 12-step raveRatio divide + isqrtLUT per
    // selection; this converts all of that to one PROGMEM word read.
     3552,  3552,  3536,  3520,  3504,  3488,  3488,  3472,
     3456,  3440,  3440,  3424,  3408,  3392,  3392,  3376,
     3360,  3344,  3344,  3328,  3312,  3312,  3296,  3296,
     3280,  3264,  3264,  3248,  3232,  3232,  3216,  3200,
};
#endif

PROGMEM const uint8_t SQRT_LUT[192] = {128,129,130,131,132,133,134,135,136,137,138,139,139,140,141,142,143,144,145,146,147,148,148,149,150,151,152,153,153,154,155,156,157,158,158,159,160,161,162,162,163,164,165,166,166,167,168,169,169,170,171,172,172,173,174,175,175,176,177,177,178,179,180,180,181,182,182,183,184,185,185,186,187,187,188,189,189,190,191,191,192,193,193,194,195,195,196,197,197,198,199,199,200,200,201,202,202,203,204,204,205,206,206,207,207,208,209,209,210,210,211,212,212,213,213,214,215,215,216,216,217,218,218,219,219,220,221,221,222,222,223,223,224,225,225,226,226,227,227,228,229,229,230,230,231,231,232,232,233,234,234,235,235,236,236,237,237,238,238,239,239,240,241,241,242,242,243,243,244,244,245,245,246,246,247,247,248,248,249,249,250,250,251,251,252,252,253,253,254,254,255,255};
__attribute__((optimize("O2")))
#ifndef ISQRT_POLY
#define ISQRT_POLY 1  // SHIP 2026-08-28: -150B at +0.22%/sim (682B/1%,
                      // best sell-side rate on record); accuracy BEATS
                      // the table (4/6 segments tighter, unbiased);
                      // minis -4 and +25 /2000 wash. -DISQRT_POLY=0
                      // restores the 192B SQRT_LUT.
#endif
#if ISQRT_POLY
// Table-free sqrt (Jay's idea, 08-27): six linear segments on the
// normalized mantissa (index m>>13, minimax-fitted A + (B*m>>16)),
// 12 bytes of constants replacing the 192B SQRT_LUT at parity
// accuracy (max mantissa err 1.23 vs the table's ~0.99).
// Half-unit intercepts (Jay's catch: integer-A rounding gave the fit a
// directional bias, -0.36 low / +0.34 high; Q1 intercepts + a rounding
// shift kill it: whole-domain bias -0.02/-0.04, max err 1.00 = exact
// table parity). r = (A2 + (B*m >> 15) + 1) >> 1, +1 baked into A2.
// Joint (A2,B) micro-search per segment (Jay's second catch: the
// mid-segments still sat off-center — B's own Q16 rounding tilts the
// residual, so slope and intercept must be searched together): max
// errs 0.93/0.83/0.73/0.69/0.65/1.00 (beats the table in 4 of 6),
// per-segment |mean| <= 0.18.
static const uint8_t ISQ_A2[6] PROGMEM = {142, 171, 190, 215, 230, 253};
static const uint8_t ISQ_B[6]  PROGMEM = {231, 192, 173, 153, 143, 130};
static uint16_t isqrtLUT(uint32_t x) {
    if(x < 2) return x;
    int8_t sh = 0;
    while(x >= (1UL << 16)) { x >>= 2; sh += 2; }
    while(x <  (1UL << 14)) { x <<= 2; sh -= 2; }   // x in [2^14, 2^16)
    uint16_t m = (uint16_t)x;
    uint8_t k = (uint8_t)((m >> 13) - 2);
    uint16_t r = (uint16_t)(pgm_read_byte(ISQ_A2 + k)
               + (uint16_t)(((uint32_t)pgm_read_byte(ISQ_B + k) * m) >> 15)) >> 1;
    if(r > 255) r = 255;   // mantissa root <= 255 (r<<8 must fit uint16)
    return sh >= 0 ? (uint16_t)(r << (sh >> 1)) : (uint16_t)(r >> ((-sh) >> 1));
}
#else
static uint16_t isqrtLUT(uint32_t x) {
    if(x < 2) return x;
    int8_t sh = 0;
    while(x >= (1UL << 16)) { x >>= 2; sh += 2; }
    while(x <  (1UL << 14)) { x <<= 2; sh -= 2; }   // x in [2^14, 2^16)
    uint16_t r = pgm_read_byte(SQRT_LUT + (((uint16_t)x >> 8) - 64));
    return sh >= 0 ? (uint16_t)(r << (sh >> 1)) : (uint16_t)(r >> ((-sh) >> 1));
}
#endif
#if defined(UCB_SHADOW) && !defined(ARDUINO)
// Differential audit (2026-08-14): recompute every selection in double
// precision against the canonical UCB1-Tuned formula and count argmax
// disagreements with the Q12 integer path. Disagreements are expected
// only at near-ties (quantization); a biased or frequent mismatch means
// a fixed-point bug.
uint32_t shadowSel, shadowMismatch;   // read by the harness after think()
double shadowGapSum;
#include <math.h>
#endif
static uint8_t selectChild(uint8_t nodeIdx) {
    // UCB1-Tuned in Q12 fixed point — software floats cost several ms
    // per root scan, so everything here is integer.
    // +1: the parent may have no real visits yet on its first selection.
    uint16_t lnN = lnQ12(nVisits(nodeIdx) + 1);

    uint8_t atRoot = (nodeIdx == 0);
    uint16_t best = 0;
    // fallback: first NON-latent child (a latent fallback once leaked
    // move|0x80 into simPlay = out-of-bounds board write). Folded into
    // the main loop — the first evaluated child claims bestC — selling
    // a wash (+0.03%) for -10 B under the perf-per-byte rule.
    uint8_t bestC = 0xFF;
    uint8_t c = node(nodeIdx).firstChild;
    for(; c != 0xFF;) {
        Node &n = node(c);
        uint8_t cur = c;
        c = n.nextSibling;
        if(n.move & 0x80) continue; // latent: not yet in the schedule
        uint16_t nv = nRefVisits(n);
#ifdef LATENT_DEBUG
        if(nv == 0) {
            fprintf(stderr,
                "ZERO-VISIT CHILD: c=%u move=%u parent=%u poolUsed=%u "
                "freeHead=%u firstChild=%u nextSib=%u\n",
                c, n.move, nodeIdx, poolUsed, freeHead,
                n.firstChild, n.nextSibling);
            abort();
        }
#endif
        // Q6 win rate via a 16-bit divide (wins < 1024 so wins<<6 fits
        // uint16): (wins<<6)/nv == (wins<<12)/nv >> 6 exactly, so q is
        // the old Q12 value with its low 6 bits zeroed. That truncation
        // is well under the sampling noise (>=2.5% even at the root).
        uint16_t q6 = winRate6(nRefWins(n), nv);
        uint16_t q = q6 << 6;                 // Q12 (Q6 precision)

        // Variance-aware exploration from the raw win rate: with binary
        // rewards the sample variance is just q(1-q). Q6*Q6 = Q12, so
        // q6*(64-q6) is the SAME Q12 variance as (q*(4096-q))>>12 but a
        // 16-bit multiply instead of a 32-bit one.
        uint16_t lnOverN;
        if(nv < 64) {
            lnOverN = (uint16_t)(((uint32_t)lnN *
                                  pgm_read_word(RECIP_TAB + nv)) >> 16);
            if((uint16_t)((lnOverN + 1) * nv) <= lnN) lnOverN++;
        } else
            lnOverN = lnN / nv;
        // The confidence term sqrt(2 lnN/n_j) alone saturates the min(1/4, .)
        // cap once 2*lnOverN >= 256 (lnOverN >= 128), because isqrt32(256<<12)
        // == 1024. So for those children v is provably 1024 — skip the isqrt
        // AND the variance. Only near-root children (n_j > lnN/128) need the
        // full form. The <32768 guard preserves the existing 2*lnOverN 16-bit
        // wrap for the rare n_j==1 / high-lnN case.
        uint32_t v;
        if(lnOverN >= 128 && lnOverN < 32768) {
            v = 1024;
        } else {
            v = (uint16_t)(q6 * (64 - q6));
            v += isqrtLUT((uint32_t)(2 * lnOverN) << 12);
            if(v > 1024) v = 1024; // min(1/4, ...)
        }

        // Root RAVE blend, Gelly-Silver β from the CHILD's visits:
        // fresh children lean on AMAF evidence, established children
        // graduate to their own record. (β from the parent's count
        // kept even a 130-visit leader half-AMAF and flattened the
        // root.) Never lift a poisoned (illegal) child back via RAVE.
        if(
#ifdef NORAVE
           0 &&
#endif
           atRoot && nv < POISONED &&
           n.move < BOARD_CELLS && raveV[n.move]
           ) {
#if RAVE_K == 100 || RAVE_K == 150 || RAVE_K == 200 || RAVE_K == 250 || RAVE_K == 300 || RAVE_K == 600
            uint16_t beta = (nv < 64)
                ? pgm_read_word(BETA_TAB + nv)
                : isqrtLUT((uint32_t)raveRatio(nv) << 12);
#else
            // sweep form: no BETA_TAB for this K -- compute always
            uint16_t beta = isqrtLUT((uint32_t)raveRatio(nv) << 12);
#endif
            uint16_t qr = winRate6(raveW[n.move], raveV[n.move]) << 6;
            // ((4096-beta)*q + beta*qr)>>12 == q + (beta*(qr-q)>>12): one
            // signed 16x16->32 multiply instead of two 32-bit multiplies,
            // value-identical (4096*q>>12 == q; arith shift floors the rest).
            int16_t d = (int16_t)qr - (int16_t)q;
            q = (uint16_t)((int32_t)q + (((int32_t)beta * d) >> 12));
        }

#if   defined(UCB_EXPLORE_NUM)
        // Fractional exploration constant c = NUM/2^NSH for sweep points
        // between the power-of-two shifts (one extra 16x8 multiply per
        // child; ship keeps the pure shift).
        uint16_t u = q + (uint16_t)(((uint32_t)isqrtLUT((uint32_t)lnOverN * v)
                                     * UCB_EXPLORE_NUM) >> UCB_EXPLORE_NSH);
#else
        uint16_t u = q + (isqrtLUT((uint32_t)lnOverN * v) >> UCB_EXPLORE_SHIFT);
#endif
        if(u > best || bestC == 0xFF) {
            best = u;
            bestC = cur;
        }
    }
#if defined(UCB_SHADOW) && !defined(ARDUINO)
    {
        // exact-math pass, same policy (RAVE blend included at root)
        double fb = -1; uint8_t fbC = 0xFF;
        uint8_t atR = (nodeIdx == 0);
        double flnN = log((double)nVisits(nodeIdx) + 1);
        for(uint8_t c2 = node(nodeIdx).firstChild; c2 != 0xFF;
            c2 = node(c2).nextSibling) {
            Node &n2 = node(c2);
            if(n2.move & 0x80) continue;
            double nv = nRefVisits(n2);
            double qRaw = nRefWins(n2) / nv;
            // mirror the integer policy exactly: variance from the RAW
            // win rate; the RAVE blend applies only to the exploitation
            // term afterward (root only)
            double V = qRaw * (1 - qRaw) + sqrt(2 * flnN / nv);
            if(V > 0.25) V = 0.25;
            double q = qRaw;
            if(atR && nv < POISONED && n2.move < BOARD_CELLS && raveV[n2.move]) {
                double beta = sqrt(409600.0 / ((nv + 100.0) * 4096.0));
                double qr = (double)raveW[n2.move] / raveV[n2.move];
                q = q + beta * (qr - q);
            }
            double u2 = q + sqrt(flnN / nv * V) / (double)(1 << UCB_EXPLORE_SHIFT);
            if(u2 > fb) { fb = u2; fbC = c2; }
        }
        shadowSel++;
        if(fbC != bestC) {
            shadowMismatch++;
            // gap: how much exact-math value the int pick gave up
            double picked = -1;
            for(uint8_t c2 = node(nodeIdx).firstChild; c2 != 0xFF;
                c2 = node(c2).nextSibling)
                if(c2 == bestC) {
                    Node &n2 = node(c2);
                    double nv = nRefVisits(n2);
                    double qRaw = nRefWins(n2) / nv;
                    double V = qRaw * (1 - qRaw) + sqrt(2 * flnN / nv);
                    if(V > 0.25) V = 0.25;
                    double q = qRaw;
                    if(atR && nv < POISONED && n2.move < BOARD_CELLS && raveV[n2.move]) {
                        double beta = sqrt(409600.0 / ((nv + 100.0) * 4096.0));
                        double qr = (double)raveW[n2.move] / raveV[n2.move];
                        q = q + beta * (qr - q);
                    }
                    picked = q + sqrt(flnN / nv * V) / (double)(1 << UCB_EXPLORE_SHIFT);
                }
            if(picked >= 0) shadowGapSum += fb - picked;
        }
    }
#endif
    return bestC;
}

// Unpack the 2-bit game board into the byte-per-cell sim board.
// packedGet per cell pays a variable-shift loop at -Os; walking the
// packed bytes with constant shifts unrolls flat (4 cells/byte).
void unpackBoard(Game &game) {   // non-static 08-29: THE board->sim
                                 // sync, shared by game.cpp's rules
                                 // adapters (hasLiberties/isValidMove)
    // byte board (08-29): the 2-bit unpack dance is a straight copy now
    memcpy(simBoard, game.board, BOARD_CELLS);
}

static void backprop(uint8_t winner);
static void mctsIterate(Game &game) {
    unpackBoard(game);
    memset(raveMask, 0, sizeof(raveMask));
    cacheLibsPos = 0xFF; // fresh position: liberty cache is stale
    uint8_t ko = rootKo;
    uint8_t toMove = rootTurn;
    uint8_t lastMove = rootLast;

    pathDepth = 0;
    uint8_t cur = 0;
    path[pathDepth++] = 0;
    descentCaptured = 0;
    uint8_t retries = 0;

    // Selection + expansion: descend the existing tree, add at most
    // ONE new node per iteration, then playout. (Chaining expansions
    // to full depth here would burn the whole pool on the first few
    // prior-guided noodles and freeze the tree for the rest of the
    // search.)
    while(pathDepth < 31) {
        uint8_t fresh = 0;
        if(node(cur).firstChild == 0xFF) {
            // Cold leaves just playout; see EXPAND_VISITS
            if(cur != 0 && nVisits(cur) < EXPAND_VISITS) break;
            if(allocReady()) {
                if(cur == 0) expandNode(cur, toMove, ko, lastMove);
                else fresh = widenNode(cur, toMove, ko, lastMove);
            }
            if(node(cur).firstChild == 0xFF) break; // terminal or pool dry
        } else {
            // Progressive widening: one more candidate as visits grow
            // (the root has its own, wider schedule)
            uint8_t maxKids;
            // Saturation gate (2026-08-17): past the cap threshold the
            // divide's result is provably the cap -- skip __udivmodhi4
            // on every established node (proof: exhaustive 0..65535).
            if(cur == 0) {
                uint16_t nv0 = nVisits(0);
                if(nv0 >= (80 - ROOT_INIT) * ROOT_WIDEN_RATE)   // 864
                    maxKids = 80;
                else
                    maxKids = (uint8_t)(ROOT_INIT + nv0 / ROOT_WIDEN_RATE);
            } else {
                uint16_t nvc = nVisits(cur);
                if(nvc >= (uint16_t)(WIDEN_CAP - 1) * WIDEN_RATE)  // 90
                    maxKids = WIDEN_CAP;
                else
                    maxKids = (uint8_t)(1 + nvc / WIDEN_RATE);
            }
            if(childCount(cur) < maxKids) {
                // a stored latent fills the slot without a scan; the
                // FIRST flagged child in the walk is the best remaining
                // (worst-first adds on a prepend list = best-first walk)
                uint8_t lat = latentBest(cur);
                if(lat != 0xFF)
                    node(lat).move &= 0x7F;
                else if(allocReady())
                    widenNode(cur, toMove, ko, lastMove);
            }
        }
#ifdef LATENT_DEBUG
        if(path[0] != 0) { fprintf(stderr,"CANARY post-trigger cur=%u pd=%u\n",cur,pathDepth); abort(); }
#endif
        uint8_t c;
        c = selectChild(cur);
#ifdef LATENT_DEBUG
        if(path[0] != 0) { fprintf(stderr,"CANARY post-select cur=%u c=%u pd=%u\n",cur,c,pathDepth); abort(); }
#endif
        uint8_t nk = simPlay(node(c).move, toMove, ko);
#ifdef LATENT_DEBUG
        if(path[0] != 0) { fprintf(stderr,"CANARY post-simPlay c=%u pd=%u\n",c,pathDepth); abort(); }
#endif
        if(nk == ILLEGAL) {
            nSetStats(c, POISONED, 0);
            if(++retries >= 4) break;
            continue;
        }
        if(toMove == rootTurn && node(c).move < BOARD_CELLS)
            raveMark(node(c).move);
        descentCaptured |= simCaptured;
        ko = nk;
        toMove ^= 3;
        lastMove = node(c).move;
        cur = c;
        path[pathDepth++] = c;
        if(fresh) break; // the freshly expanded child: playout from here
    }

    uint8_t winner = playout(toMove, ko, lastMove);

    // True-komi result feeds the eval (resignation, passing, stats);
    // the tree and RAVE learn under the virtual komi (see vKomi2)
    thinkSims++;
    if(winner == rootTurn) thinkSimWins++;
#if !defined(ARDUINO) || defined(VKOMI_WIN)
    // (device pays this only with VKOMI_WIN: the margin mean is the v2
    // engage signal — playout wr is miscalibrated at bleed sites)
    thinkMargin2Sum += (rootTurn == BLACK)
        ? (int16_t)(lastMargin2 - (int16_t)simKomi)
        : (int16_t)((int16_t)simKomi - lastMargin2);
#endif
#ifdef VKOMI_WIN
    if(vKomi2 | vKomiWin) winner = vKomiWinner();
    thinkVirtWins += (winner == rootTurn);
#else
    if(vKomi2) winner = vKomiWinner();
#endif

    // Fold this simulation into the root RAVE tables. Byte-walk the
    // mask: an all-zero byte skips 8 cells for one load, and the
    // per-cell test becomes a constant-mask AND on a cached byte
    // (same cells, same ascending order).
    uint8_t rootWin = (winner == rootTurn);
#ifndef NORAVE
    for(uint8_t b = 0; b < (BOARD_CELLS + 7) / 8; b++) {
        uint8_t m = raveMask[b];
        if(!m) continue;
        uint8_t i = (uint8_t)(b << 3);
        for(uint8_t bit = 1; bit && i < BOARD_CELLS; bit <<= 1, i++) {
            if(!(m & bit)) continue;
            if(raveV[i] == 255) { // saturate by halving, keeps the ratio
                raveV[i] >>= 1;
                raveW[i] >>= 1;
            }
            raveV[i]++;
            raveW[i] += rootWin;   // rootWin is 0/1: branchless
        }
    }
#endif

    // Backprop. path[1] was played by rootTurn, path[2] by the opponent, ...
    // win-by-parity is loop-invariant; precompute both flags once.
    backprop(winner);
}
// Backprop, extracted at O2 (2026-08-15): whole-function mctsIterate O2
// measured -1.30% but +484 B; this slice keeps its share for +34 B.
__attribute__((optimize("O2"), noinline))
static void backprop(uint8_t winner) {
    // path[1] was played by rootTurn, path[2] by the opponent, ...
    // win-by-parity is loop-invariant; precompute both flags once.
    uint8_t winOdd = (rootTurn == winner);
    uint8_t winEven = ((uint8_t)(3 - rootTurn) == winner);
    for(uint8_t i = 0; i < pathDepth; i++) {
        uint8_t win = i ? ((i & 1) ? winOdd : winEven) : 0;
        nBump(path[i], win);
    }
}



// Think-progress bar, streamed STRAIGHT to the SSD1306: sBuffer is the
// node pool during think, so the bar cannot go through the buffer -- the
// panel retains the pre-think frame and we repaint one 56-col strip on
// page 5 (bits 2-4 = y 42-44), directly under the "AI THINKING..." label
// at (66,35). The final full-window lcdWin also homes the data pointer
// to (0,0), which paintScreen()'s wrap-around addressing needs.
// CrossMux port: ESP32 builds use the no-op inline stubs (top of file).
#if defined(ARDUINO) && !defined(ESP32) && !defined(ARDUINO_ARCH_ESP32)
PROGMEM static const uint8_t WIN_BAR[6]  = {0x21, 66, 121, 0x22, 5, 5};
PROGMEM static const uint8_t WIN_FULL[6] = {0x21, 0, 127, 0x22, 0, 7};
__attribute__((noinline))
static void lcdWin(const uint8_t *t) {
    Arduboy2Core::LCDCommandMode();
    for(uint8_t i = 0; i < 6; i++)
        Arduboy2Core::SPItransfer(pgm_read_byte(t + i));
    Arduboy2Core::LCDDataMode();
}
__attribute__((noinline))
static void thinkProgressBlit(uint8_t px) {
    lcdWin(WIN_BAR);
    for(uint8_t c = 0; c < 56; c++)
        Arduboy2Core::SPItransfer(c < px ? 0x1C : 0x00);
}
#endif

// Root-child LCB, exactly the pick rule's math incl. gates and the
// crawl whisper — the ONE copy shared by the pick scan and every
// arbiter (dedup 08-28: PRIORTIE carried a full inline duplicate).
// vEff = gate-effective visits; q/LCB keep the full sample.
// winRate6, not (w<<6)/v: bit-exact below 1024 wins and carries the
// high-budget wrap guard. Returns -32768 when the child is gated out.
static int16_t rootChildLcb(uint8_t c, uint16_t vEff, uint16_t maxV) {
    uint16_t v = nVisits(c);
    if(v >= POISONED) return -32768;
    if(node(c).move & 0x80) return -32768;
    if(vEff < LCB_GATE || vEff * LCB_REL_DIV < maxV) return -32768;
    uint16_t q6 = winRate6(nWins(c), v);  // Q6 win rate, bit-exact (w<<6)/v
    uint16_t q = q6 << 6;                 // Q12 (Q6 precision)
    uint16_t var = q6 * (64 - q6);        // q(1-q), Q12, 16-bit mul
    // (var<<12)/v is Q24 of q(1-q)/n, so isqrt lands in Q12
    uint16_t term = isqrtLUT(((uint32_t)var << 12) / v);
    int16_t lcb = (int16_t)q - (int16_t)(((uint32_t)term * LCB_Z) >> 8);
    return lcb;
}


void AI::think(Game &game) {
#if !defined(ARDUINO) && defined(THINK_TRACE)
    fprintf(stderr, "THINK rng=%u epoch=%u vk=%u pool=%u\n",
            rngState, markEpoch, vKomi2, poolUsed);
#endif
    // Opponent just passed: passing back ends the game right now, so
    // if the game as it stands is already won, take it — no search.
    // Dead enemy stones make computeScore undercount our territory,
    // which only delays this trigger until they are actually captured
    // (it can never pass into a loss by the game's own scoring).
    rootNearValid = 0;   // new root board: recompute its near mask
    passToWin = 0;
    resigned = 0;
    // The naive-count path stays endgame-only (>= 45 stones: it once
    // fired on a 6-stone board, "winning" by bare komi). The vote
    // path below runs at ANY stone count -- settleVote's own
    // decisiveness guard is its reliability bar (Jay's game
    // 2026-08-01: opponent passed at 31 stones with the territory
    // decided; the old outer 45-stone bar kept the honest vote off
    // while the engine filled its own territory and threw dead
    // stones into his until the count flipped to a loss).
    if(game.consecutivePasses == 1 && !boardOpen(game)) {
        // Pass-mechanism collapse (2026-08-13): the settle vote below is
        // the ONLY pass-answer authority. The old naive path (>=45
        // stones + stale eval + areaWinner) judged by the count that
        // scores dead stones as alive, while the game is scored by the
        // scoreDead vote -- pass decisions and scoring must share one
        // truth. Same collapse deleted the settled-territory tail pass
        // in bestMove (it could fire inside the vote's deliberate
        // keep-playing band, passing into a vote-read loss).
        // Ownership-corrected settle gate: the scoreDead-style vote
        // reads the board honestly — and it is the SAME vote game-over
        // scoring applies, so a pass taken here scores the way the
        // vote says. Clearly winning → pass and bank it. Clearly lost
        // → pass and accept the count instead of flailing stones into
        // groups the vote already reads as dead. Contested → play on.
        int16_t m2c = settleVote(game);
        // Pre-endgame the gate only BANKS WINS: accepting a loss
        // early forfeits swindle equity vs fallible opponents (the
        // 100-game sanity flipped one game to a loss exactly this
        // way); losing positions keep playing until the >=45-stone
        // endgame bar as before.
        if(m2c != SETTLE_NONE &&
           (m2c > 0 ||
            (m2c <= -SETTLE_ACCEPT2 && countStones(game) >= 45))) {
            passToWin = 1;
            resignCount = 0;
            return;
        }
    }

    poolUsed = 0;
    freeHead = 0xFF;
    thinkSims = thinkSimWins = 0;
#if !defined(ARDUINO) && defined(RECLAIM_LOG)
    rlOpen(); rlIter = 0;
    memset(rlBirth, 0, sizeof(rlBirth)); memset(rlBirthD, 0, sizeof(rlBirthD));
    memset(rlDescHist, 0, sizeof(rlDescHist));
    if(rlF) fprintf(rlF, "N\n");
#endif
#ifdef STABLE_STOP
    ssSince = 0; ssPrev = 0xFF;
#endif
#ifdef VKOMI_WIN
    thinkVirtWins = 0;
#ifdef VKW_FORCE
    vKomiWin = VKW_FORCE;   // bench-only: measure the active-path cost
#endif
#endif
#ifndef ARDUINO
    thinkMargin2Sum = 0;
#endif
    rootTurn = game.turn;
    simKomi = game.kpieces;
#ifndef ARDUINO
    // Host: the engine RNG free-runs from the harness's per-game seed,
    // exactly like the device free-runs from boot. (The old per-think
    // random(0xFFFF) reseed was NEVER seeded -- srand() does not seed
    // random() on macOS -- so every think drew from one process-global
    // stream and no game could reproduce outside its batch position.)
    // forceThinkSeed (0 = off) replays a recorded think; lastThinkSeed
    // captures the state so a saved game reproduces move-for-move.
    if(forceThinkSeed) rngState = forceThinkSeed;
    lastThinkSeed = rngState;
#endif

    // Real ko point: if the last move captured exactly one of our
    // stones, forbid the immediate recapture in search. Slightly
    // over-strict for multi-stone situations, but bestMove's validation
    // against the real rules has the final say. (08-29: read directly
    // from Game's point-ko state -- koCap IS the caps==1 cell and
    // NO_KO == 0xFF, so the old prevBoard diff loop reduces to copies.)
    rootKo = game.koCap;
    rootLast = game.lastMove;
    rootStones = countStones(game);

#if NN_PRIOR_TOP
    // Net-priored MCTS: fill the net's top-3 root candidates while sBuffer is
    // still free (nnOpeningMove borrows it; the pool/loadRootBoard reuse it).
    { uint8_t na, nb; nnPriorMode = 1; nnOpeningMove(game, na, nb); nnPriorMode = 0; }
#endif

    loadRootBoard(game);

#ifndef NORAVE
    memset(raveV, 0, 2 * BOARD_CELLS);   // raveW == raveV + BOARD_CELLS, contiguous
#endif

    newNode(0xFF); // root
    uint16_t iters = mctsIterations;
    uint16_t total = iters;
    uint8_t extended = 0;
#ifdef PRIOR_DIRECT_OPEN
    // Prior-direct opening arc (2026-08-21): for the first
    // PRIOR_DIRECT_OPEN stones, run only a minimal budget so the chosen
    // move collapses to the PN_ prior's top candidate (child seed
    // visits/wins = PRIOR_BASE + prior bonus; bestMove's most-visited
    // fallback + LCB both favor the top-prior child). Above the cap =
    // full MCTS (the "fight" phase). Cap is sweepable; PDO_ITERS keeps
    // just enough playouts to expand the root. extended=1 skips the
    // flat-root budget extension. HOST-ONLY experiment flag.
#ifndef PDO_ITERS
#define PDO_ITERS 4
#endif
    if(rootStones <= (PRIOR_DIRECT_OPEN)) { total = PDO_ITERS; extended = 1; }
#endif
#ifdef ARDUINO
    uint8_t barPx = 0;
#endif
#ifdef DECIDE_PROBE
    dpLastChange = 0; dpPrevBest = 0xFF;
#endif
    for(uint16_t i = 0; i < total; i++) {
#ifdef ARDUINO
        // Asymptotic fill: each update advances 1/8 of the REMAINING bar,
        // so it leaps at the start and crawls near the end (the perceptual
        // fast-start trick), completing via the snap-full at exit. No
        // divide, no dependence on `total`.
        if((i & 63) == 0) {
            barPx += (uint8_t)(56 - barPx) >> 3;
            thinkProgressBlit(barPx);
        }
#endif
#ifndef ARDUINO
        thinkItersRun++;
#endif
#if !defined(ARDUINO) && defined(RECLAIM_LOG)
        rlIter = i;
#endif
        mctsIterate(game);
#if !defined(ARDUINO) && defined(RECLAIM_LOG)
        rlDescHist[pathDepth < 31 ? pathDepth : 31]++;
#endif
#ifdef DECIDE_PROBE
        {
            uint16_t bv = 0; uint8_t bc = 0xFF;
            for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
                uint16_t v = nVisits(c);
                if(v < POISONED && v > bv) { bv = v; bc = c; }
            }
            if(bc != dpPrevBest) {
                dpPrevBest = bc; dpLastChange = i;
#if !defined(ARDUINO)
                fprintf(stderr, "DPCHG %u\n", i);
#endif
            }
        }
#endif
        // Flat-root check at budget end (see UNCERTAIN_MIN)
        if(i + 1 == total && !extended) {
            extended = 1;
            uint16_t lead = 0;
            for(uint8_t c = node(0).firstChild; c != 0xFF;
                c = node(c).nextSibling) {
                uint16_t v = nVisits(c);
                if(v < POISONED && v > lead) lead = v;
            }
            if(lead < UNCERTAIN_MIN) total += iters / 2;
            // Dose knobs (endgame-arc P2, 2026-08-11): band 45-55%
            // (LO 9 / HI 11 twentieths) and +50% (AMT 1) are the shipped
            // defaults. Extension trace census: extensions fire ONLY
            // mid/end (~1/15 thinks), ZERO in the opening (the opening-
            // gate idea was a no-op; 27s opening moves = the 600-iter
            // boost x costlier early playouts). DOSE SWEEP MEASURED
            // NEUTRAL: L0 n=1000 x2/band/both all +0.4..0.5pp n.s.;
            // human n=1000 x2 -1.2pp / both +1.2pp (opposite signs,
            // both contain AMT=2 -> noise). The clutch is SATURATED at
            // its shipped dose (matches the CLUTCH2 verdict).
#ifndef CLUTCH_LO
#define CLUTCH_LO 0 // SHIP: clutch off at 1000 base (see boost note)
#endif
#ifndef CLUTCH_HI
#define CLUTCH_HI 0
#endif
#ifndef CLUTCH_AMT
#define CLUTCH_AMT 1
#endif
            // Clutch extension (2026-08): the flat-root check above
            // fires on visit flatness; this complements it on
            // EVALUATION closeness -- a root the playouts score 45-55%
            // is exactly where one more increment of search moves the
            // pick. Same once-only +50% budget as the opening boost.
            // Paired 2000 vs GnuGo L0: +26 (20.0% vs 18.7%), both
            // 1000-game samples positive, for +6.9% total compute
            // (~1 in 7 thinks extends; ~20s vs 13.5s on device).
            else if((uint32_t)thinkSimWins * 20 >
                        (uint32_t)thinkSims * CLUTCH_LO &&
                    (uint32_t)thinkSimWins * 20 <
                        (uint32_t)thinkSims * CLUTCH_HI)
                total += (iters * CLUTCH_AMT) / 2;
        }

        // Early stop when the visit leader's margin exceeds the
        // remaining budget. With LCB move selection this is an
        // approximation rather than exact: a dominant visit lead
        // almost always implies a dominant LCB, but not provably.
        if((i & 15) == 15) {
#if !defined(ARDUINO) && defined(RECLAIM_LOG)
            if(rlF) {
                uint8_t pd = 0, cur = 0;
                for(;;) {
                    uint8_t best = 0xFF; uint16_t bv = 0;
                    for(uint8_t c = node(cur).firstChild; c != 0xFF; c = node(c).nextSibling) {
                        if(node(c).move & 0x80) continue;
                        uint16_t v = nVisits(c);
                        if(v < POISONED && v > bv) { bv = v; best = c; }
                    }
                    if(best == 0xFF || pd >= 30) break;
                    pd++; cur = best;
                }
                fprintf(rlF, "P %u %u\n", i, pd);
            }
#endif
            uint16_t top1 = 0, top2 = 0;
#ifdef STABLE_STOP
            uint8_t argmax = 0xFF;
#endif
#if ZSTOP > 0
            uint8_t zc1 = 0xFF, zc2 = 0xFF;
#endif
            for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
                uint16_t v = nVisits(c);
                if(v >= POISONED) continue;
                if(v > top1) {
                    top2 = top1; top1 = v;
#ifdef STABLE_STOP
                    argmax = node(c).move;
#endif
#if ZSTOP > 0
                    zc2 = zc1; zc1 = c;
#endif
                }
                else if(v > top2) {
                    top2 = v;
#if ZSTOP > 0
                    zc2 = c;
#endif
                }
            }
#if ZSTOP > 0
            // Posterior-gap stop (mass-insensitivity arc, 08-26): stop
            // when the visit-leader's q separates from #2 by z sigma
            // (z = ZSTOP/16), sigma from Bernoulli variances of the two
            // children. Evidence-denominated: measures the noise instead
            // of assuming ship's evidence-per-iteration. Replaces the
            // stable-leader criterion (build with -DSTABLE_K=0).
            if(zc1 != 0xFF && zc2 != 0xFF && i >= total / 4) {
                uint16_t v1 = nVisits(zc1), v2 = nVisits(zc2);
#ifndef ZSTOP_Q6
#define ZSTOP_Q6 1   // SHIP 2026-08-27: -110B at zero think cost;
                     // mini +14/2000 wash, ITERS 0.6220 vs 0.6283.
                     // -DZSTOP_Q6=0 restores the Q12 form.
#endif
#if ZSTOP_Q6
                // Q6 form (flash grind 08-27): same z-test at the pick
                // rule's own precision (q6 = wins<<6/v, the LCB scale).
                // All-16-bit: q6(64-q6) <= 1024, <<4 <= 16384, sigma
                // <= 128, delta<<6 <= 4096. Scale match vs Q12: varS =
                // var12 >> 8 => sigmaS = sigma12/16, and the original
                // test (dq12<<4 > Z*sigma12) becomes dq6<<6 > Z*sigmaS.
                // Decision differs only by quantization (ladder-gated).
                // winRate6, not (w<<6)/v (08-29, audit sweep): bit-exact
                // here and wrap-guarded past 1023 wins -- the flat-2000
                // budget reaches that; raw shift silently wrapped.
                uint16_t q1 = winRate6(nWins(zc1), v1);
                uint16_t q2 = winRate6(nWins(zc2), v2);
                if(q1 > q2) {
                    uint16_t var = (uint16_t)((q1 * (64 - q1)) << 4) / v1
                                 + (uint16_t)((q2 * (64 - q2)) << 4) / v2;
                    uint16_t sigma = isqrtLUT(var);
                    if((uint16_t)((q1 - q2) << 6) > (uint16_t)(ZSTOP * sigma)) break;
#else
                uint32_t q1 = ((uint32_t)nWins(zc1) << 12) / v1;
                uint32_t q2 = ((uint32_t)nWins(zc2) << 12) / v2;
                if(q1 > q2) {
                    uint32_t var = q1 * (4096 - q1) / v1
                                 + q2 * (4096 - q2) / v2;
                    uint16_t sigma = isqrtLUT(var);
                    if((q1 - q2) << 4 > (uint32_t)ZSTOP * sigma) break;
#endif
                }
            }
#endif
#ifdef STABLE_STOP
            // Stable-leader stop (iterations-arc A2): if the visit-argmax
            // hasn't changed for STABLE_K iterations past the floor, the
            // pick almost never changes -- bank the remainder. K is priced
            // offline from the freeze-point change-timeline data.
            if(argmax != ssPrev) { ssPrev = argmax; ssSince = 0; }
            else if((uint16_t)(ssSince += 16) >= STABLE_K && i >= total / 4
                    )
                break;
#endif
            // Probabilistic early-stop (was: exact `lead > remaining`).
            // 2nd overtaking the leader needs it to win most of the
            // remaining budget, but UCB keeps feeding the leader -- so a
            // lead over HALF the remaining is practically safe. Banks
            // iterations on decided positions (device latency / budget for
            // the hard ones). value-changing -> gauntlet-gated.
#ifdef NOLEADSTOP
            // criterion-2 retirement test (08-26): lead-stop compiled out;
            // hypothesis: the z-test reaches every position first.
#else
            if(2u * (uint16_t)(top1 - top2) > total - 1 - i
               ) break;
#endif
        }
    }
#if defined(ARDUINO) && !defined(ESP32) && !defined(ARDUINO_ARCH_ESP32)
    thinkProgressBlit(56);           // early-stops snap the bar full
    lcdWin(WIN_FULL);                // restore full window for display()
#endif

#if !defined(ARDUINO) && defined(RECLAIM_LOG)
    if(rlF) {
        // end-of-think: alive nodes by ply, PV-child subtree, descent histogram
        uint32_t aliveA[16] = {0}, aliveL[16] = {0};
        uint8_t stk[256], dstk[256]; uint16_t sp = 0;
        stk[0] = 0; dstk[0] = 0; sp = 1;
        while(sp) {
            sp--; uint8_t nI = stk[sp], dd = dstk[sp];
            for(uint8_t c = node(nI).firstChild; c != 0xFF; c = node(c).nextSibling) {
                uint8_t cd = dd + 1 < 15 ? dd + 1 : 15;
                if(node(c).move & 0x80) { aliveL[cd]++; continue; }
                aliveA[cd]++;
                if(sp < 255) { stk[sp] = c; dstk[sp] = (uint8_t)(dd + 1); sp++; }
            }
        }
        // PV child = visit-argmax root child; its subtree size/depth/visits
        uint8_t pvc = 0xFF; uint16_t pvv = 0;
        for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
            if(node(c).move & 0x80) continue;
            uint16_t v = nVisits(c);
            if(v < POISONED && v > pvv) { pvv = v; pvc = c; }
        }
        uint32_t pvSize = 0, pvVis = 0; uint8_t pvMaxD = 0;
        if(pvc != 0xFF) {
            stk[0] = pvc; dstk[0] = 1; sp = 1;
            while(sp) {
                sp--; uint8_t nI = stk[sp], dd = dstk[sp];
                for(uint8_t c = node(nI).firstChild; c != 0xFF; c = node(c).nextSibling) {
                    if(node(c).move & 0x80) continue;
                    pvSize++; pvVis += nVisits(c);
                    if(dd + 1 > pvMaxD) pvMaxD = (uint8_t)(dd + 1);
                    if(sp < 255) { stk[sp] = c; dstk[sp] = (uint8_t)(dd + 1); sp++; }
                }
            }
        }
        fprintf(rlF, "S %u %lu %lu %u %u\n", pvc == 0xFF ? 0 : nVisits(pvc),
                (unsigned long)pvSize, (unsigned long)pvVis, pvMaxD, poolUsed);
        fprintf(rlF, "D");
        for(uint8_t d = 0; d < 16; d++) fprintf(rlF, " %lu/%lu",
                (unsigned long)aliveA[d], (unsigned long)aliveL[d]);
        fprintf(rlF, "\n");
        fprintf(rlF, "H");
        for(uint8_t d = 0; d < 32; d++) fprintf(rlF, " %lu", (unsigned long)rlDescHist[d]);
        fprintf(rlF, "\n");
        fflush(rlF);
    }
#endif
#ifndef ARDUINO
    thinkItersBudget += total;
    thinkAvgMargin2 = thinkSims
        ? (int16_t)(thinkMargin2Sum / (int32_t)thinkSims) : 0;
#ifdef DECIDE_PROBE
    fprintf(stderr, "DPEND total=%u run=%u stones=%u lastchg=%u\n",
            total, (unsigned)thinkSims, rootStones, dpLastChange);
#endif
#endif

    // Resignation check (see RESIGN_* above), two tiers; one stone gate
    if(rootStones >= RESIGN_MIN_STONES) {
        if(thinkSimWins * RESIGN_DENOM < thinkSims) resignCount++;
        else resignCount = 0;
        if(thinkSimWins * RESIGN2_DENOM < thinkSims) resignCount2++;
        else resignCount2 = 0;
    } else
        resignCount = resignCount2 = 0;
    resigned = (resignCount >= resignStreak) ||
               (resignCount2 >= RESIGN2_STREAK);

    // Dynamic-komi adaptation for the NEXT search (see vKomi2):
    // clearly losing -> ask for two points less, so the tree fights
    // for margin instead of miracle collapses; healthy -> tighten
    // back toward the real game. The cap keeps truly lost games
    // reading lost, so resignation still fires on the true eval.
    if(thinkSims) {
        uint32_t w20 = (uint32_t)thinkSimWins * 20;
        if(w20 < (uint32_t)thinkSims * 7) {
            if(vKomi2 < VKOMI_MAX2) vKomi2 += VKOMI_STEP2;
        } else if(w20 > (uint32_t)thinkSims * 11 && vKomi2) {
            vKomi2 -= VKOMI_STEP2;
        }
#ifdef VKOMI_WIN
        // v2 (probe-driven): engage on the MARGIN mean, not win-rate —
        // at the phase-0 bleed sites the playout wr reads 34-74% while
        // KataGo says +30..+56 (L&D-blind bimodal playouts), but the
        // margin mean sees the win. Demand at most HALF the perceived
        // cushion. Release (checked first, safety bias) on virtual-wr
        // collapse (anti-flail), wr floor, margin fade, or vKomi2 waking
        // (contradictory regimes must not fight).
        int16_t am = (int16_t)(thinkMargin2Sum / (int32_t)thinkSims);
        uint32_t v20 = (uint32_t)thinkVirtWins * 20;
        if(vKomiWin &&
           (v20 < (uint32_t)thinkSims * 9 ||        // virtual wr < 45%
            w20 < (uint32_t)thinkSims * 10 ||       // true wr < 50%
            vKomi2 ||
            am < (int16_t)vKomiWin * 2)) {          // demand > half margin
            vKomiWin = (vKomiWin > VKW_STEP) ? vKomiWin - VKW_STEP : 0;
        } else if(!vKomi2 && w20 >= (uint32_t)thinkSims * 10 &&
                  am > (int16_t)VKW_ENGAGE && vKomiWin < VKW_MAX &&
                  (int16_t)(vKomiWin + VKW_STEP) * 2 <= am) {
            vKomiWin += VKW_STEP;
        }
#if defined(VKW_DEBUG) && !defined(ARDUINO)
        fprintf(stderr, "VKW %u am=%d true%lu%% virt%lu%% vk2=%u\n", vKomiWin,
                am, (unsigned long)(100UL * thinkSimWins / thinkSims),
                (unsigned long)(100UL * thinkVirtWins / thinkSims), vKomi2);
#endif
#endif
    }
}

// Root self-atari veto: a move that leaves its own merged group at
// one liberty while capturing nothing is a gift stone. The prior
// penalty discourages it, but playouts approve the "trap" whenever
// the answering side fumbles locally — a real game shipped one
// (7/10 seeds reproduced it). The root choice skips such children
// and the LCB race falls through to the next-best. Cost: the rare
// legitimate throw-in tesuji, which this engine never follows up
// anyway. Reads the root position from simBoard.
static uint8_t rootSelfAtari(uint8_t pos, uint8_t toMove) {
    simBoard[pos] = toMove;
    uint8_t r = 0;
    if((uint8_t)groupLibsCore(pos, 0, 2) <= 1) {
        r = 1;
        // ...unless the stone captures: with it placed, a doomed
        // enemy neighbor chain reads zero liberties
        uint8_t q;
        FOR_EACH_NEIGHBOR(q, pos)
            if(simBoard[q] == 3 - toMove && !hasLiberty(q, 3 - toMove)) {
                r = 0;
                break;
            }
    }
    simBoard[pos] = EMPTY;
    return r;
}

static uint8_t pct100(uint16_t w, uint16_t n) {
    return n ? (uint8_t)((uint32_t)w * 100 / n) : 0;
}

// Root-move legality shared by the LCB and backup picks: on-board legal
// and not a root self-atari.
static bool rootMoveOK(Game &game, uint8_t m) {
    uint8_t mxy = posXY(m);
#ifdef OWL_LITE
    if(!game.isValidMove(mxy & 0x0F, xyHi(mxy))) return false;
    if(rootSelfAtari(m, game.turn)) return false;
    if(owlVetoActive) {
        uint8_t q, adj = 0, captures = 0;
        FOR_EACH_NEIGHBOR(q, m) {
            if(simBoard[q] != EMPTY && (owlVeto[q >> 3] & bitMask(q))) adj = 1;
            if(simBoard[q] == (uint8_t)(3 - game.turn) &&
               soleLiberty(q) == m) captures = 1;
        }
        if(adj && !captures) {
#if defined(OWL_DEBUG) && !defined(ARDUINO)
            fprintf(stderr, "OWL veto m=%u\n", m);
#endif
            return false;                    // don't feed the hopeless group
        }
    }
    return true;
#else
    return game.isValidMove(mxy & 0x0F, xyHi(mxy)) &&
           !rootSelfAtari(m, game.turn);
#endif
}

#if PRIORTIE > 0
// Shared setup for the tie-band prior probes: the far-flag bit test +
// 4-arg marshalling cost ~50 B per inline site (flash audit 08-28).
static __attribute__((noinline)) int8_t rootPrior(uint8_t m, uint8_t toMove,
        const uint8_t *ptNear, uint8_t ptAny) {
    return candidatePrior(m, toMove, rootLast,
        ptAny && !(ptNear[m >> 3] & bitMask(m)));
}
#endif

uint8_t AI::bestMove(Game &game, uint8_t &x, uint8_t &y) {
    // Stats default to the whole-root eval; the chosen child's own
    // numbers overwrite them below once it is known
    statVisits = 0;
    statPct = pct100(thinkSimWins, thinkSims);

    if(passToWin) return 0; // ending the game now wins it

    // Root move by highest LOWER confidence bound on the win rate
    // (Leela-style): lcb = q - z*sqrt(q(1-q)/n), Q12 fixed point.
    // Beats most-visited when a visit-leader's win rate is decaying.
    // Low-visit children punish themselves via the wide bound, and the
    // real-rules validation (full ko) stays lazy: an invalid favorite
    // falls through to the next-best instead of turning into a pass.
    int16_t bestL = -32768;
    uint16_t backV = 0, maxV = 0;
    uint8_t best = MOVE_PASS, backup = MOVE_PASS;
    uint8_t bestC = 0xFF, backC = 0xFF;
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v >= POISONED) continue;
        if(v > maxV) maxV = v;
    }
    // Root position for rootSelfAtari and the settled-territory check
    unpackBoard(game);
    for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
        uint16_t v = nVisits(c);
        if(v >= POISONED) continue;
        if(node(c).move & 0x80) continue; // latent: never activated
        uint8_t m = node(c).move;
        uint16_t vEff = v;

        // Fallback: most-visited, in case no child clears the LCB gate
        if(vEff > backV) {
            if(m == MOVE_PASS || rootMoveOK(game, m)) {
                backV = vEff;
                backup = m;
                backC = c;
            }
        }

        // Gate (prior-seeded children carry inflated q at tiny n and
        // would fake a strong LCB — demand real sampling first) + LCB +
        // crawl whisper: one shared copy, see rootChildLcb.
        int16_t lcb = rootChildLcb(c, vEff, maxV);
        if(lcb == -32768) continue;

        if(lcb <= bestL) continue;
        if(m != MOVE_PASS && !rootMoveOK(game, m))
            continue;
        bestL = lcb;
        best = m;
        bestC = c;
    }
    if(best == MOVE_PASS) { best = backup; bestC = backC; }
#if PRIORTIE > 0
    // Prior-informed near-tie break (2026-08-22, priordrop finding: at
    // 50% of prior-top-8 blunders the search rates the winning and the
    // losing move within ~5% and flips the coin wrong -- while the prior
    // ranks the right one top-3 at 52% of all blunders). Within an LCB
    // band of PRIORTIE Q12 units of the winner, re-pick by the PRIOR.
    // Value-changing -> gauntlet-gated host arm.
    if(best != MOVE_PASS && bestL != -32768) {
        buildChainMap();               // simBoard already = root (unpackBoard
        uint8_t ptNear[12];            // above); rootSelfAtari is chain-free
        uint8_t ptAny = buildNearMask(ptNear);
        int8_t bestPr = rootPrior(best, game.turn, ptNear, ptAny);
        for(uint8_t c = node(0).firstChild; c != 0xFF; c = node(c).nextSibling) {
            uint16_t v = nVisits(c);
            if(v >= POISONED) continue;
            if(node(c).move & 0x80) continue;
            uint8_t m = node(c).move;
            if(m == MOVE_PASS || m == best) continue;
            uint16_t ve = v;
            int16_t lcb = rootChildLcb(c, ve, maxV);
            if(lcb == -32768) continue;
            if(lcb < bestL - PRIORTIE) continue;   // outside the tie band
            int8_t pr = rootPrior(m, game.turn, ptNear, ptAny);
#ifndef PRIORTIE_MARGIN
#define PRIORTIE_MARGIN 0
#endif
            if(pr <= bestPr + PRIORTIE_MARGIN) continue;  // demand a DECISIVE prior gap
            if(!rootMoveOK(game, m)) continue;
            best = m; bestC = c; bestPr = pr;      // higher prior wins the tie
        }
    }
#endif
    if(bestC != 0xFF) {
        statVisits = nVisits(bestC);
        if(statVisits) statPct = pct100(nWins(bestC), statVisits);
    }
    if(best == MOVE_PASS) return 0;

    { uint8_t bxy = posXY(best); x = bxy & 0x0F; y = xyHi(bxy); }
    return 1;
}
