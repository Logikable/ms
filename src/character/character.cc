#include "src/character/character.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/types/span.h"
#include "src/character/arcane_force.h"
#include "src/character/consumables.h"
#include "src/character/exp_table.h"
#include "src/character/hyper_stats.h"
#include "src/character/job_branch.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/inventory.h"
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
// What levels 101-140 pay instead, so the 4th job's book comes to 200.
constexpr int kFourthJobSpPerLevel = 5;
// The last level that pays SP at all. The 4th job's band runs to the 5th
// advancement, but its book is bought out 60 levels short of that, so the
// levels above pay in Hyper SP, HP, MP and AP.
constexpr int kLastSpLevel = 140;

// The Hyper SP ladder: one point at 140 and every fifth level after it, up to
// 195. Twelve points in all, which is one for each of a job's twelve Hyper
// Skills -- so the choice the page asks for is the ORDER they are taken in,
// the unlock levels being clustered rather than one to a point.
constexpr int kFirstHyperSpLevel = 140;
constexpr int kLastHyperSpLevel = 195;
constexpr int kHyperSpLevelStep = 5;

// What a job's primary stat is worth on advancing into it: it climbs to here
// from kBaseStat, and the AP that pays for the difference comes out of the
// pool. See ResetStatsForJob.
constexpr int kAdvancementPrimaryStat = 25;

// The four stats AP buys, which are the ones an advancement redistributes.
// HP and MP live in the same message but are granted by leveling.
constexpr StatField kApStatFields[] = {STAT_FIELD_STR, STAT_FIELD_DEX,
                                       STAT_FIELD_INT, STAT_FIELD_LUK};

// The stats to take AP back off, in the order they should give it up: the
// primary last, because a character stripped of it is one the player cannot
// play, and the rest are cheaper to lose.
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

// Character levels at which each job advancement (1st..6th) unlocks. A level's
// SP goes to the highest stage whose threshold it has passed: levels 11-30
// feed stage 1, 31-60 feed stage 2, and so on.
constexpr int kAdvancementLevels[] = {10, 30, 60, 100, 200, 260};
static_assert(sizeof(kAdvancementLevels) / sizeof(kAdvancementLevels[0]) ==
                  kMaxJobStage,
              "kMaxJobStage must name the last stage there is a level for");

// The job stage a level-up's SP feeds -- the count of advancement thresholds
// the level has passed. 0 at level 10 and below, before 1st-job SP starts.
int SpStageForLevel(int level) {
  int stage = 0;
  for (int threshold : kAdvancementLevels) {
    if (level > threshold) {
      ++stage;
    }
  }
  return stage;
}

// Whether advancing INTO `stage` hands over AP. The 3rd and the 4th do. Both
// AdvanceJob and ExpectedTotalAp ask this rather than spelling the stages out,
// so a save can never be "corrected" against a rule the game stopped using.
bool AdvancementGrantsAp(int stage) {
  return stage == 3 || stage == 4;
}

// SP a level-up pays. Every book costs exactly what its own levels hand over:
// 60 for 11-30, 90 for 31-60, 120 for 61-100, and 200 for 101-140 -- the 4th
// job pays five a level rather than three, which is the whole of what makes
// its book bigger. Past kLastSpLevel there is no book left to buy, so nothing
// is paid.
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

// SP granted for completing an advancement into `job`. Every book is built to
// cost exactly what its levels pay out, so the advancement itself hands over
// nothing: reaching level 11 is no more of an event than reaching level 10
// was. A job whose skills can't be made to total their band sets a bonus here
// to cover the difference.
int JobAdvancementSpBonus(Job job) {
  switch (job) {
    default:
      return 0;
  }
}

// What a level-up grants a job in HP and MP. Real GMS varies this per class
// with no published table (the wiki doesn't state it, and level-up screenshots
// put Mercedes at 24 HP/level against Pathfinder's 36), so these are round
// numbers picked to give each branch its character: warriors bulky, mages
// frail with a deep pool, everyone else -- archers, thieves and eventually
// pirates -- in between.
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

// Appends `stacks` to the saved character under `category`. The tab an item
// belongs to is a property of the item, but it is stored alongside the count
// so a load does not have to consult the catalog to know where to put it --
// and so a stack whose prototype has since vanished lands nowhere rather than
// on the wrong tab.
void AppendStacks(const std::vector<StackableItem>& stacks,
                  ItemCategory category, Character* out) {
  for (const StackableItem& stack : stacks) {
    StackableStack* saved = out->add_stacks();
    saved->set_name(stack.name());
    saved->set_count(stack.count());
    saved->set_category(category);
  }
}

// The catalogs are keyed by the stem of the data file an entry came from
// ("sword"), but a saved item names itself the way the player sees it
// ("Sword") -- that is what Equip.equip_name and a stack's name hold. This
// index is what bridges the two, and is why a save cannot simply look an item
// up in the catalog it was loaded from.
template <typename Proto>
std::map<std::string, const Proto*> IndexByDisplayName(
    const std::map<std::string, Proto>& catalog) {
  std::map<std::string, const Proto*> by_name;
  for (const std::pair<const std::string, Proto>& entry : catalog) {
    by_name[entry.second.name()] = &entry.second;
  }
  return by_name;
}

