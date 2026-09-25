#include "src/character/character.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/character/arcane_force.h"
#include "src/character/consumables.h"
#include "src/character/equip_presets.h"
#include "src/character/exp_table.h"
#include "src/character/hyper_stats.h"
#include "src/character/job_branch.h"
#include "src/character/v_matrix.h"
#include "src/item/currency.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/inventory.h"
#include "src/item/inventory_sort.h"
#include "src/item/item.h"
#include "src/item/projectile.h"
#include "src/item/star_force_cost.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

namespace {

constexpr int kApPerLevel = 5;
constexpr int kApJobAdvancementBonus = 5;
constexpr int kSpPerLevel = 3;
// SP per level for levels 101-140, so the 4th job's book totals 200.
constexpr int kFourthJobSpPerLevel = 5;
// The last level that pays SP. The 4th job lasts until the 5th advancement, but
// its book is fully paid 60 levels earlier, so the levels after that pay Hyper
// SP, HP, MP and AP instead.
constexpr int kLastSpLevel = 140;

// Hyper SP is paid at 140 and every fifth level to 195: twelve points, one per
// Hyper Skill. So the page really asks what order to learn them in, since the
// unlock levels are bunched rather than one per point.
constexpr int kFirstHyperSpLevel = 140;
constexpr int kLastHyperSpLevel = 195;
constexpr int kHyperSpLevelStep = 5;

// A job's primary stat after advancing into it. It rises from kBaseStat, and
// the AP for the difference comes out of the pool. See ResetStatsForJob.
constexpr int kAdvancementPrimaryStat = 25;

// The four stats AP buys, which an advancement redistributes. HP and MP are in
// the same message but come from levelling.
constexpr StatField kApStatFields[] = {STAT_FIELD_STR, STAT_FIELD_DEX,
                                       STAT_FIELD_INT, STAT_FIELD_LUK};

// The stats to take AP back from, in order. The primary stat is last, because a
// character without it can't be played and the others are cheaper to lose.
std::vector<StatField> StripOrder(StatField primary) {
  std::vector<StatField> order;
  for (StatField field : kApStatFields) {
    if (field != primary) {
      order.push_back(field);
    }
  }
  if (primary != STAT_FIELD_UNSPECIFIED) {
    order.push_back(primary);
  }
  return order;
}

int ApStatValue(const AllocatedStats& stats, StatField field) {
  switch (field) {
    case STAT_FIELD_DEX:
      return stats.dex();
    case STAT_FIELD_INT:
      return stats.int_();
    case STAT_FIELD_LUK:
      return stats.luk();
    default:
      return stats.str();
  }
}

void SetApStat(AllocatedStats* stats, StatField field, int value) {
  switch (field) {
    case STAT_FIELD_DEX:
      stats->set_dex(value);
      break;
    case STAT_FIELD_INT:
      stats->set_int_(value);
      break;
    case STAT_FIELD_LUK:
      stats->set_luk(value);
      break;
    default:
      stats->set_str(value);
      break;
  }
}

// Character levels at which each job advancement (1st to 6th) unlocks. A
// level's SP goes to the highest stage whose threshold it has passed: levels
// 11-30 pay stage 1, 31-60 stage 2, and so on.
constexpr int kAdvancementLevels[] = {10, 30, 60, 100, 200, 260};
static_assert(sizeof(kAdvancementLevels) / sizeof(kAdvancementLevels[0]) ==
                  kMaxJobStage,
              "kMaxJobStage must name the last stage there is a level for");

// The job stage a level-up's SP goes to: how many advancement thresholds the
// level has passed. 0 at level 10 and below, before 1st-job SP starts.
int SpStageForLevel(int level) {
  int stage = 0;
  for (int threshold : kAdvancementLevels) {
    if (level > threshold) {
      ++stage;
    }
  }
  return stage;
}

// Whether advancing into `stage` grants AP. The 3rd and 4th do. AdvanceJob and
// ExpectedTotalAp both call this rather than listing the stages, so a save is
// never "corrected" against a rule the game no longer uses.
bool AdvancementGrantsAp(int stage) {
  return stage == 3 || stage == 4;
}

// SP a level-up pays. Every book costs exactly what its levels pay: 60 for
// 11-30, 90 for 31-60, 120 for 61-100 and 200 for 101-140. The 4th job pays
// five a level, which is the only reason its book is bigger.
int SpForLevel(int level) {
  int stage = SpStageForLevel(level);
  if (stage < 1 || level > kLastSpLevel) {
    return 0;
  }
  return stage < 4 ? kSpPerLevel : kFourthJobSpPerLevel;
}

// Hyper SP a level-up pays: one on each rung of the ladder, nothing between.
int HyperSpForLevel(int level) {
  if (level < kFirstHyperSpLevel || level > kLastHyperSpLevel) {
    return 0;
  }
  return (level - kFirstHyperSpLevel) % kHyperSpLevelStep == 0 ? 1 : 0;
}

// What levels up to `level` paid into `stage`'s book and the Hyper pool. Only
// levels count, since advancing grants no SP. Each total is what a character
// should have between their pool and what they spent.
int ExpectedSpForStage(int level, int stage) {
  int total = 0;
  for (int l = 1; l <= level; ++l) {
    if (SpStageForLevel(l) == stage) {
      total += SpForLevel(l);
    }
  }
  return total;
}

int ExpectedHyperSp(int level) {
  int total = 0;
  for (int l = 1; l <= level; ++l) {
    total += HyperSpForLevel(l);
  }
  return total;
}

// Brings one pool to what the levels say it should be, and returns how far it
// moved. If short, the difference is added. If over, it comes out of the pool
// only, because choosing which Hyper Skill to unlearn is the player's call.
int CorrectPool(std::string_view what, int delta, int* pool) {
  if (delta == 0) {
    return 0;
  }
  // Logged even though it is fixed, for the same reason as in ReconcileAp: a
  // pool that doesn't balance is either a save from older rules or a bug in how
  // points are granted, and a bug would go unnoticed if this silently fixed it.
  LOG(WARNING) << what << " is off by " << delta << "; correcting";
  int was = *pool;
  *pool = std::max(0, *pool + delta);
  return *pool - was;
}

// SP granted for advancing into `job`. Every book costs exactly what its levels
// pay, so advancing grants nothing. A job whose skills can't be made to match
// its levels would set a bonus here.
int JobAdvancementSpBonus(Job job) {
  switch (job) {
    default:
      return 0;
  }
}

// HP and MP gained per level. GMS varies it per class with no published table,
// so these are round numbers chosen to give each branch its feel: warriors
// tough, mages frail with a large MP pool, and the rest in between.
struct LevelUpGain {
  int hp;
  int mp;
};

LevelUpGain LevelUpGainFor(Job job) {
  switch (BranchOf(job)) {
    case JobBranch::kWarrior:
      return {48, 12};
    case JobBranch::kMagician:
      return {12, 48};
    default:
      return {36, 24};
  }
}

EquipJobCategory JobToCategory(Job job) {
  switch (BranchOf(job)) {
    case JobBranch::kBeginner:
      return EQUIP_JOB_CATEGORY_BEGINNER;
    case JobBranch::kWarrior:
      return EQUIP_JOB_CATEGORY_WARRIOR;
    case JobBranch::kArcher:
      return EQUIP_JOB_CATEGORY_BOWMAN;
    case JobBranch::kMagician:
      return EQUIP_JOB_CATEGORY_MAGICIAN;
    case JobBranch::kRogue:
      return EQUIP_JOB_CATEGORY_THIEF;
    case JobBranch::kNone:
      return EQUIP_JOB_CATEGORY_UNSPECIFIED;
  }
  return EQUIP_JOB_CATEGORY_UNSPECIFIED;
}

// Rebuilds one equip-tab entry from its saved state, or returns null if the
// catalogs no longer have it.
std::unique_ptr<EquipTabItem> RestoreEquipItem(
    const Equip& state,
    const std::map<std::string, const EquipPrototype*>& by_name) {
  std::map<std::string, const EquipPrototype*>::const_iterator proto =
      by_name.find(state.equip_name());
  if (proto == by_name.end()) {
    return nullptr;
  }
  return EquipItemFromState(*proto->second, state);
}

// The four beginner books. Every job in a line maps to the 1st job it came
// from, however far along the line it is.
JobAdvancement FirstAdvancement(Job job) {
  switch (BranchOf(job)) {
    case JobBranch::kWarrior:
      return JOB_ADVANCEMENT_SWORDMAN;
    case JobBranch::kArcher:
      return JOB_ADVANCEMENT_ARCHER;
    case JobBranch::kMagician:
      return JOB_ADVANCEMENT_MAGICIAN;
    case JobBranch::kRogue:
      return JOB_ADVANCEMENT_ROGUE;
    default:
      return JOB_ADVANCEMENT_UNSPECIFIED;
  }
}

// The ten 2nd job books. Later jobs still hold the one below them.
JobAdvancement SecondAdvancement(Job job) {
  switch (job) {
    case JOB_FIGHTER:
    case JOB_CRUSADER:
    case JOB_HERO:
      return JOB_ADVANCEMENT_FIGHTER;
    case JOB_PAGE:
    case JOB_WHITE_KNIGHT:
    case JOB_PALADIN:
      return JOB_ADVANCEMENT_PAGE;
    case JOB_SPEARMAN:
    case JOB_BERSERKER:
    case JOB_DARK_KNIGHT:
      return JOB_ADVANCEMENT_SPEARMAN;
    case JOB_HUNTER:
    case JOB_RANGER:
    case JOB_BOW_MASTER:
      return JOB_ADVANCEMENT_HUNTER;
    case JOB_CROSSBOWMAN:
    case JOB_SNIPER:
    case JOB_MARKSMAN:
      return JOB_ADVANCEMENT_CROSSBOWMAN;
    case JOB_ICE_LIGHTNING_WIZARD:
    case JOB_ICE_LIGHTNING_MAGE:
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return JOB_ADVANCEMENT_ICE_LIGHTNING_WIZARD;
    case JOB_FIRE_POISON_WIZARD:
    case JOB_FIRE_POISON_MAGE:
    case JOB_FIRE_POISON_ARCH_MAGE:
      return JOB_ADVANCEMENT_FIRE_POISON_WIZARD;
    case JOB_CLERIC:
    case JOB_PRIEST:
    case JOB_BISHOP:
      return JOB_ADVANCEMENT_CLERIC;
    case JOB_ASSASSIN:
    case JOB_HERMIT:
    case JOB_NIGHT_LORD:
      return JOB_ADVANCEMENT_ASSASSIN;
    case JOB_BANDIT:
    case JOB_CHIEF_BANDIT:
    case JOB_SHADOWER:
      return JOB_ADVANCEMENT_BANDIT;
    default:
      return JOB_ADVANCEMENT_UNSPECIFIED;
  }
}

// The ten 3rd job books, one per job.
JobAdvancement ThirdAdvancement(Job job) {
  switch (job) {
    case JOB_BERSERKER:
    case JOB_DARK_KNIGHT:
      return JOB_ADVANCEMENT_BERSERKER;
    case JOB_CRUSADER:
    case JOB_HERO:
      return JOB_ADVANCEMENT_CRUSADER;
    case JOB_WHITE_KNIGHT:
    case JOB_PALADIN:
      return JOB_ADVANCEMENT_WHITE_KNIGHT;
    case JOB_RANGER:
    case JOB_BOW_MASTER:
      return JOB_ADVANCEMENT_RANGER;
    case JOB_SNIPER:
    case JOB_MARKSMAN:
      return JOB_ADVANCEMENT_SNIPER;
    case JOB_ICE_LIGHTNING_MAGE:
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return JOB_ADVANCEMENT_ICE_LIGHTNING_MAGE;
    case JOB_FIRE_POISON_MAGE:
    case JOB_FIRE_POISON_ARCH_MAGE:
      return JOB_ADVANCEMENT_FIRE_POISON_MAGE;
    case JOB_PRIEST:
    case JOB_BISHOP:
      return JOB_ADVANCEMENT_PRIEST;
    case JOB_HERMIT:
    case JOB_NIGHT_LORD:
      return JOB_ADVANCEMENT_HERMIT;
    case JOB_CHIEF_BANDIT:
    case JOB_SHADOWER:
      return JOB_ADVANCEMENT_CHIEF_BANDIT;
    default:
      return JOB_ADVANCEMENT_UNSPECIFIED;
  }
}

// The 4th job books, one per 3rd job.
JobAdvancement FourthAdvancement(Job job) {
  switch (job) {
    case JOB_DARK_KNIGHT:
      return JOB_ADVANCEMENT_DARK_KNIGHT;
    case JOB_PALADIN:
      return JOB_ADVANCEMENT_PALADIN;
    case JOB_HERO:
      return JOB_ADVANCEMENT_HERO;
    case JOB_BOW_MASTER:
      return JOB_ADVANCEMENT_BOW_MASTER;
    case JOB_MARKSMAN:
      return JOB_ADVANCEMENT_MARKSMAN;
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE;
    case JOB_FIRE_POISON_ARCH_MAGE:
      return JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE;
    case JOB_BISHOP:
      return JOB_ADVANCEMENT_BISHOP;
    case JOB_NIGHT_LORD:
      return JOB_ADVANCEMENT_NIGHT_LORD;
    case JOB_SHADOWER:
      return JOB_ADVANCEMENT_SHADOWER;
    default:
      return JOB_ADVANCEMENT_UNSPECIFIED;
  }
}

// The 5th advancement. It isn't a job change: a Dark Knight stays a Dark Knight
// and just opens another book, so every 4th job maps to its own name again.
JobAdvancement FifthAdvancement(Job job) {
  switch (job) {
    case JOB_DARK_KNIGHT:
      return JOB_ADVANCEMENT_DARK_KNIGHT_V;
    case JOB_PALADIN:
      return JOB_ADVANCEMENT_PALADIN_V;
    case JOB_HERO:
      return JOB_ADVANCEMENT_HERO_V;
    case JOB_BOW_MASTER:
      return JOB_ADVANCEMENT_BOW_MASTER_V;
    case JOB_MARKSMAN:
      return JOB_ADVANCEMENT_MARKSMAN_V;
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE_V;
    case JOB_FIRE_POISON_ARCH_MAGE:
      return JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE_V;
    case JOB_BISHOP:
      return JOB_ADVANCEMENT_BISHOP_V;
    case JOB_NIGHT_LORD:
      return JOB_ADVANCEMENT_NIGHT_LORD_V;
    case JOB_SHADOWER:
      return JOB_ADVANCEMENT_SHADOWER_V;
    default:
      return JOB_ADVANCEMENT_UNSPECIFIED;
  }
}

}  // namespace

