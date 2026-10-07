#include "analysis/reference_data.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "src/character/inner_ability.h"
#include "src/frontend/widgets/game_names.h"
#include "src/item/equip_instance.h"
#include "src/item/flame.h"
#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/item/slot_order.h"
#include "src/item/star_force_cost.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

// The highest level a table is traced to. Its last run covers every level above
// it, which the JSON writes as a null ceiling.
constexpr int kTopLevel = 300;

// The item levels players gear at, and the lowest a table lists. The user's
// call: below 150 nothing is worth looking up.
constexpr int kLowestLevel = 150;
constexpr int kReferenceLevels[] = {150, 160, 200, 250};

// Star force below this star is the flat ladder; from it, the level table.
constexpr int kFirstHighStar = 15;

using Fields = std::vector<std::pair<std::string, std::string>>;

std::string Quote(std::string_view text) {
  std::string out = "\"";
  for (char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out += c;
    }
  }
  return out + "\"";
}

std::string Num(double value) {
  return absl::StrFormat("%.10g", value);
}

std::string List(const std::vector<std::string>& items) {
  return absl::StrCat("[", absl::StrJoin(items, ","), "]");
}

std::string Object(const Fields& fields) {
  std::vector<std::string> parts;
  for (const auto& [key, value] : fields) {
    parts.push_back(absl::StrCat(Quote(key), ":", value));
  }
  return absl::StrCat("{", absl::StrJoin(parts, ","), "}");
}

template <typename T, typename F>
std::string MapList(const std::vector<T>& items, F each) {
  std::vector<std::string> parts;
  for (const T& item : items) {
    parts.push_back(each(item));
  }
  return List(parts);
}

// `at(i)` for every i in [first, last], merged into runs of equal values:
// [[ceiling, value], ...], with the last ceiling null.
std::string Runs(int first, int last,
                 const std::function<std::string(int)>& at) {
  std::vector<std::string> runs;
  std::string current = at(first);
  for (int i = first + 1; i <= last + 1; ++i) {
    std::string next = i <= last ? at(i) : "";
    if (i <= last && next == current) {
      continue;
    }
    std::string ceiling = i <= last ? Num(i - 1) : "null";
    runs.push_back(List({ceiling, current}));
    current = next;
  }
  return List(runs);
}

std::vector<int> Ranks() {
  return {POTENTIAL_RANK_RARE, POTENTIAL_RANK_EPIC, POTENTIAL_RANK_UNIQUE,
          POTENTIAL_RANK_LEGENDARY};
}

// Every slot, in the game's slot order.
std::vector<EquipSlot> SlotsInOrder() {
  std::vector<EquipSlot> slots;
  for (int i = 1; i < EquipSlot_ARRAYSIZE; ++i) {
    if (EquipSlot_IsValid(i)) {
      slots.push_back(static_cast<EquipSlot>(i));
    }
  }
  std::stable_sort(slots.begin(), slots.end(), [](EquipSlot a, EquipSlot b) {
    return SlotOrder(a) < SlotOrder(b);
  });
  return slots;
}

// The names of `slots`, once each: four ring slots are one "Ring".
std::string SlotNames(const std::vector<EquipSlot>& slots) {
  std::vector<std::string> names;
  for (EquipSlot slot : slots) {
    std::string name = FormatSlot(slot);
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      names.push_back(name);
    }
  }
  return MapList(names, Quote);
}

std::string LevelList(const std::set<int>& levels) {
  return MapList(std::vector<int>(levels.begin(), levels.end()), Num);
}

std::string GroupName(PotentialGroup group) {
  switch (group) {
    case PotentialGroup::kWeaponry:
      return "Weaponry";
    case PotentialGroup::kHat:
      return "Hat";
    case PotentialGroup::kGloves:
      return "Gloves";
    case PotentialGroup::kArmor:
      return "Armor";
    case PotentialGroup::kAccessory:
      return "Accessory";
    case PotentialGroup::kNone:
      return "";
  }
  return "";
}

int CubeUnlockLevel(CubeType cube) {
  switch (cube) {
    case CubeType::kRed:
      return kPotentialUnlockLevel;
    case CubeType::kGreen:
      return kBonusPotentialUnlockLevel;
    case CubeType::kBlack:
      return kBlackCubeUnlockLevel;
    case CubeType::kWhite:
      return kWhiteCubeUnlockLevel;
  }
  return 0;
}

