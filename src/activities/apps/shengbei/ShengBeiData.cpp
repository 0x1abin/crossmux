#include "ShengBeiData.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include <cstdlib>
#include <chrono>

namespace shengbei {

namespace {

// 圣杯解签库 (吉·允诺·可行) — 24 卦吉语
constexpr const char* kShengBeiPoems[] = {
    "心随所愿，时至运转。所虑之事明朗可行，果断顺势而为。",
    "吉星高照，谋事可成。天地同力，心中所想皆有回响。",
    "信步向前，莫怀犹豫。顺应自然之律，凡事皆成美局。",
    "心诚则灵，诸般障碍皆消。当下正是进取良机。",
    "拨云见日，前路豁然。所谋之事得天时地利，速决为吉。",
    "顺风行舟，水到渠成。秉持初志，必有嘉庆。",
    "和气致祥，百事通达。坦然面对，自得吉应。",
    "乾坤定位，万物化生。心志坚定，行之必有所获。",
    "春风化雨，润物无声。所求之事正在向好，静待花开。",
    "积健为雄，厚积薄发。以往积累皆成助力，大胆向前。",
    "鸿雁传书，喜讯临门。心中所念之人与事，皆得善果。",
    "明月入怀，清澈光明。放下顾虑，按照直觉果断前行。",
    "行稳致远，得道多助。身边自有贵人提携，无须担忧。",
    "千帆过尽，渐入佳境。过往波折已平，坦途就在眼前。",
    "同心合力，所向披靡。与人携手共进，事半功倍。",
    "灵犀相通，机缘巧合。看似偶然之中，自有必然成就。",
    "守正出奇，进退自如。把握当下节奏，前途一片光明。",
    "登高望远，天地宽广。开阔胸襟，自见无限可能。",
    "源清流洁，本固枝荣。立足长远，眼下之举大有可为。",
    "瑞气盈门，所求遂心。坚持初心与善念，福泽绵长。",
    "风正一帆悬，破浪正当时。拿出魄力，开启崭新篇章。",
    "吉人天相，无往不利。顺从内心召唤，大胆迈出脚步。",
    "如日方升，光华灿烂。当下正是施展抱负的绝佳时刻。",
    "众望所归，善始善终。以诚待人，事情终将圆满落定。"
};

// 笑杯解签库 (未定·善意·再问) — 24 卦醒心
constexpr const char* kXiaoBeiPoems[] = {
    "所问未明，神明含笑。主意未定或心有杂念，宜凝神再问。",
    "事关多变，不可操切。且先静心沉淀，理清头绪后再试。",
    "天机未显，无需多虑。此问尚可转圜，换个角度思量。",
    "笑看风云，莫急于一时。放平心态，机缘稍后自至。",
    "自心犹豫，卦象难定。先行自决，再卜前程。",
    "云遮月色，虚惊一场。理清细节，重新默念再掷。",
    "神意通融，尚在两可。关键在于自身抉择，莫全托神意。",
    "风起微澜，变数尚多。当下不必急下定论，观望片刻。",
    "心中所惑，其实自知。莫因贪求捷径而失了本心。",
    "心神未定，语意含糊。闭目深呼吸，想清最核心关切。",
    "意念纷飞，难辨真伪。剔除枝节干扰，再探心底真实。",
    "机缘未熟，瓜熟蒂落。静心等候几天，局势自会明朗。",
    "或左或右，皆有可能。跳出非黑即白，寻找第三条路。",
    "笑意盈盈，无需挂碍。小事一桩不必挂心，顺其自然。",
    "问题过泛，神难定夺。将所问细化聚焦，再行请示。",
    "心有所执，反成迷障。放下预设答案，方见真实指引。",
    "吉凶未卜，皆由心生。调整当下状态，转念即是转运。",
    "镜中花月，虚多实少。多看事实少凭臆想，从容应对。",
    "潮起潮落，自有周期。眼下混沌是常态，耐心整理。",
    "言未尽意，心未全安。给自己一点时间，重新组织心念。",
    "轻舟慢摇，不必催浆。沿途自有风景，时机到了自明。",
    "得失之间，不必斤斤计较。豁达一些，答案自会浮现。",
    "看似无解，实因站位太低。换个高度审视，豁然开朗。",
    "心有灵犀，但需诚敬。稍事休息，静气平息后再掷。"
};

// 阴杯解签库 (不允·宜止·沉淀) — 24 卦箴言
constexpr const char* kYinBeiPoems[] = {
    "时机未至，前路多阻。当下宜守不宜进，审慎退止为佳。",
    "机缘未合，不可强求。退一步海阔天空，静候更佳时机。",
    "势道有违，强行恐伤。宜安分守常，修己以待天时。",
    "逆风难行，暂且驻足。省察自身缺漏，来日方长。",
    "事有曲折，强求无益。且宽心耐守，后必有转机。",
    "慎重初念，避开锋芒。韬光养晦，静观其变。",
    "过犹不及，过刚易折。收敛锋芒，保持谦抑与克制。",
    "雾锁迷津，难辨方向。盲目冒进徒增损耗，停下即是止损。",
    "强扭之瓜，终难甘甜。学会放下执念，接纳现实安排。",
    "局势晦暗，暗流涌动。守住底线与原则，切莫轻信诺言。",
    "内力不足，不可强攻。向内深耕蓄能，等待下一个风口。",
    "欲速不达，反受其累。慢下来审视全盘，查漏补缺。",
    "此路不通，另有坦途。不要在死胡同里耗尽心力。",
    "天道忌盈，满招损谦受益。眼下不宜扩张，守成为上。",
    "多言数穷，不如守中。少说多看多思，莫在风口浪尖行事。",
    "外力阻隔，不可硬抗。以柔克刚，避开正面的冲击。",
    "防微杜渐，警惕隐患。小事不慎恐生波澜，谨慎为先。",
    "人心难测，谨慎交往。守好自身阵地，不可盲目跟风。",
    "缘分未到，不必焦躁。属于你的不会错过，耐心守候。",
    "虚火上升，急躁易错。平复情绪之后再做重大决定。",
    "独木难支，孤掌难鸣。缺乏支持时切勿孤注一掷。",
    "看似诱人，实藏陷阱。保持清醒定力，不贪非分之利。",
    "冬藏之期，莫求夏长。顺应休眠节奏，休养生息。",
    "塞翁失马，焉知非福。眼下的阻碍，正是免于更大风险。"
};

constexpr size_t kShengCount = sizeof(kShengBeiPoems) / sizeof(kShengBeiPoems[0]);
constexpr size_t kXiaoCount = sizeof(kXiaoBeiPoems) / sizeof(kXiaoBeiPoems[0]);
constexpr size_t kYinCount = sizeof(kYinBeiPoems) / sizeof(kYinBeiPoems[0]);

uint32_t getRandomEntropy() {
#if defined(ESP32)
  return esp_random();
#else
  static uint32_t seed = 123456789;
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  seed = seed * 1664525u + 1013904223u + static_cast<uint32_t>(now);
  return seed ^ static_cast<uint32_t>(rand());
#endif
}

}  // namespace

ShengBeiResult getOutcomeInfo(Outcome outcome, size_t poemSeed) {
  ShengBeiResult res;
  res.outcome = outcome;

  switch (outcome) {
    case Outcome::ShengBei:
      res.name = "【 圣 杯 】";
      res.pinyin = "SHÈNG BĒI";
      res.tagline = "允诺 · 大吉 · 所谋顺遂";
      res.poem = kShengBeiPoems[poemSeed % kShengCount];
      res.leftFlat = true;   // 一阳
      res.rightFlat = false; // 一阴
      break;

    case Outcome::XiaoBei:
      res.name = "【 笑 杯 】";
      res.pinyin = "XIÀO BĒI";
      res.tagline = "未定 · 善意 · 凝神再试";
      res.poem = kXiaoBeiPoems[poemSeed % kXiaoCount];
      res.leftFlat = true;   // 阳
      res.rightFlat = true;  // 阳
      break;

    case Outcome::YinBei:
      res.name = "【 阴 杯 】";
      res.pinyin = "YĪN BĒI";
      res.tagline = "不允 · 宜止 · 沉淀待时";
      res.poem = kYinBeiPoems[poemSeed % kYinCount];
      res.leftFlat = false;  // 阴
      res.rightFlat = false; // 阴
      break;
  }

  return res;
}

ShengBeiResult castOracle() {
  const uint32_t entropy = getRandomEntropy();

  // 纯随机均匀分布：圣杯、笑杯、阴杯各 1/3 等概率
  const uint32_t choice = entropy % 3;
  Outcome outcome;
  bool leftFlat = false;
  bool rightFlat = false;

  if (choice == 0) {
    outcome = Outcome::ShengBei;
    // 圣杯：一平一凸（随机决定左平右凸或左凸右平）
    const bool flip = ((entropy >> 2) & 1) != 0;
    leftFlat = flip;
    rightFlat = !flip;
  } else if (choice == 1) {
    outcome = Outcome::XiaoBei;
    // 笑杯：双平
    leftFlat = true;
    rightFlat = true;
  } else {
    outcome = Outcome::YinBei;
    // 阴杯：双凸
    leftFlat = false;
    rightFlat = false;
  }

  const size_t poemSeed = static_cast<size_t>(entropy >> 4);
  ShengBeiResult res = getOutcomeInfo(outcome, poemSeed);
  res.leftFlat = leftFlat;
  res.rightFlat = rightFlat;
  return res;
}

}  // namespace shengbei