JobAdvancement AdvancementForJobStage(Job job, int stage) {
  // A character keeps every book below their current one, so each stage covers
  // the whole line: a Berserker still has their Swordman and Spearman skills.
  switch (stage) {
    case 1:
      return FirstAdvancement(job);
    case 2:
      return SecondAdvancement(job);
    case 3:
      return ThirdAdvancement(job);
    case 4:
      return FourthAdvancement(job);
    case 5:
      return FifthAdvancement(job);
    default:
      return JOB_ADVANCEMENT_UNSPECIFIED;
  }
}

Job JobForAdvancement(JobAdvancement advancement) {
  switch (advancement) {
    case JOB_ADVANCEMENT_SWORDMAN:
      return JOB_SWORDMAN;
    case JOB_ADVANCEMENT_ARCHER:
      return JOB_ARCHER;
    case JOB_ADVANCEMENT_MAGICIAN:
      return JOB_MAGICIAN;
    case JOB_ADVANCEMENT_ROGUE:
      return JOB_ROGUE;
    case JOB_ADVANCEMENT_FIGHTER:
      return JOB_FIGHTER;
    case JOB_ADVANCEMENT_PAGE:
      return JOB_PAGE;
    case JOB_ADVANCEMENT_SPEARMAN:
      return JOB_SPEARMAN;
    case JOB_ADVANCEMENT_HUNTER:
      return JOB_HUNTER;
    case JOB_ADVANCEMENT_CROSSBOWMAN:
      return JOB_CROSSBOWMAN;
    case JOB_ADVANCEMENT_ICE_LIGHTNING_WIZARD:
      return JOB_ICE_LIGHTNING_WIZARD;
    case JOB_ADVANCEMENT_FIRE_POISON_WIZARD:
      return JOB_FIRE_POISON_WIZARD;
    case JOB_ADVANCEMENT_CLERIC:
      return JOB_CLERIC;
    case JOB_ADVANCEMENT_ASSASSIN:
      return JOB_ASSASSIN;
    case JOB_ADVANCEMENT_BANDIT:
      return JOB_BANDIT;
    case JOB_ADVANCEMENT_BERSERKER:
      return JOB_BERSERKER;
    case JOB_ADVANCEMENT_CRUSADER:
      return JOB_CRUSADER;
    case JOB_ADVANCEMENT_WHITE_KNIGHT:
      return JOB_WHITE_KNIGHT;
    case JOB_ADVANCEMENT_RANGER:
      return JOB_RANGER;
    case JOB_ADVANCEMENT_SNIPER:
      return JOB_SNIPER;
    case JOB_ADVANCEMENT_ICE_LIGHTNING_MAGE:
      return JOB_ICE_LIGHTNING_MAGE;
    case JOB_ADVANCEMENT_FIRE_POISON_MAGE:
      return JOB_FIRE_POISON_MAGE;
    case JOB_ADVANCEMENT_PRIEST:
      return JOB_PRIEST;
    case JOB_ADVANCEMENT_HERMIT:
      return JOB_HERMIT;
    case JOB_ADVANCEMENT_CHIEF_BANDIT:
      return JOB_CHIEF_BANDIT;
    case JOB_ADVANCEMENT_DARK_KNIGHT:
    case JOB_ADVANCEMENT_DARK_KNIGHT_V:
      return JOB_DARK_KNIGHT;
    case JOB_ADVANCEMENT_PALADIN:
    case JOB_ADVANCEMENT_PALADIN_V:
      return JOB_PALADIN;
    case JOB_ADVANCEMENT_HERO:
    case JOB_ADVANCEMENT_HERO_V:
      return JOB_HERO;
    case JOB_ADVANCEMENT_BOW_MASTER:
    case JOB_ADVANCEMENT_BOW_MASTER_V:
      return JOB_BOW_MASTER;
    case JOB_ADVANCEMENT_MARKSMAN:
    case JOB_ADVANCEMENT_MARKSMAN_V:
      return JOB_MARKSMAN;
    case JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE:
    case JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE_V:
      return JOB_ICE_LIGHTNING_ARCH_MAGE;
    case JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE:
    case JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE_V:
      return JOB_FIRE_POISON_ARCH_MAGE;
    case JOB_ADVANCEMENT_BISHOP:
    case JOB_ADVANCEMENT_BISHOP_V:
      return JOB_BISHOP;
    case JOB_ADVANCEMENT_NIGHT_LORD:
    case JOB_ADVANCEMENT_NIGHT_LORD_V:
      return JOB_NIGHT_LORD;
    case JOB_ADVANCEMENT_SHADOWER:
    case JOB_ADVANCEMENT_SHADOWER_V:
      return JOB_SHADOWER;
    default:
      return JOB_UNSPECIFIED;
  }
}

int NextAdvancementLevel(int stage) {
  if (stage < 0 || stage >= kMaxJobStage) {
    return 0;
  }
  return kAdvancementLevels[stage];
}

int ExpectedTotalAp(int level, int job_stage) {
  // The Beginner's free STR is given as a stat rather than as AP, so it counts
  // as already spent from level 1.
  int total = kApPerLevel * std::max(0, level - 1) + kBeginnerStr - kBaseStat;
  for (int stage = 1; stage <= job_stage; ++stage) {
    if (AdvancementGrantsAp(stage)) {
      total += kApJobAdvancementBonus;
    }
  }
  return total;
}

std::vector<std::string> StarterEquipsFor(Job job) {
  // Each 1st job gets one weapon so it is playable right away, plus anything
  // the weapon needs. The Rogue gets three items, and holding a dagger or a
  // claw decides which skills they can use. The Archer gets arrows for the bow.
  //
  // A 2nd job gets its secondary and no weapon. It can afford a weapon of its
  // tier, so a free one would undercut that choice, but nothing else fills the
  // new secondary slot.
  switch (job) {
    case JOB_SWORDMAN:
      return {"long_sword"};
    case JOB_MAGICIAN:
      return {"wooden_staff"};
    case JOB_ARCHER:
      return {"war_bow", "bronze_arrow_for_bow"};
    case JOB_ROGUE:
      return {"subi_throwing_stars", "fruit_knife", "garnier"};
    case JOB_FIGHTER:
      return {"powers_medallion"};
    case JOB_PAGE:
      return {"holy_rosary"};
    case JOB_SPEARMAN:
      return {"stark_chain"};
    case JOB_HUNTER:
      return {"breezy_feather"};
    case JOB_CROSSBOWMAN:
      return {"one_shot"};
    case JOB_FIRE_POISON_WIZARD:
      return {"rusty_book_strophe"};
    case JOB_ICE_LIGHTNING_WIZARD:
      return {"metallic_blue_book_strophe"};
    case JOB_CLERIC:
      return {"white_gold_book_strophe"};
    case JOB_ASSASSIN:
      return {"all_souls_charm"};
    case JOB_BANDIT:
      return {"hidden_shadow"};
    default:
      return {};
  }
}

std::vector<EquipType> ExpectedWeapons(Job job) {
  switch (job) {
    // The 1st jobs, each listing what StarterEquipsFor gives it. The Rogue gets
    // both weapons, and which one they hold decides what they can use.
    case JOB_SWORDMAN:
      return {EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD};
    case JOB_MAGICIAN:
      return {EQUIP_TYPE_STAFF};
    case JOB_ARCHER:
      return {EQUIP_TYPE_BOW};
    case JOB_ROGUE:
      return {EQUIP_TYPE_DAGGER, EQUIP_TYPE_CLAW};
    // The warrior branches, each with the pair of weapon types its skills name.
    case JOB_FIGHTER:
    case JOB_CRUSADER:
    case JOB_HERO:
      // Both one- and two-handed versions, which displays as "Sword / Axe".
      // There are no one-handed axes in the game yet, but the skill books name
      // the type, so this does too.
      return {EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD,
              EQUIP_TYPE_ONE_HANDED_AXE, EQUIP_TYPE_TWO_HANDED_AXE};
    case JOB_PAGE:
    case JOB_WHITE_KNIGHT:
    case JOB_PALADIN:
      return {EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD,
              EQUIP_TYPE_ONE_HANDED_BLUNT, EQUIP_TYPE_TWO_HANDED_BLUNT};
    case JOB_SPEARMAN:
    case JOB_BERSERKER:
    case JOB_DARK_KNIGHT:
      return {EQUIP_TYPE_SPEAR, EQUIP_TYPE_POLEARM};
    case JOB_HUNTER:
    case JOB_RANGER:
    case JOB_BOW_MASTER:
      return {EQUIP_TYPE_BOW};
    case JOB_CROSSBOWMAN:
    case JOB_SNIPER:
    case JOB_MARKSMAN:
      return {EQUIP_TYPE_CROSSBOW};
    // Every mage line, however it casts: the staff is the magician's weapon and
    // no branch has a second one.
    case JOB_FIRE_POISON_WIZARD:
    case JOB_ICE_LIGHTNING_WIZARD:
    case JOB_CLERIC:
    case JOB_FIRE_POISON_MAGE:
    case JOB_ICE_LIGHTNING_MAGE:
    case JOB_PRIEST:
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
    case JOB_FIRE_POISON_ARCH_MAGE:
    case JOB_BISHOP:
      return {EQUIP_TYPE_STAFF};
    case JOB_ASSASSIN:
    case JOB_HERMIT:
    case JOB_NIGHT_LORD:
      return {EQUIP_TYPE_CLAW};
    case JOB_BANDIT:
    case JOB_CHIEF_BANDIT:
    case JOB_SHADOWER:
      return {EQUIP_TYPE_DAGGER};
    default:
      return {};
  }
}

JobAdvancement AdvancementForSecondary(EquipType type) {
  switch (type) {
    case EQUIP_TYPE_MEDALLION:
      return JOB_ADVANCEMENT_FIGHTER;
    case EQUIP_TYPE_ROSARY:
      return JOB_ADVANCEMENT_PAGE;
    case EQUIP_TYPE_IRON_CHAIN:
      return JOB_ADVANCEMENT_SPEARMAN;
    case EQUIP_TYPE_MAGIC_BOOK_FIRE_POISON:
      return JOB_ADVANCEMENT_FIRE_POISON_WIZARD;
    case EQUIP_TYPE_MAGIC_BOOK_ICE_LIGHTNING:
      return JOB_ADVANCEMENT_ICE_LIGHTNING_WIZARD;
    case EQUIP_TYPE_MAGIC_BOOK_HOLY:
      return JOB_ADVANCEMENT_CLERIC;
    case EQUIP_TYPE_ARROW_FLETCHING:
      return JOB_ADVANCEMENT_HUNTER;
    case EQUIP_TYPE_BOW_THIMBLE:
      return JOB_ADVANCEMENT_CROSSBOWMAN;
    case EQUIP_TYPE_CHARM:
      return JOB_ADVANCEMENT_ASSASSIN;
    case EQUIP_TYPE_DAGGER_SCABBARD:
      return JOB_ADVANCEMENT_BANDIT;
    default:
      return JOB_ADVANCEMENT_UNSPECIFIED;
  }
}

StatField PrimaryStatField(Job job) {
  switch (BranchOf(job)) {
    // The beginner uses STR, the warrior's stat, which their starting gear has.
    case JobBranch::kBeginner:
    case JobBranch::kWarrior:
      return STAT_FIELD_STR;
    case JobBranch::kArcher:
      return STAT_FIELD_DEX;
    case JobBranch::kMagician:
      return STAT_FIELD_INT;
    case JobBranch::kRogue:
      return STAT_FIELD_LUK;
    case JobBranch::kNone:
      return STAT_FIELD_UNSPECIFIED;
  }
  return STAT_FIELD_UNSPECIFIED;
}