std::string PotentialJson() {
  const PotentialGroup groups[] = {
      PotentialGroup::kWeaponry, PotentialGroup::kHat, PotentialGroup::kGloves,
      PotentialGroup::kArmor, PotentialGroup::kAccessory};

  std::vector<std::string> cubes;
  for (const Cube& cube : kCubes) {
    std::vector<std::string> up, prime;
    for (int rank : Ranks()) {
      up.push_back(Num(
          PotentialRankUpChance(cube.type, static_cast<PotentialRank>(rank))));
    }
    for (int i = 0; i < kPotentialLines; ++i) {
      prime.push_back(Num(PotentialPrimeChance(cube.type, i)));
    }
    cubes.push_back(Object({
        {"name", Quote(CubeName(cube.type))},
        {"track",
         Quote(cube.track == PotentialTrack::kMain ? "main" : "bonus")},
        {"cost", Num(cube.cost)},
        {"unlock", Num(CubeUnlockLevel(cube.type))},
        {"choose", cube.choose ? "true" : "false"},
        {"up", List(up)},
        {"prime", List(prime)},
    }));
  }

  std::vector<std::string> group_json;
  for (PotentialGroup group : groups) {
    std::vector<EquipSlot> slots;
    for (EquipSlot slot : SlotsInOrder()) {
      if (PotentialGroupOf(slot) == group) {
        slots.push_back(slot);
      }
    }
    group_json.push_back(Object(
        {{"name", Quote(GroupName(group))}, {"slots", SlotNames(slots)}}));
  }
  std::vector<EquipSlot> without;
  for (EquipSlot slot : SlotsInOrder()) {
    if (!SlotTakesPotential(slot)) {
      without.push_back(slot);
    }
  }

  // pools[track][group][rank]: the line ids that roll there, catalog order.
  std::set<int> line_ids;
  Fields pools;
  for (PotentialTrack track : {PotentialTrack::kMain, PotentialTrack::kBonus}) {
    std::vector<std::string> by_group;
    for (PotentialGroup group : groups) {
      std::vector<std::string> by_rank;
      for (int rank : Ranks()) {
        std::vector<std::string> ids;
        for (PotentialLineType type :
             PotentialPool(track, group, static_cast<PotentialRank>(rank))) {
          ids.push_back(Num(type));
          line_ids.insert(type);
        }
        by_rank.push_back(List(ids));
      }
      by_group.push_back(List(by_rank));
    }
    pools.push_back(
        {track == PotentialTrack::kMain ? "main" : "bonus", List(by_group)});
  }

  // Each line's value and its text at every rank and item level.
  Fields lines;
  for (int id : line_ids) {
    const auto type = static_cast<PotentialLineType>(id);
    std::vector<std::string> by_rank;
    for (int rank : Ranks()) {
      PotentialLine line;
      line.set_type(type);
      line.set_rank(static_cast<PotentialRank>(rank));
      by_rank.push_back(Runs(kLowestLevel, kTopLevel, [&](int level) {
        int value = PotentialLineValue(type, line.rank(), level);
        return value == 0 ? std::string("null")
                          : List({Num(value),
                                  Quote(PotentialLineValueText(line, level))});
      }));
    }
    lines.push_back({Num(id), Object({{"name", Quote(PotentialLineName(type))},
                                      {"values", List(by_rank)}})});
  }

  const std::set<int> item_levels(std::begin(kReferenceLevels),
                                  std::end(kReferenceLevels));

  return Object({
      {"cubes", List(cubes)},
      {"groups", List(group_json)},
      {"no_potential", SlotNames(without)},
      {"pools", Object(pools)},
      {"lines", Object(lines)},
      {"item_levels", LevelList(item_levels)},
  });
}

// --- Star force ---

EquipStats Gains(EquipSlot slot, int level, const EquipStats& base, int stars) {
  EquipPrototype proto;
  proto.set_equip_slot(slot);
  proto.set_required_level(level);
  *proto.mutable_base_stats() = base;
  return EquipInstance(proto).StarForceStatGains(stars);
}