// One equip-tab entry rebuilt from its saved state, or null if the catalogs no
// longer describe it. A trace and a live item are the same fields apart from
// the flag, which is what decides the type.
std::unique_ptr<EquipTabItem> RestoreEquipItem(
    const Equip& state,
    const std::map<std::string, const EquipPrototype*>& by_name) {
  std::map<std::string, const EquipPrototype*>::const_iterator proto =
      by_name.find(state.equip_name());
  if (proto == by_name.end()) {
    return nullptr;
  }
  if (state.trace()) {
    return std::make_unique<EquipTrace>(*proto->second, state);
  }
  return std::make_unique<EquipInstance>(*proto->second, state);
}

// The four beginner books. Every job in a line answers to the one it grew
// out of, however far along the line it is.
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

// The ten 2nd job books. A 3rd job still holds the one below it.
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

// The ten 3rd job books, one per job -- the top of every line the game has.
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

// The 4th job books. One per 3rd job, and all ten are written now.
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

// The 5th advancement, which no character has taken: reaching one is not a job
// change, so a Dark Knight stays a Dark Knight and simply opens another book.
// Written for the jobs whose 5th job skills exist, and empty for the rest.
JobAdvancement FifthAdvancement(Job job) {
  switch (job) {
    case JOB_DARK_KNIGHT:
      return JOB_ADVANCEMENT_DARK_KNIGHT_V;
    default:
      return JOB_ADVANCEMENT_UNSPECIFIED;
  }
}

}  // namespace

JobAdvancement AdvancementForJobStage(Job job, int stage) {
  // A character keeps every book below the one they are in, so each stage
  // answers for the whole line: a Berserker still holds their Swordman and
  // Spearman skills.
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
      return JOB_PALADIN;
    case JOB_ADVANCEMENT_HERO:
      return JOB_HERO;
    case JOB_ADVANCEMENT_BOW_MASTER:
      return JOB_BOW_MASTER;
    case JOB_ADVANCEMENT_MARKSMAN:
      return JOB_MARKSMAN;
    case JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE:
      return JOB_ICE_LIGHTNING_ARCH_MAGE;
    case JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE:
      return JOB_FIRE_POISON_ARCH_MAGE;
    case JOB_ADVANCEMENT_BISHOP:
      return JOB_BISHOP;
    case JOB_ADVANCEMENT_NIGHT_LORD:
      return JOB_NIGHT_LORD;
    case JOB_ADVANCEMENT_SHADOWER:
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
  // The Beginner's free STR is granted as a stat rather than as AP, so it is
  // on the books from level 1 as something already spent.
  int total = kApPerLevel * std::max(0, level - 1) + kBeginnerStr - kBaseStat;
  for (int stage = 1; stage <= job_stage; ++stage) {
    if (AdvancementGrantsAp(stage)) {
      total += kApJobAdvancementBonus;
    }
  }
  return total;
}