StatField SecondaryStatField(Job job) {
  switch (BranchOf(job)) {
    // Each branch's pair, swapped: warriors and archers share STR and DEX, and
    // magicians and thieves share LUK and DEX.
    case JobBranch::kBeginner:
    case JobBranch::kWarrior:
    case JobBranch::kRogue:
      return STAT_FIELD_DEX;
    case JobBranch::kArcher:
      return STAT_FIELD_STR;
    case JobBranch::kMagician:
      return STAT_FIELD_LUK;
    case JobBranch::kNone:
      return STAT_FIELD_UNSPECIFIED;
  }
  return STAT_FIELD_UNSPECIFIED;
}

// One job leading to the next, for the advancements that offer one choice
// instead of several.
struct Successor {
  Job from;
  Job to;
};

// The 3rd advancement: one branch per 2nd job, so the picker offers a single
// choice.
constexpr Successor kThirdJobs[] = {
    {JOB_SPEARMAN, JOB_BERSERKER},
    {JOB_FIGHTER, JOB_CRUSADER},
    {JOB_PAGE, JOB_WHITE_KNIGHT},
    {JOB_HUNTER, JOB_RANGER},
    {JOB_CROSSBOWMAN, JOB_SNIPER},
    {JOB_ICE_LIGHTNING_WIZARD, JOB_ICE_LIGHTNING_MAGE},
    {JOB_FIRE_POISON_WIZARD, JOB_FIRE_POISON_MAGE},
    {JOB_CLERIC, JOB_PRIEST},
    {JOB_ASSASSIN, JOB_HERMIT},
    {JOB_BANDIT, JOB_CHIEF_BANDIT},
};

// The 4th advancement: one per 3rd job.
constexpr Successor kFourthJobs[] = {
    {JOB_BERSERKER, JOB_DARK_KNIGHT},
    {JOB_WHITE_KNIGHT, JOB_PALADIN},
    {JOB_CRUSADER, JOB_HERO},
    {JOB_RANGER, JOB_BOW_MASTER},
    {JOB_SNIPER, JOB_MARKSMAN},
    {JOB_ICE_LIGHTNING_MAGE, JOB_ICE_LIGHTNING_ARCH_MAGE},
    {JOB_FIRE_POISON_MAGE, JOB_FIRE_POISON_ARCH_MAGE},
    {JOB_PRIEST, JOB_BISHOP},
    {JOB_HERMIT, JOB_NIGHT_LORD},
    {JOB_CHIEF_BANDIT, JOB_SHADOWER},
};

// The single job `job` advances to in `table`, or nothing.
std::vector<Job> Successors(const Successor* table, int count, Job job) {
  for (int i = 0; i < count; ++i) {
    if (table[i].from == job) {
      return {table[i].to};
    }
  }
  return {};
}

std::vector<Job> JobChoicesForStage(Job job, int stage) {
  // The four explorer branches, ordered by main stat so the list reads
  // STR/DEX/INT/LUK like the stat panel.
  if (stage == 1) {
    return {JOB_SWORDMAN, JOB_ARCHER, JOB_MAGICIAN, JOB_ROGUE};
  }
  // From here on the choices depend on the current job.
  if (stage == 2 && job == JOB_SWORDMAN) {
    return {JOB_FIGHTER, JOB_PAGE, JOB_SPEARMAN};
  }
  if (stage == 2 && job == JOB_ARCHER) {
    return {JOB_HUNTER, JOB_CROSSBOWMAN};
  }
  if (stage == 2 && job == JOB_MAGICIAN) {
    return {JOB_ICE_LIGHTNING_WIZARD, JOB_FIRE_POISON_WIZARD, JOB_CLERIC};
  }
  if (stage == 2 && job == JOB_ROGUE) {
    return {JOB_ASSASSIN, JOB_BANDIT};
  }
  if (stage == 3) {
    return Successors(kThirdJobs,
                      static_cast<int>(sizeof(kThirdJobs) / sizeof(Successor)),
                      job);
  }
  if (stage == 4) {
    return Successors(kFourthJobs,
                      static_cast<int>(sizeof(kFourthJobs) / sizeof(Successor)),
                      job);
  }
  // The 5th advancement offers the job the character already has. It opens a
  // book rather than changing the job, so there is nothing to choose.
  if (stage == 5 && FifthAdvancement(job) != JOB_ADVANCEMENT_UNSPECIFIED) {
    return {job};
  }
  return {};
}

int SkillMaxLevel(const Skill& skill) {
  if (skill.account_levels_per_level() > 0) {
    return kMaxLevel / skill.account_levels_per_level();
  }
  return skill.max_level();
}

int BuffWindowsFor(const Buff& buff) {
  return std::max(1, buff.stages()) * std::max(1, buff.stacks());
}

int StageForAdvancement(JobAdvancement advancement) {
  static_assert(JobAdvancement_ARRAYSIZE == 48,
                "a new advancement needs its stage here");
  switch (advancement) {
    case JOB_ADVANCEMENT_SWORDMAN:
    case JOB_ADVANCEMENT_ARCHER:
    case JOB_ADVANCEMENT_MAGICIAN:
    case JOB_ADVANCEMENT_ROGUE:
      return 1;
    case JOB_ADVANCEMENT_FIGHTER:
    case JOB_ADVANCEMENT_PAGE:
    case JOB_ADVANCEMENT_SPEARMAN:
    case JOB_ADVANCEMENT_HUNTER:
    case JOB_ADVANCEMENT_CROSSBOWMAN:
    case JOB_ADVANCEMENT_ICE_LIGHTNING_WIZARD:
    case JOB_ADVANCEMENT_FIRE_POISON_WIZARD:
    case JOB_ADVANCEMENT_CLERIC:
    case JOB_ADVANCEMENT_ASSASSIN:
    case JOB_ADVANCEMENT_BANDIT:
      return 2;
    case JOB_ADVANCEMENT_BERSERKER:
    case JOB_ADVANCEMENT_CRUSADER:
    case JOB_ADVANCEMENT_WHITE_KNIGHT:
    case JOB_ADVANCEMENT_RANGER:
    case JOB_ADVANCEMENT_SNIPER:
    case JOB_ADVANCEMENT_ICE_LIGHTNING_MAGE:
    case JOB_ADVANCEMENT_FIRE_POISON_MAGE:
    case JOB_ADVANCEMENT_PRIEST:
    case JOB_ADVANCEMENT_HERMIT:
    case JOB_ADVANCEMENT_CHIEF_BANDIT:
      return 3;
    case JOB_ADVANCEMENT_DARK_KNIGHT:
    case JOB_ADVANCEMENT_PALADIN:
    case JOB_ADVANCEMENT_HERO:
    case JOB_ADVANCEMENT_BOW_MASTER:
    case JOB_ADVANCEMENT_MARKSMAN:
    case JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE:
    case JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE:
    case JOB_ADVANCEMENT_BISHOP:
    case JOB_ADVANCEMENT_NIGHT_LORD:
    case JOB_ADVANCEMENT_SHADOWER:
      return 4;
    case JOB_ADVANCEMENT_DARK_KNIGHT_V:
    case JOB_ADVANCEMENT_PALADIN_V:
    case JOB_ADVANCEMENT_HERO_V:
    case JOB_ADVANCEMENT_BOW_MASTER_V:
    case JOB_ADVANCEMENT_MARKSMAN_V:
    case JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE_V:
    case JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE_V:
    case JOB_ADVANCEMENT_BISHOP_V:
    case JOB_ADVANCEMENT_NIGHT_LORD_V:
    case JOB_ADVANCEMENT_SHADOWER_V:
      return 5;
    // No one advances into any of these, so none has a stage. Nothing keyed by
    // stage (SP pools, the skills tab's numbered pages) applies to a common
    // node, the beginner book or a link skill.
    case JOB_ADVANCEMENT_COMMON:
    case JOB_ADVANCEMENT_BEGINNER:
    case JOB_ADVANCEMENT_LINK:
    default:
      return 0;
  }
}

// One Burning tier: how many characters must be above this one, and how many
// levels each level-up grants once they are. The ceiling is the level of the
// last character the tier counts, which is the lowest of them.
struct BurnTier {
  int above = 0;
  int levels = 0;
};
constexpr BurnTier kBurnTiers[] = {{1, 2}, {3, 3}, {10, 5}};

LevelGains GainsForLevels(int from_level, int to_level) {
  LevelGains gains;
  // Goes through the levels reached, which is what LevelUp grants against. Keep
  // the two consistent: a test checks real gains against this.
  for (int level = from_level + 1; level <= to_level; ++level) {
    gains.ap += kApPerLevel;
    gains.sp += SpForLevel(level);
    gains.hyper_sp += HyperSpForLevel(level);
  }
  return gains;
}

CharacterInstance::CharacterInstance(std::mt19937& rng, Character character)
    : rng_(rng), character_(std::move(character)) {
  EnsureUsername();
  EnsureInnerAbility();
}

void CharacterInstance::EnsureUsername() {
  if (character_.name().empty()) {
    character_.set_name(kDefaultUsername);
  }
}

void CharacterInstance::EnsureInnerAbility() {
  InnerAbility& ability = *character_.mutable_inner_ability();
  MigrateHyperStats(*character_.mutable_hyper_stats());
  for (int i = 0; i < kNumStatPresets; ++i) {
    AbilityPreset& lines = PresetOf(ability, StatPresetAt(i));
    if (lines.lines_size() == 0) {
      lines = DefaultAbilityPreset();
    } else if (lines.rank() == ABILITY_RANK_UNSPECIFIED) {
      // A preset with no rank has no reset price, so it could never be
      // rerolled.
      lines.set_rank(kDefaultAbilityRank);
    }
  }
}

void CharacterInstance::LevelUp() {
  character_.set_level(character_.level() + 1);
  character_.set_ap(character_.ap() + kApPerLevel);
  // HP and MP are granted at the current job, so levels gained as a Beginner
  // keep the Beginner rate. Advancing later doesn't change them.
  LevelUpGain gain = LevelUpGainFor(character_.job());
  AllocatedStats* stats = character_.mutable_allocated_stats();
  stats->set_hp(stats->hp() + gain.hp);
  stats->set_mp(stats->mp() + gain.mp);
  // The new level decides how much SP it pays and which stage's book gets it
  // (none below 11).
  int stage = SpStageForLevel(character_.level());
  if (stage >= 1) {
    (*character_.mutable_sp_by_stage())[stage] +=
        SpForLevel(character_.level());
  }
  // Hyper SP has its own pool and its own ladder, since Hyper Skills belong to
  // no stage.
  character_.set_hyper_sp(character_.hyper_sp() +
                          HyperSpForLevel(character_.level()));
}

int64_t CharacterInstance::BossClearedAt(const std::string& boss,
                                         const std::string& difficulty) const {
  for (const BossClear& clear : character_.boss_clears()) {
    if (clear.boss() == boss && clear.difficulty() == difficulty) {
      return clear.cleared_unix_seconds();
    }
  }
  return 0;
}

void CharacterInstance::RecordBossClear(const std::string& boss,
                                        const std::string& difficulty,
                                        int64_t now) {
  for (BossClear& clear : *character_.mutable_boss_clears()) {
    if (clear.boss() == boss && clear.difficulty() == difficulty) {
      clear.set_cleared_unix_seconds(now);
      return;
    }
  }
  BossClear* added = character_.add_boss_clears();
  added->set_boss(boss);
  added->set_difficulty(difficulty);
  added->set_cleared_unix_seconds(now);
}

bool CharacterInstance::ScrollPinned(const std::string& key) const {
  const google::protobuf::RepeatedPtrField<std::string>& pinned =
      character_.pinned_scrolls();
  return std::find(pinned.begin(), pinned.end(), key) != pinned.end();
}

void CharacterInstance::ToggleScrollPin(const std::string& key) {
  google::protobuf::RepeatedPtrField<std::string>* pinned =
      character_.mutable_pinned_scrolls();
  google::protobuf::RepeatedPtrField<std::string>::iterator it =
      std::find(pinned->begin(), pinned->end(), key);
  if (it == pinned->end()) {
    pinned->Add(std::string(key));
    return;
  }
  pinned->erase(it);
}

void CharacterInstance::AddExp(int64_t amount,
                               const std::vector<int>& other_levels) {
  // At the cap, EXP is thrown away rather than stored, so a character who kept
  // fighting there doesn't get a windfall when the cap is raised.
  if (character_.level() >= kTrialLevelCap) {
    return;
  }
  character_.set_exp(character_.exp() + amount);
  while (character_.level() < kTrialLevelCap) {
    int64_t threshold = ExpToNextLevel(character_.level());
    if (character_.exp() < threshold) {
      break;
    }
    // One threshold gives however many levels Burning grants. The leftover EXP
    // carries over to the new level.
    character_.set_exp(character_.exp() - threshold);
    int arrived = std::min(LevelAfterBurning(character_.level(), other_levels),
                           kTrialLevelCap);
    while (character_.level() < arrived) {
      LevelUp();
    }
  }
  if (character_.level() >= kTrialLevelCap) {
    character_.set_exp(0);
  }
}

int LevelAfterBurning(int level, const std::vector<int>& other_levels) {
  std::vector<int> above = other_levels;
  std::sort(above.begin(), above.end(), std::greater<int>());
  int arrived = level + 1;
  for (const BurnTier& tier : kBurnTiers) {
    if (static_cast<int>(above.size()) < tier.above) {
      break;
    }
    // Every tier the account qualifies for is tried and the best result wins: a
    // slower tier that goes further beats a faster one that stops at its
    // ceiling.
    int ceiling = std::min(above[tier.above - 1], kBurningLevel);
    arrived = std::max(arrived, std::min(level + tier.levels, ceiling));
  }
  return arrived;
}