// What the attempt from `star` to star + 1 adds.
EquipStats Step(EquipSlot slot, int level, const EquipStats& base, int star) {
  EquipStats before = Gains(slot, level, base, star);
  EquipStats after = Gains(slot, level, base, star + 1);
  EquipStats step;
  step.set_str(after.str() - before.str());
  step.set_max_hp(after.max_hp() - before.max_hp());
  step.set_max_mp(after.max_mp() - before.max_mp());
  step.set_attack(after.attack() - before.attack());
  step.set_def(after.def() - before.def());
  return step;
}

EquipStats Base(int str, int attack, int def) {
  EquipStats base;
  base.set_str(str);
  base.set_attack(attack);
  base.set_def(def);
  return base;
}

// One attempt at 16★ and up: the stat every shown stat gains, the attack
// gained, and whether DEF keeps growing.
struct HighStep {
  int stat;
  int attack;
  bool def;
};

// The slots whose stars behave alike, and the model the page computes their
// gains with: a flat ladder to 15★, then the level table.
struct StarCategory {
  std::vector<EquipSlot> slots;
  int low_stat[kFirstHighStar];
  int low_hp[kFirstHighStar];
  int low_mp[kFirstHighStar];
  // Flat attack per attempt, for an item that shows attack (gloves).
  int low_attack[kFirstHighStar];
  // A weapon's attack grows by 1 + this share of what it has, and from 16★
  // only if it shows attack. Zero for everything else.
  int attack_percent;
  int def_percent;
  // Indexed by band, then by star from kFirstHighStar.
  std::vector<std::vector<HighStep>> high;
};

std::vector<HighStep> HighSteps(EquipSlot slot, int level) {
  std::vector<HighStep> steps;
  for (int s = kFirstHighStar; s < EquipTabItem::MaxStarsForLevel(level); ++s) {
    steps.push_back({Step(slot, level, Base(1, 0, 0), s).str(),
                     Step(slot, level, Base(0, 1, 0), s).attack(),
                     Step(slot, level, Base(0, 0, 100), s).def() > 0});
  }
  return steps;
}

std::string HighKey(const std::vector<HighStep>& steps) {
  std::string key;
  for (const HighStep& step : steps) {
    absl::StrAppend(&key, step.stat, "/", step.attack, "/", step.def, ";");
  }
  return key;
}

StarCategory Probe(EquipSlot slot) {
  constexpr int kLevel = 200;
  StarCategory c;
  c.slots = {slot};
  for (int s = 0; s < kFirstHighStar; ++s) {
    EquipStats plain = Step(slot, kLevel, EquipStats(), s);
    c.low_stat[s] = plain.str();
    c.low_hp[s] = plain.max_hp();
    c.low_mp[s] = plain.max_mp();
    c.low_attack[s] = Step(slot, kLevel, Base(0, 1, 0), s).attack();
  }
  // 1 + 100 x share / 100 at the first attempt.
  const int first_attack = Step(slot, kLevel, Base(0, 100, 0), 0).attack();
  c.attack_percent = first_attack > 1 ? first_attack - 1 : 0;
  if (c.attack_percent > 0) {
    std::fill(std::begin(c.low_attack), std::end(c.low_attack), 0);
  }
  const int first_def = Step(slot, kLevel, Base(0, 0, 100), 0).def();
  c.def_percent = first_def > 0 ? first_def - 1 : 0;
  return c;
}

std::string LowKey(const StarCategory& c) {
  std::string key = absl::StrCat(c.attack_percent, "|", c.def_percent, "|");
  for (int s = 0; s < kFirstHighStar; ++s) {
    absl::StrAppend(&key, c.low_stat[s], ",", c.low_hp[s], ",", c.low_mp[s],
                    ",", c.low_attack[s], ";");
  }
  return key;
}

int Scaled(int base, int gained, int percent) {
  return 1 + (base + gained) * percent / 100;
}

