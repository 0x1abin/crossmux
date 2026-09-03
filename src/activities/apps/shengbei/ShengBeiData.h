#pragma once

#include <cstdint>
#include <cstddef>

namespace shengbei {

enum class Outcome : uint8_t {
  ShengBei = 0,  // 圣杯 (一阳一阴 / 一平一凸) - 吉 / 允准
  XiaoBei = 1,   // 笑杯 (两阳 / 双平朝上) - 未定 / 再试
  YinBei = 2,    // 阴杯 (两阴 / 双凸朝上) - 不允 / 宜止
};

struct ShengBeiResult {
  Outcome outcome;
  const char* name;
  const char* pinyin;
  const char* tagline;
  const char* poem;
  bool leftFlat;   // true: 平面朝上(阳), false: 凸面朝上(阴)
  bool rightFlat;  // true: 平面朝上(阳), false: 凸面朝上(阴)
};

// 掷圣杯：结合硬件熵或随机数，生成卦象与指引解签
ShengBeiResult castOracle();

// 获取指定卦象的默认信息 (用于测试或固定展示)
ShengBeiResult getOutcomeInfo(Outcome outcome, size_t poemSeed = 0);

}  // namespace shengbei
