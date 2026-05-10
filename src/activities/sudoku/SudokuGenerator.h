#pragma once

#include "SudokuBoard.h"

class SudokuGenerator {
 public:
  // 入口：生成合法完整解，再随机挖洞至目标空格数；保证唯一解。
  // 同步阻塞，预期 ≤ 1.5 s。loop 任务调用，期间 vTaskDelay(1) 喂狗。
  static bool generate(SudokuBoard& board, SudokuBoard::Difficulty diff);

 private:
  // 随机回溯填 9×9 完整解
  static bool fillSolution(uint8_t cells[81]);

  // 计数解，最多到 limit 即停（>= 2 即可证明非唯一）
  static int countSolutions(uint8_t cells[81], int limit);

  static int targetEmpties(SudokuBoard::Difficulty d);
};