// The page's model of StarForceStatGains, in the page's own terms.
EquipStats Model(const StarCategory& c, const std::vector<HighStep>& high,
                 int base_attack, int base_def, int stars) {
  int stat = 0, hp = 0, mp = 0, attack = 0, def = 0;
  for (int s = 0; s < stars; ++s) {
    if (s < kFirstHighStar) {
      stat += c.low_stat[s];
      hp += c.low_hp[s];
      mp += c.low_mp[s];
      if (base_attack > 0) {
        attack += c.attack_percent > 0
                      ? Scaled(base_attack, attack, c.attack_percent)
                      : c.low_attack[s];
      }
      if (c.def_percent > 0 && base_def > 0) {
        def += Scaled(base_def, def, c.def_percent);
      }
      continue;
    }
    const HighStep& step = high[s - kFirstHighStar];
    stat += step.stat;
    if (c.attack_percent == 0 || base_attack > 0) {
      attack += step.attack;
    }
    if (step.def && base_def > 0) {
      def += Scaled(base_def, def, c.def_percent);
    }
  }
  EquipStats out;
  out.set_str(stat);
  out.set_max_hp(hp);
  out.set_max_mp(mp);
  out.set_attack(attack);
  out.set_def(def);
  return out;
}

// Fails if the model above no longer gives what the game gives.
void CheckModel(const StarCategory& c, const std::vector<int>& band_levels) {
  for (EquipSlot slot : c.slots) {
    for (size_t b = 0; b < band_levels.size(); ++b) {
      const int level = band_levels[b];
      for (int attack : {0, 1, 7, 50, 137, 276, 400}) {
        for (int def : {0, 1, 45, 100, 600}) {
          for (int stars = 0; stars <= EquipTabItem::MaxStarsForLevel(level);
               ++stars) {
            EquipStats game = Gains(slot, level, Base(1, attack, def), stars);
            EquipStats model = Model(c, c.high[b], attack, def, stars);
            CHECK(game.str() == model.str() &&
                  game.max_hp() == model.max_hp() &&
                  game.max_mp() == model.max_mp() &&
                  game.attack() == model.attack() && game.def() == model.def())
                << "The reference page's star force model no longer matches "
                << "the game: " << FormatSlot(slot) << " level " << level
                << ", ATT " << attack << ", DEF " << def << ", " << stars
                << " stars. Game " << game.ShortDebugString() << ", model "
                << model.ShortDebugString();
          }
        }
      }
    }
  }
}

std::string IntList(const int* values, int count) {
  std::vector<std::string> parts;
  for (int i = 0; i < count; ++i) {
    parts.push_back(Num(values[i]));
  }
  return List(parts);
}

