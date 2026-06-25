#include "chess.hpp"
#include <bits/stdc++.h>

using namespace std;
using namespace chess;

#define INF 10000000
#define MATE 10000000
#define MATE_THRESH 9000000

// have fun changing this hehe, here WBW is 3 plies, so if u want 3 moves like WBWBW then put depth as 5
int ENGINE_DEPTH = 5;
int printDepth = 5;



enum { EXACT, LOWER, UPPER };

struct TTEntry {
    uint64_t hash = 0;
    int score = 0;
    int depth = -1;
    int flag = EXACT;
    Move best = Move::NO_MOVE;
};

#define TT_SIZE (1 << 22)
TTEntry tt[TT_SIZE];

int pieceVal(PieceType pt){
    if(pt == PieceType::PAWN)   return 100;
    if(pt == PieceType::KNIGHT) return 320;
    if(pt == PieceType::BISHOP) return 330;
    if(pt == PieceType::ROOK)   return 500;
    if(pt == PieceType::QUEEN)  return 900;
    if(pt == PieceType::KING)   return 1000000;
    return 0;
}

int eval(const Board& b){
    int sc = 0;
    for(int i = 0; i < 64; i++){
        Piece p = b.at(Square(i));
        if(p == Piece::NONE) continue;
        sc += (p.color() == Color::WHITE ? 1 : -1) * pieceVal(p.type());
    }
    return sc;
}

bool isRep(const vector<uint64_t>& hist, uint64_t h){
    int cnt = 0;
    for(auto x : hist) if(x == h && ++cnt >= 2) return true;
    return false;
}


//tt is pretty new to me, so implemented using AI
int alphaBeta(Board& board, int depth, int alpha, int beta, bool maxing, int ply, vector<uint64_t>& hist){
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
        e = {h, maxing ? -MATE : MATE, depth, EXACT, Move::NO_MOVE};
        return val;
    }
    if(game.second == GameResult::DRAW){
        e = {h, 0, depth, EXACT, Move::NO_MOVE};
        return 0;
    }
    if(depth == 0){
        int v = eval(board);
        e = {h, v, 0, EXACT, Move::NO_MOVE};
        return v;
    }

    Movelist moves;
    movegen::legalmoves(moves, board);

    sort(moves.begin(), moves.end(), [&](Move a, Move b){
        int sa = 0, sb = 0;

        if(a == pv) sa = 2000000;
        else if(board.isCapture(a)){
            Piece v = board.at(a.to());
            Piece at = board.at(a.from());
            sa = 1000000 + pieceVal(v.type()) * 10 - pieceVal(at.type());
        }

        if(b == pv) sb = 2000000;
        else if(board.isCapture(b)){
            Piece v = board.at(b.to());
            Piece at2 = board.at(b.from());
            sb = 1000000 + pieceVal(v.type()) * 10 - pieceVal(at2.type());
        }

        return sa > sb;
    });

    int best = maxing ? -INF : INF;
    Move bestmv = moves.empty() ? Move::NO_MOVE : moves[0];

    hist.push_back(h);
    for(auto& m : moves){
        board.makeMove(m);
        int val = alphaBeta(board, depth-1, alpha, beta, !maxing, ply+1, hist);
        board.unmakeMove(m);

        if(maxing){
            if(val > best){ best = val; bestmv = m; }
            alpha = max(alpha, best);
        }
        else{
            if(val < best){ best = val; bestmv = m; }
            beta = min(beta, best);
        }
        if(beta <= alpha) break;
    }
    hist.pop_back();

    int stored = best;
    if(best > MATE_THRESH) stored = best + ply;
    else if(best < -MATE_THRESH) stored = best - ply;

    int flag;
    if(best <= oldAlpha) flag = UPPER;
    else if(best >= beta) flag = LOWER;
    else flag = EXACT;

    e = {h, stored, depth, flag, bestmv};
    return best;
}

pair<string,int> getBestMove(Board& board, int depth, vector<uint64_t>& hist){
    Movelist moves;
    movegen::legalmoves(moves, board);
    if(moves.empty()) return {"", 0};

    bool white = board.sideToMove() == Color::WHITE;
    int bestVal = white ? -INF : INF;
    Move bestM = moves[0];

    Move pv = Move::NO_MOVE;
    TTEntry& e = tt[board.hash() & (TT_SIZE-1)];
    if(e.depth >= 0 && e.hash == board.hash()) pv = e.best;

    sort(moves.begin(), moves.end(), [&](Move a, Move b){
        int sa = 0, sb = 0;

        if(a == pv) sa = 2000000;
        else if(board.isCapture(a)){
            Piece v = board.at(a.to());
            Piece at = board.at(a.from());
            sa = 1000000 + pieceVal(v.type()) * 10 - pieceVal(at.type());
        }

        if(b == pv) sb = 2000000;
        else if(board.isCapture(b)){
            Piece v = board.at(b.to());
            Piece at2 = board.at(b.from());
            sb = 1000000 + pieceVal(v.type()) * 10 - pieceVal(at2.type());
        }

        return sa > sb;
    });

    hist.push_back(board.hash());
    for(auto& m : moves){
        board.makeMove(m);
        int val = alphaBeta(board, depth-1, -INF, INF, !white, 1, hist);
        board.unmakeMove(m);
        if(white ? val > bestVal : val < bestVal){
            bestVal = val;
            bestM = m;
        }
    }
    hist.pop_back();
    return {uci::moveToUci(bestM), bestVal};
}

