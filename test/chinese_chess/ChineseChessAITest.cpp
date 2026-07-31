#include <Arduino.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "ChineseChessAI.h"

std::string g_simulator_sd_root;

namespace {

using Board = ChineseChessBoard;
using Level = ChineseChessAiLevel;
using Move = Board::Move;
using Side = Board::Side;

bool sameMove(const Move& lhs, const Move& rhs) { return lhs.from == rhs.from && lhs.to == rhs.to; }

bool isLegalMove(const Board& board, Side side, const Move& move) {
  Move legal[Board::MAX_LEGAL_MOVES];
  const uint8_t count = board.generateLegalMoves(side, legal, Board::MAX_LEGAL_MOVES);
  for (uint8_t i = 0; i < count; i++) {
    if (sameMove(legal[i], move)) return true;
  }
  return false;
}

Board makeRepeatingCheckPosition() {
  Board board;
  std::memset(board.cells, 0, sizeof(board.cells));
  board.cells[Board::idx(9, 4)] = Board::RedKing;
  board.cells[Board::idx(2, 3)] = Board::BlackKing;
  board.cells[Board::idx(1, 4)] = Board::BlackChariot;
  board.cells[Board::idx(0, 5)] = Board::BlackPawn;
  board.cells[Board::idx(2, 5)] = Board::BlackPawn;

  board.makeMove({Board::idx(9, 4), Board::idx(9, 5)});
  board.makeMove({Board::idx(1, 4), Board::idx(1, 5)});
  board.makeMove({Board::idx(9, 5), Board::idx(9, 4)});
  return board;
}

Board makeForcedRepeatingPosition() {
  Board board;
  std::memset(board.cells, 0, sizeof(board.cells));
  board.cells[Board::idx(0, 4)] = Board::BlackKing;
  board.cells[Board::idx(1, 4)] = Board::BlackChariot;
  board.cells[Board::idx(2, 2)] = Board::RedHorse;
  board.cells[Board::idx(2, 6)] = Board::RedHorse;
  board.cells[Board::idx(3, 4)] = Board::RedChariot;
  board.cells[Board::idx(9, 3)] = Board::RedKing;

  board.makeMove({Board::idx(3, 4), Board::idx(3, 3)});
  board.makeMove({Board::idx(1, 4), Board::idx(1, 5)});
  board.makeMove({Board::idx(3, 3), Board::idx(3, 4)});
  return board;
}

TEST(ChineseChessAI, AvoidsReturningToPriorPositionAtEveryLevel) {
  const Board board = makeRepeatingCheckPosition();
  const Move repeated{Board::idx(1, 5), Board::idx(1, 4)};
  ASSERT_TRUE(isLegalMove(board, Side::Black, repeated));

  for (const Level level : {Level::Easy, Level::Medium, Level::Hard}) {
    randomSeed(1);
    const Move chosen = ChineseChessAI::chooseMove(board, Side::Black, level);
    EXPECT_TRUE(isLegalMove(board, Side::Black, chosen));
    EXPECT_FALSE(sameMove(chosen, repeated));
  }
}

TEST(ChineseChessAI, KeepsRepeatedMoveWhenItIsTheOnlyLegalMove) {
  const Board board = makeForcedRepeatingPosition();
  const Move forced{Board::idx(1, 5), Board::idx(1, 4)};
  Move legal[Board::MAX_LEGAL_MOVES];
  ASSERT_EQ(board.generateLegalMoves(Side::Black, legal, Board::MAX_LEGAL_MOVES), 1);
  ASSERT_TRUE(sameMove(legal[0], forced));

  const Move chosen = ChineseChessAI::chooseMove(board, Side::Black, Level::Medium);
  EXPECT_TRUE(sameMove(chosen, forced));
}

TEST(ChineseChessAI, ReturnsLegalMoveFromNormalPosition) {
  Board board;
  board.reset();
  ASSERT_TRUE(board.makeMove({Board::idx(6, 4), Board::idx(5, 4)}));

  const Move chosen = ChineseChessAI::chooseMove(board, Side::Black, Level::Easy);
  EXPECT_TRUE(isLegalMove(board, Side::Black, chosen));
}

}  // namespace