std::string StarForceJson(const std::map<std::string, EquipPrototype>& equips) {
  std::set<EquipSlot> starred_slots;
  for (const auto& [key, proto] : equips) {
    if (Supports(proto, UPGRADE_STAR_FORCE) &&
        proto.equip_slot() != EQUIP_SLOT_UNSPECIFIED) {
      starred_slots.insert(proto.equip_slot());
    }
  }

  std::vector<StarCategory> categories;
  for (EquipSlot slot : SlotsInOrder()) {
    if (starred_slots.count(slot) == 0) {
      continue;
    }
    StarCategory probed = Probe(slot);
    auto same = std::find_if(categories.begin(), categories.end(),
                             [&](const StarCategory& c) {
                               return LowKey(c) == LowKey(probed) &&
                                      HighKey(HighSteps(c.slots[0], 200)) ==
                                          HighKey(HighSteps(slot, 200)) &&
                                      HighKey(HighSteps(c.slots[0], 250)) ==
                                          HighKey(HighSteps(slot, 250));
                             });
    if (same != categories.end()) {
      same->slots.push_back(slot);
    } else {
      categories.push_back(probed);
    }
  }
  CHECK(!categories.empty()) << "No item in the catalog takes star force";

  // Bands of item level where max stars and every category's table agree.
  std::vector<int> band_levels;
  std::vector<std::string> bands;
  std::string previous;
  for (int level = kLowestLevel; level <= kTopLevel; ++level) {
    std::string key = Num(EquipTabItem::MaxStarsForLevel(level));
    for (const StarCategory& c : categories) {
      absl::StrAppend(&key, "#", HighKey(HighSteps(c.slots[0], level)));
    }
    if (key == previous) {
      continue;
    }
    previous = key;
    band_levels.push_back(level);
  }
  for (size_t b = 0; b < band_levels.size(); ++b) {
    const int lo = band_levels[b];
    bands.push_back(Object({
        {"lo", Num(lo)},
        {"hi", b + 1 < band_levels.size() ? Num(band_levels[b + 1] - 1)
                                          : std::string("null")},
        {"max_stars", Num(EquipTabItem::MaxStarsForLevel(lo))},
    }));
  }

  std::vector<std::string> category_json;
  for (StarCategory& c : categories) {
    for (int level : band_levels) {
      c.high.push_back(HighSteps(c.slots[0], level));
    }
    CheckModel(c, band_levels);
    std::vector<std::string> high;
    for (const std::vector<HighStep>& steps : c.high) {
      high.push_back(MapList(steps, [](const HighStep& step) {
        return List(
            {Num(step.stat), Num(step.attack), step.def ? "true" : "false"});
      }));
    }
    category_json.push_back(Object({
        {"slots", SlotNames(c.slots)},
        {"low_stat", IntList(c.low_stat, kFirstHighStar)},
        {"low_hp", IntList(c.low_hp, kFirstHighStar)},
        {"low_mp", IntList(c.low_mp, kFirstHighStar)},
        {"low_attack", IntList(c.low_attack, kFirstHighStar)},
        {"attack_percent", Num(c.attack_percent)},
        {"def_percent", Num(c.def_percent)},
        {"high", List(high)},
    }));
  }

  std::vector<std::string> rates;
  for (int s = 0; s < kMaxStarForce; ++s) {
    StarForceRate rate = EquipInstance::RateAt(s);
    rates.push_back(List({Num(rate.success), Num(rate.destroy)}));
  }

  Fields costs;
  for (int level : kReferenceLevels) {
    std::vector<std::string> per_star;
    for (int s = 0; s < EquipTabItem::MaxStarsForLevel(level); ++s) {
      per_star.push_back(Num(StarForceCost(level, s)));
    }
    costs.push_back({Num(level), List(per_star)});
  }

  // By name: items only ever name a family's first slot, so the other three
  // ring slots hold no item of their own but still take stars.
  std::set<std::string> starred_names;
  for (EquipSlot slot : starred_slots) {
    starred_names.insert(FormatSlot(slot));
  }
  std::vector<EquipSlot> unstarred;
  for (EquipSlot slot : SlotsInOrder()) {
    if (starred_names.count(FormatSlot(slot)) == 0) {
      unstarred.push_back(slot);
    }
  }

  return Object({
      {"rates", List(rates)},
      {"max_stars", Runs(kLowestLevel, kTopLevel,
                         [](int level) {
                           return Num(EquipTabItem::MaxStarsForLevel(level));
                         })},
      {"recovery", Runs(0, kMaxStarForce - 1,
                        [](int stars) {
                          return Num(EquipInstance::RecoveryStars(stars));
                        })},
      {"cost", Object(costs)},
      {"bands", List(bands)},
      {"categories", List(category_json)},
      {"no_stars", SlotNames(unstarred)},
  });
}

// --- Inner Ability ---

std::vector<AbilityRank> AbilityRanks() {
  return {ABILITY_RANK_RARE, ABILITY_RANK_EPIC, ABILITY_RANK_UNIQUE,
          ABILITY_RANK_LEGENDARY};
}

std::string InnerAbilityJson() {
  std::vector<std::string> lines;
  for (int i = 1; i < AbilityLineType_ARRAYSIZE; ++i) {
    if (!AbilityLineType_IsValid(i)) {
      continue;
    }
    const auto type = static_cast<AbilityLineType>(i);
    std::vector<std::string> values, texts, weights;
    bool rolls = false;
    for (AbilityRank rank : AbilityRanks()) {
      AbilityLine line;
      line.set_type(type);
      line.set_rank(rank);
      values.push_back(Num(AbilityLineValue(type, rank)));
      texts.push_back(Quote(AbilityLineValueText(line)));
      weights.push_back(Num(AbilityTypeWeight(type, rank)));
      rolls |= AbilityTypeWeight(type, rank) > 0;
    }
    if (!rolls) {
      continue;
    }
    lines.push_back(Object({{"name", Quote(AbilityLineName(type))},
                            {"values", List(values)},
                            {"texts", List(texts)},
                            {"weights", List(weights)}}));
  }

  std::vector<std::string> up, cost;
  for (AbilityRank rank : AbilityRanks()) {
    up.push_back(Num(AbilityRankUpChance(rank)));
    std::vector<std::string> by_locks;
    for (int locked = 0; locked <= kMaxLockedAbilityLines; ++locked) {
      by_locks.push_back(Num(AbilityResetCost(rank, locked)));
    }
    cost.push_back(List(by_locks));
  }

  // The ranks lines 2 and 3 roll at, by the ability's rank: [rank index,
  // chance] pairs. Mirrors RolledLineRank.
  const std::string lower = List({
      List({List({"0", "1"})}),
      List({List({"0", "1"})}),
      List({List({"1", Num(kEpicChanceUnderUnique)}),
            List({"0", Num(1 - kEpicChanceUnderUnique)})}),
      List({List({"2", Num(kUniqueChanceUnderLegendary)}),
            List({"1", Num(1 - kUniqueChanceUnderLegendary)})}),
  });

  const AbilityPreset start = DefaultAbilityPreset();
  return Object({
      {"lines", List(lines)},
      {"up", List(up)},
      {"cost", List(cost)},
      {"lower", lower},
      {"unlock", Num(kInnerAbilityUnlockLevel)},
      {"max_locked", Num(kMaxLockedAbilityLines)},
      {"start", Quote(absl::StrCat(
                    start.lines_size(), " ", AbilityRankName(start.rank()), " ",
                    AbilityLineName(start.lines(0).type()), " ",
                    AbilityLineValueText(start.lines(0)), " lines"))},
  });
}

