#include "chess.hpp"
#include <bits/stdc++.h>
#include <chrono>
#include <cmath>
#include <thread>
#include <mutex>
#include <atomic>
using namespace std;
using namespace chess;

#define INF 10000000
#define MATE 10000000
#define MATE_THRESH 9000000

int MAX_SEARCH_DEPTH = 64;

enum { EXACT, LOWER, UPPER };

struct TTEntry {
    uint64_t hash = 0;
    int score = 0;
    int depth = -1;
    int flag = EXACT;
    Move best = Move::NO_MOVE;
    uint8_t generation = 0;
};

#define TT_SIZE (1 << 22)
TTEntry tt[TT_SIZE];
uint8_t currentGeneration = 0;

// depth-preferred replacement, generation-aware: don't let a shallow
// qsearch/leaf store stomp a deep entry for the same slot unless it's
// actually a different position or a stale entry from a previous game -
// cheaper than physically zeroing 4M entries on every "ucinewgame".
void ttStore(uint64_t h, int score, int depth, int flag, Move best){
    TTEntry& e = tt[h & (TT_SIZE - 1)];
    if(e.hash != h || e.generation != currentGeneration || depth >= e.depth){
        e.hash = h; e.score = score; e.depth = depth; e.flag = flag; e.best = best; e.generation = currentGeneration;
    }
}

// ---------------------------------------------------------------------
// Piece-square tables (courtesy of a friend) - index 0 = a1 ... 63 = h8,
// written from White's perspective. Black looks up sq ^ 56 to mirror it.
// ---------------------------------------------------------------------
vector<int> pawnTable = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -10, -10, 0, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0,
    5, 5, 10, 20, 20, 10, 5, 5,
    10, 10, 10, 20, 20, 10, 10, 10,
    20, 20, 20, 30, 30, 30, 20, 20,
    30, 30, 30, 40, 40, 30, 30, 30,
    90, 90, 90, 90, 90, 90, 90, 90};

vector<int> knightTable = {
    -5, -10, 0, 0, 0, 0, -10, -5,
    -5, 0, 0, 0, 0, 0, 0, -5,
    -5, 5, 20, 10, 10, 20, 5, -5,
    -5, 10, 20, 30, 30, 20, 10, -5,
    -5, 10, 20, 30, 30, 20, 10, -5,
    -5, 5, 20, 20, 20, 20, 5, -5,
    -5, 0, 0, 10, 10, 0, 0, -5,
    -5, 0, 0, 0, 0, 0, 0, -5};

vector<int> bishopTable = {
    0, 0, -10, 0, 0, -10, 0, 0,
    0, 30, 0, 0, 0, 0, 30, 0,
    0, 10, 0, 0, 0, 0, 10, 0,
    0, 0, 10, 20, 20, 10, 0, 0,
    0, 0, 10, 20, 20, 10, 0, 0,
    0, 0, 0, 10, 10, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0};

vector<int> rookTable = {
    0, 0, 0, 5, 5, 0, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0,
    0, 0, 0, 5, 5, 0, 0, 0};

vector<int> queenTable = {
    -20, -10, -10, -5, -5, -10, -10, -20,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -10, 0, 5, 5, 5, 5, 0, -10,
    -5, 0, 5, 10, 10, 5, 0, -5,
    -5, 0, 5, 10, 10, 5, 0, -5,
    -10, 0, 5, 5, 5, 5, 0, -10,
    -10, 0, 0, 0, 0, 0, 0, -10,
    -20, -10, -10, -5, -5, -10, -10, -20};

// King tables are NEW - the original had none, so the king contributed zero
// positional score. It never "knew" that castling / staying behind pawns
// was good, or that it should activate in the endgame. Tapered between the
// two based on remaining material (see phase in eval()).
vector<int> kingMgTable = {
    20, 30, 10, 0, 0, 10, 30, 20,
    10, 15, 0, -10, -10, 0, 15, 10,
    -10, -15, -20, -20, -20, -20, -15, -10,
    -20, -25, -30, -30, -30, -30, -25, -20,
    -25, -30, -30, -35, -35, -30, -30, -25,
    -25, -30, -30, -35, -35, -30, -30, -25,
    -25, -30, -30, -35, -35, -30, -30, -25,
    -25, -30, -30, -35, -35, -30, -30, -25};

vector<int> kingEgTable = {
    -30, -20, -15, -10, -10, -15, -20, -30,
    -15, -5, 5, 10, 10, 5, -5, -15,
    -10, 10, 20, 25, 25, 20, 10, -10,
    -10, 15, 25, 30, 30, 25, 15, -10,
    -10, 15, 25, 30, 30, 25, 15, -10,
    -10, 10, 20, 25, 25, 20, 10, -10,
    -15, -5, 5, 10, 10, 5, -5, -15,
    -30, -20, -15, -10, -10, -15, -20, -30};

int pieceVal(PieceType pt){
    if(pt == PieceType::PAWN) return 100;
    if(pt == PieceType::KNIGHT) return 320;
    if(pt == PieceType::BISHOP) return 330;
    if(pt == PieceType::ROOK) return 500;
    if(pt == PieceType::QUEEN) return 900;
    if(pt == PieceType::KING) return 1000000;
    return 0;
}

// Separate, bounded piece values for SEE / move-ordering tiebreaks. Reusing
// pieceVal() here would let a king "attacker" (a legal, sometimes-correct
// move - kings can capture undefended pieces) drag its MVV-LVA tiebreak
// score down by ~1,000,000, which used to bury perfectly fine king captures
// under everything else in move ordering. 20000 is high enough that a king
// is still treated as "don't casually trade it", but it won't blow up the
// arithmetic the way the sentinel material value does.
int seeValue(PieceType pt){
    if(pt == PieceType::PAWN) return 100;
    if(pt == PieceType::KNIGHT) return 320;
    if(pt == PieceType::BISHOP) return 330;
    if(pt == PieceType::ROOK) return 500;
    if(pt == PieceType::QUEEN) return 900;
    if(pt == PieceType::KING) return 20000;
    return 0;
}

int pstValue(PieceType pt, int sq, Color color){
    int idx = (color == Color::WHITE) ? sq : (sq ^ 56);
    int val = 0;
    if(pt == PieceType::PAWN) val = pawnTable[idx];
    else if(pt == PieceType::KNIGHT) val = knightTable[idx];
    else if(pt == PieceType::BISHOP) val = bishopTable[idx];
    else if(pt == PieceType::ROOK) val = rookTable[idx];
    else if(pt == PieceType::QUEEN) val = queenTable[idx];
    return (color == Color::WHITE) ? val : -val;
}

// ---------------------------------------------------------------------
// Evaluation masks - precomputed once at startup (initEvalMasks(), called
// from main()) so eval() never has to rebuild them per call.
// ---------------------------------------------------------------------
Bitboard FILE_MASKS[8];
Bitboard ADJ_FILE_MASKS[8];
Bitboard RANK_MASK[8];
Bitboard RANKS_LE[8];     // ranks 0..r inclusive
Bitboard RANKS_GE[8];     // ranks r..7 inclusive
Bitboard PASSED_MASK[2][64]; // [color][sq] = the 3-file cone in front of sq, used both for
                              // "is this pawn passed" and "can an enemy pawn ever guard this outpost"

void initEvalMasks(){
    for(int f = 0; f < 8; f++) FILE_MASKS[f] = Bitboard(File(f));
    for(int f = 0; f < 8; f++){
        Bitboard adj = 0ULL;
        if(f - 1 >= 0) adj |= FILE_MASKS[f-1];
        if(f + 1 < 8) adj |= FILE_MASKS[f+1];
        ADJ_FILE_MASKS[f] = adj;
    }
    for(int r = 0; r < 8; r++) RANK_MASK[r] = Bitboard(Rank(r));
    for(int r = 0; r < 8; r++){
        Bitboard le = 0ULL, ge = 0ULL;
        for(int rr = 0; rr <= r; rr++) le |= RANK_MASK[rr];
        for(int rr = r; rr < 8; rr++) ge |= RANK_MASK[rr];
        RANKS_LE[r] = le; RANKS_GE[r] = ge;
    }
    for(int sq = 0; sq < 64; sq++){
        int f = sq & 7, r = sq >> 3;
        Bitboard wmask = 0ULL, bmask = 0ULL;
        for(int ff = max(0, f-1); ff <= min(7, f+1); ff++){
            for(int rr = r+1; rr < 8; rr++) wmask.set(rr*8 + ff);
            for(int rr = 0; rr < r; rr++) bmask.set(rr*8 + ff);
        }
        PASSED_MASK[int(Color::WHITE)][sq] = wmask;
        PASSED_MASK[int(Color::BLACK)][sq] = bmask;
    }
}