vector<string> getLine(Board board, int plies, vector<uint64_t> hist, bool& mateFound, int& mateInPlies){
    vector<string> line;
    mateFound = false;
    mateInPlies = -1;

    for(int i = 0; i < plies; i++){
        auto game = board.isGameOver();
        if(game.second == GameResult::LOSE){ mateFound = true; mateInPlies = i; break; }
        if(game.second != GameResult::NONE) break;

        auto res = getBestMove(board, ENGINE_DEPTH, hist);
        if(res.first.empty()) break;

        Move m = uci::uciToMove(board, res.first);
        if(m == Move::NO_MOVE) break;

        try{ line.push_back(uci::moveToSan(board, m)); }
        catch(...){ line.push_back(res.first); }

        hist.push_back(board.hash());
        board.makeMove(m);

        auto g2 = board.isGameOver();
        if(g2.second == GameResult::LOSE){ mateFound = true; mateInPlies = i+1; break; }
    }
    return line;
}

string formatLine(const vector<string>& moves, Color first){
    if(moves.empty()) return "(none)";
    ostringstream oss;
    int mn = 1;
    bool wfirst = (first == Color::WHITE);
    for(int i = 0; i < (int)moves.size(); i++){
        bool isWhite = wfirst ? (i%2==0) : (i%2==1);
        if(i==0 && !wfirst){
            oss << mn << "... " << moves[i];
        }
        else if(isWhite){
            if(i > 0) oss << "  ";
            oss << mn << ". " << moves[i];
        }
        else{
            oss << "  " << moves[i];
            mn++;
        }
    }
    return oss.str();
}
// took help of AI to implement reading from json, and to get output whether it is correct or not, if it is giving checkmate in required moves or not.
map<string,string> loadJson(const string& path){
    map<string,string> res;
    ifstream f(path);
    if(!f){ cerr << "cant open " << path << "\n"; return res; }
    string content((istreambuf_iterator<char>(f)), {});

    int i = 0;
    int n = content.size();
    auto skipws = [&](){ while(i<n && isspace(content[i])) i++; };
    auto readstr = [&](){
        string out;
        if(i >= n || content[i] != '"') return out;
        i++;
        while(i < n && content[i] != '"'){
            if(content[i] == '\\' && i+1 < n){
                i++;
                char c = content[i];
                if(c=='"') out+='"'; else if(c=='\\') out+='\\';
                else if(c=='n') out+='\n'; else if(c=='t') out+='\t';
                else out+=c;
            }
            else out += content[i];
            i++;
        }
        if(i<n) i++;
        return out;
    };

    skipws(); if(i<n && content[i]=='{') i++;
    while(i < n){
        skipws();
        if(i>=n || content[i]=='}') break;
        if(content[i]==','){ i++; continue; }
        string k = readstr(); skipws();
        if(i<n && content[i]==':') i++; skipws();
        string v = readstr();
        if(!k.empty()) res[k] = v;
    }
    return res;
}

int main(){
    memset(tt, 0, sizeof(tt));
    for(int i = 0; i < TT_SIZE; i++) tt[i].depth = -1;

    int mateN;
    cout << "Mate in how many? ";
    cin >> mateN; cin.ignore(9999, '\n');
    if(mateN <= 0){ cerr << "bad input\n"; return 1; }

    int PLIES = 2*mateN - 1;

    string jpath;
    cout << "JSON path: ";
    getline(cin, jpath);

    auto puzzles = loadJson(jpath);
    if(puzzles.empty()){ cerr << "no puzzles\n"; return 1; }

    struct Failed { string fen, line; };
    int total=0, correct=0;
    double totalMs = 0;
    vector<Failed> fails;

    for(auto& [fen, sol] : puzzles){
        total++;
        Board board;
        board.setFen(fen);
        Color atk = board.sideToMove();

        vector<uint64_t> hist = {board.hash()};

        auto t0 = chrono::high_resolution_clock::now();

        bool mfound; int mplies;
        auto cl = getLine(board, PLIES, hist, mfound, mplies);
        bool ok = mfound && mplies <= PLIES;

        auto t1 = chrono::high_resolution_clock::now();
        double ms = chrono::duration<double,milli>(t1-t0).count();
        totalMs += ms;

        bool pf; int pp;
        auto pl = getLine(board, printDepth, hist, pf, pp);
        string linestr = formatLine(pl, atk);

        if(ok) correct++;
        else fails.push_back({fen, linestr});

        cout << "FEN: " << fen << "\n";
        cout << "Mate in " << mateN << " (plies needed: " << PLIES << ")\n";
        cout << "Result: " << (ok ? "YES" : "NO") << "\n";
        if(mfound) cout << "Found: mate in " << mplies << " plies\n";
        else cout << "Found: no forced mate in " << PLIES << " plies\n";
        cout << "Line (" << ENGINE_DEPTH << " plies): " << linestr << "\n";
        cout << fixed << setprecision(4) << "Time: " << ms << "ms\n\n";
    }

    double acc = total > 0 ? 100.0*correct/total : 0;
    cout << "=== SUMMARY ===\n";
    cout << "Total: " << total << "\n";
    cout << "Correct: " << correct << "\n";
    cout << "Wrong: " << (total-correct) << "\n";
    cout << fixed << setprecision(2) << "Accuracy: " << acc << "%\n";
    cout << "Total time: " << totalMs << "ms\n";
    cout << "Avg: " << (total ? totalMs/total : 0) << "ms\n";

    if(!fails.empty()){
        cout << "\n=== FAILED (" << fails.size() << ") ===\n";
        for(int i = 0; i < (int)fails.size(); i++){
            cout << "\n[" << i+1 << "] " << fails[i].fen << "\n";
            cout << "    engine: " << fails[i].line << "\n";
        }
    }

    return 0;
}