// --- Flames ---

std::vector<int> FlameTiers() {
  std::set<int> tiers;
  for (const Flame& flame : kFlames) {
    for (int t = flame.min_tier; t < flame.min_tier + kFlameTiers; ++t) {
      tiers.insert(t);
    }
  }
  return std::vector<int>(tiers.begin(), tiers.end());
}

EquipPrototype FlameProbe(bool weapon, int level, int base_attack) {
  EquipPrototype proto;
  proto.set_equip_slot(weapon ? EQUIP_SLOT_PRIMARY_WEAPON : EQUIP_SLOT_HAT);
  proto.set_required_level(level);
  proto.mutable_base_stats()->set_attack(base_attack);
  proto.mutable_base_stats()->set_magic_attack(base_attack);
  return proto;
}

bool ScalesWithBase(bool weapon, FlameStat stat) {
  return weapon &&
         (stat == FLAME_STAT_ATTACK || stat == FLAME_STAT_MAGIC_ATTACK);
}

// A line's value at each tier, or null where it scales with the weapon's own
// ATT: the page computes those from att_percent.
std::string FlameValues(bool weapon, FlameStat stat, int level) {
  if (ScalesWithBase(weapon, stat)) {
    return "null";
  }
  std::vector<std::string> values;
  for (int tier : FlameTiers()) {
    FlameLine line;
    line.set_stat(stat);
    line.set_tier(tier);
    values.push_back(Num(FlameLineValue(line, FlameProbe(weapon, level, 0))));
  }
  return List(values);
}

std::string AttackPercents(int level) {
  std::vector<std::string> percents;
  for (int tier : FlameTiers()) {
    percents.push_back(Num(FlameWeaponAttackPercent(level, tier)));
  }
  return List(percents);
}

// Fails if the page's rounding of att_percent no longer gives the game's ATT.
void CheckFlameAttack(int level) {
  for (int tier : FlameTiers()) {
    const double percent =
        std::stod(Num(FlameWeaponAttackPercent(level, tier)));
    for (int base = 1; base <= 500; ++base) {
      FlameLine line;
      line.set_stat(FLAME_STAT_ATTACK);
      line.set_tier(tier);
      const int game = FlameLineValue(line, FlameProbe(true, level, base));
      const int page =
          static_cast<int>(std::ceil(base * percent / 100.0 - 1e-9));
      CHECK_EQ(game, page) << "The reference page's flame ATT model no "
                           << "longer matches the game: level " << level
                           << ", tier " << tier << ", base " << base;
    }
  }
}