Bitboard pawnAttacksBB(Bitboard pawns, Color c){
    Bitboard result = 0ULL;
    Bitboard p = pawns;
    while(p){
        int s = p.pop();
        result |= attacks::pawn(c, Square(s));
    }
    return result;
}

// A tiny mg/eg pair - every new positional term below reports both a
// middlegame and endgame contribution (from White's perspective), and
// eval() blends them together using the same "phase" used for the king
// tables, instead of bolting on one flat number for every game stage.
struct MgEg {
    int mg = 0, eg = 0;
    MgEg& operator+=(const MgEg& o){ mg += o.mg; eg += o.eg; return *this; }
};

// ---- tunable weights -------------------------------------------------
// These are reasonable hand-picked values, not SPSA-tuned against real
// games. They're a solid starting point; if you have infrastructure to
// self-play and tune, these are the first constants worth optimizing.
const int DOUBLED_MG = -8,  DOUBLED_EG = -16;
const int ISOLATED_MG = -10, ISOLATED_EG = -18;
const int BACKWARD_MG = -6,  BACKWARD_EG = -10;
const int CONNECTED_PASSED_MG = 8, CONNECTED_PASSED_EG = 16;
const int PASSED_MG[8] = {0, 5, 10, 20, 35, 60, 100, 140};
const int PASSED_EG[8] = {0, 10, 20, 40, 70, 120, 190, 260};

const int ROOK_OPEN_MG = 18, ROOK_OPEN_EG = 10;
const int ROOK_SEMI_MG = 10, ROOK_SEMI_EG = 6;
const int ROOK_7TH_MG  = 15, ROOK_7TH_EG  = 28;

const int MOB_MG[6] = {0, 4, 5, 2, 1, 0}; // indexed by PieceType (PAWN..KING)
const int MOB_EG[6] = {0, 4, 5, 3, 2, 0};
const int KS_WEIGHT[6] = {0, 20, 20, 40, 80, 0}; // king-danger weight per attacking piece type

const int KING_OPEN_FILE_MG = -25;
const int KING_SEMI_OPEN_FILE_MG = -12;
const int SHIELD_PRESENT_MG = 8;

const int OUTPOST_KNIGHT_MG = 18, OUTPOST_KNIGHT_EG = 8;
const int OUTPOST_BISHOP_MG = 10, OUTPOST_BISHOP_EG = 5;

const int PAWN_THREAT_MINOR_MG = 35, PAWN_THREAT_MINOR_EG = 25;
const int PAWN_THREAT_ROOK_MG  = 48, PAWN_THREAT_ROOK_EG  = 35;
const int PAWN_THREAT_QUEEN_MG = 55, PAWN_THREAT_QUEEN_EG = 40;
const int MINOR_THREAT_ROOK_MG  = 22, MINOR_THREAT_ROOK_EG  = 16;
const int MINOR_THREAT_QUEEN_MG = 28, MINOR_THREAT_QUEEN_EG = 20;

const int SPACE_WEIGHT = 2;

// ---- pawn structure: isolated / doubled / backward / passed / connected passed
MgEg evalPawns(const Board& b){
    MgEg r;
    for(int ci = 0; ci < 2; ci++){
        Color c = Color(ci);
        Color enemy = ~c;
        int sign = (c == Color::WHITE) ? 1 : -1;
        Bitboard ownPawns = b.pieces(PieceType::PAWN, c);
        Bitboard enemyPawns = b.pieces(PieceType::PAWN, enemy);

        for(int f = 0; f < 8; f++){
            int cnt = (ownPawns & FILE_MASKS[f]).count();
            if(cnt > 1){
                r.mg += sign * DOUBLED_MG * (cnt - 1);
                r.eg += sign * DOUBLED_EG * (cnt - 1);
            }
        }

        Bitboard pawns = ownPawns;
        while(pawns){
            int s = pawns.pop();
            int f = s & 7, rk = s >> 3;

            if(!(ownPawns & ADJ_FILE_MASKS[f])){
                r.mg += sign * ISOLATED_MG;
                r.eg += sign * ISOLATED_EG;
            }

            bool passed = !(enemyPawns & PASSED_MASK[ci][s]);
            if(passed){
                int relRank = (c == Color::WHITE) ? rk : 7 - rk;
                r.mg += sign * PASSED_MG[relRank];
                r.eg += sign * PASSED_EG[relRank];

                Bitboard neighbors = ownPawns & ADJ_FILE_MASKS[f];
                while(neighbors){
                    int ns = neighbors.pop();
                    int nrk = ns >> 3;
                    if(abs(nrk - rk) <= 1 && !(enemyPawns & PASSED_MASK[ci][ns])){
                        r.mg += sign * CONNECTED_PASSED_MG;
                        r.eg += sign * CONNECTED_PASSED_EG;
                        break;
                    }
                }
            } else {
                Bitboard behindMask = (c == Color::WHITE) ? RANKS_LE[rk] : RANKS_GE[rk];
                bool hasSupportPotential = bool(ownPawns & ADJ_FILE_MASKS[f] & behindMask);
                if(!hasSupportPotential){
                    int stopSq = (c == Color::WHITE) ? s + 8 : s - 8;
                    if(stopSq >= 0 && stopSq < 64){
                        bool stopAttackedByEnemy = bool(attacks::pawn(c, Square(stopSq)) & enemyPawns);
                        bool stopDefendedByOwn   = bool(attacks::pawn(enemy, Square(stopSq)) & ownPawns);
                        if(stopAttackedByEnemy && !stopDefendedByOwn){
                            r.mg += sign * BACKWARD_MG;
                            r.eg += sign * BACKWARD_EG;
                        }
                    }
                }
            }
        }
    }
    return r;
}

// ---- rooks: open/semi-open file, 7th rank
MgEg evalRooksAndQueens(const Board& b){
    MgEg r;
    Bitboard allPawns = b.pieces(PieceType::PAWN);
    for(int ci = 0; ci < 2; ci++){
        Color c = Color(ci);
        int sign = (c == Color::WHITE) ? 1 : -1;
        Bitboard ownPawns = b.pieces(PieceType::PAWN, c);
        Bitboard rooks = b.pieces(PieceType::ROOK, c);
        while(rooks){
            int s = rooks.pop();
            int f = s & 7, rk = s >> 3;
            bool ownPawnOnFile = bool(ownPawns & FILE_MASKS[f]);
            bool anyPawnOnFile = bool(allPawns & FILE_MASKS[f]);
            if(!anyPawnOnFile){
                r.mg += sign * ROOK_OPEN_MG; r.eg += sign * ROOK_OPEN_EG;
            } else if(!ownPawnOnFile){
                r.mg += sign * ROOK_SEMI_MG; r.eg += sign * ROOK_SEMI_EG;
            }
            int relRank = (c == Color::WHITE) ? rk : 7 - rk;
            if(relRank == 6){
                r.mg += sign * ROOK_7TH_MG; r.eg += sign * ROOK_7TH_EG;
            }
        }
    }
    return r;
}