void CharacterInstance::AdvanceJob(Job next_job) {
  int stage = character_.job_stage() + 1;
  character_.set_job_stage(stage);
  character_.set_job(next_job);
  if (AdvancementGrantsAp(stage)) {
    character_.set_ap(character_.ap() + kApJobAdvancementBonus);
  }
  // Advancing grants SP only for a job that sets JobAdvancementSpBonus.
  // Normally levels pay all the SP.
  (*character_.mutable_sp_by_stage())[stage] += JobAdvancementSpBonus(next_job);
  // A worn Arcane Symbol grants the wearer's primary stat, and the job just
  // changed which stat that is.
  RecomputeEquipStats();
}

void CharacterInstance::ResetStatsForJob(Job job) {
  // Everything above base was bought with AP, including the Beginner's free 13
  // STR. It is refunded rather than wasted, so the character ends up exactly
  // where a new character of this job would. Worked out from the current stats.
  AllocatedStats* stats = character_.mutable_allocated_stats();
  int pool = character_.ap();
  for (StatField field : kApStatFields) {
    pool += ApStatValue(*stats, field) - kBaseStat;
    SetApStat(stats, field, kBaseStat);
  }
  StatField primary = PrimaryStatField(job);
  if (primary != STAT_FIELD_UNSPECIFIED) {
    SetApStat(stats, primary, kAdvancementPrimaryStat);
    pool -= kAdvancementPrimaryStat - kBaseStat;
  }
  character_.set_ap(std::max(0, pool));
}

int CharacterInstance::ReconcileAp() {
  AllocatedStats* stats = character_.mutable_allocated_stats();
  int held = character_.ap();
  for (StatField field : kApStatFields) {
    held += ApStatValue(*stats, field) - kBaseStat;
  }
  int delta =
      ExpectedTotalAp(character_.level(), character_.job_stage()) - held;
  if (delta == 0) {
    return 0;
  }
  // Logged even though it is fixed: a character whose AP doesn't balance is
  // either a save from older rules or a bug in how AP is granted, and a bug
  // would go unnoticed if this silently fixed it.
  LOG(WARNING) << "Character AP is off by " << delta << " at level "
               << character_.level() << ", job stage " << character_.job_stage()
               << "; correcting";
  if (delta > 0) {
    character_.set_ap(character_.ap() + delta);
    return delta;
  }
  // AP to take back. The pool goes first, so a character who had nothing
  // unspent keeps every stat they bought.
  int owed = -delta;
  int from_pool = std::min(owed, character_.ap());
  character_.set_ap(character_.ap() - from_pool);
  owed -= from_pool;
  for (StatField field : StripOrder(PrimaryStatField(character_.job()))) {
    if (owed == 0) {
      break;
    }
    int spent = std::max(0, ApStatValue(*stats, field) - kBaseStat);
    int take = std::min(owed, spent);
    SetApStat(stats, field, ApStatValue(*stats, field) - take);
    owed -= take;
  }
  // Anything still owed can't be taken: every stat is at its base and the pool
  // is empty. That is a new character's state and the closest this can get.
  return delta;
}

namespace {

// Every skill of `book` that could still take a point: below its max, with its
// requirement met and its level reached. It is called once per point, so a
// skill unlocked by the last point is included.
std::vector<const Skill*> TakersIn(const CharacterInstance& character,
                                   const std::map<std::string, Skill>& skills,
                                   JobAdvancement book, bool hyper) {
  std::vector<const Skill*> takers;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    // A derived skill never takes points, since nothing buys its levels. See
    // Skill.account_levels_per_level.
    if (!ListedIn(skill, book) || skill.hyper() != hyper ||
        skill.account_levels_per_level() > 0 ||
        character.skill_level(skill) >= SkillMaxLevel(skill) ||
        character.proto().level() < skill.required_level() ||
        !character.MeetsSkillRequirement(skill)) {
      continue;
    }
    takers.push_back(&skill);
  }
  return takers;
}

}  // namespace

int CharacterInstance::ReconcileSkills(
    const std::map<std::string, Skill>& skills) {
  int moved = 0;
  // Goes through the catalog rather than the learned levels, because a display
  // name repeats across branches and the book the character holds says which
  // one they learned.
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& taught = entry.second;
    JobAdvancement book = BookHeldFor(taught);
    if (book == JOB_ADVANCEMENT_UNSPECIFIED) {
      continue;
    }
    int spare = skill_level(taught) - SkillMaxLevel(taught);
    if (spare <= 0) {
      continue;
    }
    // Logged even though it is fixed, for the same reason as in ReconcileAp: a
    // book that no longer fits is either a save from older data or a bug in how
    // points are granted, and a bug would go unnoticed if this silently fixed
    // it.
    LOG(WARNING) << taught.name() << " is taught to " << skill_level(taught)
                 << " of a maximum " << SkillMaxLevel(taught)
                 << "; cutting it back and re-spending " << spare;
    (*character_.mutable_skill_levels())[taught.name()] = SkillMaxLevel(taught);
    moved += spare;
    for (; spare > 0; --spare) {
      std::vector<const Skill*> takers =
          TakersIn(*this, skills, book, taught.hyper());
      if (takers.empty()) {
        // There's nowhere in the book to put it. Since a book costs exactly
        // what its levels pay, this shouldn't happen; the point goes back to
        // the pool that paid for it rather than being lost.
        if (taught.v_node() != V_NODE_KIND_UNSPECIFIED) {
          character_.set_v_points(
              character_.v_points() +
              VNodeStepCost(taught.v_node(), taught.max_level() + 1));
        } else if (taught.hyper()) {
          character_.set_hyper_sp(character_.hyper_sp() + 1);
        } else {
          (*character_.mutable_sp_by_stage())[StageForAdvancement(book)] += 1;
        }
        continue;
      }
      std::uniform_int_distribution<std::size_t> pick(0, takers.size() - 1);
      (*character_.mutable_skill_levels())[takers[pick(rng_)]->name()] += 1;
    }
  }
  return moved;
}

int CharacterInstance::ReconcileSp(const std::map<std::string, Skill>& skills) {
  // What the character has spent from each pool. It goes through the catalog,
  // since a display name repeats across branches and the book held says which
  // one was learned. A Vengeance form is skipped, since its level is stored on
  // another row.
  std::map<int, int> spent;
  int hyper_spent = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    JobAdvancement book = BookHeldFor(skill);
    if (!skill.replaces_skill_name().empty() ||
        book == JOB_ADVANCEMENT_UNSPECIFIED) {
      continue;
    }
    if (skill.hyper()) {
      hyper_spent += skill_level(skill);
    } else {
      spent[StageForAdvancement(book)] += skill_level(skill);
    }
  }
  int level = character_.level();
  int moved = 0;
  // Only stages a level-up can pay into. Books after the last one (the 5th
  // job's) have no SP income, so there is nothing to correct.
  for (int stage = 1; stage <= SpStageForLevel(kLastSpLevel); ++stage) {
    int pool = sp(stage);
    int delta = ExpectedSpForStage(level, stage) - pool - spent[stage];
    if (delta == 0) {
      continue;
    }
    moved += CorrectPool(absl::StrCat("Stage ", stage, " SP"), delta, &pool);
    (*character_.mutable_sp_by_stage())[stage] = pool;
  }
  int hyper = character_.hyper_sp();
  int delta = ExpectedHyperSp(level) - hyper - hyper_spent;
  moved += CorrectPool("Hyper SP", delta, &hyper);
  character_.set_hyper_sp(hyper);
  return moved;
}

bool CharacterInstance::CanAdvanceJob() const {
  // The next stage and the level it unlocks at. An advancement with no choices
  // defined isn't offered, so this never claims a 2nd job exists before its
  // jobs do.
  int stage = character_.job_stage();
  if (stage >= kMaxJobStage) {
    return false;
  }
  return character_.level() >= kAdvancementLevels[stage] &&
         !JobChoicesForStage(character_.job(), stage + 1).empty();
}

bool CharacterInstance::AllocateStat(StatField field, int amount) {
  if (field == STAT_FIELD_UNSPECIFIED) {
    return false;
  }
  if (amount > character_.ap()) {
    return false;
  }
  AllocatedStats* stats = character_.mutable_allocated_stats();
  switch (field) {
    case STAT_FIELD_STR:
      stats->set_str(stats->str() + amount);
      break;
    case STAT_FIELD_DEX:
      stats->set_dex(stats->dex() + amount);
      break;
    case STAT_FIELD_INT:
      stats->set_int_(stats->int_() + amount);
      break;
    case STAT_FIELD_LUK:
      stats->set_luk(stats->luk() + amount);
      break;
    case STAT_FIELD_HP:
      // TODO: Demon Avenger gains 15 HP per AP instead of 1.
      stats->set_hp(stats->hp() + amount);
      break;
    case STAT_FIELD_MP:
      stats->set_mp(stats->mp() + amount);
      break;
    default:
      return false;
  }
  character_.set_ap(character_.ap() - amount);
  return true;
}

StatPreset CharacterInstance::SlotInUse(PresetKind kind) const {
  switch (kind) {
    case PresetKind::kHyperStats:
      return StatPresetAt(character_.hyper_stats().active());
    case PresetKind::kInnerAbility:
      return StatPresetAt(character_.inner_ability().active());
    case PresetKind::kEquip:
      return StatPresetAt(character_.equip_presets().active());
    case PresetKind::kLinkSkills:
      return StatPresetAt(character_.link_skills().active());
  }
  return StatPreset::kFirst;
}

void CharacterInstance::SetSlotInUse(PresetKind kind, StatPreset slot) {
  switch (kind) {
    case PresetKind::kHyperStats:
      character_.mutable_hyper_stats()->set_active(IndexOf(slot));
      return;
    case PresetKind::kInnerAbility:
      character_.mutable_inner_ability()->set_active(IndexOf(slot));
      return;
    case PresetKind::kEquip:
      character_.mutable_equip_presets()->set_active(IndexOf(slot));
      return;
    case PresetKind::kLinkSkills:
      character_.mutable_link_skills()->set_active(IndexOf(slot));
      return;
  }
}

void CharacterInstance::SwapPresets(PresetKind kind, StatPreset a,
                                    StatPreset b) {
  if (a == b) {
    return;
  }
  if (kind == PresetKind::kEquip) {
    // Gear presets are not swapped. The first holds every slot and the others
    // only hold what differs from it, so swapping two would leave the character
    // wearing only one preset's overrides. Nothing offers this.
    return;
  }
  if (kind == PresetKind::kHyperStats) {
    HyperStats& stats = *character_.mutable_hyper_stats();
    MigrateHyperStats(stats);
    stats.mutable_presets()->SwapElements(IndexOf(a), IndexOf(b));
  } else if (kind == PresetKind::kLinkSkills) {
    LinkSkills& link = *character_.mutable_link_skills();
    PresetOf(link, StatPresetAt(kNumStatPresets - 1));  // creates every preset
    link.mutable_presets()->SwapElements(IndexOf(a), IndexOf(b));
  } else {
    InnerAbility& ability = *character_.mutable_inner_ability();
    MigrateInnerAbility(ability);
    ability.mutable_presets()->SwapElements(IndexOf(a), IndexOf(b));
  }
  const StatPreset in_use = SlotInUse(kind);
  if (in_use == a) {
    SetSlotInUse(kind, b);
  } else if (in_use == b) {
    SetSlotInUse(kind, a);
  }
}

StatPreset CharacterInstance::SlotFor(PresetKind kind,
                                      Activity activity) const {
  return autoswap_presets_ ? AutoswapSlotFor(activity) : SlotInUse(kind);
}

int CharacterInstance::arcane_force(Activity activity) const {
  return arcane_force_[IndexOf(SlotFor(PresetKind::kEquip, activity))] +
         static_cast<int>(
             hyper_stat_bonus(HYPER_STAT_FIELD_ARCANE_FORCE,
                              SlotFor(PresetKind::kHyperStats, activity)));
}

int CharacterInstance::sacred_power(Activity /*activity*/) const {
  return 0;
}

int CharacterInstance::hyper_stat_points() const {
  return TotalHyperStatPoints(character_.level());
}

int CharacterInstance::hyper_stat_points_left(StatPreset preset) const {
  return hyper_stat_points() -
         HyperStatPointsSpent(PresetOf(character_.hyper_stats(), preset));
}

int CharacterInstance::hyper_stat_level(HyperStatField field,
                                        StatPreset preset) const {
  return HyperStatLevel(PresetOf(character_.hyper_stats(), preset), field);
}

double CharacterInstance::hyper_stat_bonus(HyperStatField field,
                                           StatPreset preset) const {
  return HyperStatBonus(field, hyper_stat_level(field, preset));
}

int CharacterInstance::max_hyper_stat_level() const {
  return MaxHyperStatLevel(character_.job_stage());
}

int64_t CharacterInstance::ability_reset_cost(StatPreset preset) const {
  const AbilityPreset& lines = ability(preset);
  return AbilityResetCost(lines.rank(), LockedAbilityLines(lines));
}