std::string FlameJson() {
  std::vector<std::string> flames;
  for (const Flame& flame : kFlames) {
    std::vector<std::string> tiers;
    for (int t = flame.min_tier; t < flame.min_tier + kFlameTiers; ++t) {
      tiers.push_back(Num(t));
    }
    flames.push_back(Object({{"name", Quote(FlameName(flame.type))},
                             {"cost", Num(flame.cost)},
                             {"tiers", List(tiers)},
                             {"choose", flame.choose ? "true" : "false"}}));
  }

  std::vector<EquipSlot> with, without;
  std::set<std::string> with_names;
  for (EquipSlot slot : SlotsInOrder()) {
    if (SlotTakesFlame(slot)) {
      with.push_back(slot);
      with_names.insert(FormatSlot(slot));
    }
  }
  for (EquipSlot slot : SlotsInOrder()) {
    if (with_names.count(FormatSlot(slot)) == 0) {
      without.push_back(slot);
    }
  }
  std::vector<EquipSlot> armor;
  for (EquipSlot slot : with) {
    if (slot != EQUIP_SLOT_PRIMARY_WEAPON) {
      armor.push_back(slot);
    }
  }

  Fields stats;
  for (int i = 1; i < FlameStat_ARRAYSIZE; ++i) {
    if (!FlameStat_IsValid(i)) {
      continue;
    }
    FlameLine line;
    line.set_stat(static_cast<FlameStat>(i));
    line.set_tier(FlameTiers().front());
    const std::string text = FlameLineValueText(line, FlameProbe(true, 200, 1));
    stats.push_back(
        {Num(i), Object({{"name", Quote(FlameStatName(line.stat()))},
                         {"percent", text.back() == '%' ? "true" : "false"}})});
  }

  std::vector<std::string> kinds;
  for (bool weapon : {true, false}) {
    Fields levels;
    std::set<int> ids;
    for (int level : kReferenceLevels) {
      if (weapon) {
        CheckFlameAttack(level);
      }
      Fields values;
      std::vector<std::string> pool;
      for (FlameStat stat : FlamePool(FlameProbe(weapon, level, 0))) {
        pool.push_back(Num(stat));
        ids.insert(stat);
        values.push_back({Num(stat), FlameValues(weapon, stat, level)});
      }
      levels.push_back(
          {Num(level),
           Object({{"pool", List(pool)},
                   {"values", Object(values)},
                   {"att_percent", weapon ? AttackPercents(level) : "null"}})});
    }
    // Every line's values over the whole level range, for the raw tables.
    Fields raw;
    for (int id : ids) {
      const auto stat = static_cast<FlameStat>(id);
      raw.push_back({Num(id), Runs(kLowestLevel, kTopLevel, [&](int level) {
                       return ScalesWithBase(weapon, stat)
                                  ? AttackPercents(level)
                                  : FlameValues(weapon, stat, level);
                     })});
    }
    kinds.push_back(Object({
        {"name", Quote(weapon ? "Weapon" : "Armor & accessories")},
        {"weapon", weapon ? "true" : "false"},
        {"slots",
         SlotNames(weapon ? std::vector<EquipSlot>{EQUIP_SLOT_PRIMARY_WEAPON}
                          : armor)},
        {"levels", Object(levels)},
        {"raw", Object(raw)},
    }));
  }

  return Object({
      {"flames", List(flames)},
      {"unlock", Num(kFlameUnlockLevel)},
      {"lines", Num(kFlameLines)},
      {"tiers", MapList(FlameTiers(), Num)},
      {"stats", Object(stats)},
      {"kinds", List(kinds)},
      {"slots", SlotNames(with)},
      {"no_flame", SlotNames(without)},
  });
}

}  // namespace

std::string ReferenceDataJson(
    const std::map<std::string, EquipPrototype>& equips) {
  std::vector<std::string> ranks;
  for (int rank : Ranks()) {
    ranks.push_back(Quote(PotentialRankName(static_cast<PotentialRank>(rank))));
  }
  return Object({
      {"ranks", List(ranks)},
      {"levels", MapList(std::vector<int>(std::begin(kReferenceLevels),
                                          std::end(kReferenceLevels)),
                         Num)},
      {"potential", PotentialJson()},
      {"star_force", StarForceJson(equips)},
      {"inner_ability", InnerAbilityJson()},
      {"flame", FlameJson()},
  });
}

std::string ReferencePage(const std::string& page_template,
                          const std::string& json) {
  std::string page = page_template;
  const size_t at = page.find(kReferenceDataPlaceholder);
  CHECK(at != std::string::npos)
      << "The page template has no " << kReferenceDataPlaceholder;
  page.replace(at, std::string_view(kReferenceDataPlaceholder).size(), json);
  return page;
}

}  // namespace ms