// ---- mobility + king safety, combined into one pass over the pieces so we
// only compute each piece's attack bitboard once.
MgEg evalMobilityKingSafety(const Board& b){
    MgEg r;
    Bitboard occ = b.occ();
    Square wKing = b.kingSq(Color::WHITE);
    Square bKing = b.kingSq(Color::BLACK);
    Bitboard kingZone[2];
    kingZone[int(Color::WHITE)] = attacks::king(wKing) | Bitboard::fromSquare(wKing);
    kingZone[int(Color::BLACK)] = attacks::king(bKing) | Bitboard::fromSquare(bKing);
    Bitboard whitePawnAtk = pawnAttacksBB(b.pieces(PieceType::PAWN, Color::WHITE), Color::WHITE);
    Bitboard blackPawnAtk = pawnAttacksBB(b.pieces(PieceType::PAWN, Color::BLACK), Color::BLACK);
    int attackWeight[2] = {0, 0};

    for(int ci = 0; ci < 2; ci++){
        Color c = Color(ci);
        int sign = (c == Color::WHITE) ? 1 : -1;
        int enemyIdx = 1 - ci;
        Bitboard ownOcc = b.us(c);
        Bitboard enemyPawnAtk = (c == Color::WHITE) ? blackPawnAtk : whitePawnAtk;
        Bitboard safeSquares = ~ownOcc & ~enemyPawnAtk; // "safe mobility": don't credit squares enemy pawns just recapture on
        Bitboard enemyZone = kingZone[enemyIdx];

        auto proc = [&](PieceType pt, Bitboard atk){
            int mob = (atk & safeSquares).count();
            r.mg += sign * mob * MOB_MG[int(pt)];
            r.eg += sign * mob * MOB_EG[int(pt)];
            int hits = (atk & enemyZone).count();
            if(hits > 0) attackWeight[enemyIdx] += hits * KS_WEIGHT[int(pt)];
        };

        Bitboard knights = b.pieces(PieceType::KNIGHT, c);
        while(knights){ int s = knights.pop(); proc(PieceType::KNIGHT, attacks::knight(Square(s))); }
        Bitboard bishops = b.pieces(PieceType::BISHOP, c);
        while(bishops){ int s = bishops.pop(); proc(PieceType::BISHOP, attacks::bishop(Square(s), occ)); }
        Bitboard rooks = b.pieces(PieceType::ROOK, c);
        while(rooks){ int s = rooks.pop(); proc(PieceType::ROOK, attacks::rook(Square(s), occ)); }
        Bitboard queens = b.pieces(PieceType::QUEEN, c);
        while(queens){ int s = queens.pop(); proc(PieceType::QUEEN, attacks::queen(Square(s), occ)); }
    }

    // Classic quadratic damping: a couple of attackers near the king barely
    // matter, but the danger ramps up fast once several pieces pile in.
    // King safety mostly matters while there's material on the board to
    // attack with, so this term is applied to the middlegame score only.
    int wDanger = attackWeight[int(Color::WHITE)];
    int bDanger = attackWeight[int(Color::BLACK)];
    r.mg -= (wDanger * wDanger) / 60;
    r.mg += (bDanger * bDanger) / 60;
    return r;
}

// ---- pawn shield + open files next to the king
MgEg evalKingShield(const Board& b){
    MgEg r;
    Bitboard allPawns = b.pieces(PieceType::PAWN);
    for(int ci = 0; ci < 2; ci++){
        Color c = Color(ci);
        int sign = (c == Color::WHITE) ? 1 : -1;
        Square king = b.kingSq(c);
        int kf = king.index() & 7, kr = king.index() >> 3;
        Bitboard ownPawns = b.pieces(PieceType::PAWN, c);
        int relKingRank = (c == Color::WHITE) ? kr : 7 - kr;
        // Only meaningful once the king has settled near its own back rank
        // (e.g. after castling). A king that has already marched to the
        // center isn't "missing a shield" in any useful sense.
        if(relKingRank <= 1){
            for(int df = -1; df <= 1; df++){
                int f = kf + df;
                if(f < 0 || f > 7) continue;
                Bitboard fileMask = FILE_MASKS[f];
                Bitboard ownOnFile = ownPawns & fileMask;
                if(!ownOnFile){
                    bool enemyAlsoMissing = !(allPawns & fileMask & ~ownPawns);
                    r.mg += sign * (enemyAlsoMissing ? KING_OPEN_FILE_MG : KING_SEMI_OPEN_FILE_MG);
                } else {
                    r.mg += sign * SHIELD_PRESENT_MG;
                }
            }
        }
    }
    return r;
}

// ---- knight/bishop outposts: defended by a pawn, can never be chased off by an enemy pawn
MgEg evalOutposts(const Board& b){
    MgEg r;
    for(int ci = 0; ci < 2; ci++){
        Color c = Color(ci);
        Color enemy = ~c;
        int sign = (c == Color::WHITE) ? 1 : -1;
        Bitboard ownPawns = b.pieces(PieceType::PAWN, c);
        Bitboard enemyPawns = b.pieces(PieceType::PAWN, enemy);
        Bitboard pieces = b.pieces(PieceType::KNIGHT, c) | b.pieces(PieceType::BISHOP, c);
        while(pieces){
            int s = pieces.pop();
            int f = s & 7, rk = s >> 3;
            int relRank = (c == Color::WHITE) ? rk : 7 - rk;
            if(relRank < 3 || relRank > 5) continue;
            bool defendedByPawn = bool(attacks::pawn(enemy, Square(s)) & ownPawns);
            if(!defendedByPawn) continue;
            bool canBeChallenged = bool(ADJ_FILE_MASKS[f] & PASSED_MASK[ci][s] & enemyPawns);
            if(canBeChallenged) continue;
            bool isKnight = bool(b.pieces(PieceType::KNIGHT, c) & Bitboard::fromSquare(s));
            r.mg += sign * (isKnight ? OUTPOST_KNIGHT_MG : OUTPOST_BISHOP_MG);
            r.eg += sign * (isKnight ? OUTPOST_KNIGHT_EG : OUTPOST_BISHOP_EG);
        }
    }
    return r;
}

// ---- threats: pawns hitting pieces, minors hitting rooks/queens
MgEg evalThreats(const Board& b){
    MgEg r;
    Bitboard occ = b.occ();
    for(int ci = 0; ci < 2; ci++){
        Color c = Color(ci);
        Color enemy = ~c;
        int sign = (c == Color::WHITE) ? 1 : -1;
        Bitboard ownPawns = b.pieces(PieceType::PAWN, c);
        Bitboard pawnAtk = pawnAttacksBB(ownPawns, c);
        Bitboard enemyMinors = b.pieces(PieceType::KNIGHT, enemy) | b.pieces(PieceType::BISHOP, enemy);
        Bitboard enemyRooks = b.pieces(PieceType::ROOK, enemy);
        Bitboard enemyQueens = b.pieces(PieceType::QUEEN, enemy);

        int hitMinor = (pawnAtk & enemyMinors).count();
        int hitRook  = (pawnAtk & enemyRooks).count();
        int hitQueen = (pawnAtk & enemyQueens).count();
        r.mg += sign * (hitMinor*PAWN_THREAT_MINOR_MG + hitRook*PAWN_THREAT_ROOK_MG + hitQueen*PAWN_THREAT_QUEEN_MG);
        r.eg += sign * (hitMinor*PAWN_THREAT_MINOR_EG + hitRook*PAWN_THREAT_ROOK_EG + hitQueen*PAWN_THREAT_QUEEN_EG);

        Bitboard minors = b.pieces(PieceType::KNIGHT, c) | b.pieces(PieceType::BISHOP, c);
        while(minors){
            int s = minors.pop();
            bool isKnight = bool(b.pieces(PieceType::KNIGHT, c) & Bitboard::fromSquare(s));
            Bitboard atk = isKnight ? attacks::knight(Square(s)) : attacks::bishop(Square(s), occ);
            int hr = (atk & enemyRooks).count();
            int hq = (atk & enemyQueens).count();
            r.mg += sign * (hr*MINOR_THREAT_ROOK_MG + hq*MINOR_THREAT_QUEEN_MG);
            r.eg += sign * (hr*MINOR_THREAT_ROOK_EG + hq*MINOR_THREAT_QUEEN_EG);
        }
    }
    return r;
}

// ---- space: safe central squares behind your own lines
MgEg evalSpace(const Board& b){
    MgEg r;
    Bitboard centerFiles = FILE_MASKS[2] | FILE_MASKS[3] | FILE_MASKS[4] | FILE_MASKS[5];
    Bitboard whiteZone = centerFiles & (RANK_MASK[1] | RANK_MASK[2] | RANK_MASK[3]);
    Bitboard blackZone = centerFiles & (RANK_MASK[4] | RANK_MASK[5] | RANK_MASK[6]);
    Bitboard blackPawnAtk = pawnAttacksBB(b.pieces(PieceType::PAWN, Color::BLACK), Color::BLACK);
    Bitboard whitePawnAtk = pawnAttacksBB(b.pieces(PieceType::PAWN, Color::WHITE), Color::WHITE);
    int wSpace = (whiteZone & ~blackPawnAtk & ~b.us(Color::BLACK)).count();
    int bSpace = (blackZone & ~whitePawnAtk & ~b.us(Color::WHITE)).count();
    r.mg += (wSpace - bSpace) * SPACE_WEIGHT;
    return r;
}

