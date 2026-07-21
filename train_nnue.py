"""
Setup:
    pip install torch numpy chess

Usage:
    python train_nnue.py --csv chessData.csv --out weights.bin

"""

import argparse
import csv
import struct
import time

import chess
import numpy as np
import torch
import torch.nn as nn

# ---------------------------------------------------------------- features

NUM_FEATURES = 780
PIECE_TYPES = [chess.PAWN, chess.KNIGHT, chess.BISHOP, chess.ROOK, chess.QUEEN, chess.KING]


def encode(board):
    """
    Board -> 780 float vector, from the side-to-move's point of view.
      [0..767]   piece-square: 2 (us/them) x 6 (piece type) x 64 (square)
                 board mirrored (rank-flipped) when Black is to move
      [768..771] castling rights: us-KS, us-QS, them-KS, them-QS
      [772..779] en-passant file, one-hot
    """
    x = np.zeros(NUM_FEATURES, dtype=np.float32)
    us, them = board.turn, not board.turn
    for square, piece in board.piece_map().items():
        rel = 0 if piece.color == us else 1
        t = PIECE_TYPES.index(piece.piece_type)
        sq = square if us == chess.WHITE else chess.square_mirror(square)
        x[rel * 6 * 64 + t * 64 + sq] = 1.0
    x[768] = board.has_kingside_castling_rights(us)
    x[769] = board.has_queenside_castling_rights(us)
    x[770] = board.has_kingside_castling_rights(them)
    x[771] = board.has_queenside_castling_rights(them)
    if board.ep_square is not None:
        x[772 + chess.square_file(board.ep_square)] = 1.0
    return x


# ------------------------------------------------------------------- data

CP_CLIP = 1000.0


def parse_eval(raw):
    raw = raw.strip()
    if raw.startswith("#"):
        return -CP_CLIP if raw.startswith("#-") else CP_CLIP
    return float(raw)


def load_csv(paths, limit=None):
    X, y = [], []
    total = 0
    for path in paths:
        with open(path, newline="") as f:
            reader = csv.DictReader(f)
            for row in reader:
                if limit and total >= limit:
                    return np.array(X, dtype=np.float32), np.array(y, dtype=np.float32)
                try:
                    board = chess.Board(row["FEN"])
                    cp = parse_eval(row["Evaluation"])
                except ValueError:
                    continue
                if board.turn == chess.BLACK:
                    cp = -cp
                cp = max(-CP_CLIP, min(CP_CLIP, cp))
                X.append(encode(board))
                y.append(cp)
                total += 1
                if total % 100000 == 0:
                    print(f"  encoded {total:,} rows")
    return np.array(X, dtype=np.float32), np.array(y, dtype=np.float32)


# ------------------------------------------------------------------ model

HIDDEN = (256, 32, 32)
SCORE_SCALE = 400.0  # net output * SCORE_SCALE = centipawns


class ChessEvalNet(nn.Module):
    def __init__(self):
        super().__init__()
        h1, h2, h3 = HIDDEN
        self.fc1 = nn.Linear(NUM_FEATURES, h1)
        self.fc2 = nn.Linear(h1, h2)
        self.fc3 = nn.Linear(h2, h3)
        self.fc4 = nn.Linear(h3, 1)
        self.relu = nn.ReLU()
        self.drop = nn.Dropout(0.1)
        for layer in (self.fc1, self.fc2, self.fc3):
            nn.init.kaiming_normal_(layer.weight, nonlinearity="relu")

    def forward(self, x):
        x = self.drop(self.relu(self.fc1(x)))
        x = self.drop(self.relu(self.fc2(x)))
        x = self.relu(self.fc3(x))
        return self.fc4(x).squeeze(-1)


# ----------------------------------------------------------------- export

MAGIC = 0x43484545  # must match ENGINE_MAGIC in nnue_eval.hpp


def export_weights(model, path):
    model.eval()
    with open(path, "wb") as f:
        f.write(struct.pack("<Iiiiif", MAGIC, NUM_FEATURES, *HIDDEN, SCORE_SCALE))
        for name in ("fc1", "fc2", "fc3", "fc4"):
            layer = getattr(model, name)
            f.write(layer.weight.detach().numpy().astype("float32").tobytes())
            f.write(layer.bias.detach().numpy().astype("float32").tobytes())
    print(f"wrote {path}")


# ------------------------------------------------------------------ train

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True, nargs="+")
    ap.add_argument("--out", default="weights.bin")
    ap.add_argument("--epochs", type=int, default=30)
    ap.add_argument("--batch-size", type=int, default=2048)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--limit", type=int, default=None, help="only use first N rows, for a quick test")
    args = ap.parse_args()

    print("Loading + encoding CSV (this is the slow part on a big file)...")
    X, y = load_csv(args.csv, args.limit)
    print(f"{len(X):,} usable positions")

    X = torch.from_numpy(X)
    y = torch.from_numpy(y) / SCORE_SCALE

    n = X.size(0)
    n_val = max(1, int(n * 0.1))
    perm = torch.randperm(n)
    val_idx, train_idx = perm[:n_val], perm[n_val:]
    X_tr, y_tr, X_val, y_val = X[train_idx], y[train_idx], X[val_idx], y[val_idx]

    model = ChessEvalNet()
    opt = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=1e-5)
    loss_fn = nn.MSELoss()

    best_rmse, best_state, stale = float("inf"), None, 0

    for epoch in range(1, args.epochs + 1):
        model.train()
        order = torch.randperm(X_tr.size(0))
        start = time.time()
        for i in range(0, len(order), args.batch_size):
            idx = order[i:i + args.batch_size]
            opt.zero_grad()
            pred = model(X_tr[idx])
            loss = loss_fn(pred, y_tr[idx])
            loss.backward()
            opt.step()

        model.eval()
        with torch.no_grad():
            val_pred = model(X_val)
            val_rmse_cp = torch.sqrt(torch.mean(((val_pred - y_val) * SCORE_SCALE) ** 2)).item()
        print(f"epoch {epoch:3d}/{args.epochs}  val RMSE {val_rmse_cp:7.2f} cp  ({time.time() - start:.1f}s)")

        if val_rmse_cp < best_rmse:
            best_rmse = val_rmse_cp
            best_state = {k: v.clone() for k, v in model.state_dict().items()}
            stale = 0
        else:
            stale += 1
            if stale >= 6:
                print("No improvement in 6 epochs -- stopping early.")
                break

    model.load_state_dict(best_state)
    torch.save(model.state_dict(), "model.pt")
    export_weights(model, args.out)
    print(f"\nBest val RMSE: {best_rmse:.2f} cp. Saved model.pt and {args.out}.")


if __name__ == "__main__":
    main()