bool CharacterInstance::LockAbilityLine(int index, bool locked,
                                        StatPreset preset) {
  return SetAbilityLineLocked(
      PresetOf(*character_.mutable_inner_ability(), preset), index, locked);
}

bool CharacterInstance::ResetAbility(StatPreset preset) {
  const int64_t cost = ability_reset_cost(preset);
  if (!inner_ability_unlocked() || cost <= 0 || character_.honor() < cost) {
    return false;
  }
  character_.set_honor(character_.honor() - cost);
  RerollAbility(PresetOf(*character_.mutable_inner_ability(), preset), rng_);
  return true;
}

void CharacterInstance::SetAbility(const AbilityPreset& lines,
                                   StatPreset preset) {
  PresetOf(*character_.mutable_inner_ability(), preset) = lines;
}

bool CharacterInstance::AllocateHyperStat(HyperStatField field,
                                          StatPreset preset, int amount) {
  if (amount <= 0 || !HyperStatUnlocked(field, character_.level())) {
    return false;
  }
  int level = hyper_stat_level(field, preset);
  if (level + amount > max_hyper_stat_level()) {
    return false;
  }
  // Each level in the range is priced separately, since each costs more than
  // the last.
  int price = HyperStatTotalCost(level + amount) - HyperStatTotalCost(level);
  if (price > hyper_stat_points_left(preset)) {
    return false;
  }
  SetHyperStatLevel(PresetOf(*character_.mutable_hyper_stats(), preset), field,
                    level + amount);
  return true;
}

bool CharacterInstance::RefundHyperStat(HyperStatField field, StatPreset preset,
                                        int amount) {
  int level = hyper_stat_level(field, preset);
  if (amount <= 0 || amount > level) {
    return false;
  }
  // The points refund themselves: points left are the pool minus what the
  // allocation holds, so lowering the level is the whole refund.
  SetHyperStatLevel(PresetOf(*character_.mutable_hyper_stats(), preset), field,
                    level - amount);
  return true;
}

void CharacterInstance::ResetHyperStats(StatPreset preset) {
  PresetOf(*character_.mutable_hyper_stats(), preset).clear_levels();
}

void CharacterInstance::ResetVMatrix(
    const std::map<std::string, Skill>& skills) {
  google::protobuf::Map<std::string, int32_t>& levels =
      *character_.mutable_skill_levels();
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    if (skill.v_node() == V_NODE_KIND_UNSPECIFIED) {
      continue;
    }
    int level = skill_level(skill);
    if (level <= 0) {
      continue;
    }
    // Priced from level 0, which is what reaching that level cost. The ladder
    // is the same whatever order the points were spent in.
    character_.set_v_points(character_.v_points() +
                            VNodeCost(skill.v_node(), 0, level));
    levels.erase(skill.name());
  }
}

int CharacterInstance::ReconcileHyperPreset(StatPreset preset) {
  HyperStatPreset& allocation =
      PresetOf(*character_.mutable_hyper_stats(), preset);
  int moved = 0;
  // The stats to check, in enum order, so two saves in the same state are
  // corrected the same way.
  std::vector<int> fields;
  for (const std::pair<const int, int>& entry : allocation.levels()) {
    fields.push_back(entry.first);
  }
  std::sort(fields.begin(), fields.end());
  for (int key : fields) {
    HyperStatField field = static_cast<HyperStatField>(key);
    int level = allocation.levels().at(key);
    int allowed = std::min(std::max(0, level), max_hyper_stat_level());
    // A stat the data no longer has, or one this character's level has locked,
    // keeps nothing.
    if (!HyperStatField_IsValid(key) ||
        !HyperStatUnlocked(field, character_.level())) {
      allowed = 0;
    }
    if (allowed == level) {
      continue;
    }
    moved += HyperStatTotalCost(level) - HyperStatTotalCost(allowed);
    SetHyperStatLevel(allocation, field, allowed);
  }
  // What remains may still cost more than the pool, for example in a save from
  // before a level cap was lowered. The highest level is removed first, since
  // it is the most expensive, so the fewest levels are lost.
  while (HyperStatPointsSpent(allocation) > hyper_stat_points()) {
    int dearest = 0;
    int at = 0;
    for (const std::pair<const int, int>& entry : allocation.levels()) {
      if (entry.second > at) {
        dearest = entry.first;
        at = entry.second;
      }
    }
    if (at <= 0) {
      break;
    }
    moved += HyperStatLevelCost(at);
    SetHyperStatLevel(allocation, static_cast<HyperStatField>(dearest), at - 1);
  }
  return moved;
}

int CharacterInstance::ReconcileHyperStats() {
  int moved = 0;
  for (int i = 0; i < kNumStatPresets; ++i) {
    moved += ReconcileHyperPreset(StatPresetAt(i));
  }
  if (moved > 0) {
    LOG(WARNING) << "Character Hyper Stats were over by " << moved
                 << " points at level " << character_.level() << "; correcting";
  }
  return moved;
}

bool CharacterInstance::HasAdvancement(JobAdvancement advancement) const {
  if (advancement == JOB_ADVANCEMENT_BEGINNER) {
    // Everyone's first book, and it is never lost: a Night Lord still has
    // Blessing of the Fairy.
    return true;
  }
  if (advancement == JOB_ADVANCEMENT_UNSPECIFIED) {
    return false;
  }
  int stage = StageForAdvancement(advancement);
  if (stage <= 0 || stage > character_.job_stage()) {
    return false;
  }
  // The character has reached this stage; what's left is whether it is their
  // own branch.
  return AdvancementForJobStage(character_.job(), stage) == advancement;
}

bool CharacterInstance::MeetsSkillRequirement(const Skill& skill) const {
  if (!skill.has_required_skill()) {
    return true;
  }
  const SkillRequirement& required = skill.required_skill();
  // Learned levels are keyed by display name, which is what the requirement
  // names, so no catalog lookup is needed.
  google::protobuf::Map<std::string, int32_t>::const_iterator it =
      character_.skill_levels().find(required.skill_name());
  int level = it == character_.skill_levels().end() ? 0 : it->second;
  return level >= required.level();
}

bool CharacterInstance::SkillToggledOn(const std::string& name) const {
  for (const std::string& active : character_.active_skill()) {
    if (active == name) {
      return true;
    }
  }
  return false;
}

bool CharacterInstance::ToggleSkill(const Skill& skill) {
  if (!skill.toggle() || skill_level(skill) <= 0) {
    return false;
  }
  google::protobuf::RepeatedPtrField<std::string>& active =
      *character_.mutable_active_skill();
  for (int i = 0; i < active.size(); ++i) {
    if (active.Get(i) == skill.name()) {
      active.DeleteSubrange(i, 1);
      return false;
    }
  }
  active.Add(std::string(skill.name()));
  return true;
}

bool CharacterInstance::LearnSkill(const Skill& skill, int amount) {
  if (amount <= 0) {
    return false;
  }
  // A Vengeance form is bought by buying the skill it replaces. Its own name
  // holds no level, so a point spent here would be lost.
  if (!skill.replaces_skill_name().empty()) {
    return false;
  }
  // A V Matrix node is bought with V Points and follows its kind's ladder, so
  // the SP rules below don't apply.
  if (skill.v_node() != V_NODE_KIND_UNSPECIFIED) {
    return LearnVNode(skill, amount);
  }
  if (!HasBookFor(skill)) {
    return false;
  }
  if (!MeetsSkillRequirement(skill)) {
    return false;
  }
  if (character_.level() < skill.required_level()) {
    return false;
  }
  if (skill_level(skill) + amount > SkillMaxLevel(skill)) {
    return false;
  }
  // A Hyper Skill is bought from the character's Hyper pool. The checks above
  // apply to it too: it names the advancement whose book it is in, so a Paladin
  // can't buy a Dark Knight's.
  if (amount > SpFor(skill)) {
    return false;
  }
  if (skill.hyper()) {
    character_.set_hyper_sp(character_.hyper_sp() - amount);
    (*character_.mutable_skill_levels())[skill.name()] += amount;
    return true;
  }
  int stage = StageForAdvancement(BookOf(skill));
  (*character_.mutable_skill_levels())[skill.name()] += amount;
  (*character_.mutable_sp_by_stage())[stage] -= amount;
  return true;
}

JobAdvancement CharacterInstance::BookHeldFor(const Skill& skill) const {
  for (const SkillPlacement& placement : skill.placement()) {
    if (HasAdvancement(placement.job_advancement())) {
      return placement.job_advancement();
    }
  }
  return JOB_ADVANCEMENT_UNSPECIFIED;
}

bool CharacterInstance::HoldsSkillFrom(const Skill& skill,
                                       Activity activity) const {
  if (skill.v_node() != V_NODE_KIND_UNSPECIFIED) {
    return ReachesVNode(skill);
  }
  if (skill.link_line() != JOB_UNSPECIFIED) {
    return HoldsLinkSkill(skill, activity);
  }
  return HasBookFor(skill);
}

const google::protobuf::RepeatedPtrField<std::string>&
CharacterInstance::link_skills(StatPreset slot) const {
  return PresetOf(character_.link_skills(), slot).skills();
}

bool CharacterInstance::HoldsLinkSkill(const Skill& skill,
                                       Activity activity) const {
  if (skill.link_line() == JOB_UNSPECIFIED || link_skills_off_) {
    return false;
  }
  // The character's own line's link skill is always active and doesn't use one
  // of the twelve slots. That is GMS's rule, and why no preset lists it.
  if (BranchOf(skill.link_line()) == BranchOf(character_.job())) {
    return true;
  }
  return absl::c_linear_search(
      link_skills(SlotFor(PresetKind::kLinkSkills, activity)), skill.name());
}

int CharacterInstance::LinkSkillLevel(const Skill& skill,
                                      Activity activity) const {
  return HoldsLinkSkill(skill, activity) ? LinkSkillLevelOffered(skill) : 0;
}

int CharacterInstance::LinkSkillLevelOffered(const Skill& skill) const {
  if (skill.link_line() == JOB_UNSPECIFIED) {
    return 0;
  }
  int level = link_tally_.With(character_.job(), character_.level())
                  .LevelFor(skill.link_line());
  if (BranchOf(skill.link_line()) == BranchOf(character_.job())) {
    level = std::max(level, 1);
  }
  return std::min(SkillMaxLevel(skill), level);
}

bool CharacterInstance::EquipLinkSkill(const std::string& name,
                                       StatPreset slot) {
  LinkPreset& preset = PresetOf(*character_.mutable_link_skills(), slot);
  if (preset.skills_size() >= kMaxEquippedLinkSkills ||
      absl::c_linear_search(preset.skills(), name)) {
    return false;
  }
  preset.add_skills(name);
  return true;
}

bool CharacterInstance::UnequipLinkSkill(const std::string& name,
                                         StatPreset slot) {
  LinkPreset& preset = PresetOf(*character_.mutable_link_skills(), slot);
  auto it = absl::c_find(preset.skills(), name);
  if (it == preset.skills().end()) {
    return false;
  }
  preset.mutable_skills()->erase(it);
  return true;
}

int CharacterInstance::ReconcileLinkSkills(
    const std::map<std::string, Skill>& skills) {
  // Whether a name could be equipped by this character: a link skill still in
  // the catalog, from another line. The catalog is keyed by file name and a
  // preset by display name, so this scans the entries instead of looking one
  // up.
  auto equippable = [this, &skills](const std::string& name) {
    for (const std::pair<const std::string, Skill>& entry : skills) {
      const Skill& skill = entry.second;
      if (skill.name() == name && skill.link_line() != JOB_UNSPECIFIED &&
          BranchOf(skill.link_line()) != BranchOf(character_.job())) {
        return true;
      }
    }
    return false;
  };
  int moved = 0;
  for (int i = 0; i < kNumStatPresets; ++i) {
    const StatPreset slot = StatPresetAt(i);
    LinkPreset& preset = PresetOf(*character_.mutable_link_skills(), slot);
    for (int at = preset.skills_size() - 1; at >= 0; --at) {
      if (!equippable(preset.skills(at))) {
        preset.mutable_skills()->DeleteSubrange(at, 1);
        ++moved;
      }
    }
    for (const std::pair<const std::string, Skill>& entry : skills) {
      const Skill& skill = entry.second;
      if (skill.link_line() == JOB_UNSPECIFIED ||
          BranchOf(skill.link_line()) == BranchOf(character_.job())) {
        continue;
      }
      moved += EquipLinkSkill(skill.name(), slot) ? 1 : 0;
    }
  }
  return moved;
}

bool CharacterInstance::ReachesVNode(const Skill& skill) const {
  if (!v_matrix_unlocked()) {
    return false;
  }
  // A common node belongs to no job, so every matrix can hold it. Any other
  // node names the 5th advancement whose job may buy it.
  return ListedIn(skill, JOB_ADVANCEMENT_COMMON) || HasBookFor(skill);
}

int CharacterInstance::VNodeCostFor(const Skill& skill, int amount) const {
  if (skill.v_node() == V_NODE_KIND_UNSPECIFIED || amount <= 0) {
    return 0;
  }
  int level = skill_level(skill);
  return VNodeCost(skill.v_node(), level, level + amount);
}