int eval(const Board& b){
    int sc = 0;
    int whiteBishops = 0, blackBishops = 0;
    int phase = 0; // 24 = full material (opening), 0 = bare-bones endgame
    int kingMg = 0, kingEg = 0;
    for(int i = 0; i < 64; i++){
        Piece p = b.at(Square(i));
        if(p == Piece::NONE) continue;
        PieceType pt = p.type();
        Color c = p.color();
        int sign = (c == Color::WHITE) ? 1 : -1;
        if(pt == PieceType::KING){
            int idx = (c == Color::WHITE) ? i : (i ^ 56);
            kingMg += sign * kingMgTable[idx];
            kingEg += sign * kingEgTable[idx];
            continue; // king material (huge sentinel value) always cancels out, skip it
        }
        sc += sign * pieceVal(pt);
        sc += pstValue(pt, i, c);
        if(pt == PieceType::BISHOP){
            if(c == Color::WHITE) whiteBishops++; else blackBishops++;
        }
        if(pt == PieceType::KNIGHT || pt == PieceType::BISHOP) phase += 1;
        else if(pt == PieceType::ROOK) phase += 2;
        else if(pt == PieceType::QUEEN) phase += 4;
    }
    if(whiteBishops >= 2) sc += 30;
    if(blackBishops >= 2) sc -= 30;
    if(phase > 24) phase = 24; // extra queens from promotion shouldn't push us "past" the opening
    sc += (kingMg * phase + kingEg * (24 - phase)) / 24;

    MgEg extra;
    extra += evalPawns(b);
    extra += evalRooksAndQueens(b);
    extra += evalMobilityKingSafety(b);
    extra += evalKingShield(b);
    extra += evalOutposts(b);
    extra += evalThreats(b);
    extra += evalSpace(b);
    sc += (extra.mg * phase + extra.eg * (24 - phase)) / 24;

    return sc;
}

bool isRep(const vector<uint64_t>& hist, uint64_t h){
    int cnt = 0;
    for(auto x : hist) if(x == h && ++cnt >= 2) return true;
    return false;
}

// ---------------------------------------------------------------------
// Static Exchange Evaluation - walks the capture sequence on a single
// square without touching the real board, to answer "if all the pieces
// pile in on this square, who comes out ahead materially?". Used to (a)
// separate winning captures from losing ones in move ordering, (b) prune
// hopeless captures in quiescence, and (c) decide which captures are safe
// to skip Late Move Reductions for in the main search.
// ---------------------------------------------------------------------
Bitboard seeAttackers(const Board& board, Square sq, Bitboard occ){
    Bitboard att = 0ULL;
    att |= attacks::pawn(Color::BLACK, sq) & board.pieces(PieceType::PAWN, Color::WHITE);
    att |= attacks::pawn(Color::WHITE, sq) & board.pieces(PieceType::PAWN, Color::BLACK);
    att |= attacks::knight(sq) & board.pieces(PieceType::KNIGHT);
    att |= attacks::king(sq) & board.pieces(PieceType::KING);
    Bitboard bishopsQueens = board.pieces(PieceType::BISHOP) | board.pieces(PieceType::QUEEN);
    Bitboard rooksQueens = board.pieces(PieceType::ROOK) | board.pieces(PieceType::QUEEN);
    att |= attacks::bishop(sq, occ) & bishopsQueens;
    att |= attacks::rook(sq, occ) & rooksQueens;
    return att & occ;
}

Square lvaSquare(const Board& board, Bitboard attackers, Color color, PieceType& outType){
    static const PieceType order[6] = {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                                        PieceType::ROOK, PieceType::QUEEN, PieceType::KING};
    for(PieceType pt : order){
        Bitboard bb = attackers & board.pieces(pt, color);
        if(bb){
            outType = pt;
            return Square(bb.lsb());
        }
    }
    outType = PieceType::NONE;
    return Square(Square::underlying::NO_SQ);
}

int see(const Board& board, Move m){
    Square from = m.from();
    Square to = m.to();
    Color us = board.sideToMove();
    Color stm = ~us;

    Bitboard occ = board.occ();
    int gain[32];
    int d = 0;

    PieceType curAttackerType = board.at<PieceType>(from);

    if(m.typeOf() == Move::ENPASSANT){
        Square capSq(us == Color::WHITE ? to.index() - 8 : to.index() + 8);
        gain[0] = seeValue(PieceType::PAWN);
        occ &= ~Bitboard::fromSquare(capSq);
    } else {
        Piece victim = board.at(to);
        gain[0] = (victim == Piece::NONE) ? 0 : seeValue(victim.type());
    }

    if(m.typeOf() == Move::PROMOTION){
        gain[0] += seeValue(m.promotionType()) - seeValue(PieceType::PAWN);
        curAttackerType = m.promotionType();
    }

    occ &= ~Bitboard::fromSquare(from);
    Bitboard attackers = seeAttackers(board, to, occ);

    while(true){
        Bitboard stmAttackers = attackers & board.us(stm) & occ;
        if(!stmAttackers) break;
        d++;
        gain[d] = seeValue(curAttackerType) - gain[d-1];
        // NOTE: no early-break here. The reference "swap algorithm" (chessprogramming.org,
        // SEE - The Swap Algorithm) only applies max(-gain[d-1], gain[d]) in the FINAL
        // fold-back loop below, after every attacker has been discovered - never as an
        // early exit inside this loop. I tested an earlier version of this function that
        // added `if(max(-gain[d-1], gain[d]) < 0) break;` here as a speed optimization, and
        // it gives a demonstrably wrong answer whenever a deeper attacker (an x-rayed piece
        // behind the current one, for instance) would have changed the optimal stopping
        // point: two rooks stacked behind each other attacking a defended queen, for
        // example, computes 400 with the early break vs the correct 500 without it - the
        // break fires before the algorithm ever finds out the second rook exists. Full
        // exploration costs a little more (bounded by the number of attackers on the
        // square, realistically 1-4), which is a good trade for not misjudging exchanges.
        PieceType lvaType;
        Square lvaSq = lvaSquare(board, stmAttackers, stm, lvaType);
        occ &= ~Bitboard::fromSquare(lvaSq);
        attackers = seeAttackers(board, to, occ); // re-derive: removing a piece can reveal an x-ray attacker behind it
        curAttackerType = lvaType;
        stm = ~stm;
    }

    // Fold the exchange back up via minimax. Written as a bounded for-loop
    // rather than the classic `while(--d)` idiom: when there are zero
    // recaptures (d stays 0, e.g. capturing into a completely undefended
    // square), `while(--d)` wraps d to -1, which is still nonzero/truthy
    // for a plain int and walks off the front of gain[] - exactly the kind
    // of one-past-the-obvious-case bug ASan catches immediately but a
    // quick read-through doesn't.
    for(int k = d; k >= 1; k--) gain[k-1] = -max(-gain[k-1], gain[k]);
    return gain[0];
}

// ---------------------------------------------------------------------
// Time management - lets the search bail out cleanly once cutechess's
// clock says we should move, instead of blowing past our time budget.
//
// These are atomics because, with pondering, the search runs on its own
// thread while the main thread keeps reading stdin - a "ponderhit" needs
// to reset the clock and swap in the real time budget while the search
// is still running, and "stop" needs to be visible to the search thread
// immediately. searchStartMs is stored as an offset from a fixed
// programStart reference (rather than a raw time_point) purely so it can
// live in a plain atomic<long long> without worrying about whether
// atomic<time_point> is lock-free/well-behaved on a given platform.
// ---------------------------------------------------------------------
chrono::steady_clock::time_point programStart;
atomic<long long> searchStartMs{0};
atomic<long long> timeLimitMs{0};
atomic<long long> softLimitMs{0};
atomic<bool> stopSearch{false};
atomic<bool> isPondering{false};
long long nodeCount = 0; // search-thread-only: never touched by main() while a search is in flight
mutex outputMutex;       // keeps "info"/"bestmove"/"readyok" lines from interleaving across threads

long long nowMs(){
    return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - programStart).count();
}

bool outOfTime(){
    long long lim = timeLimitMs.load();
    if(lim <= 0) return false; // 0 or negative = no time limit (fixed-depth search)
    return (nowMs() - searchStartMs.load()) >= lim;
}

const int MAX_PLY = 300;
Move killers[MAX_PLY][2];
int history[64][64];
Move counterMoveTable[12][64];
int contHistory[12][64][12][64];

Move plyMove[MAX_PLY];   // the move that was played to reach this ply (set by the parent)
Piece plyPiece[MAX_PLY]; // the (colored) piece that made that move - only valid when plyMove[ply] != NO_MOVE

