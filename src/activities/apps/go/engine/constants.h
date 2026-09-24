#pragma once

#define BOARD_SIZE 9
#define BOARD_CELLS (BOARD_SIZE * BOARD_SIZE)

// 0-based: the switch jump-table indexes stage directly (no subi), and
// stage's zero initializer lives in .bss instead of .data (08-29)
#define STAGE_TITLE 0
#define STAGE_PLAY 1
#define STAGE_PASS_CONFIRM 2
#define STAGE_SCORING 3
#define STAGE_GAME_OVER 4
#define STAGE_HELP 5
#define STAGE_DIFFSEL 6

#define MODE_VS_HUMAN 0
#define MODE_VS_AI 1

#define EMPTY 0
#define BLACK 1
#define WHITE 2