int CharacterInstance::LevelsAffordable(const Skill& skill) const {
  int room = SkillMaxLevel(skill) - skill_level(skill);
  if (room <= 0) {
    return 0;
  }
  if (skill.v_node() == V_NODE_KIND_UNSPECIFIED) {
    return std::min(SpFor(skill), room);
  }
  // Steps up the ladder rather than dividing, because a node's levels don't all
  // cost the same, so how many the pool buys depends on the node's current
  // level.
  int levels = 0;
  while (levels < room && VNodeCostFor(skill, levels + 1) <= v_points()) {
    ++levels;
  }
  return levels;
}

bool CharacterInstance::LearnVNode(const Skill& skill, int amount) {
  if (!ReachesVNode(skill)) {
    return false;
  }
  if (skill_level(skill) + amount > SkillMaxLevel(skill)) {
    return false;
  }
  int cost = VNodeCostFor(skill, amount);
  if (cost > character_.v_points()) {
    return false;
  }
  character_.set_v_points(character_.v_points() - cost);
  (*character_.mutable_skill_levels())[skill.name()] += amount;
  return true;
}

EquipType CharacterInstance::weapon_type(StatPreset preset) const {
  const EquipInstance* weapon = WornAt(preset, EQUIP_SLOT_PRIMARY_WEAPON);
  return weapon != nullptr ? weapon->prototype().equip_type()
                           : EQUIP_TYPE_UNSPECIFIED;
}

bool CharacterInstance::has_secondary(StatPreset preset) const {
  return WornAt(preset, EQUIP_SLOT_SECONDARY) != nullptr;
}

bool CharacterInstance::AttackCounts(const EquipPrototype& proto,
                                     StatPreset preset) const {
  EquipType drawn_by = WeaponDrawing(proto.equip_type());
  if (drawn_by == EQUIP_TYPE_UNSPECIFIED) {
    return true;  // not ammunition, so it always counts
  }
  return weapon_type(preset) == drawn_by;
}

void CharacterInstance::UseEquipSets(std::map<std::string, EquipSet> sets) {
  equip_sets_ = std::move(sets);
  for (int i = 0; i < kNumStatPresets; ++i) {
    RecomputeSetBonuses(StatPresetAt(i));
  }
}

bool CharacterInstance::IsWearing(const std::string& item_name,
                                  StatPreset preset) const {
  // Matched by display name, as a save names items: the character holds
  // prototypes, not the catalog keys they were loaded under.
  for (const std::pair<const EquipSlot, const EquipInstance*>& kv :
       equipped(preset)) {
    if (kv.second->prototype().name() == item_name) {
      return true;
    }
  }
  return false;
}

std::string CharacterInstance::WornOfFamily(const std::string& family,
                                            StatPreset preset) const {
  if (family.empty()) {
    return "";  // an ordinary item has no family, and "" would match every one
  }
  for (const std::pair<const EquipSlot, const EquipInstance*>& kv :
       equipped(preset)) {
    if (kv.second->prototype().set_family() == family) {
      return kv.second->prototype().name();
    }
  }
  return "";
}

std::string CharacterInstance::WornOfMember(const EquipSetMember& member,
                                            StatPreset preset) const {
  // A member lists the items that fill its slot, a family of items, or both.
  // One slot counts once however many of them are worn, so the first match
  // decides.
  for (const std::string& name : member.items().name()) {
    if (IsWearing(name, preset)) {
      return name;
    }
  }
  return member.has_family() ? WornOfFamily(member.family(), preset) : "";
}

int CharacterInstance::PiecesWornOf(const EquipSet& set,
                                    StatPreset preset) const {
  int worn = 0;
  for (const EquipSetMember& member : set.members()) {
    if (!WornOfMember(member, preset).empty()) {
      ++worn;
    }
  }
  return worn;
}

void CharacterInstance::RecomputeSetBonuses(StatPreset preset) {
  std::vector<SkillEffect>& bonuses = set_bonuses_[IndexOf(preset)];
  bonuses.clear();
  for (const std::pair<const std::string, EquipSet>& entry : equip_sets_) {
    const EquipSet& set = entry.second;
    int worn = PiecesWornOf(set, preset);
    for (const EquipSetTier& tier : set.tiers()) {
      if (worn >= tier.pieces()) {
        bonuses.push_back(tier.effect());
      }
    }
  }
}

const EquipInstance* CharacterInstance::WornAt(StatPreset preset,
                                               EquipSlot slot) const {
  const WornGear& gear = resolved_[IndexOf(preset)];
  WornGear::const_iterator it = gear.find(slot);
  return it == gear.end() ? nullptr : it->second;
}

EquipInstance* CharacterInstance::WornIn(StatPreset preset, EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>& own = worn_[IndexOf(preset)];
  std::map<EquipSlot, EquipInstance>::iterator it = own.find(slot);
  if (it != own.end()) {
    return &it->second;
  }
  if (preset == StatPreset::kFirst) {
    return nullptr;  // the first preset inherits from nothing
  }
  std::map<EquipSlot, EquipInstance>& base = worn_[IndexOf(StatPreset::kFirst)];
  it = base.find(slot);
  return it == base.end() ? nullptr : &it->second;
}

bool CharacterInstance::InheritsSlot(StatPreset preset, EquipSlot slot) const {
  return preset != StatPreset::kFirst &&
         worn_[IndexOf(preset)].count(slot) == 0 &&
         WornAt(preset, slot) != nullptr;
}

std::optional<EquipInstance> CharacterInstance::TakeWorn(StatPreset preset,
                                                         EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>& own = worn_[IndexOf(preset)];
  std::map<EquipSlot, EquipInstance>::iterator it = own.find(slot);
  if (it == own.end()) {
    return std::nullopt;  // inherited or empty
  }
  EquipInstance taken = std::move(it->second);
  own.erase(it);
  return taken;
}

// One preset's worn totals. An attack that doesn't count is dropped here, where
// equipment becomes stats, so the damage formula, combat power and the stat
// panel always agree.
void CharacterInstance::RecomputePreset(StatPreset preset) {
  const int index = IndexOf(preset);
  std::vector<EquipStats> list;
  std::vector<EquipStats> symbols;
  arcane_force_[index] = 0;
  potential_totals_[index] = PotentialTotals();
  for (const std::pair<const EquipSlot, const EquipInstance*>& kv :
       resolved_[index]) {
    const EquipInstance& item = *kv.second;
    AddPotential(item.potential(), item.prototype().required_level(),
                 potential_totals_[index]);
    // A symbol's stats aren't on its prototype: it grants its level in the
    // wearer's primary stat. Its Arcane Force is added in the same pass.
    if (IsArcaneSymbol(item.prototype())) {
      int level = SymbolLevel(item.equip_state());
      arcane_force_[index] += SymbolArcaneForce(level);
      EquipStats granted =
          SymbolStatsFor(PrimaryStatField(character_.job()), level);
      symbols.push_back(granted);
      list.push_back(std::move(granted));
      continue;
    }
    EquipStats stats = item.stats();
    if (!AttackCounts(item.prototype(), preset)) {
      stats.set_attack(0);
    }
    list.push_back(std::move(stats));
  }
  equip_stats_[index] = SumEquipStats(absl::MakeSpan(list));
  symbol_stats_[index] = SumEquipStats(absl::MakeSpan(symbols));
  // The set bonus comes from the same gear and changes with it, so both are
  // recomputed together.
  RecomputeSetBonuses(preset);
}

namespace {

// Whether `own` has the same item as `inherited` in another slot of `slot`'s
// family. If so, the preset leaves the inherited slot empty: no preset may show
// the same ring twice, and the preset keeps its own copy.
bool OwnCopyElsewhere(const std::map<EquipSlot, EquipInstance>& own,
                      EquipSlot slot, const EquipInstance& inherited) {
  for (EquipSlot other : SlotFamily(slot)) {
    if (other == slot) {
      continue;
    }
    std::map<EquipSlot, EquipInstance>::const_iterator it = own.find(other);
    if (it != own.end() &&
        it->second.prototype().name() == inherited.prototype().name()) {
      return true;
    }
  }
  return false;
}

}  // namespace

void CharacterInstance::RecomputeEquipStats() {
  const std::map<EquipSlot, EquipInstance>& base =
      worn_[IndexOf(StatPreset::kFirst)];
  for (int i = 0; i < kNumStatPresets; ++i) {
    WornGear& gear = resolved_[i];
    gear.clear();
    for (const std::pair<const EquipSlot, EquipInstance>& kv : base) {
      if (i != IndexOf(StatPreset::kFirst) &&
          OwnCopyElsewhere(worn_[i], kv.first, kv.second)) {
        continue;
      }
      gear[kv.first] = &kv.second;
    }
    for (const std::pair<const EquipSlot, EquipInstance>& kv : worn_[i]) {
      gear[kv.first] = &kv.second;
    }
    RecomputePreset(StatPresetAt(i));
  }
}

int CharacterInstance::SpareSymbols(EquipSlot slot) const {
  return static_cast<int>(SpareSymbolWorths(slot).size());
}

std::vector<int> CharacterInstance::SpareSymbolWorths(EquipSlot slot) const {
  std::vector<int> worths;
  // Backwards, which is the order CombineSymbols uses them in.
  for (int i = inventory_.size() - 1; i >= 0; --i) {
    const EquipInstance* spare = inventory_.equip_instance(i);
    if (spare != nullptr && IsArcaneSymbol(spare->prototype()) &&
        spare->prototype().equip_slot() == slot) {
      worths.push_back(SymbolWorth(spare->equip_state()));
    }
  }
  return worths;
}

int CharacterInstance::CombineSymbols(EquipSlot slot, int count,
                                      StatPreset preset) {
  EquipInstance* symbol = WornIn(preset, slot);
  if (count <= 0 || symbol == nullptr || !IsArcaneSymbol(symbol->prototype())) {
    return 0;
  }
  ms::Equip state = symbol->equip_state();
  int taken = 0;
  // Backwards, so removing one doesn't shift the ones not yet checked.
  for (int i = inventory_.size() - 1; i >= 0 && taken < count; --i) {
    const EquipInstance* spare = inventory_.equip_instance(i);
    if (spare == nullptr || !IsArcaneSymbol(spare->prototype()) ||
        spare->prototype().equip_slot() != slot) {
      continue;
    }
    // A consumed symbol's EXP is added, not lost: its levels are converted back
    // into EXP, and every duplicate absorbed into them counts.
    state.set_symbol_exp(state.symbol_exp() +
                         SymbolWorth(spare->equip_state()));
    inventory_.remove_equip(i);
    ++taken;
  }
  if (taken > 0) {
    *symbol = EquipInstance(symbol->prototype(), state);
  }
  return taken;
}

bool CharacterInstance::CubeWorn(EquipSlot slot, CubeType cube,
                                 StatPreset preset) {
  EquipInstance* item = WornIn(preset, slot);
  if (item == nullptr || !item->Cube(cube, rng_)) {
    return false;
  }
  // The lines are worn stats, so the totals have changed.
  RecomputeEquipStats();
  return true;
}

bool CharacterInstance::CubeWornUpTo(EquipSlot slot, CubeType cube,
                                     PotentialRank want, int rolls,
                                     StatPreset preset) {
  EquipInstance* item = WornIn(preset, slot);
  if (item == nullptr) {
    return false;
  }
  for (int roll = 0; roll < rolls && item->potential().rank() < want; ++roll) {
    if (!item->Cube(cube, rng_)) {
      break;
    }
  }
  RecomputeEquipStats();
  return item->potential().rank() >= want;
}

// Pays for a cube, or returns false and spends nothing. It also checks the
// item, so a cube that can't be used isn't charged.
bool CharacterInstance::PayForCube(const EquipInstance& item) {
  if (!item.CanCube() || kCubeCost > character_.meso()) {
    return false;
  }
  character_.set_meso(character_.meso() - kCubeCost);
  return true;
}

bool CharacterInstance::CubeEquipped(EquipSlot slot, CubeType cube,
                                     StatPreset preset) {
  EquipInstance* item = WornIn(preset, slot);
  if (item == nullptr || !PayForCube(*item)) {
    return false;
  }
  return CubeWorn(slot, cube, preset);
}

bool CharacterInstance::CubeInventory(int index, CubeType cube) {
  EquipInstance* item = inventory_.equip_instance(index);
  return item != nullptr && PayForCube(*item) && item->Cube(cube, rng_);
}

std::optional<Potential> CharacterInstance::BuyCube(EquipSlot slot,
                                                    CubeType cube,
                                                    StatPreset preset) {
  const EquipInstance* item = WornAt(preset, slot);
  if (item == nullptr || !item->CanCube() || character_.meso() < kCubeCost) {
    return std::nullopt;
  }
  character_.set_meso(character_.meso() - kCubeCost);
  return CubePotential(item->equip_state().main_potential(), cube,
                       PotentialGroupOf(item->prototype().equip_slot()), rng_);
}

bool CharacterInstance::TakePotential(EquipSlot slot,
                                      const Potential& potential,
                                      StatPreset preset) {
  EquipInstance* item = WornIn(preset, slot);
  if (item == nullptr) {
    return false;
  }
  item->SetPotential(potential);
  // The lines are worn stats, so the totals have changed.
  RecomputeEquipStats();
  return true;
}