const int HISTORY_MAX = 16384;
inline void updateHistory(int& val, int bonus){
    bonus = std::clamp(bonus, -HISTORY_MAX, HISTORY_MAX);
    val += bonus - val * abs(bonus) / HISTORY_MAX;
}

double lmrTable[64][64];
void initLMR(){
    for(int d = 1; d < 64; d++)
        for(int mv = 1; mv < 64; mv++)
            lmrTable[d][mv] = 0.75 + log((double)d) * log((double)mv) / 2.25;
}
int lmrReduction(int depth, int moveIndex){
    int d = min(depth, 63), mv = min(moveIndex, 63);
    if(d < 1 || mv < 1) return 0;
    return (int)lmrTable[d][mv];
}

bool hasNonPawnMaterial(Board& board, Color c){
    return bool(board.pieces(PieceType::KNIGHT, c)) || bool(board.pieces(PieceType::BISHOP, c)) ||
           bool(board.pieces(PieceType::ROOK, c)) || bool(board.pieces(PieceType::QUEEN, c));
}

void clearSearchState(){
    currentGeneration++;
    for(int i = 0; i < MAX_PLY; i++){
        killers[i][0] = Move::NO_MOVE;
        killers[i][1] = Move::NO_MOVE;
        plyMove[i] = Move::NO_MOVE;
    }
    memset(history, 0, sizeof(history));
    memset(counterMoveTable, 0, sizeof(counterMoveTable));
    memset(contHistory, 0, sizeof(contHistory));
}

// Scores each move once (captures get a real SEE call, quiets get
// history+continuation-history), then permutes the list by that score.
// The old version recomputed its comparator's lambda on every pairwise
// sort comparison, which is fine for cheap heuristics but would have
// meant calling SEE O(n log n) times per node instead of O(n).
void orderMoves(Board& board, Movelist& moves, Move pvMove, int ply, Move counterMv){
    int n = moves.size();
    vector<long long> scores(n);
    Move prevMove = (ply >= 0 && ply < MAX_PLY) ? plyMove[ply] : Move::NO_MOVE;
    Piece prevPiece = (ply >= 0 && ply < MAX_PLY && prevMove != Move::NO_MOVE) ? plyPiece[ply] : Piece::NONE;

    for(int i = 0; i < n; i++){
        Move& m = moves[i];
        long long s;
        if(m == pvMove){
            s = 1000000000LL;
        } else if(board.isCapture(m)){
            int sv = see(board, m);
            m.setScore((int16_t)std::clamp(sv, -32000, 32000)); // cache it - the main search reuses this instead of calling see() again
            Piece victim = board.at(m.to());
            int victimVal = (victim == Piece::NONE) ? 100 : pieceVal(victim.type()); // en passant: captured pawn isn't on m.to()
            Piece attacker = board.at(m.from());
            long long mvvlva = (long long)victimVal * 10 - seeValue(attacker.type());
            s = (sv >= 0) ? (100000000LL + (long long)sv * 1000 + mvvlva)
                          : (-100000000LL + (long long)sv * 1000 + mvvlva);
        } else if(m.typeOf() == Move::PROMOTION && m.promotionType() == PieceType::QUEEN){
            s = 90000000LL;
        } else if(ply >= 0 && ply < MAX_PLY && killers[ply][0] == m){
            s = 80000000LL;
        } else if(ply >= 0 && ply < MAX_PLY && killers[ply][1] == m){
            s = 79000000LL;
        } else if(counterMv != Move::NO_MOVE && m == counterMv){
            s = 78000000LL;
        } else {
            PieceType pt = board.at<PieceType>(m.from());
            long long hs = history[m.from().index()][m.to().index()];
            if(prevMove != Move::NO_MOVE){
                Piece curFull(pt, board.sideToMove());
                hs += contHistory[int(prevPiece)][prevMove.to().index()][int(curFull)][m.to().index()];
            }
            s = hs;
        }
        scores[i] = s;
    }

    vector<int> idx(n);
    for(int i = 0; i < n; i++) idx[i] = i;
    sort(idx.begin(), idx.end(), [&](int a, int b){ return scores[a] > scores[b]; });

    vector<Move> tmp(n);
    for(int i = 0; i < n; i++) tmp[i] = moves[idx[i]];
    for(int i = 0; i < n; i++) moves[i] = tmp[i];
}

// ---------------------------------------------------------------------
// Quiescence search - resolves captures (and check evasions) past the
// main search's horizon before trusting the static eval. Without this, //
// the engine will happily grab any capture that nets material *right at*
// the depth cutoff, because it never looks far enough to see the
// recapture. This was the single biggest source of blunders.
// ---------------------------------------------------------------------
const int DELTA_MARGIN = 200;
const int QSEARCH_PLY_CAP = 250; // hard safety net against runaway recursion

int quiesce(Board& board, int alpha, int beta, bool maxing, int ply, vector<uint64_t>& hist){
    if(stopSearch) return 0;
    if(((++nodeCount) & 2047) == 0 && outOfTime()){ stopSearch = true; return 0; }

    uint64_t h = board.hash();
    if(ply > 0 && isRep(hist, h)) return 0;

    auto game = board.isGameOver();
    if(game.second == GameResult::LOSE) return maxing ? -(MATE - ply) : (MATE - ply);
    if(game.second == GameResult::DRAW) return 0;

    bool inCheck = board.inCheck();
    int standPat = eval(board);
    if(!inCheck){
        if(maxing){
            if(standPat >= beta) return standPat;
            if(standPat > alpha) alpha = standPat;
        } else {
            if(standPat <= alpha) return standPat;
            if(standPat < beta) beta = standPat;
        }
    }
    if(ply >= QSEARCH_PLY_CAP) return standPat;

    Movelist moves;
    if(inCheck) movegen::legalmoves(moves, board); // must consider every evasion, can't "stand pat" in check
    else movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, board);
    if(inCheck && moves.empty()) return maxing ? -(MATE - ply) : (MATE - ply); // checkmated mid-qsearch

    orderMoves(board, moves, Move::NO_MOVE, ply, Move::NO_MOVE);

    int best = inCheck ? (maxing ? -INF : INF) : standPat;
    hist.push_back(h);
    for(auto& m : moves){
        if(!inCheck && board.isCapture(m)){
            Piece victim = board.at(m.to());
            int victimVal = (victim == Piece::NONE) ? pieceVal(PieceType::PAWN) : pieceVal(victim.type());
            // delta pruning: even winning this piece outright can't raise alpha (or lower beta) - skip it
            if(maxing && standPat + victimVal + DELTA_MARGIN < alpha) continue;
            if(!maxing && standPat - victimVal - DELTA_MARGIN > beta) continue;
            // SEE pruning: don't bother recapturing into a losing exchange
            if(see(board, m) < 0) continue;
        }
        board.makeMove(m);
        int val = quiesce(board, alpha, beta, !maxing, ply + 1, hist);
        board.unmakeMove(m);
        if(maxing){
            if(val > best) best = val;
            if(best > alpha) alpha = best;
        } else {
            if(val < best) best = val;
            if(best < beta) beta = best;
        }
        if(beta <= alpha) break;
        if(stopSearch) break;
    }
    hist.pop_back();
    return best;
}

//tt is pretty new to me, so implemented using AI
const int MAX_CHECK_EXTENSIONS = 16; // per search path, not per node - see note at the extension site below

