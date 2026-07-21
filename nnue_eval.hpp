#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "chess.hpp"

namespace nnue {

using namespace chess;

constexpr uint32_t ENGINE_MAGIC = 0x43484545u;  // must match export_weights.py's ENGINE_MAGIC
constexpr int IN_DIM = 780;

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    int32_t in_dim, h1, h2, h3;
    float score_scale;
};
#pragma pack(pop)

struct Weights {
    int h1 = 0, h2 = 0, h3 = 0;
    float score_scale = 1.0f;
    std::vector<float> w1, b1;
    std::vector<float> w2, b2;
    std::vector<float> w3, b3;
    std::vector<float> w4, b4;
    bool loaded = false;
};

inline Weights g_weights;

// Returns false (and leaves g_weights untouched) on any mismatch, so the
// caller can fall back to a classical eval instead of running on garbage.
inline bool load_weights(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    Header hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f || hdr.magic != ENGINE_MAGIC || hdr.in_dim != IN_DIM) {
        return false;  // wrong/stale file
    }

    Weights w;
    w.h1 = hdr.h1;
    w.h2 = hdr.h2;
    w.h3 = hdr.h3;
    w.score_scale = hdr.score_scale;

    auto read_vec = [&](std::vector<float>& v, size_t count) {
        v.resize(count);
        f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(count * sizeof(float)));
    };

    read_vec(w.w1, static_cast<size_t>(w.h1) * IN_DIM);
    read_vec(w.b1, static_cast<size_t>(w.h1));
    read_vec(w.w2, static_cast<size_t>(w.h2) * w.h1);
    read_vec(w.b2, static_cast<size_t>(w.h2));
    read_vec(w.w3, static_cast<size_t>(w.h3) * w.h2);
    read_vec(w.b3, static_cast<size_t>(w.h3));
    read_vec(w.w4, static_cast<size_t>(w.h3));  // 1 x h3
    read_vec(w.b4, 1);

    w.loaded = static_cast<bool>(f);
    if (w.loaded) g_weights = std::move(w);
    return w.loaded;
}

// Mirrors feature_encoding.py's encode() exactly -- keep the two in sync
// by hand if you ever change one of them.
inline void build_features(const Board& board, std::array<float, IN_DIM>& x) {
    x.fill(0.0f);

    Color us = board.sideToMove();
    Color them = (us == Color::WHITE) ? Color::BLACK : Color::WHITE;

    for (int sq = 0; sq < 64; sq++) {
        Piece p = board.at(Square(sq));
        if (p == Piece::NONE) continue;

        int relative = (p.color() == us) ? 0 : 1;
        int type_idx = static_cast<int>(p.type());  // PAWN=0 .. KING=5, same order as PIECE_TYPES in Python
        int mapped_sq = (us == Color::WHITE) ? sq : (sq ^ 56);

        x[relative * 6 * 64 + type_idx * 64 + mapped_sq] = 1.0f;
    }

    constexpr int base = 768;
    x[base + 0] = board.castlingRights().has(us, Board::CastlingRights::Side::KING_SIDE) ? 1.0f : 0.0f;
    x[base + 1] = board.castlingRights().has(us, Board::CastlingRights::Side::QUEEN_SIDE) ? 1.0f : 0.0f;
    x[base + 2] = board.castlingRights().has(them, Board::CastlingRights::Side::KING_SIDE) ? 1.0f : 0.0f;
    x[base + 3] = board.castlingRights().has(them, Board::CastlingRights::Side::QUEEN_SIDE) ? 1.0f : 0.0f;

    Square ep = board.enpassantSq();
    if (ep != Square::NO_SQ) {
        x[base + 4 + ep.file()] = 1.0f;
    }
}

inline void linear(const float* in, int in_dim, const float* w, const float* b,
                    float* out, int out_dim, bool relu) {
    for (int o = 0; o < out_dim; o++) {
        float sum = b[o];
        const float* row = w + static_cast<size_t>(o) * in_dim;
        for (int i = 0; i < in_dim; i++) sum += row[i] * in[i];
        out[o] = relu ? (sum > 0.0f ? sum : 0.0f) : sum;
    }
}

// Returns centipawns from the side-to-move's point of view (positive =
// good for whoever is about to move) -- the same convention the training
// targets used, so callers need no extra sign-flipping.
inline int evaluate(const Board& board) {
    std::array<float, IN_DIM> x{};
    build_features(board, x);

    const Weights& w = g_weights;

    static thread_local std::vector<float> h1, h2, h3;
    h1.resize(w.h1);
    h2.resize(w.h2);
    h3.resize(w.h3);
    float out[1];

    linear(x.data(), IN_DIM, w.w1.data(), w.b1.data(), h1.data(), w.h1, true);
    linear(h1.data(), w.h1, w.w2.data(), w.b2.data(), h2.data(), w.h2, true);
    linear(h2.data(), w.h2, w.w3.data(), w.b3.data(), h3.data(), w.h3, true);
    linear(h3.data(), w.h3, w.w4.data(), w.b4.data(), out, 1, false);

    return static_cast<int>(std::lround(out[0] * w.score_scale));
}

}  // namespace nnue