bool CharacterInstance::LevelUpSymbol(EquipSlot slot, StatPreset preset) {
  EquipInstance* symbol = WornIn(preset, slot);
  if (symbol == nullptr || !IsArcaneSymbol(symbol->prototype())) {
    return false;
  }
  ms::Equip state = symbol->equip_state();
  if (!SymbolCanLevelUp(state)) {
    return false;
  }
  int64_t cost = SymbolLevelUpCost(symbol->prototype(), SymbolLevel(state));
  if (character_.meso() < cost) {
    return false;
  }
  character_.set_meso(character_.meso() - cost);
  ms::LevelUpSymbol(state);
  // Rebuilt rather than edited in place: an item's state is its own, and a
  // symbol's level is the one outside thing that changes.
  *symbol = EquipInstance(symbol->prototype(), state);
  // A symbol's force and stats come from its level, so both have changed.
  RecomputeEquipStats();
  return true;
}

bool CharacterInstance::PickUp(std::unique_ptr<EquipTabItem> item) {
  if (inventory_.full()) {
    return false;
  }
  inventory_.add(std::move(item));
  return true;
}

void CharacterInstance::ClearEquipInventory() {
  // Backwards, so removing one doesn't shift the ones not yet checked.
  for (int i = inventory_.size() - 1; i >= 0; --i) {
    inventory_.remove_equip(i);
  }
}

int CharacterInstance::RoomFor(const EquipPrototype& proto) const {
  // The item doesn't matter: every copy takes one slot.
  (void)proto;
  return inventory_.room();
}

int64_t CharacterInstance::CountItem(const ItemPrototype& proto) const {
  return IsCurrency(proto) ? currencies_.Count(proto.name())
                           : CountItem(proto.name());
}

int64_t CharacterInstance::CountItem(const std::string& name) const {
  // Check the purse first, and the bag only for names not in the purse. Nothing
  // is in both, and a currency the character has none of returns zero from
  // either.
  if (currencies_.Holds(name)) {
    return currencies_.Count(name);
  }
  return etc_items_.Count(name);
}

bool CharacterInstance::SpendItem(const std::string& name, int64_t count) {
  if (currencies_.Holds(name)) {
    return currencies_.Spend(name, count);
  }
  return etc_items_.Spend(name, count);
}

int CharacterInstance::CountOwned(const EquipPrototype& proto) const {
  // Matched by name, which identifies an equip everywhere. Every preset's own
  // items count: a copy kept for bossing is owned as much as the one used for
  // farming.
  int owned = 0;
  for (const std::map<EquipSlot, EquipInstance>& gear : worn_) {
    for (const std::pair<const EquipSlot, EquipInstance>& item : gear) {
      if (item.second.name() == proto.name()) {
        ++owned;
      }
    }
  }
  for (int i = 0; i < inventory_.size(); ++i) {
    // Traces are excluded two ways: equip_instance() returns nullptr, and the
    // name has a suffix that wouldn't match. The check is explicit rather than
    // relying on the suffix, which is only for display.
    const EquipInstance* item = inventory_.equip_instance(i);
    if (item != nullptr && item->name() == proto.name()) {
      ++owned;
    }
  }
  return owned;
}

int CharacterInstance::RoomFor(const ItemPrototype& proto) const {
  // A currency has no limit: it is a number in the save, not a row in a bag.
  if (IsCurrency(proto)) {
    return INT_MAX;
  }
  return etc_items_.RoomFor(proto);
}

int CharacterInstance::AddItem(const ItemPrototype& proto, int count) {
  if (count <= 0) {
    return 0;
  }
  if (IsCurrency(proto)) {
    currencies_.Add(proto, count);
    return count;
  }
  return etc_items_.Add(proto, count);
}

int CharacterInstance::TakeStack(int index, int count) {
  return etc_items_.Take(index, count);
}

std::unique_ptr<EquipTabItem> CharacterInstance::TakeEquip(int index) {
  if (index < 0 || index >= inventory_.size()) {
    return nullptr;
  }
  return inventory_.remove_equip(index);
}

bool CharacterInstance::SpendMeso(int64_t amount) {
  if (amount <= 0 || character_.meso() < amount) {
    return amount <= 0;
  }
  character_.set_meso(character_.meso() - amount);
  return true;
}

void CharacterInstance::AddMeso(int64_t amount) {
  if (amount <= 0) {
    return;
  }
  character_.set_meso(character_.meso() + amount);
}

namespace {

// The index of `type` in `list`, or -1 if it isn't there.
int IndexOfConsumable(const google::protobuf::RepeatedField<int>& list,
                      ConsumableType type) {
  for (int i = 0; i < list.size(); ++i) {
    if (list.Get(i) == type) {
      return i;
    }
  }
  return -1;
}

}  // namespace

bool CharacterInstance::ConsumableOwned(ConsumableType type) const {
  return IndexOfConsumable(character_.consumables().owned(), type) >= 0;
}

bool CharacterInstance::ConsumableActive(ConsumableType type) const {
  return IndexOfConsumable(character_.consumables().active(), type) >= 0;
}

bool CharacterInstance::ConsumableInEffect(ConsumableType type) const {
  const ConsumableInfo* info = ConsumableInfoFor(type);
  return info != nullptr && character_.level() >= info->unlock_level &&
         ConsumableActive(type);
}

bool CharacterInstance::ToggleConsumable(ConsumableType type) {
  const ConsumableInfo* info = ConsumableInfoFor(type);
  if (info == nullptr || character_.level() < info->unlock_level) {
    return false;
  }
  google::protobuf::RepeatedField<int>& active =
      *character_.mutable_consumables()->mutable_active();
  int at = IndexOfConsumable(active, type);
  if (at >= 0) {
    active.erase(active.begin() + at);
    return false;
  }
  active.Add(type);
  return true;
}

bool CharacterInstance::BuyConsumable(ConsumableType type) {
  const ConsumableInfo* info = ConsumableInfoFor(type);
  if (info == nullptr || character_.level() < info->unlock_level ||
      ConsumableOwned(type) || character_.meso() < info->permanent_price) {
    return false;
  }
  character_.set_meso(character_.meso() - info->permanent_price);
  character_.mutable_consumables()->add_owned(type);
  return true;
}

int64_t CharacterInstance::ChargeConsumable(ConsumableType type, double procs) {
  const ConsumableInfo* info = ConsumableInfoFor(type);
  if (info == nullptr || procs <= 0.0 || ConsumableOwned(type) ||
      !ConsumableInEffect(type)) {
    return 0;
  }
  consumable_debt_ += info->price * procs;
  // A nudge before the floor: three ticks at a thousand a second add up to
  // 999.999... in binary, and a debt just under a whole meso should count as
  // one.
  constexpr double kMesoEpsilon = 1e-6;
  int64_t owed =
      static_cast<int64_t>(std::floor(consumable_debt_ + kMesoEpsilon));
  consumable_debt_ -= owed;
  int64_t taken = std::min(owed, character_.meso());
  character_.set_meso(character_.meso() - taken);
  return taken;
}

int64_t CharacterInstance::ChargeFarmingConsumables(double seconds) {
  int64_t taken = 0;
  for (const ConsumableInfo& info : AllConsumables()) {
    if (info.per_second) {
      taken += ChargeConsumable(info.type, seconds);
    }
  }
  return taken;
}

void CharacterInstance::AddHonor(int64_t amount) {
  if (amount <= 0) {
    return;
  }
  character_.set_honor(character_.honor() + amount);
}

void CharacterInstance::AddVPoints(int64_t amount) {
  if (amount <= 0) {
    return;
  }
  character_.set_v_points(character_.v_points() + amount);
}

int64_t CharacterInstance::SellStackable(int index, int count) {
  if (index < 0 || index >= etc_items_.size()) {
    return 0;
  }
  const StackableItem& stack = etc_items_[index];
  int price = stack.prototype().sell_price();
  BuyBackEntry entry;
  entry.mutable_stack()->set_name(stack.name());
  entry.set_unit_price(price);
  count = etc_items_.Take(index, count);
  if (count <= 0) {
    return 0;
  }
  entry.mutable_stack()->set_count(count);
  int64_t earned = static_cast<int64_t>(count) * price;
  AddMeso(earned);
  RecordSale(std::move(entry));
  return earned;
}

int64_t CharacterInstance::SellEquip(int index) {
  if (index < 0 || index >= inventory_.size()) {
    return 0;
  }
  // A trace records a destroyed item and isn't a copy of it, so it sells for
  // what the record is worth. Selling one is how the player gets rid of it
  // after giving up on recovering it.
  bool is_trace = inventory_.equip_instance(index) == nullptr;
  int64_t earned = is_trace ? 0 : SellPrice(inventory_[index].prototype());
  // SavedState rather than equip_state, so the shelf can tell a trace from a
  // live item when it gives the row back.
  BuyBackEntry entry;
  *entry.mutable_equip() = inventory_[index].SavedState();
  entry.set_unit_price(earned);
  inventory_.remove_equip(index);
  AddMeso(earned);
  RecordSale(std::move(entry));
  return earned;
}

void CharacterInstance::RecordSale(BuyBackEntry entry) {
  // Newest first, so the row the player wants is the first one. The shelf has
  // only kBuyBackSlots rows, so moving the new entry to the front is cheap
  // enough without a deque.
  *character_.add_buy_backs() = std::move(entry);
  for (int i = character_.buy_backs_size() - 1; i > 0; --i) {
    character_.mutable_buy_backs()->SwapElements(i, i - 1);
  }
  if (character_.buy_backs_size() > kBuyBackSlots) {
    character_.mutable_buy_backs()->DeleteSubrange(
        kBuyBackSlots, character_.buy_backs_size() - kBuyBackSlots);
  }
}

bool CharacterInstance::BuyBack(
    int index, int count, const std::map<std::string, EquipPrototype>& equips,
    const std::map<std::string, ItemPrototype>& items) {
  if (index < 0 || index >= character_.buy_backs_size()) {
    return false;
  }
  // Copied before anything is removed: `entry` would be a reference into the
  // shelf, and removing the row would leave it pointing at the next one.
  const BuyBackEntry entry = character_.buy_backs(index);
  if (entry.has_equip()) {
    return BuyBackEquip(index, entry, equips);
  }
  return BuyBackStack(index, entry, count, items);
}

bool CharacterInstance::BuyBackEquip(
    int index, const BuyBackEntry& entry,
    const std::map<std::string, EquipPrototype>& equips) {
  std::unique_ptr<EquipTabItem> item =
      RestoreEquipItem(entry.equip(), IndexByDisplayName(equips));
  // Can't return an item that has since been removed from data/, just as
  // loading a save naming it would drop it.
  if (item == nullptr || entry.unit_price() > character_.meso() ||
      inventory_.full()) {
    return false;
  }
  character_.set_meso(character_.meso() - entry.unit_price());
  PickUp(std::move(item));
  character_.mutable_buy_backs()->DeleteSubrange(index, 1);
  return true;
}

bool CharacterInstance::BuyBackStack(
    int index, const BuyBackEntry& entry, int count,
    const std::map<std::string, ItemPrototype>& items) {
  count = std::clamp(count, 0, entry.stack().count());
  std::map<std::string, const ItemPrototype*> by_name =
      IndexByDisplayName(items);
  std::map<std::string, const ItemPrototype*>::const_iterator proto =
      by_name.find(entry.stack().name());
  if (count <= 0 || proto == by_name.end()) {
    return false;
  }
  int64_t cost = static_cast<int64_t>(count) * entry.unit_price();
  if (cost > character_.meso() || count > RoomFor(*proto->second)) {
    return false;
  }
  character_.set_meso(character_.meso() - cost);
  AddItem(*proto->second, count);
  // Buying back part of a row leaves the rest in its place on the shelf. The
  // shelf is a history, and buying some back doesn't make it a newer sale.
  int left = entry.stack().count() - count;
  if (left > 0) {
    character_.mutable_buy_backs(index)->mutable_stack()->set_count(left);
  } else {
    character_.mutable_buy_backs()->DeleteSubrange(index, 1);
  }
  return true;
}

bool CharacterInstance::Buy(const EquipPrototype& proto, int count) {
  // Checks whether a price exists, not its size: a price of zero means the item
  // is free, not that it isn't sold.
  if (count <= 0 || !proto.has_shop_price()) {
    return false;
  }
  // The whole cost is checked at once rather than per copy, so a purchase the
  // character can't finish never charges for part of it.
  int64_t cost = static_cast<int64_t>(count) * proto.shop_price();
  if (cost > character_.meso()) {
    return false;
  }
  // Space is checked first for the same reason: a purchase the bag can't hold
  // must not charge for the part that would fit.
  if (count > RoomFor(proto)) {
    return false;
  }
  character_.set_meso(character_.meso() - cost);
  for (int i = 0; i < count; ++i) {
    PickUp(std::make_unique<EquipInstance>(proto));
  }
  return true;
}

bool CharacterInstance::BuyWithToken(const EquipPrototype& proto,
                                     const ItemPrototype& token, int count) {
  // A mark is what makes an item a currency, so a token without one buys
  // nothing.
  if (count <= 0 || proto.token_price() <= 0 || token.currency_mark().empty()) {
    return false;
  }
  // Space first, then the whole price at once. SpendItem is all or nothing, so
  // a purchase the character can't finish never spends tokens on part of it.
  if (count > RoomFor(proto)) {
    return false;
  }
  if (!SpendItem(token.name(),
                 static_cast<int64_t>(count) * proto.token_price())) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    PickUp(std::make_unique<EquipInstance>(proto));
  }
  return true;
}

