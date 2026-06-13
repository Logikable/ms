#include "src/equip_instance.h"

#include <random>

#include "src/equip_stats.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

// Success and destruction rates in hundredths of a percent (10000 = 100%).
// Index i = attempt from i★ to (i+1)★. Failure = 10000 - success - destroy.
constexpr StarForceRate kRates[kMaxStarForce] = {
    {9500, 0},     // 0★
    {9000, 0},     // 1★
    {8500, 0},     // 2★
    {8500, 0},     // 3★
    {8000, 0},     // 4★
    {7500, 0},     // 5★
    {7000, 0},     // 6★
    {6500, 0},     // 7★
    {6000, 0},     // 8★
    {5500, 0},     // 9★
    {5000, 0},     // 10★
    {4500, 0},     // 11★
    {4000, 0},     // 12★
    {3500, 0},     // 13★
    {3000, 0},     // 14★
    {3000, 210},   // 15★
    {3000, 210},   // 16★
    {1500, 680},   // 17★
    {1500, 680},   // 18★
    {1500, 850},   // 19★
    {3000, 1050},  // 20★
    {1500, 1275},  // 21★
    {1500, 1700},  // 22★
    {1000, 1800},  // 23★
    {1000, 1800},  // 24★
    {1000, 1800},  // 25★
    {700, 1860},   // 26★
    {500, 1900},   // 27★
    {300, 1940},   // 28★
    {100, 1980},   // 29★
};

// Per-star primary stat deltas for 1–15★ (index i = gain for i★→(i+1)★).
constexpr int kPrimaryStatDeltas[15] = {
    2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};

// Per-star HP/MP deltas for 1–15★ weapons (same gain for both HP and MP).
constexpr int kHpMpDeltas[15] = {
    5, 5, 5, 10, 10, 10, 20, 20, 20, 25, 25, 25, 25, 25, 25,
};

// Stat and ATT gained by a weapon when reaching the given star level (16–30★),
// by required level range. Index i = gain for (i+15)★→(i+16)★.
struct WeaponHighStarEntry {
  int stat;
  int att;
};

// Max stars for this range is 20★; entries 5–14 are padding.
constexpr WeaponHighStarEntry kHighStar128_137[15] = {
    {7, 6}, {7, 7}, {7, 7}, {7, 8}, {7, 9}, {0, 0}, {0, 0}, {0, 0},
    {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
};
constexpr WeaponHighStarEntry kHighStar138_149[15] = {
    {9, 7},  {9, 8},  {9, 8},  {9, 9},  {9, 10}, {9, 11}, {9, 12}, {0, 30},
    {0, 31}, {0, 32}, {0, 33}, {0, 34}, {0, 35}, {0, 36}, {0, 37},
};
constexpr WeaponHighStarEntry kHighStar150_159[15] = {
    {11, 8}, {11, 9}, {11, 9}, {11, 10}, {11, 11}, {11, 12}, {11, 13}, {0, 31},
    {0, 32}, {0, 33}, {0, 34}, {0, 35},  {0, 36},  {0, 37},  {0, 38},
};
constexpr WeaponHighStarEntry kHighStar160_199[15] = {
    {13, 9}, {13, 9}, {13, 10}, {13, 11}, {13, 12}, {13, 13}, {13, 14}, {0, 32},
    {0, 33}, {0, 34}, {0, 35},  {0, 36},  {0, 37},  {0, 38},  {0, 39},
};
constexpr WeaponHighStarEntry kHighStar200_249[15] = {
    {15, 13}, {15, 13}, {15, 14}, {15, 14}, {15, 15},
    {15, 16}, {15, 17}, {0, 34},  {0, 35},  {0, 36},
    {0, 37},  {0, 38},  {0, 39},  {0, 40},  {0, 41},
};
// 23–30★ entries are {0,0}: no star-forceable Lv250+ weapon exists in GMS.
// TODO: Superior equipment has a different stat table and a 15★ cap; handle
// separately when Superior items are added.
constexpr WeaponHighStarEntry kHighStar250Plus[15] = {
    {17, 16}, {17, 16}, {17, 17}, {17, 17}, {17, 18},
    {17, 19}, {17, 20}, {0, 0},   {0, 0},   {0, 0},
    {0, 0},   {0, 0},   {0, 0},   {0, 0},   {0, 0},
};

WeaponHighStarEntry WeaponHighStarGainAt(int required_level, int star_to) {
  int idx = star_to - 16;
  if (required_level >= 250) {
    return kHighStar250Plus[idx];
  }
  if (required_level >= 200) {
    return kHighStar200_249[idx];
  }
  if (required_level >= 160) {
    return kHighStar160_199[idx];
  }
  if (required_level >= 150) {
    return kHighStar150_159[idx];
  }
  if (required_level >= 138) {
    return kHighStar138_149[idx];
  }
  if (required_level >= 128) {
    return kHighStar128_137[idx];
  }
  return {0, 0};
}

// Flags indicating which primary stats a weapon gains per star force level.
struct StatFlags {
  bool str = false;
  bool dex = false;
  bool int_ = false;
  bool luk = false;
};

StatFlags StatsForJobCategory(EquipJobCategory cat) {
  switch (cat) {
    case EQUIP_JOB_CATEGORY_WARRIOR:
    case EQUIP_JOB_CATEGORY_BOWMAN:
    case EQUIP_JOB_CATEGORY_PIRATE:
      return {true, true, false, false};
    case EQUIP_JOB_CATEGORY_MAGICIAN:
      return {false, false, true, true};
    case EQUIP_JOB_CATEGORY_THIEF:
      return {false, true, false, true};
    default:
      return {true, true, true, true};
  }
}

StatFlags PrimaryStatFlags(const EquipPrototype& proto) {
  if (proto.equip_job_categories_size() == 0) {
    return {true, true, true, true};
  }
  StatFlags flags;
  for (int i = 0; i < proto.equip_job_categories_size(); ++i) {
    StatFlags j = StatsForJobCategory(
        static_cast<EquipJobCategory>(proto.equip_job_categories(i)));
    flags.str |= j.str;
    flags.dex |= j.dex;
    flags.int_ |= j.int_;
    flags.luk |= j.luk;
  }
  return flags;
}

}  // namespace