std::vector<std::string> StarterEquipsFor(Job job) {
  // One weapon per 1st job, at the level it happens, so an advancement is
  // playable straight away, plus whatever that weapon draws from. The Rogue
  // gets three: which of the dagger or the claw is held decides what they can
  // swing, so one would pick their build. The Archer gets arrows for the bow
  // only -- a crossbow is the 2nd job's choice, and its arrows are bought.
  //
  // A 2nd job gets its off-hand and no weapon. It arrives already armed and
  // able to afford the tier, so a free weapon would undercut the choice of
  // which to buy -- but nothing else would ever fill the new slot.
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
    // The 1st jobs, each naming what StarterEquipsFor hands it. The Rogue is
    // handed both, and which is held decides what they can swing.
    case JOB_SWORDMAN:
      return {EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD};
    case JOB_MAGICIAN:
      return {EQUIP_TYPE_STAFF};
    case JOB_ARCHER:
      return {EQUIP_TYPE_BOW};
    case JOB_ROGUE:
      return {EQUIP_TYPE_DAGGER, EQUIP_TYPE_CLAW};
    // The warrior branches, each with a pair its skills name by hand.
    case JOB_FIGHTER:
    case JOB_CRUSADER:
    case JOB_HERO:
      // Both hands of each, which reads as "Sword / Axe". Nothing shipped is a
      // one-handed axe yet; the books name the type, so this does too.
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
    // Every mage line, however it casts: the staff is the magician's weapon
    // and no branch of them has a second one.
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
    // The beginner swings on STR, which is the warrior's stat and the one
    // their starting gear carries.
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

// One job leading to the next, for the two advancements that narrow rather
// than fork.
struct Successor {
  Job from;
  Job to;
};

// The 3rd advancement: one branch per 2nd job, so the picker offers a single
// choice rather than a set.
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

// The 4th, and all ten are written. Nothing is written past one, so a 4th job
// is offered nothing at all.
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

// The single job `table` leads on to from `job`, or nothing.
std::vector<Job> Successors(const Successor* table, int count, Job job) {
  for (int i = 0; i < count; ++i) {
    if (table[i].from == job) {
      return {table[i].to};
    }
  }
  return {};
}

std::vector<Job> JobChoicesForStage(Job job, int stage) {
  // The four explorer branches, ordered by the stat each one lives on, so the
  // list reads down STR/DEX/INT/LUK the same way the stat panel does. A
  // Beginner is the only thing that reaches stage 1, so what they hold does
  // not come into it.
  if (stage == 1) {
    return {JOB_SWORDMAN, JOB_ARCHER, JOB_MAGICIAN, JOB_ROGUE};
  }
  // From here on the choice is a function of the job already held.
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
  return {};
}

int StageForAdvancement(JobAdvancement advancement) {
  static_assert(JobAdvancement_ARRAYSIZE == 36,
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
      return 5;
    default:
      return 0;
  }
}

LevelGains GainsForLevels(int from_level, int to_level) {
  LevelGains gains;
  // Walks the levels arrived at, which is what LevelUp grants against: it
  // raises the level first and reads the new one to decide the SP. Keep the
  // two reading the same way -- a test levels a character up and holds the
  // real gains against this, so a change to one that misses the other fails.
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
  for (StatPreset preset : {StatPreset::kFarming, StatPreset::kBossing}) {
    if (PresetOf(ability, preset).lines_size() == 0) {
      PresetOf(ability, preset) = DefaultAbilityPreset();
    }
  }
}

void CharacterInstance::LevelUp() {
  character_.set_level(character_.level() + 1);
  character_.set_ap(character_.ap() + kApPerLevel);
  // HP and MP are granted at the job held right now, so levels earned as a
  // Beginner keep the Beginner rate -- advancing later does not backdate them.
  LevelUpGain gain = LevelUpGainFor(character_.job());
  AllocatedStats* stats = character_.mutable_allocated_stats();
  stats->set_hp(stats->hp() + gain.hp);
  stats->set_mp(stats->mp() + gain.mp);
  // The new level's band decides both how much SP it pays and which stage's
  // book it goes to (none below 11).
  int stage = SpStageForLevel(character_.level());
  if (stage >= 1) {
    (*character_.mutable_sp_by_stage())[stage] +=
        SpForLevel(character_.level());
  }
  // Its own pool, on its own ladder: a Hyper Skill is not any stage's.
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

void CharacterInstance::AddExp(int64_t amount) {
  // At the cap the EXP is dropped rather than banked, so a character who kept
  // fighting there is not sitting on a windfall the day the cap is lifted.
  if (character_.level() >= kTrialLevelCap) {
    return;
  }
  character_.set_exp(character_.exp() + amount);
  while (character_.level() < kTrialLevelCap) {
    int64_t threshold = ExpToNextLevel(character_.level());
    if (character_.exp() < threshold) {
      break;
    }
    character_.set_exp(character_.exp() - threshold);
    LevelUp();
  }
  if (character_.level() >= kTrialLevelCap) {
    character_.set_exp(0);
  }
}

void CharacterInstance::AdvanceJob(Job next_job) {
  int stage = character_.job_stage() + 1;
  character_.set_job_stage(stage);
  character_.set_job(next_job);
  if (AdvancementGrantsAp(stage)) {
    character_.set_ap(character_.ap() + kApJobAdvancementBonus);
  }
  // Each advancement opens a new skill set, so it comes with SP for that stage.
  (*character_.mutable_sp_by_stage())[stage] += JobAdvancementSpBonus(next_job);
  // A worn Arcane Symbol grants the wearer's primary stat, and the job just
  // changed which one that is.
  RecomputeEquipStats();
}

void CharacterInstance::ResetStatsForJob(Job job) {
  // Everything above the base was bought with AP -- including the Beginner's
  // free 13 in STR, which is refunded here rather than stranded, so a character
  // ends up exactly where a fresh one of the new job would be. Working from the
  // stats on hand rather than from the level keeps this right whether or not
  // the player had already spent anything.
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
  // Logged even though it is fixed: a character whose books do not balance is
  // either a save from older rules or a bug in the granting, and the second
  // one is invisible if this quietly tidies up after it.
  LOG(WARNING) << "Character AP is off by " << delta << " at level "
               << character_.level() << ", job stage " << character_.job_stage()
               << "; correcting";
  if (delta > 0) {
    character_.set_ap(character_.ap() + delta);
    return delta;
  }
  // Owed back. The pool goes first, so a character who had spent nothing keeps
  // every stat they did buy.
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
  // Anything still owed had nowhere to come from: every stat is at its base
  // and the pool is empty, which is a fresh character's worth and the closest
  // to right this can get.
  return delta;
}

namespace {

// Every skill of `book`'s own book the character could still put one point
// into: below its own max, its requirement met, its level reached. Asked once
// per point, so a skill that the point before it has just unlocked joins the
// list rather than being missed.
std::vector<const Skill*> TakersIn(const CharacterInstance& character,
                                   const std::map<std::string, Skill>& skills,
                                   const Skill& book) {
  std::vector<const Skill*> takers;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    if (skill.job_advancement() != book.job_advancement() ||
        skill.hyper() != book.hyper() ||
        character.skill_level(skill) >= skill.max_level() ||
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
  // Walked over the catalog rather than over the learned levels, because a
  // display name repeats across branches -- two Endures, ten Maple Warriors --
  // and the book the character holds is what says which one they learned.
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& taught = entry.second;
    if (!HasAdvancement(taught.job_advancement())) {
      continue;
    }
    int spare = skill_level(taught) - taught.max_level();
    if (spare <= 0) {
      continue;
    }
    // Logged even though it is fixed, for the reason ReconcileAp logs: a book
    // that no longer fits is either a save from older data or a bug in the
    // granting, and the second is invisible if this quietly tidies up.
    LOG(WARNING) << taught.name() << " is taught to " << skill_level(taught)
                 << " of a maximum " << taught.max_level()
                 << "; cutting it back and re-spending " << spare;
    (*character_.mutable_skill_levels())[taught.name()] = taught.max_level();
    moved += spare;
    for (; spare > 0; --spare) {
      std::vector<const Skill*> takers = TakersIn(*this, skills, taught);
      if (takers.empty()) {
        // Nowhere in the book to put it. A book costs exactly what its levels
        // pay out, so there always should be -- the point goes back to the
        // pool that bought it rather than being lost.
        if (taught.hyper()) {
          character_.set_hyper_sp(character_.hyper_sp() + 1);
        } else {
          (*character_.mutable_sp_by_stage())[StageForAdvancement(
              taught.job_advancement())] += 1;
        }
        continue;
      }
      std::uniform_int_distribution<std::size_t> pick(0, takers.size() - 1);
      (*character_.mutable_skill_levels())[takers[pick(rng_)]->name()] += 1;
    }
  }
  return moved;
}

bool CharacterInstance::CanAdvanceJob() const {
  // The stage the character would move into, and the level it opens at. An
  // advancement with no choices defined is not offered -- that is what stops
  // this from claiming a 2nd job exists before the jobs behind it do.
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

int CharacterInstance::arcane_force(StatPreset preset) const {
  return arcane_force_ + static_cast<int>(hyper_stat_bonus(
                             HYPER_STAT_FIELD_ARCANE_FORCE, preset));
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

bool CharacterInstance::AllocateHyperStat(HyperStatField field,
                                          StatPreset preset, int amount) {
  if (amount <= 0 || !HyperStatUnlocked(field, character_.level())) {
    return false;
  }
  int level = hyper_stat_level(field, preset);
  if (level + amount > max_hyper_stat_level()) {
    return false;
  }
  // Every level of the run is priced, since each one costs more than the last.
  int price = HyperStatTotalCost(level + amount) - HyperStatTotalCost(level);
  if (price > hyper_stat_points_left(preset)) {
    return false;
  }
  SetHyperStatLevel(PresetOf(*character_.mutable_hyper_stats(), preset), field,
                    level + amount);
  return true;
}

void CharacterInstance::ResetHyperStats(StatPreset preset) {
  PresetOf(*character_.mutable_hyper_stats(), preset).clear_levels();
}

int CharacterInstance::ReconcileHyperPreset(StatPreset preset) {
  HyperStatPreset& allocation =
      PresetOf(*character_.mutable_hyper_stats(), preset);
  int moved = 0;
  // The stats to walk, in enum order, so two saves in the same state are
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
    // A stat the data no longer names, or one this character's level has
    // closed, keeps nothing.
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
  // What is left may still outspend the pool -- a save from a level cap that
  // has since come down. The highest level goes first: it is the dearest one,
  // so the fewest of them are taken.
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
  const StatPreset presets[] = {StatPreset::kFarming, StatPreset::kBossing};
  for (StatPreset preset : presets) {
    moved += ReconcileHyperPreset(preset);
  }
  if (moved > 0) {
    LOG(WARNING) << "Character Hyper Stats were over by " << moved
                 << " points at level " << character_.level() << "; correcting";
  }
  return moved;
}

bool CharacterInstance::HasAdvancement(JobAdvancement advancement) const {
  if (advancement == JOB_ADVANCEMENT_UNSPECIFIED) {
    return false;
  }
  int stage = StageForAdvancement(advancement);
  if (stage <= 0 || stage > character_.job_stage()) {
    return false;
  }
  // The stage is one the character has reached; the question left is whether
  // it is their own branch of it.
  return AdvancementForJobStage(character_.job(), stage) == advancement;
}

bool CharacterInstance::MeetsSkillRequirement(const Skill& skill) const {
  if (!skill.has_required_skill()) {
    return true;
  }
  const SkillRequirement& required = skill.required_skill();
  // Learned levels are keyed by display name, which is exactly what the
  // requirement names -- so this needs no catalog to resolve.
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
  // A Vengeance form is bought by buying the skill it stands in for. Its own
  // name holds no level at all, so a point spent here would vanish.
  if (!skill.replaces_skill_name().empty()) {
    return false;
  }
  if (!HasAdvancement(skill.job_advancement())) {
    return false;
  }
  if (!MeetsSkillRequirement(skill)) {
    return false;
  }
  if (character_.level() < skill.required_level()) {
    return false;
  }
  if (skill_level(skill) + amount > skill.max_level()) {
    return false;
  }
  // A Hyper Skill is bought out of the character's own pool. Everything above
  // holds for it too: it names the advancement whose book it belongs to, so a
  // Paladin cannot buy a Dark Knight's.
  if (amount > SpFor(skill)) {
    return false;
  }
  if (skill.hyper()) {
    character_.set_hyper_sp(character_.hyper_sp() - amount);
    (*character_.mutable_skill_levels())[skill.name()] += amount;
    return true;
  }
  int stage = StageForAdvancement(skill.job_advancement());
  (*character_.mutable_skill_levels())[skill.name()] += amount;
  (*character_.mutable_sp_by_stage())[stage] -= amount;
  return true;
}

EquipType CharacterInstance::weapon_type() const {
  std::map<EquipSlot, EquipInstance>::const_iterator weapon =
      equipped_.find(EQUIP_SLOT_PRIMARY_WEAPON);
  return weapon != equipped_.end() ? weapon->second.prototype().equip_type()
                                   : EQUIP_TYPE_UNSPECIFIED;
}

bool CharacterInstance::has_secondary() const {
  return equipped_.find(EQUIP_SLOT_SECONDARY) != equipped_.end();
}

bool CharacterInstance::AttackCounts(const EquipPrototype& proto) const {
  EquipType drawn_by = WeaponDrawing(proto.equip_type());
  if (drawn_by == EQUIP_TYPE_UNSPECIFIED) {
    return true;  // not ammunition, so nothing has to draw it
  }
  return weapon_type() == drawn_by;
}

void CharacterInstance::UseEquipSets(std::map<std::string, EquipSet> sets) {
  equip_sets_ = std::move(sets);
  RecomputeSetBonuses();
}

bool CharacterInstance::IsWearing(const std::string& item_name) const {
  // By display name, the way a save names what it holds: the character carries
  // prototypes, not the catalog keys they were loaded under.
  for (const std::pair<const EquipSlot, EquipInstance>& kv : equipped_) {
    if (kv.second.prototype().name() == item_name) {
      return true;
    }
  }
  return false;
}

std::string CharacterInstance::WornOfFamily(const std::string& family) const {
  if (family.empty()) {
    return "";  // an ordinary item names no family, and would match every one
  }
  for (const std::pair<const EquipSlot, EquipInstance>& kv : equipped_) {
    if (kv.second.prototype().set_family() == family) {
      return kv.second.prototype().name();
    }
  }
  return "";
}

std::string CharacterInstance::WornOfMember(
    const EquipSetMember& member) const {
  // A member names the items that fill its slot, the family any of several
  // fill, or both. One slot counts once however many of them are on, so the
  // first answer is the answer.
  for (const std::string& name : member.items().name()) {
    if (IsWearing(name)) {
      return name;
    }
  }
  return member.has_family() ? WornOfFamily(member.family()) : "";
}

int CharacterInstance::PiecesWornOf(const EquipSet& set) const {
  int worn = 0;
  for (const EquipSetMember& member : set.members()) {
    if (!WornOfMember(member).empty()) {
      ++worn;
    }
  }
  return worn;
}

void CharacterInstance::RecomputeSetBonuses() {
  set_bonuses_.clear();
  for (const std::pair<const std::string, EquipSet>& entry : equip_sets_) {
    const EquipSet& set = entry.second;
    int worn = PiecesWornOf(set);
    for (const EquipSetTier& tier : set.tiers()) {
      if (worn >= tier.pieces()) {
        set_bonuses_.push_back(tier.effect());
      }
    }
  }
}

void CharacterInstance::RecomputeEquipStats() {
  // The set bonus is worked out from the same map and changes with it, so the
  // two are recomputed together and nothing can update one without the other.
  RecomputeSetBonuses();
  // An attack that doesn't count is dropped here -- once, at the one place
  // equipment becomes stats, so that the damage chain, combat power and the
  // stat panel cannot come to different conclusions about the same stars.
  std::vector<EquipStats> list;
  arcane_force_ = 0;
  for (const std::pair<const EquipSlot, EquipInstance>& kv : equipped_) {
    // A symbol's stats are not on its prototype: what it grants is its level
    // in the wearer's own primary stat, so it is worked out here rather than
    // read. Its Arcane Force is totalled in the same pass, since both come
    // off the same worn symbol.
    if (IsArcaneSymbol(kv.second.prototype())) {
      int level = SymbolLevel(kv.second.equip_state());
      arcane_force_ += SymbolArcaneForce(level);
      list.push_back(SymbolStatsFor(PrimaryStatField(character_.job()), level));
      continue;
    }
    EquipStats stats = kv.second.stats();
    if (!AttackCounts(kv.second.prototype())) {
      stats.set_attack(0);
    }
    list.push_back(std::move(stats));
  }
  equip_stats_ = SumEquipStats(absl::MakeSpan(list));
}

int CharacterInstance::SpareSymbols(EquipSlot slot) const {
  int count = 0;
  for (int i = 0; i < inventory_.size(); ++i) {
    const EquipInstance* spare = inventory_.equip_instance(i);
    if (spare != nullptr && IsArcaneSymbol(spare->prototype()) &&
        spare->prototype().equip_slot() == slot) {
      ++count;
    }
  }
  return count;
}

int CharacterInstance::CombineSymbols(EquipSlot slot, int count) {
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (count <= 0 || it == equipped_.end() ||
      !IsArcaneSymbol(it->second.prototype())) {
    return 0;
  }
  ms::Equip state = it->second.equip_state();
  int taken = 0;
  // Backwards, so removing one does not slide the ones still to be looked at.
  for (int i = inventory_.size() - 1; i >= 0 && taken < count; --i) {
    const EquipInstance* spare = inventory_.equip_instance(i);
    if (spare == nullptr || !IsArcaneSymbol(spare->prototype()) ||
        spare->prototype().equip_slot() != slot) {
      continue;
    }
    // A copy is worth one, plus whatever it had banked itself: what a
    // sacrificed symbol carries is added rather than lost. Nothing can level a
    // symbol sitting in the bag, so today that second term is always zero.
    state.set_symbol_exp(state.symbol_exp() + 1 +
                         spare->equip_state().symbol_exp());
    inventory_.remove_equip(i);
    ++taken;
  }
  if (taken > 0) {
    it->second = EquipInstance(it->second.prototype(), state);
  }
  return taken;
}

bool CharacterInstance::LevelUpSymbol(EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it == equipped_.end() || !IsArcaneSymbol(it->second.prototype())) {
    return false;
  }
  ms::Equip state = it->second.equip_state();
  if (!SymbolCanLevelUp(state)) {
    return false;
  }
  int64_t cost = SymbolLevelUpCost(it->second.prototype(), SymbolLevel(state));
  if (character_.meso() < cost) {
    return false;
  }
  character_.set_meso(character_.meso() - cost);
  ms::LevelUpSymbol(state);
  // Rebuilt rather than written through: an item's state is its own, and a
  // symbol's ladder is the one thing outside it that moves.
  it->second = EquipInstance(it->second.prototype(), state);
  // The level is what a symbol's force and stats are read off, so both have
  // just moved.
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
  // Backwards, so removing one does not slide the ones still to go.
  for (int i = inventory_.size() - 1; i >= 0; --i) {
    inventory_.remove_equip(i);
  }
}

int CharacterInstance::RoomFor(const EquipPrototype& proto) const {
  // Nothing about the item matters: every copy takes one slot, whatever it is.
  (void)proto;
  return inventory_.room();
}

int CharacterInstance::CountStackable(const ItemPrototype& proto) const {
  return CountStackable(proto.category(), proto.name());
}

int CharacterInstance::CountStackable(ItemCategory category,
                                      const std::string& name) const {
  int owned = 0;
  for (const StackableItem& stack : StacksFor(category)) {
    if (stack.name() == name) {
      owned += stack.count();
    }
  }
  return owned;
}

bool CharacterInstance::ConsumeStackable(ItemCategory category,
                                         const std::string& name, int count) {
  if (count <= 0 || CountStackable(category, name) < count) {
    return false;
  }
  std::vector<StackableItem>& stacks = StacksFor(category);
  // Emptied stacks are dropped as they go, so spending the last trace leaves
  // no zero row behind in the bag.
  for (int i = static_cast<int>(stacks.size()) - 1; i >= 0 && count > 0; --i) {
    if (stacks[i].name() != name) {
      continue;
    }
    int taken = std::min(count, stacks[i].count());
    stacks[i].add_count(-taken);
    count -= taken;
    if (stacks[i].count() == 0) {
      stacks.erase(stacks.begin() + i);
    }
  }
  return true;
}

int CharacterInstance::CountOwned(const EquipPrototype& proto) const {
  // Matched on name, which is what identifies an equip everywhere else it
  // crosses a boundary -- the save writes items by display name, and the shop
  // looks its own selection back up the same way.
  int owned = 0;
  for (const std::pair<const EquipSlot, EquipInstance>& worn : equipped_) {
    if (worn.second.name() == proto.name()) {
      ++owned;
    }
  }
  for (int i = 0; i < inventory_.size(); ++i) {
    // Traces are excluded twice over: equip_instance() answers nullptr for
    // one, and EquipTrace::name() carries a suffix that would not match
    // anyway. Kept explicit rather than resting on the suffix, which is a
    // display decision and could reasonably change.
    const EquipInstance* item = inventory_.equip_instance(i);
    if (item != nullptr && item->name() == proto.name()) {
      ++owned;
    }
  }
  return owned;
}

int CharacterInstance::RoomFor(const ItemPrototype& proto) const {
  const std::vector<StackableItem>& stacks = StacksFor(proto.category());
  int free_slots = kTabCapacity - static_cast<int>(stacks.size());
  // A stack that is open but not full takes more without costing a slot.
  int room = 0;
  for (const StackableItem& stack : stacks) {
    if (stack.name() == proto.name()) {
      room += stack.max_stack() - stack.count();
    }
  }
  if (free_slots <= 0) {
    return room;
  }
  // Sized from the prototype rather than an existing stack, so an item the
  // character has none of still reports what a fresh stack would hold.
  StackableItem fresh(proto, 0);
  return room + free_slots * fresh.max_stack();
}

std::vector<StackableItem>& CharacterInstance::StacksFor(
    ItemCategory category) {
  switch (category) {
    case ITEM_CATEGORY_USE:
      return use_items_;
    default:
      // Etc stacks double as the fail-safe destination for unspecified items.
      return etc_items_;
  }
}

const std::vector<StackableItem>& CharacterInstance::StacksFor(
    ItemCategory category) const {
  switch (category) {
    case ITEM_CATEGORY_USE:
      return use_items_;
    default:
      return etc_items_;
  }
}

int CharacterInstance::AddStackable(const ItemPrototype& proto, int count) {
  if (count <= 0) {
    return 0;
  }
  count = std::min(count, RoomFor(proto));
  int added = count;
  std::vector<StackableItem>& stacks = StacksFor(proto.category());
  // Top up existing stacks of the same item before opening new ones.
  for (StackableItem& stack : stacks) {
    if (count <= 0) {
      break;
    }
    if (stack.name() != proto.name()) {
      continue;
    }
    int room = stack.max_stack() - stack.count();
    if (room <= 0) {
      continue;
    }
    int added = std::min(room, count);
    stack.add_count(added);
    count -= added;
  }
  // Open new stacks for any remaining overflow.
  while (count > 0) {
    StackableItem stack(proto, 0);
    int added = std::min(stack.max_stack(), count);
    stack.add_count(added);
    count -= added;
    stacks.push_back(std::move(stack));
  }
  return added;
}

void CharacterInstance::AddMeso(int64_t amount) {
  if (amount <= 0) {
    return;
  }
  character_.set_meso(character_.meso() + amount);
}

namespace {

// Whether `list` names `type`, and where. -1 for one it does not hold.
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
  // Nudged before the floor: three ticks of a thousand a second come to
  // 999.999... in binary, and a debt a hair under a whole meso is a whole one.
  constexpr double kMesoEpsilon = 1e-6;
  int64_t owed =
      static_cast<int64_t>(std::floor(consumable_debt_ + kMesoEpsilon));
  consumable_debt_ -= owed;
  int64_t taken = std::min(owed, character_.meso());
  character_.set_meso(character_.meso() - taken);
  return taken;
}

void CharacterInstance::AddHonor(int64_t amount) {
  if (amount <= 0) {
    return;
  }
  character_.set_honor(character_.honor() + amount);
}

int64_t CharacterInstance::SellStackable(ItemCategory category, int index,
                                         int count) {
  std::vector<StackableItem>& stacks = StacksFor(category);
  if (index < 0 || index >= static_cast<int>(stacks.size())) {
    return 0;
  }
  StackableItem& stack = stacks[index];
  count = std::clamp(count, 0, stack.count());
  int price = stack.prototype().sell_price();
  if (count <= 0) {
    return 0;
  }
  int64_t earned = static_cast<int64_t>(count) * price;
  BuyBackEntry entry;
  entry.mutable_stack()->set_name(stack.name());
  entry.mutable_stack()->set_count(count);
  entry.mutable_stack()->set_category(category);
  entry.set_unit_price(price);
  stack.add_count(-count);
  if (stack.count() <= 0) {
    stacks.erase(stacks.begin() + index);
  }
  AddMeso(earned);
  RecordSale(std::move(entry));
  return earned;
}

int64_t CharacterInstance::SellEquip(int index) {
  if (index < 0 || index >= inventory_.size()) {
    return 0;
  }
  // A trace is the record of a destroyed item, not a copy of it, so it is
  // worth what the record is worth. Selling one is how the player throws it
  // away once they have given up on recovering it.
  bool is_trace = inventory_.equip_instance(index) == nullptr;
  int64_t earned = is_trace ? 0 : SellPrice(inventory_[index].prototype());
  // SavedState rather than equip_state: the shelf has to be able to tell a
  // trace from a live item when it hands the row back.
  BuyBackEntry entry;
  *entry.mutable_equip() = inventory_[index].SavedState();
  entry.set_unit_price(earned);
  inventory_.remove_equip(index);
  AddMeso(earned);
  RecordSale(std::move(entry));
  return earned;
}

void CharacterInstance::RecordSale(BuyBackEntry entry) {
  // Newest first, so the row the player wants is the one they land on. The
  // shelf is 32 long, so walking the new entry up it costs nothing worth a
  // deque.
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
  // Copied out before anything is removed: `entry` is a reference into the
  // shelf, and taking the row off leaves it pointing at the next one.
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
  // Nothing to hand back: the item has since left data/, exactly as a save
  // naming it would find on load.
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
  AddStackable(*proto->second, count);
  // Part of a row leaves the rest of it on the shelf, in its own place: the
  // shelf is a history, and taking some of a sale back does not make it a
  // newer one.
  int left = entry.stack().count() - count;
  if (left > 0) {
    character_.mutable_buy_backs(index)->mutable_stack()->set_count(left);
  } else {
    character_.mutable_buy_backs()->DeleteSubrange(index, 1);
  }
  return true;
}

bool CharacterInstance::UseStackable(ItemCategory category, int index) {
  std::vector<StackableItem>& stacks = StacksFor(category);
  if (index < 0 || index >= static_cast<int>(stacks.size())) {
    return false;
  }
  StackableItem& stack = stacks[index];
  // Read before the stack is touched: applying the effect may look at the
  // character, and erasing the stack would invalidate the reference.
  ItemEffect effect = stack.prototype().effect();
  if (effect == ITEM_EFFECT_UNSPECIFIED) {
    return false;
  }
  if (effect == ITEM_EFFECT_LEVEL_UP) {
    LevelUp();
  }
  stack.add_count(-1);
  if (stack.count() <= 0) {
    stacks.erase(stacks.begin() + index);
  }
  return true;
}

bool CharacterInstance::Buy(const EquipPrototype& proto, int count) {
  // Presence, not size: the shop stocks one item for nothing, and a price of
  // zero is what it charges rather than a refusal to sell.
  if (count <= 0 || !proto.has_shop_price()) {
    return false;
  }
  // Priced in one go rather than a copy at a time, so a purchase the character
  // cannot finish never takes the meso for the part it could.
  int64_t cost = static_cast<int64_t>(count) * proto.shop_price();
  if (cost > character_.meso()) {
    return false;
  }
  // Room is checked up front for the same reason the price is: a purchase the
  // bag cannot hold must not take the meso for the part of it that would fit.
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
  // A mark is what makes an item a currency, so an item without one buys
  // nothing however many of it the caller passes.
  if (count <= 0 || proto.token_price() <= 0 || token.currency_mark().empty()) {
    return false;
  }
  // Room first, then the whole price in one go: ConsumeStackable is all or
  // nothing, so a purchase the character cannot finish never spends the tokens
  // for the part of it they could.
  if (count > RoomFor(proto)) {
    return false;
  }
  if (!ConsumeStackable(ITEM_CATEGORY_ETC, token.name(),
                        count * proto.token_price())) {
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
  AddStackable(proto, count);
  return true;
}

std::vector<const EquipTrace*> CharacterInstance::traces() const {
  return inventory_.traces();
}

EquipSlot CharacterInstance::SlotToFill(const EquipPrototype& proto) const {
  if (proto.equip_slot() == EQUIP_SLOT_UNSPECIFIED) {
    return EQUIP_SLOT_UNSPECIFIED;
  }
  std::vector<EquipSlot> family = SlotFamily(proto.equip_slot());
  if (family.size() == 1) {
    return family.front();
  }
  for (EquipSlot slot : family) {
    std::map<EquipSlot, EquipInstance>::const_iterator it =
        equipped_.find(slot);
    if (it != equipped_.end() &&
        it->second.prototype().name() == proto.name()) {
      return EQUIP_SLOT_UNSPECIFIED;
    }
  }
  for (EquipSlot slot : family) {
    if (equipped_.count(slot) == 0) {
      return slot;
    }
  }
  // Every one of them is worn. The first goes back to the bag, which is the
  // slot a player who wants a different one gone can empty for themselves.
  return family.front();
}

bool CharacterInstance::Equip(int inventory_index) {
  EquipInstance* raw = inventory_.equip_instance(inventory_index);
  if (raw == nullptr) {
    return false;
  }
  EquipSlot slot = SlotToFill(raw->prototype());
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return false;
  }
  // Remove the item from inventory; move it out before the unique_ptr drops.
  std::unique_ptr<EquipTabItem> ptr = inventory_.remove_equip(inventory_index);
  EquipInstance item = std::move(static_cast<EquipInstance&>(*ptr));

  // If the slot was occupied, put the displaced item in the vacated position.
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it != equipped_.end()) {
    inventory_.add(std::make_unique<EquipInstance>(std::move(it->second)),
                   inventory_index);
    equipped_.erase(it);
  }

  // Equip the item.
  equipped_.emplace(slot, std::move(item));
  RecomputeEquipStats();
  return true;
}

bool CharacterInstance::Unequip(EquipSlot slot) {
  if (slot == EQUIP_SLOT_UNSPECIFIED) {
    return false;
  }
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it == equipped_.end()) {
    return false;
  }
  inventory_.add(std::make_unique<EquipInstance>(std::move(it->second)));
  equipped_.erase(it);
  RecomputeEquipStats();
  return true;
}

ScrollOutcome CharacterInstance::ScrollEquipped(EquipSlot slot,
                                                const Scroll& scroll) {
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it == equipped_.end()) {
    return kScrollFail;
  }
  ScrollOutcome result = it->second.Scroll(scroll, rng_);
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

// Takes the price of one attempt, or leaves the purse alone and says no. GMS
// charges for the roll, not for the star, so a failure and a destroy cost the
// same as a success -- this is why the top of the ladder is expensive.
bool CharacterInstance::PayForStarForce(const EquipInstance& item) {
  int64_t cost = StarForceCost(item.prototype().required_level(), item.stars());
  if (cost > character_.meso()) {
    return false;
  }
  character_.set_meso(character_.meso() - cost);
  return true;
}

StarForceOutcome CharacterInstance::StarForceEquipped(EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it == equipped_.end()) {
    return kStarForceFail;
  }
  if (!PayForStarForce(it->second)) {
    return kStarForceNoMeso;
  }
  StarForceOutcome outcome = it->second.StarForce(rng_);
  if (outcome == kStarForceDestroy) {
    // equip_state() captures the item's state before the destroy attempt
    // (stars at the doomed level, not stars+1).
    inventory_.add(std::make_unique<EquipTrace>(it->second.prototype(),
                                                it->second.equip_state()));
    equipped_.erase(it);
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

// Takes a hammer's price, or leaves the purse alone and says no. Asked of the
// item as well: a hammer that will not go in is not charged for.
bool CharacterInstance::PayForHammer(const EquipInstance& item) {
  if (!item.CanHammer() || kGoldenHammerCost > character_.meso()) {
    return false;
  }
  character_.set_meso(character_.meso() - kGoldenHammerCost);
  return true;
}

bool CharacterInstance::HammerEquipped(EquipSlot slot) {
  std::map<EquipSlot, EquipInstance>::iterator it = equipped_.find(slot);
  if (it == equipped_.end() || !PayForHammer(it->second) ||
      !it->second.Hammer()) {
    return false;
  }
  // Nothing a hammer opens is worn yet, but the worn totals are rebuilt after
  // every change to a worn item, and one exception is how they drift.
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
  // A secondary asks for one branch of one job category, so it is asked
  // first: the category below would let every warrior hold every warrior
  // off-hand, and the three of them are not interchangeable.
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
  // Rebuilt from scratch rather than kept in step as items move: these fields
  // are written here and nowhere else, so there is one place for them to be
  // wrong rather than a dozen.
  saved.clear_inventory();
  saved.clear_equipped();
  saved.clear_stacks();
  for (int i = 0; i < inventory_.size(); ++i) {
    *saved.mutable_inventory()->add_equip_tab() = inventory_[i].SavedState();
  }
  for (const std::pair<const EquipSlot, EquipInstance>& worn : equipped_) {
    (*saved.mutable_equipped())[static_cast<int>(worn.first)] =
        worn.second.equip_state();
  }
  AppendStacks(use_items_, ITEM_CATEGORY_USE, &saved);
  AppendStacks(etc_items_, ITEM_CATEGORY_ETC, &saved);
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
  // A save written before characters had names.
  EnsureUsername();
  // The item fields are the live containers' business from here; leaving
  // copies behind would let the two drift and ToProto pick the stale one.
  character_.clear_inventory();
  character_.clear_equipped();
  character_.clear_stacks();

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

  equipped_.clear();
  for (const std::pair<const int, ms::Equip>& worn : saved.equipped()) {
    std::map<std::string, const EquipPrototype*>::const_iterator proto =
        equips_by_name.find(worn.second.equip_name());
    if (proto == equips_by_name.end()) {
      continue;
    }
    equipped_.emplace(static_cast<EquipSlot>(worn.first),
                      EquipInstance(*proto->second, worn.second));
  }

  use_items_.clear();
  etc_items_.clear();
  for (const StackableStack& stack : saved.stacks()) {
    std::map<std::string, const ItemPrototype*>::const_iterator proto =
        items_by_name.find(stack.name());
    if (proto == items_by_name.end()) {
      continue;
    }
    StacksFor(stack.category())
        .push_back(StackableItem(*proto->second, stack.count()));
  }

  RecomputeEquipStats();
}

}  // namespace ms