int alphaBeta(Board& board, int depth, int alpha, int beta, bool maxing, int ply, vector<uint64_t>& hist,
               bool prevNull = false, int extCount = 0){
    if(stopSearch) return 0;
    if(((++nodeCount) & 2047) == 0 && outOfTime()){ stopSearch = true; return 0; }

    uint64_t h = board.hash();
    int oldAlpha = alpha;
    if(ply > 0 && isRep(hist, h)) return 0;

    Move pv = Move::NO_MOVE;
    TTEntry& e = tt[h & (TT_SIZE-1)];
    if(e.depth >= 0 && e.hash == h){
        pv = e.best;
        if(e.depth >= depth){
            int s = e.score;
            if(s > MATE_THRESH) s -= ply;
            else if(s < -MATE_THRESH) s += ply;
            if(e.flag == EXACT) return s;
            if(e.flag == LOWER) alpha = max(alpha, s);
            if(e.flag == UPPER) beta = min(beta, s);
            if(alpha >= beta) return s;
        }
    }

    auto game = board.isGameOver();
    if(game.second == GameResult::LOSE){
        int val = maxing ? -(MATE - ply) : (MATE - ply);
        ttStore(h, maxing ? -MATE : MATE, depth, EXACT, Move::NO_MOVE);
        return val;
    }
    if(game.second == GameResult::DRAW){
        ttStore(h, 0, depth, EXACT, Move::NO_MOVE);
        return 0;
    }

    if(depth <= 0){
        int v = quiesce(board, alpha, beta, maxing, ply, hist);
        ttStore(h, v, 0, EXACT, Move::NO_MOVE);
        return v;
    }

    bool inCheck = board.inCheck();

    // Static eval, computed once and shared by the pruning heuristics below
    // (reverse futility, futility, late move pruning) that only fire at low
    // depth - not paid for at all once depth is high enough that none of
    // them apply.
    const int PRUNE_MAX_DEPTH = 8;
    int staticEval = 0;
    bool haveStaticEval = false;
    if(!inCheck && depth <= PRUNE_MAX_DEPTH && ply > 0){
        staticEval = eval(board);
        haveStaticEval = true;
    }

    // Reverse futility pruning (a.k.a. static null move pruning): if the
    // static eval already clears beta by a margin that grows with depth,
    // trust it and skip the node entirely - cheaper than null-move pruning
    // since it doesn't even need a reduced search. Same "don't bet on a
    // shaky position" guards as null-move (not in check, not near mate).
    const int RFP_MARGIN = 90;
    if(haveStaticEval && beta < MATE_THRESH && alpha > -MATE_THRESH){
        if(maxing && staticEval - RFP_MARGIN * depth >= beta) return staticEval;
        if(!maxing && staticEval + RFP_MARGIN * depth <= alpha) return staticEval;
    }

    // Razoring: the mirror image of RFP - if static eval is already well
    // below alpha at very shallow depth, the position is probably lost
    // regardless of what a full search finds. Unlike RFP, don't trust that
    // blindly: verify with a quiescence search first, since static eval
    // alone can miss a tactical shot that would still save the position.
    const int RAZOR_MAX_DEPTH = 3;
    const int RAZOR_MARGIN = 200;
    if(haveStaticEval && depth <= RAZOR_MAX_DEPTH && beta < MATE_THRESH && alpha > -MATE_THRESH){
        bool wouldRazor = maxing ? (staticEval + RAZOR_MARGIN * depth <= alpha)
                                  : (staticEval - RAZOR_MARGIN * depth >= beta);
        if(wouldRazor){
            int qval = quiesce(board, alpha, beta, maxing, ply, hist);
            bool stillFails = maxing ? (qval <= alpha) : (qval >= beta);
            if(stillFails) return qval;
        }
    }

    // Null-move pruning: let the side to move "pass" and search shallower.
    // If we're doing so well that even a free tempo for the opponent can't
    // stop us beating beta, assume a real move does at least as well and
    // prune. Skipped in check, near mate scores, with only king+pawns left
    // (zugzwang risk), and right after another null move.
    if(!inCheck && !prevNull && depth >= 3 && ply > 0 && beta < MATE_THRESH && alpha > -MATE_THRESH &&
       hasNonPawnMaterial(board, board.sideToMove())){
        int R = 2 + (depth >= 6 ? 1 : 0);
        board.makeNullMove();
        if(ply + 1 < MAX_PLY) plyMove[ply+1] = Move::NO_MOVE; // a null move isn't a real continuation - don't let contHistory/countermove read through it
        int nullScore = maxing ? alphaBeta(board, depth-1-R, beta-1, beta, false, ply+1, hist, true, extCount)
                                : alphaBeta(board, depth-1-R, alpha, alpha+1, true, ply+1, hist, true, extCount);
        board.unmakeNullMove();
        if(stopSearch) return 0;
        if(maxing && nullScore >= beta) return nullScore;
        if(!maxing && nullScore <= alpha) return nullScore;
    }

    // Internal Iterative Reduction: no TT move means we're about to search
    // this node with worse-than-usual ordering. Rather than a full separate
    // shallow search first (classic IID - real cost, and modern engines
    // have mostly moved away from it now that history/countermove/cont-
    // history usually give decent ordering on their own), just shave a ply
    // off here. Cheap, and it stops a TT-miss at high depth from paying
    // full price purely because it lacked a good first move to try.
    if(pv == Move::NO_MOVE && depth >= 4 && !inCheck && ply > 0){
        depth -= 1;
    }

    Move counterMv = Move::NO_MOVE;
    if(ply > 0 && ply < MAX_PLY && plyMove[ply] != Move::NO_MOVE){
        counterMv = counterMoveTable[int(plyPiece[ply])][plyMove[ply].to().index()];
    }

    Movelist moves;
    movegen::legalmoves(moves, board);
    orderMoves(board, moves, pv, ply, counterMv);

    int best = maxing ? -INF : INF;
    Move bestmv = moves.empty() ? Move::NO_MOVE : moves[0];
    hist.push_back(h);

    vector<pair<Move,Piece>> quietsTried;
    quietsTried.reserve(moves.size());

    int moveIndex = 0;
    const int FUTILITY_MARGIN = 120;
    const int LMP_BASE = 5;
    for(auto& m : moves){
        bool isCap = board.isCapture(m);
        bool isPromo = (m.typeOf() == Move::PROMOTION);

        // Futility pruning + late move pruning: quiet, non-promotion moves
        // past the first one, at low depth, when we're not in check. Both
        // skip the move without even making it - cheaper than LMR, which
        // still pays for a reduced search.
        if(haveStaticEval && !isCap && !isPromo && !(m == pv) && moveIndex >= 1){
            // Futility: even the most optimistic quiet move at this depth
            // can't plausibly close a gap this size - don't bother.
            bool futile = maxing ? (staticEval + FUTILITY_MARGIN * depth <= alpha)
                                  : (staticEval - FUTILITY_MARGIN * depth >= beta);
            // Late move pruning: this many quiet moves, ordered by
            // killers/history/countermove, already tried at this shallow a
            // depth without a cutoff - later ones are unlikely to fare
            // better, so stop searching them rather than just reducing.
            bool tooLate = moveIndex >= LMP_BASE + 2 * depth * depth;
            if(futile || tooLate){ moveIndex++; continue; }
        }

        bool isBadCapture = isCap && m.score() < 0; // score() was set by orderMoves' SEE call - avoids recomputing it
        PieceType movedPT = board.at<PieceType>(m.from());
        Color moverColor = board.sideToMove();
        Piece movedPieceFull(movedPT, moverColor);

        bool canReduce = depth >= 3 && moveIndex >= 2 && !inCheck && !isPromo &&
                          (!isCap || isBadCapture) && !(m == pv);

        board.makeMove(m);
        bool givesCheck = board.inCheck();
        // Check extension, capped per search path rather than by a loose
        // global ply ceiling - a run of "spite checks" that don't lead
        // anywhere can otherwise blow up node counts for no benefit.
        // MAX_CHECK_EXTENSIONS is generous (16 - deeper than essentially
        // any real mating net), so this is a safety valve, not a
        // restriction on normal tactical extension.
        int ext = (givesCheck && extCount < MAX_CHECK_EXTENSIONS) ? 1 : 0;
        int newDepth = depth - 1 + ext;
        int childExtCount = extCount + ext;
        if(ext != 0) canReduce = false; // don't reduce a move we just decided to extend

        if(ply + 1 < MAX_PLY){
            plyMove[ply+1] = m;
            plyPiece[ply+1] = movedPieceFull;
        }

        int val;
        if(canReduce){
            int R = lmrReduction(depth, moveIndex + 1);
            if(m == killers[ply][0] || m == killers[ply][1] || m == counterMv) R = max(0, R - 1);
            R = min(R, newDepth - 1);
            if(R > 0){
                int reducedDepth = newDepth - R;
                val = alphaBeta(board, reducedDepth, alpha, beta, !maxing, ply+1, hist, false, childExtCount);
                bool improved = !stopSearch && (maxing ? (val > alpha) : (val < beta));
                if(improved) val = alphaBeta(board, newDepth, alpha, beta, !maxing, ply+1, hist, false, childExtCount);
            } else {
                val = alphaBeta(board, newDepth, alpha, beta, !maxing, ply+1, hist, false, childExtCount);
            }
        } else {
            val = alphaBeta(board, newDepth, alpha, beta, !maxing, ply+1, hist, false, childExtCount);
        }

        board.unmakeMove(m);

        if(maxing){
            if(val > best){ best = val; bestmv = m; }
            alpha = max(alpha, best);
        } else {
            if(val < best){ best = val; bestmv = m; }
            beta = min(beta, best);
        }

        if(beta <= alpha){
            if(!isCap && ply < MAX_PLY){
                if(!(killers[ply][0] == m)){
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = m;
                }
                int bonus = depth * depth;
                updateHistory(history[m.from().index()][m.to().index()], bonus);
                if(plyMove[ply] != Move::NO_MOVE){
                    updateHistory(contHistory[int(plyPiece[ply])][plyMove[ply].to().index()][int(movedPieceFull)][m.to().index()], bonus);
                    counterMoveTable[int(plyPiece[ply])][plyMove[ply].to().index()] = m;
                }
                // malus: quiet moves we already tried at this node clearly weren't as good as this one
                for(auto& qp : quietsTried){
                    updateHistory(history[qp.first.from().index()][qp.first.to().index()], -bonus);
                    if(plyMove[ply] != Move::NO_MOVE){
                        updateHistory(contHistory[int(plyPiece[ply])][plyMove[ply].to().index()][int(qp.second)][qp.first.to().index()], -bonus);
                    }
                }
            }
            break;
        }
        if(!isCap) quietsTried.push_back({m, movedPieceFull});
        if(stopSearch) break;
        moveIndex++;
    }
    hist.pop_back();
    if(stopSearch) return best;

    int stored = best;
    if(best > MATE_THRESH) stored = best + ply;
    else if(best < -MATE_THRESH) stored = best - ply;
    int flag;
    if(best <= oldAlpha) flag = UPPER;
    else if(best >= beta) flag = LOWER;
    else flag = EXACT;
    ttStore(h, stored, depth, flag, bestmv);
    return best;
}

