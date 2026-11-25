#include "src/bis_search.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.h"
#include "src/character/account/familiar.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/exp_table.h"
#include "src/character/force/symbol.h"
#include "src/character/inner_ability.h"
#include "src/character/matrix/hexa_matrix.h"
#include "src/character/matrix/hexa_stat.h"
#include "src/character/max_character.h"
#include "src/character/progression.h"
#include "src/character/stat_preset.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/item/upgrade/flame.h"
#include "src/item/upgrade/potential.h"

namespace ms {
namespace {

// Items a slot measures by fight, after a swing has ranked the rest. A slot
// can offer forty rings; the swing sees everything an item gives but a
// cooldown or a buff, and no item's worth is mostly those.
constexpr int kItemShortlist = 4;
// The same for a line, of which a pool offers ten to twenty.
constexpr int kLineShortlist = 3;
// Whole passes before giving up on settling. Each settles most of what the
// last moved; the third has never been seen to move more than a line.
constexpr int kMaxPasses = 4;
// A choice must beat the one held by this share to replace it, so a tie or
// float noise keeps what is there.
constexpr double kMinGain = 1e-6;

constexpr int kFlameTop = 7;

StatPreset BossSlot() {
  return AutoswapSlotFor(Activity::kBossing);
}

PotentialLineType AttackPctLine(StatField primary) {
  return primary == STAT_FIELD_INT ? POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT
                                   : POTENTIAL_LINE_TYPE_ATTACK_PCT;
}

PotentialLineType StatPctLine(StatField primary, PotentialTrack track) {
  const bool bonus = track == PotentialTrack::kBonus;
  switch (primary) {
    case STAT_FIELD_DEX:
      return bonus ? POTENTIAL_LINE_TYPE_BONUS_DEX_PCT
                   : POTENTIAL_LINE_TYPE_DEX_PCT;
    case STAT_FIELD_INT:
      return bonus ? POTENTIAL_LINE_TYPE_BONUS_INT_PCT
                   : POTENTIAL_LINE_TYPE_INT_PCT;
    case STAT_FIELD_LUK:
      return bonus ? POTENTIAL_LINE_TYPE_BONUS_LUK_PCT
                   : POTENTIAL_LINE_TYPE_LUK_PCT;
    default:
      return bonus ? POTENTIAL_LINE_TYPE_BONUS_STR_PCT
                   : POTENTIAL_LINE_TYPE_STR_PCT;
  }
}

Potential DefaultPotential(EquipSlot slot, StatField primary,
                           PotentialTrack track) {
  Potential potential;
  const PotentialGroup group = PotentialGroupOf(slot);
  if (group == PotentialGroup::kNone) {
    return potential;
  }
  const PotentialLineType type = group == PotentialGroup::kWeaponry
                                     ? AttackPctLine(primary)
                                     : StatPctLine(primary, track);
  potential.set_rank(POTENTIAL_RANK_LEGENDARY);
  for (int i = 0; i < kPotentialLines; ++i) {
    PotentialLine& line = *potential.add_lines();
    line.set_type(type);
    line.set_rank(POTENTIAL_RANK_LEGENDARY);
  }
  return potential;
}

// `from` on `proto`: its lines the item's pool has, each once, at the top
// tier, then the pool in order until there are four.
FlameLines FlameFor(const EquipPrototype& proto, const FlameLines& from) {
  const std::vector<FlameStat> pool = FlamePool(proto);
  FlameLines lines;
  std::set<FlameStat> taken;
  auto add = [&](FlameStat stat) {
    if (lines.size() < kFlameLines &&
        std::find(pool.begin(), pool.end(), stat) != pool.end() &&
        taken.insert(stat).second) {
      FlameLine& line = *lines.Add();
      line.set_stat(stat);
      line.set_tier(kFlameTop);
    }
  };
  for (const FlameLine& line : from) {
    add(line.stat());
  }
  for (FlameStat stat : pool) {
    add(stat);
  }
  return lines;
}

FlameLines DefaultFlame(const EquipPrototype& proto, Job job) {
  return FlameFor(proto, MaxFlameFor(proto, kMaxLevel, PrimaryStatField(job),
                                     SecondaryStatField(job)));
}

// The first of `wanted` the pool has, else the pool's first.
template <typename T>
T FirstIn(const std::vector<T>& pool, std::initializer_list<T> wanted) {
  for (T type : wanted) {
    if (std::find(pool.begin(), pool.end(), type) != pool.end()) {
      return type;
    }
  }
  return pool.empty() ? T{} : pool.front();
}

FamiliarBook BisFamiliars() {
  const std::vector<FamiliarLineType> pool =
      FamiliarPool(POTENTIAL_RANK_LEGENDARY);
  FamiliarBook book;
  for (const FamiliarSpecies& species : FamiliarRoster()) {
    Familiar& familiar = *book.add_familiars();
    familiar.set_name(species.name);
    familiar.set_level(kFamiliarMaxLevel);
    for (FamiliarLineType type :
         {FirstIn(pool, {FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40}),
          FirstIn(pool, {FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_40})}) {
      FamiliarLine& line = *familiar.add_lines();
      line.set_type(type);
      line.set_rank(POTENTIAL_RANK_LEGENDARY);
    }
  }
  return book;
}

// The rank each Inner Ability line can hold at best: the top one Legendary,
// the other two a rank below, as RerollAbility rolls them.
AbilityRank AbilityLineRank(int index) {
  return index == 0 ? ABILITY_RANK_LEGENDARY : ABILITY_RANK_UNIQUE;
}

std::vector<AbilityLineType> AbilityPool(AbilityRank rank) {
  std::vector<AbilityLineType> pool;
  const google::protobuf::EnumDescriptor* descriptor =
      AbilityLineType_descriptor();
  for (int i = 0; i < descriptor->value_count(); ++i) {
    const AbilityLineType type =
        static_cast<AbilityLineType>(descriptor->value(i)->number());
    if (AbilityTypeWeight(type, rank) > 0) {
      pool.push_back(type);
    }
  }
  return pool;
}

AbilityPreset DefaultAbility() {
  AbilityPreset preset;
  preset.set_rank(ABILITY_RANK_LEGENDARY);
  std::set<AbilityLineType> taken;
  for (int i = 0; i < kAbilityLines; ++i) {
    std::vector<AbilityLineType> pool;
    for (AbilityLineType type : AbilityPool(AbilityLineRank(i))) {
      if (taken.count(type) == 0) {
        pool.push_back(type);
      }
    }
    const AbilityLineType type =
        FirstIn(pool, {ABILITY_LINE_TYPE_BOSS_DAMAGE, ABILITY_LINE_TYPE_ATTACK,
                       ABILITY_LINE_TYPE_CRIT_RATE});
    taken.insert(type);
    AbilityLine& line = *preset.add_lines();
    line.set_type(type);
    line.set_rank(AbilityLineRank(i));
  }
  return preset;
}

// Every HEXA node raised as far as it goes, paid for as it is bought. Repeated
// because a node can need another first.
void BuyHexaMatrix(GameState& state) {
  CharacterInstance& character = state.character;
  for (bool grew = true; grew;) {
    grew = false;
    for (const auto& [name, skill] : state.skills) {
      if (skill.hexa_node() == HEXA_NODE_KIND_UNSPECIFIED ||
          !character.ReachesHexaNode(skill)) {
        continue;
      }
      const int room =
          std::min(SkillMaxLevel(skill), MaxHexaNodeLevel(skill.hexa_node())) -
          character.skill_level(skill);
      if (room <= 0) {
        continue;
      }
      const HexaPrice price = character.HexaNodePriceFor(skill, room);
      character.AddSolErdaEnergy(price.energy);
      character.AddSolErdaFragments(price.fragments);
      grew = character.LearnSkill(skill, room) || grew;
    }
  }
}

constexpr HexaStat kHexaStats[] = {
    HEXA_STAT_CRIT_DAMAGE, HEXA_STAT_BOSS_DAMAGE, HEXA_STAT_IGNORE_DEFENSE,
    HEXA_STAT_DAMAGE,      HEXA_STAT_ATTACK,      HEXA_STAT_MAIN_STAT,
};

HexaStats DefaultHexaStats(int level) {
  // Three primaries, and each additional stat on two nodes: GMS's limit.
  const HexaStat picks[kHexaStatNodes][3] = {
      {HEXA_STAT_BOSS_DAMAGE, HEXA_STAT_ATTACK, HEXA_STAT_MAIN_STAT},
      {HEXA_STAT_IGNORE_DEFENSE, HEXA_STAT_ATTACK, HEXA_STAT_DAMAGE},
      {HEXA_STAT_CRIT_DAMAGE, HEXA_STAT_MAIN_STAT, HEXA_STAT_DAMAGE},
  };
  HexaStats stats;
  for (int i = 0; i < HexaStatNodesOpen(level); ++i) {
    HexaStatNode& node = *stats.add_nodes();
    node.set_primary(picks[i][0]);
    node.set_first(picks[i][1]);
    node.set_second(picks[i][2]);
    node.set_primary_level(kHexaStatMaxLevel);
    node.set_first_level(kHexaStatNodeLevels - kHexaStatMaxLevel);
  }
  return stats;
}

// Wears `proto` in `slot` built as `built`, any displaced piece discarded.
void WearBuilt(CharacterInstance& character, EquipSlot slot,
               const EquipPrototype& proto, const Equip& built) {
  character.ClearEquipInventory();
  const int row = static_cast<int>(character.inventory().size());
  if (character.PickUp(std::make_unique<EquipInstance>(proto, built))) {
    character.Equip(row, StatPreset::kFirst, slot);
  }
  character.ClearEquipInventory();
}

void RespendMaxAllocations(GameState& state) {
  SpendMaxLegion(state.character, state.skills, state.bosses, state.mobs);
  SpendMaxNoblesse(state.character, state.skills, state.bosses, state.mobs);
  SpendMaxHyperStats(state.character, state.skills, state.bosses, state.mobs);
}

bool Conflicts(const EquipPrototype& a, const EquipPrototype& b) {
  return a.name() == b.name() ||
         (a.has_equipment_skill() && b.has_equipment_skill() &&
          a.equipment_skill().skill() == b.equipment_skill().skill());
}

class Search {
 public:
  Search(GameState& state, const BisRate& rate) : state_(state), rate_(rate) {
  }