bool CharacterInstance::Buy(const ItemPrototype& proto, int count) {
  if (count <= 0 || proto.shop_price() <= 0) {
    return false;
  }
  int64_t cost = static_cast<int64_t>(count) * proto.shop_price();
  if (cost > character_.meso() || count > RoomFor(proto)) {
    return false;
  }
  character_.set_meso(character_.meso() - cost);
  AddItem(proto, count);
  return true;
}

std::vector<const EquipTrace*> CharacterInstance::traces() const {
  return inventory_.traces();
}

EquipSlot CharacterInstance::SlotToFill(const EquipPrototype& proto,
                                        StatPreset preset) const {
  if (proto.equip_slot() == EQUIP_SLOT_UNSPECIFIED) {
    return EQUIP_SLOT_UNSPECIFIED;
  }
  std::vector<EquipSlot> family = SlotFamily(proto.equip_slot());
  if (family.size() == 1) {
    return family.front();
  }
  // A copy of the same item already worn takes precedence over any free slot.
  // No two of the four rings may be the same ring, so the second copy replaces
  // the first instead of becoming a fifth ring. This checks what the preset
  // shows, not what it owns, since an inherited ring is worn just as much as
  // its own.
  for (EquipSlot slot : family) {
    const EquipInstance* worn = WornAt(preset, slot);
    if (worn != nullptr && worn->prototype().name() == proto.name()) {
      return slot;
    }
  }
  for (EquipSlot slot : family) {
    if (WornAt(preset, slot) == nullptr) {
      return slot;
    }
  }
  // Every slot is full. The first slot's item goes back to the bag; a player
  // who wants a different one replaced can empty that slot themselves.
  return family.front();
}

bool CharacterInstance::Equip(int inventory_index, StatPreset preset) {
  EquipInstance* raw = inventory_.equip_instance(inventory_index);
  if (raw == nullptr) {
    return false;
  }
  EquipSlot slot = SlotToFill(raw->prototype(), preset);
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return false;
  }
  // Remove the item from inventory; move it out before the unique_ptr drops.
  std::unique_ptr<EquipTabItem> ptr = inventory_.remove_equip(inventory_index);
  EquipInstance item = std::move(static_cast<EquipInstance&>(*ptr));

  // If the preset had its own item in that slot, put it where the new one was.
  // An inherited item isn't displaced: it belongs to another preset, which
  // keeps wearing it.
  std::optional<EquipInstance> displaced = TakeWorn(preset, slot);
  if (displaced.has_value()) {
    inventory_.add(std::make_unique<EquipInstance>(*std::move(displaced)),
                   inventory_index);
  }

  worn_[IndexOf(preset)].emplace(slot, std::move(item));
  RecomputeEquipStats();
  return true;
}

CharacterInstance CharacterInstance::Wearing(
    const EquipTabItem& item, StatPreset preset,
    std::optional<EquipSlot> slot_override) const {
  CharacterInstance probe(*this);
  EquipSlot slot = slot_override.value_or(SlotToFill(item.prototype(), preset));
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return probe;
  }
  // Rebuilt from the state rather than copied, so a trace is priced as the item
  // it came from.
  probe.worn_[IndexOf(preset)].insert_or_assign(
      slot, EquipInstance(item.prototype(), item.equip_state()));
  probe.RecomputeEquipStats();
  return probe;
}

bool CharacterInstance::Unequip(EquipSlot slot, StatPreset preset) {
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return false;
  }
  std::optional<EquipInstance> taken = TakeWorn(preset, slot);
  if (!taken.has_value()) {
    return false;
  }
  inventory_.add(std::make_unique<EquipInstance>(*std::move(taken)));
  RecomputeEquipStats();
  return true;
}

ScrollOutcome CharacterInstance::ScrollEquipped(EquipSlot slot,
                                                const Scroll& scroll,
                                                StatPreset preset) {
  EquipInstance* item = WornIn(preset, slot);
  if (item == nullptr) {
    return kScrollFail;
  }
  ScrollOutcome result = item->Scroll(scroll, rng_);
  if (result == kScrollSuccess) {
    RecomputeEquipStats();
  }
  return result;
}

ScrollOutcome CharacterInstance::ScrollInventory(int index,
                                                 const Scroll& scroll) {
  EquipInstance* item = inventory_.equip_instance(index);
  if (item == nullptr) {
    return kScrollFail;
  }
  return item->Scroll(scroll, rng_);
}

// Pays for one attempt, or returns false and spends nothing. GMS charges for
// the attempt, not the star, so a failure or destroy costs the same as a
// success. That is why the top of the ladder is expensive.
bool CharacterInstance::PayForStarForce(const EquipInstance& item) {
  int64_t cost = StarForceCost(item.prototype().required_level(), item.stars());
  if (cost > character_.meso()) {
    return false;
  }
  character_.set_meso(character_.meso() - cost);
  return true;
}

StarForceOutcome CharacterInstance::StarForceEquipped(EquipSlot slot,
                                                      StatPreset preset) {
  EquipInstance* item = WornIn(preset, slot);
  if (item == nullptr) {
    return kStarForceFail;
  }
  if (!PayForStarForce(*item)) {
    return kStarForceNoMeso;
  }
  StarForceOutcome outcome = item->StarForce(rng_);
  if (outcome == kStarForceDestroy) {
    // equip_state() captures the item before the destroy attempt, with stars at
    // the level it was destroyed at, not stars+1. The trace goes in the bag,
    // even for an inherited item, since there was only ever one item.
    inventory_.add(
        std::make_unique<EquipTrace>(item->prototype(), item->equip_state()));
    if (!TakeWorn(preset, slot).has_value()) {
      TakeWorn(StatPreset::kFirst, slot);
    }
  }
  RecomputeEquipStats();
  return outcome;
}

StarForceOutcome CharacterInstance::StarForceInventory(int index) {
  EquipInstance* item = inventory_.equip_instance(index);
  if (item == nullptr) {
    return kStarForceFail;
  }
  if (!PayForStarForce(*item)) {
    return kStarForceNoMeso;
  }
  StarForceOutcome outcome = item->StarForce(rng_);
  if (outcome == kStarForceDestroy) {
    // Arguments to make_unique are evaluated before set() destructs the old
    // item, so item->prototype() and item->equip_state() are safe to call here.
    inventory_.set(index, std::make_unique<EquipTrace>(item->prototype(),
                                                       item->equip_state()));
  }
  return outcome;
}

// Pays for a hammer, or returns false and spends nothing. It also checks the
// item, so a hammer that can't be used isn't charged.
bool CharacterInstance::PayForHammer(const EquipInstance& item) {
  if (!item.CanHammer() || kGoldenHammerCost > character_.meso()) {
    return false;
  }
  character_.set_meso(character_.meso() - kGoldenHammerCost);
  return true;
}

bool CharacterInstance::HammerEquipped(EquipSlot slot, StatPreset preset) {
  EquipInstance* item = WornIn(preset, slot);
  if (item == nullptr || !PayForHammer(*item) || !item->Hammer()) {
    return false;
  }
  // Nothing a hammer adds changes worn stats yet, but the worn totals are
  // rebuilt after every change to a worn item, and one exception is how they
  // drift apart.
  RecomputeEquipStats();
  return true;
}

bool CharacterInstance::HammerInventory(int index) {
  EquipInstance* item = inventory_.equip_instance(index);
  return item != nullptr && PayForHammer(*item) && item->Hammer();
}

int CharacterInstance::RecoverTrace(int trace_index, int base_item_index) {
  int recovery_stars =
      EquipInstance::RecoveryStars(inventory_[trace_index].stars());
  EquipPrototype proto = inventory_[trace_index].prototype();
  ::ms::Equip new_state = inventory_[trace_index].equip_state();
  new_state.set_equip_name(proto.name());
  new_state.set_stars(recovery_stars);

  // Remove the higher index first to keep the lower index valid.
  int lo = std::min(trace_index, base_item_index);
  int hi = std::max(trace_index, base_item_index);
  inventory_.remove_equip(hi);
  inventory_.remove_equip(lo);
  inventory_.add(std::make_unique<EquipInstance>(proto, new_state));
  return recovery_stars;
}

void CharacterInstance::SortEquipTab() {
  inventory_.Sort(
      [this](const EquipPrototype& proto) { return CanEquip(proto); });
}

void CharacterInstance::SortStackTab() {
  etc_items_.Sort();
}

bool CharacterInstance::CanEquip(const EquipPrototype& proto) const {
  if (proto.required_level() > 0 &&
      character_.level() < proto.required_level()) {
    return false;
  }
  EquipJobCategory char_cat = JobToCategory(character_.job());
  if (char_cat == EQUIP_JOB_CATEGORY_UNSPECIFIED) {
    return false;
  }
  for (int cat : proto.equip_job_categories()) {
    if (cat == EQUIP_JOB_CATEGORY_UNIVERSAL || cat == char_cat) {
      return true;
    }
  }
  return false;
}

bool CharacterInstance::MeetsLevel(const EquipPrototype& proto) const {
  return proto.required_level() == 0 ||
         character_.level() >= proto.required_level();
}

bool CharacterInstance::MeetsJob(const EquipPrototype& proto) const {
  // A secondary belongs to one branch of one job category, so that is checked
  // first. The category check alone would let every warrior use every warrior
  // secondary, and the three aren't interchangeable.
  JobAdvancement owner = AdvancementForSecondary(proto.equip_type());
  if (owner != JOB_ADVANCEMENT_UNSPECIFIED) {
    return HasAdvancement(owner);
  }
  if (proto.equip_job_categories_size() == 0) {
    return true;
  }
  EquipJobCategory char_cat = JobToCategory(character_.job());
  if (char_cat == EQUIP_JOB_CATEGORY_UNSPECIFIED) {
    return false;
  }
  for (int cat : proto.equip_job_categories()) {
    if (cat == EQUIP_JOB_CATEGORY_UNIVERSAL || cat == char_cat) {
      return true;
    }
  }
  return false;
}

Character CharacterInstance::ToProto() const {
  Character saved = character_;
  // Rebuilt from scratch rather than kept in sync as items move. These fields
  // are only written here, so there is one place for them to be wrong rather
  // than many.
  saved.clear_inventory();
  saved.clear_legacy_equipped();
  saved.mutable_equip_presets()->clear_presets();
  saved.clear_stacks();
  *saved.mutable_currencies() = currencies_.ToProto();
  for (int i = 0; i < inventory_.size(); ++i) {
    *saved.mutable_inventory()->add_equip_tab() = inventory_[i].SavedState();
  }
  for (const std::map<EquipSlot, EquipInstance>& gear : worn_) {
    EquipPreset& preset = *saved.mutable_equip_presets()->add_presets();
    for (const std::pair<const EquipSlot, EquipInstance>& worn : gear) {
      (*preset.mutable_equipped())[static_cast<int>(worn.first)] =
          worn.second.equip_state();
    }
  }
  etc_items_.AppendTo(saved.mutable_stacks());
  return saved;
}

void CharacterInstance::SetUsername(const std::string& name) {
  if (name.empty()) {
    return;
  }
  character_.set_name(name);
}

void CharacterInstance::RestoreFrom(
    const Character& saved, const std::map<std::string, EquipPrototype>& equips,
    const std::map<std::string, ItemPrototype>& items) {
  character_ = saved;
  // Handles a save from before characters had names or Inner Ability existed.
  // Assigning over character_ loses what the constructor set up, so both
  // loading paths have to call these again.
  EnsureUsername();
  EnsureInnerAbility();
  // From here the live containers own the items. Leaving copies in the proto
  // would let the two drift apart and ToProto might use the stale one.
  character_.clear_inventory();
  character_.clear_legacy_equipped();
  character_.clear_stacks();
  character_.clear_currencies();

  std::map<std::string, const EquipPrototype*> equips_by_name =
      IndexByDisplayName(equips);
  std::map<std::string, const ItemPrototype*> items_by_name =
      IndexByDisplayName(items);

  inventory_ = InventoryInstance();
  for (const ms::Equip& state : saved.inventory().equip_tab()) {
    std::unique_ptr<EquipTabItem> item =
        RestoreEquipItem(state, equips_by_name);
    if (item != nullptr) {
      inventory_.add(std::move(item));
    }
  }

  Character migrated = saved;
  MigrateEquipPresets(migrated);
  for (int i = 0; i < kNumStatPresets; ++i) {
    std::map<EquipSlot, EquipInstance>& gear = worn_[i];
    gear.clear();
    const EquipPreset& preset =
        PresetOf(migrated.equip_presets(), StatPresetAt(i));
    for (const std::pair<const int, ms::Equip>& worn : preset.equipped()) {
      std::map<std::string, const EquipPrototype*>::const_iterator proto =
          equips_by_name.find(worn.second.equip_name());
      if (proto == equips_by_name.end()) {
        continue;
      }
      gear.emplace(static_cast<EquipSlot>(worn.first),
                   EquipInstance(*proto->second, worn.second));
    }
  }
  character_.mutable_equip_presets()->clear_presets();

  currencies_.RestoreFrom(saved.currencies(), items_by_name);

  etc_items_.RestoreFrom(saved.stacks(), items_by_name);

  RecomputeEquipStats();
}

}  // namespace ms