// windowAlpha/windowBeta let iterativeDeepening pass a narrow aspiration
// window instead of always searching (-INF, INF) from scratch.
pair<string,int> getBestMove(Board& board, int depth, vector<uint64_t>& hist, int windowAlpha, int windowBeta){
    Movelist moves;
    movegen::legalmoves(moves, board);
    if(moves.empty()) return {"", 0};
    bool white = board.sideToMove() == Color::WHITE;
    Move bestM = moves[0];
    int bestVal = white ? -INF : INF;
    Move pv = Move::NO_MOVE;
    TTEntry& e = tt[board.hash() & (TT_SIZE-1)];
    if(e.depth >= 0 && e.hash == board.hash()) pv = e.best;
    orderMoves(board, moves, pv, 0, Move::NO_MOVE);

    // Full PVS at the root: the first move (almost always the TT/PV move)
    // gets the full aspiration window; every later move is scouted with a
    // 1-point window first and only gets a full re-search if it actually
    // looks like it might beat what we already have. Cheaper than the
    // previous version's plain alpha-beta narrowing, same exact result.
    int alpha = windowAlpha, beta = windowBeta;
    bool first = true;
    hist.push_back(board.hash());
    for(auto& m : moves){
        if(stopSearch) break;

        if(1 < MAX_PLY){ plyMove[1] = m; plyPiece[1] = Piece(board.at<PieceType>(m.from()), board.sideToMove()); }

        board.makeMove(m);
        int val;
        if(first){
            val = alphaBeta(board, depth-1, alpha, beta, !white, 1, hist, false, 0);
        } else {
            if(white) val = alphaBeta(board, depth-1, alpha, alpha+1, false, 1, hist, false, 0);
            else       val = alphaBeta(board, depth-1, beta-1, beta, true, 1, hist, false, 0);

            bool promising = white ? (val > alpha) : (val < beta);
            if(promising){
                val = alphaBeta(board, depth-1, alpha, beta, !white, 1, hist, false, 0);
            }
        }
        board.unmakeMove(m);
        if(stopSearch) break; // this move's score may be from an aborted search - don't trust it

        if(first || (white ? val > bestVal : val < bestVal)){
            bestVal = val;
            bestM = m;
        }
        if(white) alpha = max(alpha, bestVal);
        else      beta  = min(beta, bestVal);
        first = false;
    }
    hist.pop_back();
    if(stopSearch) return {"", 0}; // signals an incomplete iteration to the caller
    return {uci::moveToUci(bestM), bestVal};
}

// ---------------------------------------------------------------------
// Iterative deepening driver - repeatedly calls getBestMove() at
// increasing depth until the time budget runs out, keeping only the
// last fully-completed depth's result.
// ---------------------------------------------------------------------
string iterativeDeepening(Board& board, vector<uint64_t>& hist, int maxDepth){
    searchStartMs = nowMs();
    stopSearch = false;
    nodeCount = 0;
    for(int i = 0; i < MAX_PLY; i++){
        killers[i][0] = Move::NO_MOVE;
        killers[i][1] = Move::NO_MOVE;
    }

    string bestMoveStr = "";
    int bestVal = 0;
    bool haveScore = false;
    long long lastIterMs = 0;
    const int ASP_WINDOW = 25;

    for(int d = 1; d <= maxDepth; d++){
        if(outOfTime()) break;

        // Predictive stop: if the last iteration already took long enough
        // that even a conservative estimate of the next one (2x - real
        // growth is often more, but this stays safe when ordering is
        // good and iterations grow slowly) won't fit in the soft budget,
        // don't start it. A started-but-aborted iteration contributes
        // nothing (its result is discarded below) - it only burns clock
        // time that could have gone toward later moves in the game.
        long long sLim = softLimitMs.load();
        if(d > 1 && lastIterMs > 0 && sLim > 0){
            long long elapsedSoFar = nowMs() - searchStartMs.load();
            if(elapsedSoFar + lastIterMs * 2 > sLim) break;
        }

        int alpha = -INF, beta = INF;
        if(haveScore && d >= 2 && abs(bestVal) < MATE_THRESH - 1000){
            alpha = bestVal - ASP_WINDOW;
            beta  = bestVal + ASP_WINDOW;
        }

        auto iterStart = chrono::steady_clock::now();
        pair<string,int> result;
        while(true){
            result = getBestMove(board, d, hist, alpha, beta);
            if(stopSearch || result.first.empty()) break;
            if(alpha > -INF && result.second <= alpha){ alpha = -INF; continue; } // fail low - widen and retry this depth
            if(beta  <  INF && result.second >= beta){  beta  = INF;  continue; } // fail high - widen and retry this depth
            break;
        }
        if(stopSearch || result.first.empty()) break;
        lastIterMs = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - iterStart).count();

        bestMoveStr = result.first;
        bestVal = result.second;
        haveScore = true;

        long long elapsed = nowMs() - searchStartMs.load();
        int uciScore = (board.sideToMove() == Color::WHITE) ? bestVal : -bestVal;
        {
            lock_guard<mutex> lock(outputMutex);
            cout << "info depth " << d << " score ";
            if(uciScore > MATE_THRESH){
                int mateInMoves = (MATE - uciScore + 1) / 2;
                cout << "mate " << mateInMoves;
            } else if(uciScore < -MATE_THRESH){
                int mateInMoves = (MATE + uciScore + 1) / 2;
                cout << "mate " << -mateInMoves;
            } else {
                cout << "cp " << uciScore;
            }
            cout << " nodes " << nodeCount << " time " << elapsed << " pv " << bestMoveStr << "\n";
        }

        if(abs(bestVal) >= MATE_THRESH) break; // found a forced mate, no point going deeper
    }
    return bestMoveStr;
}

// ---------------------------------------------------------------------
// UCI protocol plumbing
// ---------------------------------------------------------------------
Board board;
vector<uint64_t> gameHistory; // real-game position hashes played so far (excludes current position)

long long moveBudget(int rem, int inc, int movestogo){
    if(rem <= 0) return 50; // no clock info / already flagging - just move fast
    int divisor = movestogo > 0 ? movestogo : 30;
    long long t = (long long)rem / divisor + (long long)inc * 3 / 4;
    t = min(t, (long long)rem / 5);
    t -= 30; // small safety buffer for I/O overhead
    return max(10LL, t);
}

void parsePosition(const string &command){
    stringstream ss(command);
    string token;
    ss >> token; // "position"
    ss >> token; // "startpos" or "fen"
    gameHistory.clear();
    if(token == "startpos"){
        board.setFen(constants::STARTPOS);
    } else if(token == "fen"){
        string fen;
        for(int i = 0; i < 6; i++){ ss >> token; fen += token; if(i != 5) fen += " "; }
        board.setFen(fen);
    }
    gameHistory.push_back(board.hash());
    if(ss >> token && token == "moves"){
        while(ss >> token){
            Move move = uci::uciToMove(board, token);
            if(move == Move::NO_MOVE) break;
            board.makeMove(move);
            gameHistory.push_back(board.hash());
        }
    }
    gameHistory.pop_back(); // getBestMove() re-pushes the current position itself
}

int wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0;
int movetimeArg = -1, depthArg = -1;
bool hasTimeInfo = false;
bool infiniteMode = false;
bool ponderMode = false;

void parseGo(const string &command){
    stringstream ss(command);
    string token;
    ss >> token; // "go"
    wtime = btime = winc = binc = movestogo = 0;
    movetimeArg = -1; depthArg = -1; hasTimeInfo = false; infiniteMode = false; ponderMode = false;
    while(ss >> token){
        if(token == "wtime"){ ss >> wtime; hasTimeInfo = true; }
        else if(token == "btime"){ ss >> btime; hasTimeInfo = true; }
        else if(token == "winc"){ ss >> winc; }
        else if(token == "binc"){ ss >> binc; }
        else if(token == "movestogo"){ ss >> movestogo; }
        else if(token == "movetime"){ ss >> movetimeArg; }
        else if(token == "depth"){ ss >> depthArg; }
        else if(token == "infinite"){ infiniteMode = true; }
        else if(token == "ponder"){ ponderMode = true; }
        // "searchmoves" etc. is accepted but not specially handled
    }
}

// After a search, the TT entry for "board after bestMove" usually still
// holds that position's own best move (populated during the search that
// just ran, since evaluating bestMove's subtree necessarily searched it).
// That's what most engines suggest as the ponder move - it's exactly what
// we predict the opponent will reply with. Verified against legalmoves()
// before trusting it: a TT hash-collision handing back an illegal or
// wrong-position move here would otherwise go straight into "bestmove ...
// ponder <garbage>", which could confuse or crash a GUI.
string extractPonderMove(Board boardCopy, const string& bestMoveUci){
    Move best = uci::uciToMove(boardCopy, bestMoveUci);
    if(best == Move::NO_MOVE) return "";
    boardCopy.makeMove(best);

    TTEntry& e = tt[boardCopy.hash() & (TT_SIZE - 1)];
    if(e.depth < 0 || e.hash != boardCopy.hash() || e.best == Move::NO_MOVE) return "";

    Movelist legal;
    movegen::legalmoves(legal, boardCopy);
    for(auto& m : legal){
        if(m == e.best) return uci::moveToUci(e.best);
    }
    return "";
}

// ---------------------------------------------------------------------
// Runs on its own thread so the main loop can keep reading stdin (and
// answering "isready", "stop", "ponderhit") while a search is in flight.
// Takes its own copies of the board and history - the main thread must
// never touch the shared `board`/`gameHistory` while a search thread is
// active (every command handler in main() enforces this by stopping and
// joining any running search first).
// ---------------------------------------------------------------------
void searchWorker(int maxDepth, Board boardCopy, vector<uint64_t> histCopy){
    string bestMoveStr = iterativeDeepening(boardCopy, histCopy, maxDepth);

    // UCI: must not send bestmove during pondering until the GUI says what
    // actually happened, even if our own search already "finished" (hit
    // max depth, found a mate) while there was no real time limit yet.
    while(isPondering.load() && !stopSearch.load()){
        this_thread::sleep_for(chrono::milliseconds(1));
    }

    if(bestMoveStr.empty()){
        Movelist moves;
        movegen::legalmoves(moves, boardCopy);
        if(!moves.empty()) bestMoveStr = uci::moveToUci(moves[0]);
    }

    string ponderMoveStr = bestMoveStr.empty() ? "" : extractPonderMove(boardCopy, bestMoveStr);

    lock_guard<mutex> lock(outputMutex);
    if(bestMoveStr.empty()){
        cout << "bestmove 0000" << endl;
    } else if(!ponderMoveStr.empty()){
        cout << "bestmove " << bestMoveStr << " ponder " << ponderMoveStr << endl;
    } else {
        cout << "bestmove " << bestMoveStr << endl;
    }
}

int main() {
    cout << unitbuf; // flush after every write - GUIs expect "info" lines live, not batched at the end
    programStart = chrono::steady_clock::now();
    initEvalMasks();
    initLMR();
    board.setFen(constants::STARTPOS);
    gameHistory.push_back(board.hash());

    thread searchThread;
    long long realSoftLimitMs = 0, realHardLimitMs = 0; // stashed budget while pondering, applied at ponderhit

    // Stops and joins any in-flight search. Every handler that touches
    // shared state (board, gameHistory, TT, killers, ...) calls this
    // first - it's what makes it safe for those to stay unsynchronized.
    auto stopAndJoin = [&](){
        if(searchThread.joinable()){
            stopSearch = true;
            isPondering = false;
            searchThread.join();
        }
    };

    string line;
    while(getline(cin, line)){
        if(line == "uci"){
            lock_guard<mutex> lock(outputMutex);
            cout << "id name lol\n";
            cout << "id author Karmanya\n";
            cout << "option name Ponder type check default true\n";
            cout << "uciok" << endl;
        } else if(line == "isready"){
            // Deliberately answered immediately even mid-search - that's
            // the whole point of moving the search to its own thread.
            lock_guard<mutex> lock(outputMutex);
            cout << "readyok" << endl;
        } else if(line == "ucinewgame"){
            stopAndJoin();
            board.setFen(constants::STARTPOS);
            gameHistory.clear();
            gameHistory.push_back(board.hash());
            clearSearchState();
        } else if(line.rfind("position", 0) == 0){
            stopAndJoin();
            parsePosition(line);
        } else if(line.rfind("go", 0) == 0){
            stopAndJoin(); // defensive - a compliant GUI won't send "go" while we're still thinking
            parseGo(line);

            int maxDepth = MAX_SEARCH_DEPTH;
            long long soft, hard;
            if(depthArg > 0){
                maxDepth = min(depthArg, MAX_SEARCH_DEPTH);
                soft = hard = 24LL * 60 * 60 * 1000; // depth is the limiting factor, not time
            } else if(movetimeArg >= 0){
                soft = hard = max(10, movetimeArg - 30); // exact target, no soft/hard split needed
            } else if(infiniteMode){
                soft = hard = 24LL * 60 * 60 * 1000; // no clock cutoff; only depth/mate will stop us
            } else if(hasTimeInfo){
                int rem = (board.sideToMove() == Color::WHITE) ? wtime : btime;
                int inc = (board.sideToMove() == Color::WHITE) ? winc : binc;
                soft = moveBudget(rem, inc, movestogo);
                // Hard ceiling: let a genuinely hard position use extra time (up to
                // 4x the normal budget) rather than cut off exactly at the soft
                // target, but never risk the actual clock - stay well inside what's
                // really left regardless of what the multiplier suggests.
                hard = min((long long)rem - 30, soft * 4);
                if(hard < soft) hard = soft; // pathologically low rem: moveBudget() already handled the safety floor
            } else {
                soft = hard = 3000; // no clock info given at all - safe default
            }

            stopSearch = false;
            if(ponderMode){
                // We're thinking on the opponent's time - none of the real
                // clock has been spent yet, so search as good as unbounded
                // and stash the real budget for "ponderhit" to apply the
                // moment it actually starts counting.
                realSoftLimitMs = soft;
                realHardLimitMs = hard;
                softLimitMs = timeLimitMs = 24LL * 60 * 60 * 1000;
                isPondering = true;
            } else {
                softLimitMs = soft;
                timeLimitMs = hard;
                isPondering = false;
            }

            vector<uint64_t> histCopy = gameHistory;
            searchThread = thread(searchWorker, maxDepth, board, histCopy);
        } else if(line == "stop"){
            stopAndJoin(); // blocks briefly (search checks stopSearch every ~2048 nodes) until bestmove is printed
        } else if(line.rfind("ponderhit", 0) == 0){
            // The predicted move was actually played: start the real clock
            // now and switch the running search over to the real budget
            // computed back when "go ponder" arrived. The search keeps
            // running (on its own thread) and reports bestmove whenever it
            // naturally finishes under that budget - nothing to join here.
            searchStartMs = nowMs();
            timeLimitMs = realHardLimitMs;
            softLimitMs = realSoftLimitMs;
            isPondering = false;
        } else if(line == "quit"){
            stopAndJoin();
            break;
        }
    }
    stopAndJoin(); // stdin can close (EOF) without an explicit "quit" - don't destroy a joinable thread
    return 0;
}