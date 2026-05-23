#pragma once

#include <HalStorage.h>

#include <cstdint>

class ChineseChessBoard {
 public:
  static constexpr uint8_t RANKS = 10;
  static constexpr uint8_t FILES = 9;
  static constexpr uint8_t CELLS = RANKS * FILES;  // 90
  static constexpr uint16_t MAX_MOVES = 256;       // upper bound on plies stored
  static constexpr uint8_t MAX_LEGAL_MOVES = 80;   // upper bound at any position
  static constexpr uint8_t INVALID_IDX = 0xFF;

  enum class Side : uint8_t { Red = 0, Black = 1 };
  enum class Kind : uint8_t { King = 0, Advisor = 1, Elephant = 2, Horse = 3, Chariot = 4, Cannon = 5, Pawn = 6 };

  // Cell encoding:
  //   0 = empty
  //   1..7  = Red    King..Pawn
  //   9..15 = Black  King..Pawn
  // Bit 3 (0x08) = side flag; bits 0..2 = (kind + 1).
  enum Piece : uint8_t {
    Empty = 0,
    RedKing = 1,
    RedAdvisor = 2,
    RedElephant = 3,
    RedHorse = 4,
    RedChariot = 5,
    RedCannon = 6,
    RedPawn = 7,
    BlackKing = 9,
    BlackAdvisor = 10,
    BlackElephant = 11,
    BlackHorse = 12,
    BlackChariot = 13,
    BlackCannon = 14,
    BlackPawn = 15,
  };

  struct Move {
    uint8_t from = INVALID_IDX;
    uint8_t to = INVALID_IDX;
    uint8_t captured = 0;  // Piece value at `to` before the move (0 = none)
  };

  enum class Result : uint8_t { Ongoing = 0, RedWins = 1, BlackWins = 2, Draw = 3 };

  uint8_t cells[CELLS] = {};
  Move moveHistory[MAX_MOVES] = {};
  uint16_t moveCount = 0;
  Result result = Result::Ongoing;
  bool resigned = false;  // true if game ended via resignation (overrides natural detection)

  // Incrementally-maintained Zobrist hash of (cells + side-to-move). XOR-updated
  // by makeMove/undo; fully rebuilt by reset/readFrom. Not serialized — it is
  // derived state. Invariant: matches cells whenever no in-progress trial-move
  // mutation is active (see generateLegalMoves note).
  uint64_t zobristHash = 0;

  // ---------- Helpers ----------
  static constexpr uint8_t idx(uint8_t r, uint8_t c) { return static_cast<uint8_t>(r * FILES + c); }
  static constexpr uint8_t rowOf(uint8_t i) { return static_cast<uint8_t>(i / FILES); }
  static constexpr uint8_t colOf(uint8_t i) { return static_cast<uint8_t>(i % FILES); }
  static constexpr bool inBounds(int r, int c) { return r >= 0 && r < RANKS && c >= 0 && c < FILES; }

  static constexpr bool isEmpty(uint8_t v) { return v == 0; }
  static constexpr Side sideOf(uint8_t v) { return (v & 0x08) ? Side::Black : Side::Red; }
  static constexpr Kind kindOf(uint8_t v) { return static_cast<Kind>((v & 0x07) - 1); }
  static constexpr uint8_t makePiece(Side s, Kind k) {
    return static_cast<uint8_t>(((s == Side::Black) ? 0x08 : 0x00) | ((static_cast<uint8_t>(k) + 1) & 0x07));
  }

  // Red occupies ranks 5..9 (bottom). Black occupies ranks 0..4 (top).
  static constexpr bool isRedSide(uint8_t r) { return r >= 5; }
  static constexpr bool isBlackSide(uint8_t r) { return r <= 4; }
  static constexpr bool inOwnHalf(Side s, uint8_t r) { return (s == Side::Red) ? isRedSide(r) : isBlackSide(r); }
  // Palace: ranks 7..9 / 0..2, files 3..5
  static constexpr bool inPalace(Side s, uint8_t r, uint8_t c) {
    if (c < 3 || c > 5) return false;
    return (s == Side::Red) ? (r >= 7 && r <= 9) : (r <= 2);
  }

  // ---------- Lifecycle ----------
  void reset();

  uint8_t at(uint8_t r, uint8_t c) const;
  Side nextTurn() const { return (moveCount % 2 == 0) ? Side::Red : Side::Black; }
  bool isOver() const { return result != Result::Ongoing; }

  // ---------- Move generation ----------
  // Generates pseudo-legal moves (does not filter for self-check).
  uint8_t generatePseudoMoves(Side side, Move* out, uint8_t outCap) const;
  // Generates fully legal moves (excludes self-check and king-facing positions).
  uint8_t generateLegalMoves(Side side, Move* out, uint8_t outCap) const;
  // Generates fully legal moves originating from a single square (UI helper).
  uint8_t generateLegalMovesFrom(uint8_t from, Move* out, uint8_t outCap) const;

  // ---------- Check / make / undo ----------
  bool inCheck(Side side) const;
  bool kingsFacing() const;
  // Apply a move (assumes pseudo-legality but verifies bounds). Returns false on bad input.
  bool makeMove(const Move& m);
  bool undo();

  // After makeMove(), call to detect natural game-over for the side-to-move.
  // Sets `result` if checkmate or stalemate.
  void updateResult();

  // Trial-make the move and report whether it puts the opponent in check. Used
  // by the AI to classify perpetual-check cycles. Mutates cells temporarily
  // then restores them; does NOT update zobristHash or moveHistory.
  bool givesCheck(Side mover, const Move& m) const;

  // ---------- Serialization ----------
  // Format:
  //   uint8 version (=BOARD_VERSION)
  //   uint8 result, uint8 resigned
  //   uint16 moveCount
  //   For each move: uint8 from, uint8 to, uint8 captured
  // Cells are reconstructed from initial position + move history.
  bool writeTo(HalFile& f) const;
  bool readFrom(HalFile& f);

  static constexpr uint8_t BOARD_VERSION = 1;

 private:
  void genKing(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const;
  void genAdvisor(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const;
  void genElephant(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const;
  void genHorse(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const;
  void genChariot(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const;
  void genCannon(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const;
  void genPawn(uint8_t from, Move* out, uint8_t& n, uint8_t outCap) const;
  uint8_t findKing(Side side) const;
  void recomputeZobrist();
};