EquipInstance::EquipInstance(EquipPrototype prototype)
    : prototype_(std::move(prototype)) {
  state_.set_equip_name(prototype_.name());
  state_.set_remaining_upgrade_slots(prototype_.upgrade_slots());
}

EquipInstance::EquipInstance(EquipPrototype prototype, Equip state)
    : prototype_(std::move(prototype)), state_(std::move(state)) {
}

ScrollOutcome EquipInstance::Scroll(const ms::Scroll& scroll,
                                    std::mt19937& rng) {
  if (state_.remaining_upgrade_slots() == 0) {
    return kScrollNoSlots;
  }
  state_.set_remaining_upgrade_slots(state_.remaining_upgrade_slots() - 1);
  std::uniform_int_distribution<int> dist(1, 100);
  if (dist(rng) > scroll.success_rate()) {
    return kScrollFail;
  }
  const EquipStats scroll_sources[] = {state_.scroll_stats(), scroll.stats()};
  *state_.mutable_scroll_stats() = SumEquipStats(scroll_sources);
  return kScrollSuccess;
}

EquipStats EquipInstance::StarForceStatGains() const {
  int stars = state_.stars();
  bool is_weapon = (prototype_.equip_slot() == EQUIP_SLOT_PRIMARY_WEAPON);
  StatFlags flags = PrimaryStatFlags(prototype_);
  int required_level = prototype_.required_level();
  int base_att =
      prototype_.base_stats().attack() + state_.scroll_stats().attack();
  int base_matt = prototype_.base_stats().magic_attack() +
                  state_.scroll_stats().magic_attack();

  EquipStats gains;
  int sf_att = 0;
  int sf_matt = 0;

  for (int s = 0; s < stars; ++s) {
    if (s < 15) {
      int delta = kPrimaryStatDeltas[s];
      if (flags.str) {
        gains.set_str(gains.str() + delta);
      }
      if (flags.dex) {
        gains.set_dex(gains.dex() + delta);
      }
      if (flags.int_) {
        gains.set_int_(gains.int_() + delta);
      }
      if (flags.luk) {
        gains.set_luk(gains.luk() + delta);
      }
      if (is_weapon) {
        gains.set_max_hp(gains.max_hp() + kHpMpDeltas[s]);
        gains.set_max_mp(gains.max_mp() + kHpMpDeltas[s]);
        sf_att += (base_att + sf_att) / 50 + 1;
        sf_matt += (base_matt + sf_matt) / 50 + 1;
      }
    } else if (is_weapon) {
      WeaponHighStarEntry entry = WeaponHighStarGainAt(required_level, s + 1);
      if (flags.str) {
        gains.set_str(gains.str() + entry.stat);
      }
      if (flags.dex) {
        gains.set_dex(gains.dex() + entry.stat);
      }
      if (flags.int_) {
        gains.set_int_(gains.int_() + entry.stat);
      }
      if (flags.luk) {
        gains.set_luk(gains.luk() + entry.stat);
      }
      sf_att += entry.att;
      sf_matt += entry.att;
    }
  }

  if (is_weapon) {
    gains.set_attack(sf_att);
    gains.set_magic_attack(sf_matt);
  }
  return gains;
}

EquipStats EquipInstance::stats() const {
  const EquipStats stat_sources[] = {
      prototype_.base_stats(), state_.scroll_stats(), StarForceStatGains()};
  return SumEquipStats(stat_sources);
}

StarForceOutcome EquipInstance::StarForce(std::mt19937& rng) {
  if (state_.remaining_upgrade_slots() > 0) {
    return kStarForceFail;
  }
  int s = state_.stars();
  if (s >= max_stars()) {
    return kStarForceFail;
  }
  const StarForceRate& rate = kRates[s];
  std::uniform_int_distribution<int> dist(1, 10000);
  int roll = dist(rng);
  if (roll <= rate.success) {
    state_.set_stars(s + 1);
    return kStarForceSuccess;
  }
  if (rate.destroy > 0 && roll > 10000 - rate.destroy) {
    return kStarForceDestroy;
  }
  return kStarForceFail;
}

int EquipInstance::MaxStarsForLevel(int required_level) {
  if (required_level >= 138) {
    return 30;
  }
  if (required_level >= 128) {
    return 20;
  }
  if (required_level >= 118) {
    return 15;
  }
  if (required_level >= 108) {
    return 10;
  }
  if (required_level >= 95) {
    return 8;
  }
  return 5;
}

StarForceRate EquipInstance::RateAt(int stars) {
  if (stars < 0 || stars >= kMaxStarForce) {
    return {0, 0};
  }
  return kRates[stars];
}

}  // namespace ms