  void Run() {
    best_ = rate_(state_);
    for (int pass = 0; pass < kMaxPasses; ++pass) {
      const double start = best_;
      Gear();
      Scrolls();
      Potentials();
      Flames();
      Souls();
      Familiars();
      Ability();
      Hexa();
      // The allocations max mode ranks by swing, re-read for the new gear.
      RespendMaxAllocations(state_);
      best_ = rate_(state_);
      if (best_ <= start * (1.0 + 1e-4)) {
        break;
      }
    }
  }

 private:
  CharacterInstance& c() {
    return state_.character;
  }

  // Chooses among `count` alternatives to what is held: `apply(k)` puts the
  // k-th on the character and `apply(-1)` puts back what was held. A swing
  // ranks them all and the fight measures the leaders, with every one
  // `blind(k)` says a swing can't see. Leaves the winner on, or what was held,
  // and returns its index or -1.
  int Pick(int count, const std::function<void(int)>& apply,
           const std::function<bool(int)>& blind, int shortlist) {
    std::vector<std::pair<double, int>> ranked;
    std::vector<int> finalists;
    for (int k = 0; k < count; ++k) {
      apply(k);
      if (blind(k)) {
        finalists.push_back(k);
      } else {
        ranked.push_back({BisSwingRate(state_), k});
      }
    }
    std::stable_sort(
        ranked.begin(), ranked.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    for (int i = 0; i < static_cast<int>(ranked.size()) && i < shortlist; ++i) {
      finalists.push_back(ranked[i].second);
    }
    int chosen = -1;
    for (int k : finalists) {
      apply(k);
      const double rate = rate_(state_);
      if (rate > best_ * (1.0 + kMinGain)) {
        best_ = rate;
        chosen = k;
      }
    }
    apply(chosen);
    return chosen;
  }

  // Wears `proto` in `slot` built as `built`, any displaced piece discarded.
  void Wear(EquipSlot slot, const EquipPrototype& proto, const Equip& built) {
    WearBuilt(c(), slot, proto, built);
  }

  // `proto` built for `slot`, carrying over what `held` has: its potentials,
  // the flame lines `proto`'s pool allows, the weapon's soul.
  Equip Dressed(EquipSlot slot, const EquipPrototype& proto,
                const EquipInstance* held) {
    Equip built = BisUpgradedWith(state_, proto, DefaultScroll(proto));
    const EquipInstance probe(proto, built);
    const StatField primary = PrimaryStatField(c().proto().job());
    if (probe.CanCube()) {
      for (PotentialTrack track :
           {PotentialTrack::kMain, PotentialTrack::kBonus}) {
        const bool carried =
            held != nullptr && held->CanCube() &&
            PotentialOf(held->equip_state(), track).lines_size() ==
                kPotentialLines;
        *MutablePotentialOf(built, track) =
            carried ? PotentialOf(held->equip_state(), track)
                    : DefaultPotential(slot, primary, track);
      }
    }
    if (probe.CanFlame()) {
      *built.mutable_flame() =
          held != nullptr && held->equip_state().flame_size() > 0
              ? FlameFor(proto, held->equip_state().flame())
              : DefaultFlame(proto, c().proto().job());
    }
    if (held != nullptr && held->equip_state().has_soul() &&
        probe.CanTakeSoul()) {
      *built.mutable_soul() = held->equip_state().soul();
    }
    return built;
  }

  std::vector<EquipSlot> GearSlots() const {
    std::vector<EquipSlot> slots;
    const google::protobuf::EnumDescriptor* descriptor = EquipSlot_descriptor();
    for (int i = 0; i < descriptor->value_count(); ++i) {
      const EquipSlot slot =
          static_cast<EquipSlot>(descriptor->value(i)->number());
      if (slot != EQUIP_SLOT_UNSPECIFIED && !FamilyTakesCopies(slot)) {
        slots.push_back(slot);
      }
    }
    // The weapon first: every other piece's worth is read through it.
    std::stable_partition(slots.begin(), slots.end(), [](EquipSlot slot) {
      return slot == EQUIP_SLOT_PRIMARY_WEAPON;
    });
    return slots;
  }

  // What `slot` could hold besides `held`: anything the character can wear
  // there that isn't already worn elsewhere in its family. Symbols are set
  // apart, at their maximum, and pets keep max mode's.
  std::vector<const EquipPrototype*> Candidates(EquipSlot slot,
                                                const EquipInstance* held) {
    std::vector<const EquipPrototype*> found;
    const std::vector<EquipSlot> family = SlotFamily(slot);
    for (const auto& [key, proto] : state_.equips) {
      // A Genesis weapon waits for the level the Liberation opens at.
      if (IsSymbol(proto) || SlotFamily(proto.equip_slot()) != family ||
          !c().MeetsLevel(proto) || !c().MeetsJob(proto) ||
          (IsGenesisWeapon(proto) && c().proto().level() < kLiberationLevel) ||
          (held != nullptr && held->prototype().name() == proto.name())) {
        continue;
      }
      bool clash = false;
      for (EquipSlot other : family) {
        const EquipInstance* worn = c().WornAt(StatPreset::kFirst, other);
        clash = clash || (other != slot && worn != nullptr &&
                          Conflicts(worn->prototype(), proto));
      }
      if (!clash) {
        found.push_back(&proto);
      }
    }
    return found;
  }

  void Gear() {
    for (EquipSlot slot : GearSlots()) {
      const EquipInstance* worn = c().WornAt(StatPreset::kFirst, slot);
      std::unique_ptr<EquipInstance> held;
      if (worn != nullptr) {
        held = std::make_unique<EquipInstance>(worn->prototype(),
                                               worn->equip_state());
      }
      const std::vector<const EquipPrototype*> options =
          Candidates(slot, held.get());
      if (options.empty()) {
        continue;
      }
      Pick(
          static_cast<int>(options.size()),
          [&](int k) {
            if (k >= 0) {
              Wear(slot, *options[k], Dressed(slot, *options[k], held.get()));
            } else if (held != nullptr) {
              Wear(slot, held->prototype(), held->equip_state());
            } else {
              c().Unequip(slot);
              c().ClearEquipInventory();
            }
          },
          // A skill ring's buff is only seen by the fight.
          [&](int k) { return options[k]->has_equipment_skill(); },
          kItemShortlist);
    }
  }

  const Scroll* DefaultScroll(const EquipPrototype& proto) {
    const std::vector<const Scroll*> choices = BisScrollChoices(state_, proto);
    return choices.empty() ? nullptr : choices.front();
  }

  // Every worn piece's scroll, tried against the others it could take.
  void Scrolls() {
    for (EquipSlot slot : GearSlots()) {
      const EquipInstance* worn = c().WornAt(StatPreset::kFirst, slot);
      if (worn == nullptr) {
        continue;
      }
      const EquipPrototype proto = worn->prototype();
      const Equip held = worn->equip_state();
      const std::vector<const Scroll*> choices =
          BisScrollChoices(state_, proto);
      std::vector<Equip> builds;
      for (const Scroll* scroll : choices) {
        Equip built = BisUpgradedWith(state_, proto, scroll);
        *built.mutable_main_potential() = held.main_potential();
        *built.mutable_bonus_potential() = held.bonus_potential();
        *built.mutable_flame() = held.flame();
        if (held.has_soul()) {
          *built.mutable_soul() = held.soul();
        }
        builds.push_back(std::move(built));
      }
      Pick(
          static_cast<int>(builds.size()),
          [&](int k) { Wear(slot, proto, k >= 0 ? builds[k] : held); },
          [](int) { return false; }, kLineShortlist);
    }
  }

  void Potentials() {
    for (EquipSlot slot : GearSlots()) {
      const EquipInstance* worn = c().WornAt(StatPreset::kFirst, slot);
      if (worn == nullptr || !worn->CanCube()) {
        continue;
      }
      const PotentialGroup group = PotentialGroupOf(slot);
      for (PotentialTrack track :
           {PotentialTrack::kMain, PotentialTrack::kBonus}) {
        Potential kept = PotentialOf(
            c().WornAt(StatPreset::kFirst, slot)->equip_state(), track);
        const std::vector<PotentialLineType> pool =
            PotentialPool(track, group, POTENTIAL_RANK_LEGENDARY);
        for (int i = 0; i < kept.lines_size(); ++i) {
          const int k = Pick(
              static_cast<int>(pool.size()),
              [&](int k) {
                Potential trial = kept;
                if (k >= 0) {
                  trial.mutable_lines(i)->set_type(pool[k]);
                }
                c().TakePotential(slot, track, trial);
              },
              [&](int k) {
                return pool[k] == POTENTIAL_LINE_TYPE_COOLDOWN_1 ||
                       pool[k] == POTENTIAL_LINE_TYPE_COOLDOWN_2;
              },
              kLineShortlist);
          if (k >= 0) {
            kept.mutable_lines(i)->set_type(pool[k]);
          }
        }
      }
    }
  }

  void Flames() {
    for (EquipSlot slot : GearSlots()) {
      const EquipInstance* worn = c().WornAt(StatPreset::kFirst, slot);
      if (worn == nullptr || !worn->CanFlame()) {
        continue;
      }
      const std::vector<FlameStat> pool = FlamePool(worn->prototype());
      FlameLines kept = worn->equip_state().flame();
      for (int i = 0; i < kept.size(); ++i) {
        std::vector<FlameStat> free;
        for (FlameStat stat : pool) {
          bool taken = false;
          for (const FlameLine& line : kept) {
            taken = taken || line.stat() == stat;
          }
          if (!taken) {
            free.push_back(stat);
          }
        }
        const int k = Pick(
            static_cast<int>(free.size()),
            [&](int k) {
              FlameLines trial = kept;
              if (k >= 0) {
                trial.Mutable(i)->set_stat(free[k]);
              }
              c().TakeFlame(slot, trial);
            },
            [](int) { return false; }, kLineShortlist);
        if (k >= 0) {
          kept.Mutable(i)->set_stat(free[k]);
        }
      }
    }
  }

  void Souls() {
    const EquipInstance* weapon =
        c().WornAt(StatPreset::kFirst, EQUIP_SLOT_PRIMARY_WEAPON);
    if (weapon == nullptr || !weapon->equip_state().has_soul()) {
      return;
    }
    const Soul held = weapon->equip_state().soul();
    std::vector<SoulLine> lines;
    for (int line = SOUL_LINE_ATTACK; line <= SOUL_LINE_BOSS_DAMAGE; ++line) {
      if (line != held.line()) {
        lines.push_back(static_cast<SoulLine>(line));
      }
    }
    Pick(
        static_cast<int>(lines.size()),
        [&](int k) {
          Soul trial = held;
          if (k >= 0) {
            trial.set_line(lines[k]);
          }
          c().TakeSoul(EQUIP_SLOT_PRIMARY_WEAPON, trial);
        },
        [](int) { return false; }, kLineShortlist);
  }

  void Familiars() {
    std::vector<std::string> summoned(
        c().summoned_familiars(BossSlot()).begin(),
        c().summoned_familiars(BossSlot()).end());
    if (summoned.empty()) {
      summoned.assign(StarterFamiliars().names().begin(),
                      StarterFamiliars().names().end());
    }
    const std::vector<FamiliarLineType> pool =
        FamiliarPool(POTENTIAL_RANK_LEGENDARY);
    FamiliarBook kept = c().familiars();
    for (const std::string& name : summoned) {
      for (int f = 0; f < kept.familiars_size(); ++f) {
        if (kept.familiars(f).name() != name) {
          continue;
        }
        for (int i = 0; i < kept.familiars(f).lines_size(); ++i) {
          const int k = Pick(
              static_cast<int>(pool.size()),
              [&](int k) {
                FamiliarBook trial = kept;
                if (k >= 0) {
                  trial.mutable_familiars(f)->mutable_lines(i)->set_type(
                      pool[k]);
                }
                c().set_familiars(trial);
              },
              [](int) { return false; }, kLineShortlist);
          if (k >= 0) {
            kept.mutable_familiars(f)->mutable_lines(i)->set_type(pool[k]);
          }
        }
      }
    }
  }

  void Ability() {
    if (!c().inner_ability_unlocked()) {
      return;
    }
    AbilityPreset kept = c().ability(BossSlot());
    for (int i = 0; i < kept.lines_size(); ++i) {
      std::vector<AbilityLineType> free;
      for (AbilityLineType type : AbilityPool(AbilityLineRank(i))) {
        bool taken = false;
        for (const AbilityLine& line : kept.lines()) {
          taken = taken || line.type() == type;
        }
        if (!taken) {
          free.push_back(type);
        }
      }
      const int k = Pick(
          static_cast<int>(free.size()),
          [&](int k) {
            AbilityPreset trial = kept;
            if (k >= 0) {
              trial.mutable_lines(i)->set_type(free[k]);
            }
            c().SetAbility(trial, BossSlot());
          },
          [&](int k) {
            return free[k] == ABILITY_LINE_TYPE_BUFF_DURATION ||
                   free[k] == ABILITY_LINE_TYPE_ATTACK_SPEED;
          },
          kLineShortlist);
      if (k >= 0) {
        kept.mutable_lines(i)->set_type(free[k]);
      }
    }
  }

  // Each node in turn: every primary and pair of additional stats, the
  // levels either all on the first additional stat or split between them.
  void Hexa() {
    for (int n = 0; n < c().hexa_stats().nodes_size(); ++n) {
      const HexaStats held = c().hexa_stats();
      std::vector<HexaStats> trials;
      for (HexaStat primary : kHexaStats) {
        for (HexaStat first : kHexaStats) {
          for (HexaStat second : kHexaStats) {
            for (int first_level :
                 {kHexaStatNodeLevels - kHexaStatMaxLevel,
                  (kHexaStatNodeLevels - kHexaStatMaxLevel) / 2}) {
              HexaStats trial = held;
              HexaStatNode& node = *trial.mutable_nodes(n);
              node.set_primary(primary);
              node.set_first(first);
              node.set_second(second);
              node.set_primary_level(kHexaStatMaxLevel);
              node.set_first_level(first_level);
              node.set_second_level(kHexaStatNodeLevels - kHexaStatMaxLevel -
                                    first_level);
              if (HexaStatsAllowed(trial)) {
                trials.push_back(std::move(trial));
              }
            }
          }
        }
      }
      Pick(
          static_cast<int>(trials.size()),
          [&](int k) { c().SetHexaStats(k >= 0 ? trials[k] : held); },
          [](int) { return false; }, kLineShortlist);
    }
  }

  GameState& state_;
  const BisRate& rate_;
  double best_ = 0.0;
};

}  // namespace

void DressBisDefaults(GameState& state) {
  CharacterInstance& character = state.character;
  const Job job = character.proto().job();
  const StatField primary = PrimaryStatField(job);
  std::vector<std::pair<EquipSlot, const EquipInstance*>> worn(
      character.equipped().begin(), character.equipped().end());
  for (const auto& [slot, piece] : worn) {
    const EquipInstance* now = character.WornAt(StatPreset::kFirst, slot);
    if (now == nullptr) {
      continue;
    }
    if (now->CanCube()) {
      for (PotentialTrack track :
           {PotentialTrack::kMain, PotentialTrack::kBonus}) {
        character.TakePotential(slot, track,
                                DefaultPotential(slot, primary, track));
      }
    }
    now = character.WornAt(StatPreset::kFirst, slot);
    if (now->CanFlame()) {
      character.TakeFlame(slot, DefaultFlame(now->prototype(), job));
    }
  }
  character.set_familiars(BisFamiliars());
  if (character.inner_ability_unlocked()) {
    for (Activity activity : {Activity::kFarming, Activity::kBossing}) {
      character.SetAbility(DefaultAbility(), AutoswapSlotFor(activity));
    }
  }
  if (character.proto().job_stage() >= kSixthJobStage) {
    BuyHexaMatrix(state);
    character.SetHexaStats(DefaultHexaStats(character.proto().level()));
  }
}

void SearchBis(GameState& state, const BisRate& rate) {
  Search(state, rate).Run();
}

const BisBuild* FindBisBuild(const std::vector<BisTable>& tables, Job job,
                             int level) {
  const BisBuild* found = nullptr;
  for (const BisTable& table : tables) {
    if (table.job() != job) {
      continue;
    }
    for (const BisBuild& build : table.builds()) {
      if (build.level() <= level &&
          (found == nullptr || build.level() > found->level())) {
        found = &build;
      }
    }
  }
  return found;
}

BisBuild ReadBisBuild(const GameState& state) {
  const CharacterInstance& character = state.character;
  BisBuild build;
  build.set_level(character.proto().level());
  for (const auto& [slot, worn] : character.equipped()) {
    const EquipPrototype& proto = worn->prototype();
    if (IsSymbol(proto) || FamilyTakesCopies(slot)) {
      continue;
    }
    BisPiece& piece = *build.add_pieces();
    piece.set_slot(slot);
    for (const auto& [key, entry] : state.equips) {
      if (entry.name() == proto.name()) {
        piece.set_item(key);
      }
    }
    // Which scroll it took isn't kept, so match what each choice adds.
    for (const Scroll* scroll : BisScrollChoices(state, proto)) {
      const Equip built = BisUpgradedWith(state, proto, scroll);
      if (built.scroll_stats().SerializeAsString() ==
          worn->equip_state().scroll_stats().SerializeAsString()) {
        for (const auto& [key, entry] : state.scrolls) {
          if (&entry == scroll) {
            piece.set_scroll(key);
          }
        }
        break;
      }
    }
    const Equip& held = worn->equip_state();
    if (held.main_potential().lines_size() > 0) {
      *piece.mutable_main_potential() = held.main_potential();
    }
    if (held.bonus_potential().lines_size() > 0) {
      *piece.mutable_bonus_potential() = held.bonus_potential();
    }
    *piece.mutable_flame() = held.flame();
    if (held.has_soul()) {
      piece.set_soul(held.soul().line());
    }
  }
  const StatPreset boss = BossSlot();
  for (const std::string& name : character.summoned_familiars(boss)) {
    if (const Familiar* familiar = FindFamiliar(character.familiars(), name)) {
      *build.add_familiars() = *familiar;
    }
  }
  if (character.inner_ability_unlocked()) {
    *build.mutable_ability() = character.ability(boss);
  }
  *build.mutable_hexa_stats() = character.hexa_stats();
  for (const CharacterSave& alt : state.inactive_characters) {
    build.add_legion(alt.character().job());
  }
  return build;
}

void ApplyBisBuild(GameState& state, const BisBuild& build) {
  CharacterInstance& character = state.character;
  for (const BisPiece& piece : build.pieces()) {
    auto found = state.equips.find(piece.item());
    if (found == state.equips.end() || !character.MeetsLevel(found->second) ||
        !character.MeetsJob(found->second)) {
      continue;
    }
    const EquipPrototype& proto = found->second;
    auto scroll = state.scrolls.find(piece.scroll());
    Equip built = BisUpgradedWith(
        state, proto,
        scroll == state.scrolls.end() ? nullptr : &scroll->second);
    const EquipInstance probe(proto, built);
    if (probe.CanCube()) {
      *built.mutable_main_potential() = piece.main_potential();
      *built.mutable_bonus_potential() = piece.bonus_potential();
    }
    if (probe.CanFlame()) {
      *built.mutable_flame() = piece.flame();
    }
    // The soul's boss and tier come from the level; only the line is chosen.
    const EquipInstance* worn =
        character.WornAt(StatPreset::kFirst, piece.slot());
    if (worn != nullptr && worn->equip_state().has_soul() &&
        probe.CanTakeSoul()) {
      *built.mutable_soul() = worn->equip_state().soul();
      if (piece.soul() != SOUL_LINE_UNSPECIFIED) {
        built.mutable_soul()->set_line(piece.soul());
      }
    }
    WearBuilt(character, piece.slot(), proto, built);
  }
  FamiliarBook book = character.familiars();
  for (const Familiar& chosen : build.familiars()) {
    for (Familiar& familiar : *book.mutable_familiars()) {
      if (familiar.name() == chosen.name()) {
        *familiar.mutable_lines() = chosen.lines();
      }
    }
  }
  character.set_familiars(book);
  if (character.inner_ability_unlocked() &&
      build.ability().lines_size() == kAbilityLines) {
    character.SetAbility(build.ability(), BossSlot());
  }
  if (character.proto().job_stage() >= kSixthJobStage) {
    // A level past the checkpoint may have a node more open; it keeps its
    // default, unless that breaks the rules, when the defaults all stand.
    const HexaStats defaults = character.hexa_stats();
    HexaStats stats;
    for (int i = 0; i < defaults.nodes_size(); ++i) {
      *stats.add_nodes() = i < build.hexa_stats().nodes_size()
                               ? build.hexa_stats().nodes(i)
                               : defaults.nodes(i);
    }
    character.SetHexaStats(stats);
  }
  RespendMaxAllocations(state);
}

double BisSwingRate(GameState& state) {
  return MaxBossSwingRate(state.character, state.skills, state.bosses,
                          state.mobs);
}

}  // namespace ms
