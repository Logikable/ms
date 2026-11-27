#include "src/character/max_character.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "src/character/account/familiar.h"
#include "src/character/account/legion_plan.h"
#include "src/character/account/noblesse_plan.h"
#include "src/character/character.h"
#include "src/character/character_stats.h"
#include "src/character/inner_ability.h"
#include "src/character/job_branch.h"
#include "src/character/matrix/hexa_matrix.h"
#include "src/character/matrix/hexa_stat.h"
#include "src/character/matrix/hyper_plan.h"
#include "src/character/matrix/hyper_stats.h"
#include "src/character/set_pieces.h"
#include "src/character/stat_preset.h"
#include "src/combat/damage.h"
#include "src/item/item.h"
#include "src/item/upgrade/flame.h"
#include "src/item/upgrade/potential.h"
#include "src/item/upgrade/soul.h"
#include "src/protos/boss.pb.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

namespace {

// The stars //analysis/sims:progression_sim's sweep wore on arriving at each
// level: the fifth of ten branches from the top over every starred piece (the
// median piece of each branch). 200 on re-read from the uncapped sweep of
// 2026-11-21, three seeds of 365 days; 140 and 170 from 2026-10-04. Read on
// arrival everywhere, 260 included, now that no climb stops there. 260 re-read
// once star runs risked a boom with no spare (ca741071): Arcane armour is
// mid-climb on arrival, 10-17, and 17-18 by 263.
struct GearBand {
  int level;
  MaxGear gear;
};

constexpr GearBand kBands[] = {
    {0, {5, 5, 0}},       {140, {8, 14, 0}},    {170, {10, 14, 0}},
    {200, {10, 15, 200}}, {230, {11, 13, 230}}, {260, {17, 17, 260}},
    {270, {19, 19, 270}}, {280, {22, 23, 280}},
};

// One piece of an outfit: a catalog key every job wears, or the job's own
// piece of a set family in `slot`.
struct Pick {
  const char* item;
  SetFamily family;
  EquipSlot slot;
};

constexpr Pick Item(const char* key) {
  return {key, SetFamily::kRootAbyss, EQUIP_SLOT_UNSPECIFIED};
}
constexpr Pick Set(SetFamily family, EquipSlot slot) {
  return {nullptr, family, slot};
}

constexpr int kMaxPicks = 32;
struct OutfitBand {
  int level;
  Pick picks[kMaxPicks];
};

constexpr SetFamily kRA = SetFamily::kRootAbyss;
constexpr SetFamily kAL = SetFamily::kAbsoLab;
constexpr SetFamily kAU = SetFamily::kArcaneUmbra;
constexpr SetFamily kET = SetFamily::kEternal;

// What the sweep wore in each slot on arriving at each level: the fifth of ten
// branches from the top, by set family so each job wears its own piece. The
// job's own weapon, secondary and ammunition come first, from the workbench's
// table. 100-170 from 2026-10-04; 200 on from the uncapped sweep of 2026-11-21.
constexpr OutfitBand kOutfits[] = {
    {100, {Item("frozen_hat"), Item("frozen_top"), Item("frozen_bottom")}},
    {140,
     {Item("frozen_hat"), Item("frozen_top"), Item("frozen_bottom"),
      Item("frozen_cape"), Item("gold_maple_leaf_emblem"),
      Item("master_adventurer"), Item("lightning_god_ring"),
      Item("condensed_power_crystal")}},
    {170,
     {Item("frozen_hat"), Item("frozen_top"), Item("frozen_bottom"),
      Item("frozen_cape"), Item("frozen_gloves"), Item("frozen_boots"),
      Item("gold_maple_leaf_emblem"), Item("master_adventurer"),
      Item("lightning_god_ring"), Item("meister_ring"),
      Item("silver_blossom_ring"), Item("condensed_power_crystal"),
      Item("aquatic_letter_eye_accessory"), Item("dea_sidus_earring"),
      Item("horntail_necklace"), Item("stone_of_eternal_life"),
      Item("royal_black_metal_shoulder"), Item("crystal_ventus_badge")}},
    {200,
     {Item("frozen_hat"),
      Item("frozen_top"),
      Item("frozen_bottom"),
      Item("frozen_cape"),
      Item("frozen_gloves"),
      Item("frozen_boots"),
      Item("gold_maple_leaf_emblem"),
      Item("master_adventurer"),
      Item("lightning_god_ring"),
      Item("meister_ring"),
      Item("silver_blossom_ring"),
      Item("condensed_power_crystal"),
      Item("black_bean_mark"),
      Item("dea_sidus_earring"),
      Item("horntail_necklace"),
      Item("dominator_pendant"),
      Item("pink_holy_cup"),
      Item("royal_black_metal_shoulder"),
      Item("crystal_ventus_badge"),
      Item("golden_clover_belt"),
      Item("bronze_incense_burner_totem")}},
    {230,
     {Set(kAL, EQUIP_SLOT_HAT),
      Set(kAL, EQUIP_SLOT_TOP),
      Set(kAL, EQUIP_SLOT_BOTTOM),
      Set(kAL, EQUIP_SLOT_CAPE),
      Set(kAL, EQUIP_SLOT_GLOVES),
      Set(kAL, EQUIP_SLOT_SHOES),
      Set(kAL, EQUIP_SLOT_SHOULDER),
      Set(kAL, EQUIP_SLOT_PRIMARY_WEAPON),
      Set(SetFamily::kPrincessNo, EQUIP_SLOT_SECONDARY),
      Item("gold_maple_leaf_emblem"),
      Item("master_adventurer"),
      Item("guardian_angel_ring"),
      Item("meister_ring"),
      Item("silver_blossom_ring"),
      Item("kannas_treasure"),
      Item("condensed_power_crystal"),
      Item("papulatus_mark"),
      Item("dea_sidus_earring"),
      Item("horntail_necklace"),
      Item("dominator_pendant"),
      Item("pink_holy_cup"),
      Item("crystal_ventus_badge"),
      Item("golden_clover_belt"),
      Item("bronze_incense_burner_totem"),
      Item("horseback_riding_doll_totem"),
      Item("jade_kettle_totem")}},
    {260,
     {Set(kAU, EQUIP_SLOT_HAT),
      Set(kAU, EQUIP_SLOT_TOP),
      Set(kAU, EQUIP_SLOT_BOTTOM),
      Set(kAU, EQUIP_SLOT_CAPE),
      Set(kAU, EQUIP_SLOT_GLOVES),
      Set(kAU, EQUIP_SLOT_SHOES),
      Set(kAU, EQUIP_SLOT_SHOULDER),
      Set(kAU, EQUIP_SLOT_PRIMARY_WEAPON),
      Set(SetFamily::kPrincessNo, EQUIP_SLOT_SECONDARY),
      Item("gold_maple_leaf_emblem"),
      Item("master_adventurer"),
      Item("endless_terror"),
      Item("meister_ring"),
      Item("silver_blossom_ring"),
      Item("kannas_treasure"),
      Item("berserked"),
      Item("magic_eyepatch"),
      Item("estella_earrings"),
      Item("daybreak_pendant"),
      Item("dominator_pendant"),
      Item("pink_holy_cup"),
      Item("black_heart"),
      Item("crystal_ventus_badge"),
      Item("golden_clover_belt"),
      Item("bronze_incense_burner_totem"),
      Item("horseback_riding_doll_totem"),
      Item("jade_kettle_totem")}},
    // Mitra's Rage, the Pitched Boss pieces and a Ring of Restraint by 270 in
    // the uncapped sweep of 2026-11-21; Arcane Umbra still holds the armour.
    {270,
     {Set(kAU, EQUIP_SLOT_HAT),
      Set(kAU, EQUIP_SLOT_TOP),
      Set(kAU, EQUIP_SLOT_BOTTOM),
      Set(kAU, EQUIP_SLOT_CAPE),
      Set(kAU, EQUIP_SLOT_GLOVES),
      Set(kAU, EQUIP_SLOT_SHOES),
      Set(kAU, EQUIP_SLOT_SHOULDER),
      Set(kAU, EQUIP_SLOT_PRIMARY_WEAPON),
      Set(SetFamily::kPrincessNo, EQUIP_SLOT_SECONDARY),
      Set(SetFamily::kMitrasRage, EQUIP_SLOT_EMBLEM),
      Item("master_adventurer"),
      Item("endless_terror"),
      Item("meister_ring"),
      Item("ring_of_restraint_4"),
      Item("kannas_treasure"),
      Item("berserked"),
      Item("magic_eyepatch"),
      Item("commanding_force_earring"),
      Item("source_of_suffering"),
      Item("dominator_pendant"),
      Item("pink_holy_cup"),
      Item("black_heart"),
      Item("crystal_ventus_badge"),
      Item("dreamy_belt"),
      Item("bronze_incense_burner_totem"),
      Item("horseback_riding_doll_totem"),
      Item("jade_kettle_totem")}},
    // The four Eternal pieces Kalos's Determination buys, by 280.
    {280,
     {Set(kET, EQUIP_SLOT_HAT),
      Set(kET, EQUIP_SLOT_TOP),
      Set(kET, EQUIP_SLOT_BOTTOM),
      Set(kAU, EQUIP_SLOT_CAPE),
      Set(kAU, EQUIP_SLOT_GLOVES),
      Set(kAU, EQUIP_SLOT_SHOES),
      Set(kET, EQUIP_SLOT_SHOULDER),
      Set(kAU, EQUIP_SLOT_PRIMARY_WEAPON),
      Set(SetFamily::kPrincessNo, EQUIP_SLOT_SECONDARY),
      Set(SetFamily::kMitrasRage, EQUIP_SLOT_EMBLEM),
      Item("master_adventurer"),
      Item("endless_terror"),
      Item("meister_ring"),
      Item("ring_of_restraint_4"),
      Item("kannas_treasure"),
      Item("berserked"),
      Item("magic_eyepatch"),
      Item("commanding_force_earring"),
      Item("source_of_suffering"),
      Item("dominator_pendant"),
      Item("pink_holy_cup"),
      Item("black_heart"),
      Item("crystal_ventus_badge"),
      Item("dreamy_belt"),
      Item("bronze_incense_burner_totem"),
      Item("horseback_riding_doll_totem"),
      Item("jade_kettle_totem")}},
};

// What the farming preset wore in place of the bossing preset's piece, re-read
// off the sweep of 2026-11-21 (the same from 260 to the run's end): a slot
// where at least five of ten branches farmed in a piece of their own, and the
// lines most of them held. The bossing preset keeps the piece kOutfits names
// there, with no farm lines at all.
struct FarmBand {
  int level;
  MaxFarmPiece pieces[6];
};

constexpr MaxFarmPiece kFarmEye = {EQUIP_SLOT_EYE_ACCESSORY,
                                   "aquatic_letter_eye_accessory", true, true};

constexpr FarmBand kFarmOutfits[] = {
    {230, {kFarmEye, {EQUIP_SLOT_RING, "lightning_god_ring", true, false}}},
    {260,
     {kFarmEye,
      {EQUIP_SLOT_FACE_ACCESSORY, "condensed_power_crystal", true, true},
      {EQUIP_SLOT_EARRINGS, "dea_sidus_earring", false, true},
      {EQUIP_SLOT_RING, "lightning_god_ring", true, false},
      {EQUIP_SLOT_RING_2, "meister_ring", true, true}}},
};

// Each symbol's level on arrival, read off the same sweep as kBands: the six
// Arcane Symbols in EQUIP_SLOT_SYMBOL_* order, then the Sacred ones from
// Cernium to Carcion, Tallahart and Geardock. At 200 it is the level reward
// alone; Sacred Symbols trail, since a daily pays only the share of an area
// the character farms fast enough (//src:sacred_dailies).
struct SymbolBand {
  int level;
  int arcane[6];
  int sacred[8];
};

constexpr SymbolBand kSymbolBands[] = {
    {200, {1, 0, 0, 0, 0, 0}, {}},
    {230, {14, 13, 11, 9, 0, 0}, {}},
    {260, {20, 20, 20, 20, 20, 19}, {3, 2, 0, 0, 0, 0, 0, 0}},
    {270, {20, 20, 20, 20, 20, 20}, {7, 6, 6, 5, 0, 0, 0, 0}},
    {280, {20, 20, 20, 20, 20, 20}, {9, 7, 8, 8, 8, 7, 0, 0}},
};

// The Sacred Symbol slots in kSymbolBands' order.
constexpr EquipSlot kSacredSlots[] = {
    EQUIP_SLOT_SYMBOL_CERNIUM,   EQUIP_SLOT_SYMBOL_ARCUS,
    EQUIP_SLOT_SYMBOL_ODIUM,     EQUIP_SLOT_SYMBOL_SHANGRI_LA,
    EQUIP_SLOT_SYMBOL_ARTERIA,   EQUIP_SLOT_SYMBOL_CARCION,
    EQUIP_SLOT_SYMBOL_TALLAHART, EQUIP_SLOT_SYMBOL_GEARDOCK,
};

// The level each fight's first solo clear came at, the fifth of ten branches
// from the top in the uncapped sweep of 2026-11-27, once buff windows kept
// the force factor (three seeds of 365 days). The Liberation isn't in the
// sweep's table; its 264 is older. Normal Kaling fell to one branch, so it
// has no entry and max mode never clears it.
struct Clear {
  const char* boss;
  const char* difficulty;
  int level;
};

constexpr Clear kClears[] = {
    {"zakum", "Normal", 110},
    {"hilla", "Normal", 130},
    {"horntail", "Normal", 150},
    {"magnus", "Normal", 160},
    {"pink_bean", "Normal", 170},
    {"arkarium", "Normal", 180},
    {"hilla", "Hard", 190},
    {"horntail", "Chaos", 190},
    {"cygnus", "Normal", 201},
    {"pierre", "Chaos", 201},
    {"von_bon", "Chaos", 201},
    {"crimson_queen", "Chaos", 204},
    {"magnus", "Hard", 203},
    {"zakum", "Chaos", 203},
    {"pink_bean", "Chaos", 205},
    {"vellum", "Chaos", 205},
    {"papulatus", "Chaos", 209},
    {"princess_no", "Normal", 209},
    {"damien", "Normal", 220},
    {"lotus", "Normal", 221},
    {"guardian_angel_slime", "Normal", 227},
    {"lucid", "Normal", 236},
    {"will", "Normal", 241},
    {"damien", "Hard", 243},
    {"lotus", "Hard", 244},
    {"gloom", "Normal", 244},
    {"darknell", "Normal", 245},
    {"verus_hilla", "Normal", 250},
    {"will", "Hard", 251},
    {"guardian_angel_slime", "Chaos", 253},
    {"verus_hilla", "Hard", 259},
    {"gloom", "Chaos", 259},
    {"lucid", "Hard", 261},
    {"darknell", "Hard", 260},
    {"liberation", "Genesis", 260},
    {"black_mage", "Hard", 265},
    {"seren", "Normal", 264},
    {"kalos", "Easy", 265},
    {"seren", "Hard", 267},
    {"first_adversary", "Easy", 270},
    {"kaling", "Easy", 275},
    {"kalos", "Normal", 273},
    {"first_adversary", "Normal", 279},
    {"lotus", "Extreme", 281},
};

// The alts the same sweep had levelled by each level: how many past 70, and
// each line's commonest level, the lines most branches levelled first. A line
// the main is on is skipped and the next one taken.
struct AltBand {
  int level;
  int count;
  MaxAlt lines[9];
};

constexpr AltBand kAltBands[] = {
    {230,
     6,
     {{JOB_ICE_LIGHTNING_WIZARD, 120},
      {JOB_FIRE_POISON_WIZARD, 70},
      {JOB_CROSSBOWMAN, 120},
      {JOB_BANDIT, 120},
      {JOB_HUNTER, 120},
      {JOB_ASSASSIN, 70},
      {JOB_CLERIC, 70}}},
    {260,
     6,
     {{JOB_CLERIC, 210},
      {JOB_FIRE_POISON_WIZARD, 120},
      {JOB_ICE_LIGHTNING_WIZARD, 210},
      {JOB_CROSSBOWMAN, 120},
      {JOB_BANDIT, 70},
      {JOB_HUNTER, 120},
      {JOB_ASSASSIN, 120}}},
    {270,
     8,
     {{JOB_CLERIC, 210},
      {JOB_ICE_LIGHTNING_WIZARD, 210},
      {JOB_FIRE_POISON_WIZARD, 120},
      {JOB_CROSSBOWMAN, 210},
      {JOB_HUNTER, 120},
      {JOB_BANDIT, 120},
      {JOB_ASSASSIN, 120},
      {JOB_FIGHTER, 70},
      {JOB_PAGE, 70}}},
    {280,
     8,
     {{JOB_CLERIC, 210},
      {JOB_ICE_LIGHTNING_WIZARD, 210},
      {JOB_FIRE_POISON_WIZARD, 210},
      {JOB_BANDIT, 210},
      {JOB_CROSSBOWMAN, 210},
      {JOB_HUNTER, 120},
      {JOB_ASSASSIN, 210},
      {JOB_FIGHTER, 70},
      {JOB_PAGE, 70}}},
};

// The familiars the same sweep had on reaching each level, read at the fifth
// of ten branches from the top by total familiar levels: the three summoned
// mains' level and lines, and every familiar's levels added up. The second
// line is a rank below, as nine rolls in ten deal it. At 200 the mains are
// Unique, levelled for Familiar Bond and never cubed; an unspecified line is
// the job's own %stat.
struct FamiliarBandLine {
  FamiliarLineType type;
  PotentialRank rank;
};

struct FamiliarBand {
  int level;
  int main_level;
  int total_levels;
  FamiliarBandLine lines[kFamiliarLines];
};

constexpr FamiliarBand kFamiliarBands[] = {
    {200,
     3,
     26,
     {{FAMILIAR_LINE_TYPE_UNSPECIFIED, POTENTIAL_RANK_UNIQUE},
      {FAMILIAR_LINE_TYPE_ATTACK, POTENTIAL_RANK_EPIC}}},
    {230,
     4,
     40,
     {{FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_40, POTENTIAL_RANK_LEGENDARY},
      {FAMILIAR_LINE_TYPE_BOSS_DAMAGE_30, POTENTIAL_RANK_UNIQUE}}},
    {260,
     4,
     40,
     {{FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40, POTENTIAL_RANK_LEGENDARY},
      {FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_30, POTENTIAL_RANK_UNIQUE}}},
    // Every familiar at the top level by 270 in the sweep of 2026-11-21, and
    // the mains' second line Legendary by 280.
    {270,
     4,
     80,
     {{FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_40, POTENTIAL_RANK_LEGENDARY},
      {FAMILIAR_LINE_TYPE_BOSS_DAMAGE_30, POTENTIAL_RANK_UNIQUE}}},
    {280,
     4,
     80,
     {{FAMILIAR_LINE_TYPE_BOSS_DAMAGE_40, POTENTIAL_RANK_LEGENDARY},
      {FAMILIAR_LINE_TYPE_IGNORE_DEFENSE_40, POTENTIAL_RANK_LEGENDARY}}},
};

FamiliarLineType StatPctLine(StatField primary) {
  switch (primary) {
    case STAT_FIELD_DEX:
      return FAMILIAR_LINE_TYPE_DEX_PCT;
    case STAT_FIELD_INT:
      return FAMILIAR_LINE_TYPE_INT_PCT;
    case STAT_FIELD_LUK:
      return FAMILIAR_LINE_TYPE_LUK_PCT;
    default:
      return FAMILIAR_LINE_TYPE_STR_PCT;
  }
}

// The share of each node kind's levels the same sweep had bought, over every
// node of the kind it reached (re-read 2026-11-21). The sweep
// spends on the nodes it measures best and maxes them; spreading the same
// share over every node of the kind keeps the points without choosing for
// each job.
struct MatrixBand {
  int level;
  double job;
  double boost;
  double archetype;
  double common;
};

constexpr MatrixBand kMatrixBands[] = {
    {200, 0.0, 0.0, 0.0, 0.0},
    {230, 0.8, 0.55, 0.3, 0.06},
    {260, 1.0, 1.0, 1.0, 1.0},
};

// The HEXA Matrix the same way, by node kind, from the sweep of 2026-11-26
// (re-read after buff windows kept the force cut, c3599460).
// Only Bow Master's 6th job is written, so only it climbs past 260 and these
// are its own shares; re-read them over every branch once others are.
struct HexaBand {
  int level;
  double origin;
  double ascent;
  double mastery;
  double enhancement;
  double common;
  double archetype;
};

constexpr HexaBand kHexaBands[] = {
    {270, 0.13, 0.0, 0.71, 0.29, 0.22, 0.23},
    {280, 0.63, 0.27, 1.0, 0.87, 0.93, 0.63},
};

// Bow Master's HEXA Stat nodes on arriving at each band, as the sweep rolled
// and reselected them: primary, then the two additional stats, each with its
// level.
struct HexaStatBand {
  int level;
  int nodes;
  HexaStat stats[kHexaStatNodes][3];
  int levels[kHexaStatNodes][3];
};

constexpr HexaStatBand kHexaStatBands[] = {
    {270,
     2,
     {{HEXA_STAT_ATTACK, HEXA_STAT_MAIN_STAT, HEXA_STAT_BOSS_DAMAGE},
      {HEXA_STAT_CRIT_DAMAGE, HEXA_STAT_ATTACK, HEXA_STAT_MAIN_STAT},
      {}},
     {{8, 6, 6}, {3, 9, 8}, {}}},
    {280,
     3,
     {{HEXA_STAT_ATTACK, HEXA_STAT_MAIN_STAT, HEXA_STAT_BOSS_DAMAGE},
      {HEXA_STAT_CRIT_DAMAGE, HEXA_STAT_ATTACK, HEXA_STAT_MAIN_STAT},
      {HEXA_STAT_IGNORE_DEFENSE, HEXA_STAT_CRIT_DAMAGE, HEXA_STAT_ATTACK}},
     {{8, 6, 6}, {3, 9, 8}, {5, 5, 10}}},
};

// The level from which the sweep's bossing Inner Ability had reached
// Legendary. Farming is rolled once bossing settles: Epic by 230 and
// Legendary by 260 in the sweep of 2026-11-21.
constexpr int kLegendaryAbilityLevel = 170;
constexpr int kEpicFarmingAbilityLevel = 230;
constexpr int kLegendaryFarmingAbilityLevel = 260;

// The %stat line for the stat the character fights with.
PotentialLineType StatShareFor(StatField primary) {
  switch (primary) {
    case STAT_FIELD_DEX:
      return POTENTIAL_LINE_TYPE_DEX_PCT;
    case STAT_FIELD_INT:
      return POTENTIAL_LINE_TYPE_INT_PCT;
    case STAT_FIELD_LUK:
      return POTENTIAL_LINE_TYPE_LUK_PCT;
    default:
      return POTENTIAL_LINE_TYPE_STR_PCT;
  }
}

// The same on a non-weapon's bonus potential, which has its own %stat lines.
PotentialLineType BonusStatShareFor(StatField primary) {
  switch (primary) {
    case STAT_FIELD_DEX:
      return POTENTIAL_LINE_TYPE_BONUS_DEX_PCT;
    case STAT_FIELD_INT:
      return POTENTIAL_LINE_TYPE_BONUS_INT_PCT;
    case STAT_FIELD_LUK:
      return POTENTIAL_LINE_TYPE_BONUS_LUK_PCT;
    default:
      return POTENTIAL_LINE_TYPE_BONUS_STR_PCT;
  }
}

// The attack line, which is what a weapon slot is cubed for.
PotentialLineType AttackShareFor(StatField primary) {
  return primary == STAT_FIELD_INT ? POTENTIAL_LINE_TYPE_MAGIC_ATTACK_PCT
                                   : POTENTIAL_LINE_TYPE_ATTACK_PCT;
}

void AddLine(Potential& potential, PotentialLineType type, PotentialRank rank) {
  PotentialLine& line = *potential.add_lines();
  line.set_type(type);
  line.set_rank(rank);
}

// What a line does for the character, turned into a line type by LineTypeFor.
enum class Share {
  kStat,     // %stat of the stat the character fights with
  kAttack,   // %ATT, or %M.ATT for a magician
  kAllStat,  // %all stats, a smaller line of the same kind
  kMaxHp,    // %Max HP, an armour piece's dead line
  kIed,
  kBoss,
  kCritDamage,
  kDamage,
};

// The kinds of item that wear different lines.
enum class SlotKind {
  kWeapon,
  kSecondary,
  kEmblem,
  kHat,
  kGloves,
  kArmour,
  kAccessory,
};
constexpr int kSlotKinds = 7;

SlotKind KindOf(EquipSlot slot) {
  switch (slot) {
    case EQUIP_SLOT_PRIMARY_WEAPON:
      return SlotKind::kWeapon;
    case EQUIP_SLOT_SECONDARY:
      return SlotKind::kSecondary;
    case EQUIP_SLOT_EMBLEM:
      return SlotKind::kEmblem;
    case EQUIP_SLOT_HAT:
      return SlotKind::kHat;
    case EQUIP_SLOT_GLOVES:
      return SlotKind::kGloves;
    default:
      return PotentialGroupOf(slot) == PotentialGroup::kAccessory
                 ? SlotKind::kAccessory
                 : SlotKind::kArmour;
  }
}

// One kind of item's potential: its rank and its three lines, prime first.
// Only `prime` leading lines are at the rank, as a player keeps what the cube
// offers; more than one is for a line only the rank itself rolls.
struct Recipe {
  PotentialRank rank = POTENTIAL_RANK_UNSPECIFIED;
  Share lines[kPotentialLines] = {};
  int prime = 1;
};

// Every kind's potential on both tracks, in SlotKind order.
struct PotentialBand {
  int level;
  Recipe main[kSlotKinds];
  Recipe bonus[kSlotKinds];
};

constexpr PotentialRank R = POTENTIAL_RANK_RARE;
constexpr PotentialRank E = POTENTIAL_RANK_EPIC;
constexpr PotentialRank U = POTENTIAL_RANK_UNIQUE;
constexpr PotentialRank L = POTENTIAL_RANK_LEGENDARY;
constexpr Share S = Share::kStat;
constexpr Share A = Share::kAttack;
constexpr Share H = Share::kMaxHp;
constexpr Share T = Share::kAllStat;
constexpr Share B = Share::kBoss;
constexpr Share I = Share::kIed;
constexpr Share C = Share::kCritDamage;
constexpr Share D = Share::kDamage;

// What //analysis/sims:progression_sim's sweeps wore on arriving at each level,
// the fifth of ten branches from the top: a max character spent well, not
// luckily. 230 on re-read 2026-11-21 off the uncapped sweep, three seeds of 365
// days: weaponry Legendary by 230, armour by 260, everything by 270. Mitra's
// Rage, the emblem from 270, is never cubed. A piece left blank has no
// potential.
constexpr PotentialBand kPotentialBands[] = {
    {200, {{R, {A, S, S}, 3}, {R, {A, S, S}, 3}, {R, {A, S, S}, 3}}, {}},
    {230,
     {{L, {A, I, B}},
      {L, {A, A, S}},
      {E, {A, A, S}},
      {E, {S, S, H}, 2},
      {U, {T, T, S}, 2},
      {E, {T, S, H}},
      {R, {S, H, H}, 3}},
     {{E, {A, D, D}},
      {R, {S, A, A}, 3},
      {R, {D, D, A}, 3},
      {R, {S, H, H}, 3},
      {R, {S, H, H}, 3},
      {R, {S, S, H}, 3},
      {}}},
    {260,
     {{L, {A, B, B}},
      {L, {B, A, A}},
      {L, {A, B, A}},
      {L, {S, S, T}},
      {L, {C, S, H}},
      {L, {S, T, S}},
      {U, {S, S, H}, 2}},
     {{L, {A, S, A}},
      {L, {A, D, A}},
      {U, {A, A, D}},
      {U, {S, S, H}},
      {E, {S, H, H}},
      {E, {S, T, H}, 2},
      {R, {S, S, H}, 3}}},
    {270,
     {{L, {A, B, A}, 2},
      {L, {B, B, A}},
      {},
      {L, {S, S, S}},
      {L, {C, S, S}, 2},
      {L, {S, T, S}},
      {L, {S, S, T}, 2}},
     {{L, {A, A, B}, 2},
      {L, {A, A, B}},
      {},
      {L, {S, S, T}},
      {U, {S, S, S}},
      {L, {S, S, H}},
      {L, {S, S, S}, 2}}},
    {280,
     {{L, {B, A, B}, 2},
      {L, {B, A, B}, 2},
      {},
      {L, {S, T, S}, 2},
      {L, {C, C, T}, 2},
      {L, {S, S, S}, 2},
      {L, {S, S, S}, 2}},
     {{L, {A, A, A}, 2},
      {L, {A, A, A}},
      {},
      {L, {S, T, T}},
      {L, {S, S, T}},
      {L, {S, S, T}},
      {L, {S, S, T}}}},
};

// The band `level` names, or null for none.
const PotentialBand* BandFor(int level) {
  for (const PotentialBand& band : kPotentialBands) {
    if (band.level == level) {
      return &band;
    }
  }
  return nullptr;
}

// The line type `share` is on `track` at `rank`. Bonus weaponry reuses the
// main %stat and %attack types; the rest of bonus has its own. Ignored defence
// and boss damage come in one size per rank.
PotentialLineType LineTypeFor(Share share, PotentialRank rank,
                              PotentialTrack track, bool weaponry,
                              StatField primary) {
  const bool own_bonus = track == PotentialTrack::kBonus && !weaponry;
  switch (share) {
    case Share::kStat:
      return own_bonus ? BonusStatShareFor(primary) : StatShareFor(primary);
    case Share::kAttack:
      return AttackShareFor(primary);
    case Share::kAllStat:
      return own_bonus ? POTENTIAL_LINE_TYPE_BONUS_ALL_STATS_PCT
                       : POTENTIAL_LINE_TYPE_ALL_STATS_PCT;
    case Share::kMaxHp:
      return track == PotentialTrack::kBonus
                 ? POTENTIAL_LINE_TYPE_BONUS_MAX_HP_PCT
                 : POTENTIAL_LINE_TYPE_MAX_HP_PCT;
    case Share::kIed:
      return rank == L   ? POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_40
             : rank == U ? POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_30
                         : POTENTIAL_LINE_TYPE_IGNORE_DEFENSE_15;
    case Share::kBoss:
      if (track == PotentialTrack::kBonus) {
        return POTENTIAL_LINE_TYPE_BONUS_BOSS_DAMAGE;
      }
      return rank == L ? POTENTIAL_LINE_TYPE_BOSS_DAMAGE_40
                       : POTENTIAL_LINE_TYPE_BOSS_DAMAGE_30;
    case Share::kCritDamage:
      return POTENTIAL_LINE_TYPE_CRIT_DAMAGE_PCT;
    case Share::kDamage:
      return POTENTIAL_LINE_TYPE_DAMAGE_PCT;
  }
  return POTENTIAL_LINE_TYPE_UNSPECIFIED;
}

// What a flame line does for the character, turned into a FlameStat by
// FlameStatFor.
enum class FlameShare {
  kPrimary,
  kPrimaryPair,  // the primary stat with the secondary
  kOtherPair,    // the primary stat with one of the other two
  kAttack,       // ATT, or MATT for a magician
  kAllStat,
  kBoss,
  kDamage,
};

struct FlameRecipeLine {
  FlameShare share;
  int tier;
};

// What //analysis/sims:progression_sim's sweep wore on arriving at each level,
// at the upper quartile of each kind's tier total, re-read 2026-11-21: the
// weapon 21, 25, 26, 26 and 26 from 200 to 280, armour 20, 21, 24, 25 and 25,
// accessories 20, 20, 23, 25 and 26. The climb flames from 200, and Black
// flames take some lines to tier 7.
struct FlameBand {
  int level;
  FlameRecipeLine weapon[kFlameLines];
  FlameRecipeLine armour[kFlameLines];
  FlameRecipeLine accessory[kFlameLines];
};

constexpr FlameShare P = FlameShare::kPrimary;
constexpr FlameShare PP = FlameShare::kPrimaryPair;
constexpr FlameShare OP = FlameShare::kOtherPair;
constexpr FlameShare FA = FlameShare::kAttack;
constexpr FlameShare FT = FlameShare::kAllStat;
constexpr FlameShare FB = FlameShare::kBoss;
constexpr FlameShare FD = FlameShare::kDamage;

constexpr FlameBand kFlameBands[] = {
    {200,
     {{FA, 6}, {FB, 5}, {FD, 5}, {P, 5}},
     {{P, 5}, {FA, 5}, {FT, 5}, {OP, 5}},
     {{P, 5}, {PP, 5}, {FT, 5}, {OP, 5}}},
    {230,
     {{FA, 7}, {FB, 6}, {FD, 6}, {P, 6}},
     {{P, 6}, {FA, 5}, {FT, 5}, {OP, 5}},
     {{P, 5}, {PP, 5}, {FT, 5}, {OP, 5}}},
    {260,
     {{FA, 7}, {FB, 7}, {FD, 6}, {P, 6}},
     {{P, 6}, {FA, 6}, {FT, 6}, {OP, 6}},
     {{P, 6}, {PP, 6}, {FT, 6}, {OP, 5}}},
    {270,
     {{FA, 7}, {FB, 7}, {FD, 6}, {P, 6}},
     {{P, 7}, {FA, 6}, {FT, 6}, {OP, 6}},
     {{P, 7}, {PP, 6}, {FT, 6}, {OP, 6}}},
    {280,
     {{FA, 7}, {FB, 7}, {FD, 6}, {P, 6}},
     {{P, 7}, {FA, 6}, {FT, 6}, {OP, 6}},
     {{P, 7}, {PP, 7}, {FT, 6}, {OP, 6}}},
};

FlameStat SingleStat(StatField stat) {
  switch (stat) {
    case STAT_FIELD_DEX:
      return FLAME_STAT_DEX;
    case STAT_FIELD_INT:
      return FLAME_STAT_INT;
    case STAT_FIELD_LUK:
      return FLAME_STAT_LUK;
    default:
      return FLAME_STAT_STR;
  }
}

// The pair line holding both stats, in either order.
FlameStat PairOf(StatField a, StatField b) {
  auto has = [&](StatField x, StatField y) {
    return (a == x && b == y) || (a == y && b == x);
  };
  if (has(STAT_FIELD_STR, STAT_FIELD_DEX)) {
    return FLAME_STAT_STR_DEX;
  }
  if (has(STAT_FIELD_STR, STAT_FIELD_INT)) {
    return FLAME_STAT_STR_INT;
  }
  if (has(STAT_FIELD_STR, STAT_FIELD_LUK)) {
    return FLAME_STAT_STR_LUK;
  }
  if (has(STAT_FIELD_DEX, STAT_FIELD_INT)) {
    return FLAME_STAT_DEX_INT;
  }
  if (has(STAT_FIELD_DEX, STAT_FIELD_LUK)) {
    return FLAME_STAT_DEX_LUK;
  }
  return FLAME_STAT_INT_LUK;
}

// The primary stat's pair with the first stat that is neither it nor the
// secondary, in STR DEX INT LUK order.
FlameStat OtherPairOf(StatField primary, StatField secondary) {
  for (StatField other :
       {STAT_FIELD_STR, STAT_FIELD_DEX, STAT_FIELD_INT, STAT_FIELD_LUK}) {
    if (other != primary && other != secondary) {
      return PairOf(primary, other);
    }
  }
  return PairOf(primary, secondary);
}

FlameStat FlameStatFor(FlameShare share, StatField primary,
                       StatField secondary) {
  switch (share) {
    case FlameShare::kPrimary:
      return SingleStat(primary);
    case FlameShare::kPrimaryPair:
      return PairOf(primary, secondary);
    case FlameShare::kOtherPair:
      return OtherPairOf(primary, secondary);
    case FlameShare::kAttack:
      return primary == STAT_FIELD_INT ? FLAME_STAT_MAGIC_ATTACK
                                       : FLAME_STAT_ATTACK;
    case FlameShare::kAllStat:
      return FLAME_STAT_ALL_STAT;
    case FlameShare::kBoss:
      return FLAME_STAT_BOSS_DAMAGE;
    case FlameShare::kDamage:
      return FLAME_STAT_DAMAGE;
  }
  return FLAME_STAT_UNSPECIFIED;
}

// The monster the preset is spent against: for the Boss allocation, the
// toughest boss the character's level allows; for the Farm one, the toughest
// normal monster at or below their level. Both are read from the catalogs, not
// chosen, so a new boss changes this automatically.
//
// Null for empty catalogs, in which case the rating falls back to plain combat
// power.
const Mob* NominalTarget(const std::map<std::string, Boss>& bosses,
                         const std::map<std::string, Mob>& mobs, int level,
                         Activity preset) {
  const Mob* worst = nullptr;
  auto harder = [&worst](const Mob& mob) {
    if (worst == nullptr || mob.pdr() > worst->pdr()) {
      worst = &mob;
    }
  };
  if (preset == Activity::kFarming) {
    for (const std::pair<const std::string, Mob>& entry : mobs) {
      if (!entry.second.boss() && entry.second.level() <= level) {
        harder(entry.second);
      }
    }
    return worst;
  }
  for (const std::pair<const std::string, Boss>& entry : bosses) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (difficulty.coming_soon() || difficulty.unlock_level() > level) {
        continue;
      }
      for (const BossPhase& phase : difficulty.phases()) {
        for (const Spawn& spawn : phase.spawns()) {
          auto found = mobs.find(spawn.mob());
          if (found != mobs.end()) {
            harder(found->second);
          }
        }
      }
    }
  }
  return worst;
}

// What the character is worth to the allocation: one plain swing at their
// target, through the full damage chain. It uses a real target instead of just
// combat power because combat power has no target in it, so a stat that only
// helps against defence (Ignore Defense) would be valued at zero and never
// bought.
//
// A monster that cancels the swing entirely leaves every allocation at the
// 1-damage floor, and the preset then buys nothing. That means the fight is out
// of reach, not that the choices are tied.
double MaxHyperRate(CharacterInstance& character,
                    const std::map<std::string, Skill>& skills, Activity preset,
                    const Mob* target) {
  const OffenseStats offense = CharacterOffense(character, skills, preset);
  if (target == nullptr) {
    return CombatPower(offense, preset == Activity::kBossing);
  }
  return ExpectedAttackDamage(offense, *target);
}

}  // namespace

MaxGear MaxGearForLevel(int level) {
  MaxGear gear = kBands[0].gear;
  for (const GearBand& band : kBands) {
    if (level >= band.level) {
      gear = band.gear;
    }
  }
  return gear;
}

std::vector<std::string> MaxOutfit(Job job, int level) {
  const OutfitBand* band = nullptr;
  for (const OutfitBand& candidate : kOutfits) {
    if (level >= candidate.level) {
      band = &candidate;
    }
  }
  std::vector<std::string> keys;
  if (band == nullptr) {
    return keys;
  }
  for (const Pick& pick : band->picks) {
    std::string key = pick.item != nullptr ? pick.item
                      : pick.slot != EQUIP_SLOT_UNSPECIFIED
                          ? SetPieceFor(pick.family, pick.slot, job)
                          : "";
    if (!key.empty()) {
      keys.push_back(std::move(key));
    }
  }
  return keys;
}

std::vector<MaxFarmPiece> MaxFarmOutfit(int level) {
  std::vector<MaxFarmPiece> pieces;
  for (const FarmBand& band : kFarmOutfits) {
    if (level >= band.level) {
      pieces.clear();
      for (const MaxFarmPiece& piece : band.pieces) {
        if (piece.key != nullptr) {
          pieces.push_back(piece);
        }
      }
    }
  }
  return pieces;
}

Potential MaxFarmPotential(const MaxFarmPiece& piece, StatField primary) {
  // Meso and drop roll only at Legendary, so both lead at the rank.
  Potential potential;
  potential.set_rank(L);
  if (piece.drop) {
    AddLine(potential, POTENTIAL_LINE_TYPE_ITEM_DROP_RATE, L);
  }
  if (piece.meso) {
    AddLine(potential, POTENTIAL_LINE_TYPE_MESO_RATE, L);
  }
  while (potential.lines_size() < kPotentialLines) {
    AddLine(potential, StatShareFor(primary), U);
  }
  return potential;
}

int MaxClearLevel(const std::string& boss, const std::string& difficulty) {
  for (const Clear& clear : kClears) {
    if (boss == clear.boss && difficulty == clear.difficulty) {
      return clear.level;
    }
  }
  return 0;
}

std::vector<MaxAlt> MaxAlts(Job played_line, int level) {
  std::vector<MaxAlt> alts;
  const AltBand* band = nullptr;
  for (const AltBand& candidate : kAltBands) {
    if (level >= candidate.level) {
      band = &candidate;
    }
  }
  if (band == nullptr) {
    return alts;
  }
  for (const MaxAlt& alt : band->lines) {
    if (static_cast<int>(alts.size()) == band->count) {
      break;
    }
    if (alt.line != JOB_UNSPECIFIED && alt.line != played_line) {
      alts.push_back(alt);
    }
  }
  return alts;
}

int MaxMatrixLevel(const Skill& node, int level) {
  double share = 0.0;
  for (const MatrixBand& band : kMatrixBands) {
    if (level < band.level) {
      continue;
    }
    switch (node.v_node()) {
      case V_NODE_KIND_JOB:
        share = band.job;
        break;
      case V_NODE_KIND_BOOST:
        share = band.boost;
        break;
      case V_NODE_KIND_ARCHETYPE:
        share = band.archetype;
        break;
      default:
        share = band.common;
        break;
    }
  }
  return static_cast<int>(std::lround(share * SkillMaxLevel(node)));
}

int MaxHexaMatrixLevel(const Skill& node, int level) {
  double share = 0.0;
  for (const HexaBand& band : kHexaBands) {
    if (level < band.level) {
      continue;
    }
    switch (node.hexa_node()) {
      case HEXA_NODE_KIND_ORIGIN:
        share = band.origin;
        break;
      case HEXA_NODE_KIND_ASCENT:
        share = band.ascent;
        break;
      case HEXA_NODE_KIND_MASTERY:
        share = band.mastery;
        break;
      case HEXA_NODE_KIND_ENHANCEMENT:
        share = band.enhancement;
        break;
      case HEXA_NODE_KIND_COMMON:
        share = band.common;
        break;
      case HEXA_NODE_KIND_ARCHETYPE:
        share = band.archetype;
        break;
      default:
        share = 0.0;
        break;
    }
  }
  return static_cast<int>(
      std::lround(share * MaxHexaNodeLevel(node.hexa_node())));
}

HexaStats MaxHexaStats(int level) {
  HexaStats stats;
  const HexaStatBand* held = nullptr;
  for (const HexaStatBand& band : kHexaStatBands) {
    if (level >= band.level) {
      held = &band;
    }
  }
  if (held == nullptr) {
    return stats;
  }
  for (int i = 0; i < held->nodes; ++i) {
    HexaStatNode& node = *stats.add_nodes();
    node.set_primary(held->stats[i][0]);
    node.set_first(held->stats[i][1]);
    node.set_second(held->stats[i][2]);
    node.set_primary_level(held->levels[i][0]);
    node.set_first_level(held->levels[i][1]);
    node.set_second_level(held->levels[i][2]);
  }
  return stats;
}

int MaxSymbolLevel(EquipSlot slot, int level) {
  const int area = slot - EQUIP_SLOT_SYMBOL_VANISHING_JOURNEY;
  int sacred = -1;
  for (int i = 0; i < 8; ++i) {
    if (kSacredSlots[i] == slot) {
      sacred = i;
    }
  }
  if ((area < 0 || area >= 6) && sacred < 0) {
    return 0;
  }
  int symbol_level = 0;
  for (const SymbolBand& band : kSymbolBands) {
    if (level >= band.level) {
      symbol_level = sacred >= 0 ? band.sacred[sacred] : band.arcane[area];
    }
  }
  return symbol_level;
}

Potential MaxPotentialFor(EquipSlot slot, const MaxGear& gear,
                          StatField primary, PotentialTrack track) {
  Potential potential;
  const PotentialGroup group = PotentialGroupOf(slot);
  const PotentialBand* band = BandFor(gear.potential_level);
  if (group == PotentialGroup::kNone || band == nullptr) {
    return potential;
  }
  const Recipe& recipe = (track == PotentialTrack::kMain
                              ? band->main
                              : band->bonus)[static_cast<int>(KindOf(slot))];
  if (recipe.rank == POTENTIAL_RANK_UNSPECIFIED) {
    return potential;
  }
  potential.set_rank(recipe.rank);
  const bool weaponry = group == PotentialGroup::kWeaponry;
  for (int i = 0; i < kPotentialLines; ++i) {
    const PotentialRank rank =
        i < recipe.prime ? recipe.rank : PreviousPotentialRank(recipe.rank);
    AddLine(potential,
            LineTypeFor(recipe.lines[i], rank, track, weaponry, primary), rank);
  }
  return potential;
}

FlameLines MaxFlameFor(const EquipPrototype& proto, int level,
                       StatField primary, StatField secondary) {
  FlameLines lines;
  const FlameBand* band = nullptr;
  for (const FlameBand& candidate : kFlameBands) {
    if (level >= candidate.level) {
      band = &candidate;
    }
  }
  if (band == nullptr || !SlotTakesFlame(proto.equip_slot()) ||
      !Supports(proto, UPGRADE_FLAME)) {
    return lines;
  }
  const FlameRecipeLine* recipe =
      proto.equip_slot() == EQUIP_SLOT_PRIMARY_WEAPON ? band->weapon
      : PotentialGroupOf(proto.equip_slot()) == PotentialGroup::kAccessory
          ? band->accessory
          : band->armour;
  // A line the item's pool lacks, such as ATT below level 60, is left off
  // rather than replaced.
  const std::vector<FlameStat> pool = FlamePool(proto);
  for (int i = 0; i < kFlameLines; ++i) {
    const FlameStat stat = FlameStatFor(recipe[i].share, primary, secondary);
    if (std::find(pool.begin(), pool.end(), stat) == pool.end()) {
      continue;
    }
    FlameLine& line = *lines.Add();
    line.set_stat(stat);
    line.set_tier(recipe[i].tier);
  }
  return lines;
}

void SpendMaxNoblesse(CharacterInstance& character,
                      const std::map<std::string, Skill>& skills,
                      const std::map<std::string, Boss>& bosses,
                      const std::map<std::string, Mob>& mobs) {
  const Mob* target = NominalTarget(bosses, mobs, character.proto().level(),
                                    Activity::kBossing);
  RefundNoblesseSp(character, skills);
  SpendNoblesseSp(character, skills, [&skills, target](CharacterInstance& c) {
    return MaxHyperRate(c, skills, Activity::kBossing, target);
  });
}

void SpendMaxLegion(CharacterInstance& character,
                    const std::map<std::string, Skill>& skills,
                    const std::map<std::string, Boss>& bosses,
                    const std::map<std::string, Mob>& mobs) {
  const int level = character.proto().level();
  for (Activity activity : {Activity::kFarming, Activity::kBossing}) {
    const Mob* target = NominalTarget(bosses, mobs, level, activity);
    SpendLegion(character, AutoswapSlotFor(activity),
                [&skills, activity, target](CharacterInstance& c) {
                  return MaxHyperRate(c, skills, activity, target);
                });
  }
}

void SpendMaxHyperStats(CharacterInstance& character,
                        const std::map<std::string, Skill>& skills,
                        const std::map<std::string, Boss>& bosses,
                        const std::map<std::string, Mob>& mobs) {
  const int level = character.proto().level();
  for (Activity activity : {Activity::kFarming, Activity::kBossing}) {
    const Mob* target = NominalTarget(bosses, mobs, level, activity);
    const StatPreset slot = AutoswapSlotFor(activity);
    HyperWorth worth = MeasureHyperWorth(
        character, slot, [&skills, activity, target](CharacterInstance& c) {
          return MaxHyperRate(c, skills, activity, target);
        });
    SpendHyperStats(character, slot, worth);
  }
}

void WearMaxSoul(CharacterInstance& character,
                 const std::map<std::string, Skill>& skills,
                 const std::map<std::string, Boss>& bosses,
                 const std::map<std::string, Mob>& mobs,
                 const std::map<std::string, ItemPrototype>& items) {
  const int level = character.proto().level();
  if (level < kSoulUnlockLevel) {
    return;
  }
  // The latest boss of the best tier, so the name is the one a player at this
  // level would be farming.
  const ItemPrototype* best = nullptr;
  int best_unlock = 0;
  for (const std::pair<const std::string, Boss>& entry : bosses) {
    for (const BossDifficulty& difficulty : entry.second.difficulties()) {
      if (difficulty.coming_soon() || difficulty.unlock_level() > level) {
        continue;
      }
      for (const MobDrop& drop : difficulty.drops()) {
        auto found = items.find(drop.item());
        if (found == items.end() ||
            found->second.soul_tier() == SOUL_TIER_UNSPECIFIED) {
          continue;
        }
        const ItemPrototype& shard = found->second;
        if (best == nullptr || shard.soul_tier() > best->soul_tier() ||
            (shard.soul_tier() == best->soul_tier() &&
             difficulty.unlock_level() > best_unlock)) {
          best = &shard;
          best_unlock = difficulty.unlock_level();
        }
      }
    }
  }
  if (best == nullptr) {
    return;
  }
  const Mob* target = NominalTarget(bosses, mobs, level, Activity::kBossing);
  Soul soul;
  soul.set_boss(best->short_name());
  soul.set_tier(best->soul_tier());
  SoulLine best_line = SOUL_LINE_UNSPECIFIED;
  double best_rate = -1.0;
  for (int line = SOUL_LINE_ATTACK; line <= SOUL_LINE_BOSS_DAMAGE; ++line) {
    soul.set_line(static_cast<SoulLine>(line));
    if (!character.TakeSoul(EQUIP_SLOT_PRIMARY_WEAPON, soul)) {
      return;
    }
    const double rate =
        MaxHyperRate(character, skills, Activity::kBossing, target);
    if (rate > best_rate) {
      best_rate = rate;
      best_line = soul.line();
    }
  }
  soul.set_line(best_line);
  character.TakeSoul(EQUIP_SLOT_PRIMARY_WEAPON, soul);
}

AbilityPreset MaxAbilityPreset(Activity preset, StatField primary, int level) {
  AbilityPreset built;
  // Meso on top and drop below it, the lines the sweep's farming rolls chase.
  if (preset == Activity::kFarming && level >= kEpicFarmingAbilityLevel) {
    const AbilityRank top = level >= kLegendaryFarmingAbilityLevel
                                ? ABILITY_RANK_LEGENDARY
                                : ABILITY_RANK_EPIC;
    built.set_rank(top);
    const AbilityLineType types[] = {ABILITY_LINE_TYPE_MESO,
                                     ABILITY_LINE_TYPE_ITEM_DROP,
                                     ABILITY_LINE_TYPE_MESO};
    for (int i = 0; i < kAbilityLines; ++i) {
      AbilityLine& line = *built.add_lines();
      line.set_type(types[i]);
      line.set_rank(i == 0 ? top : ABILITY_RANK_EPIC);
    }
    return built;
  }
  // The rank a character starts at, on three All Stat lines.
  if (preset == Activity::kFarming || level < kLegendaryAbilityLevel) {
    built.set_rank(ABILITY_RANK_RARE);
    for (int i = 0; i < kAbilityLines; ++i) {
      AbilityLine& line = *built.add_lines();
      line.set_type(ABILITY_LINE_TYPE_ALL_STATS);
      line.set_rank(ABILITY_RANK_RARE);
    }
    return built;
  }
  AbilityLineType stat = ABILITY_LINE_TYPE_STR;
  switch (primary) {
    case STAT_FIELD_DEX:
      stat = ABILITY_LINE_TYPE_DEX;
      break;
    case STAT_FIELD_INT:
      stat = ABILITY_LINE_TYPE_INT;
      break;
    case STAT_FIELD_LUK:
      stat = ABILITY_LINE_TYPE_LUK;
      break;
    default:
      break;
  }
  // One Legendary line and two Epic ones, which is what resetting realistically
  // lands on: only the top line has the ability's rank, and the two below roll
  // Epic far more often than Unique. Critical rate on top is what
  // //analysis/plan:ability_plan finds best for bosses.
  //
  // Attack Speed is the line GMS players chase, and it's deliberately left out:
  // the Extreme Green Potion already gives boss fights an extra stage past the
  // cap this line is limited by, so the line would be useless in every boss
  // fight.
  const AbilityLineType attack = primary == STAT_FIELD_INT
                                     ? ABILITY_LINE_TYPE_MAGIC_ATTACK
                                     : ABILITY_LINE_TYPE_ATTACK;
  built.set_rank(ABILITY_RANK_LEGENDARY);
  const AbilityLineType types[] = {ABILITY_LINE_TYPE_CRIT_RATE, attack, stat};
  const AbilityRank ranks[] = {ABILITY_RANK_LEGENDARY, ABILITY_RANK_EPIC,
                               ABILITY_RANK_EPIC};
  for (int i = 0; i < kAbilityLines; ++i) {
    AbilityLine& line = *built.add_lines();
    line.set_type(types[i]);
    line.set_rank(ranks[i]);
  }
  return built;
}

FamiliarBook MaxFamiliars(StatField primary, int level) {
  FamiliarBook book;
  const FamiliarBand* band = nullptr;
  for (const FamiliarBand& candidate : kFamiliarBands) {
    if (candidate.level <= level) {
      band = &candidate;
    }
  }
  if (band == nullptr) {
    return book;
  }
  for (int i = 0; i < kMaxSummonedFamiliars; ++i) {
    Familiar& main = *book.add_familiars();
    main.set_name(FamiliarRoster()[i].name);
    main.set_level(band->main_level);
    for (const FamiliarBandLine& held : band->lines) {
      FamiliarLine& line = *main.add_lines();
      line.set_type(held.type == FAMILIAR_LINE_TYPE_UNSPECIFIED
                        ? StatPctLine(primary)
                        : held.type);
      line.set_rank(held.rank);
    }
  }
  // The rest, one level at a time on whichever is lowest: the cheapest step.
  std::vector<Familiar*> rest;
  for (std::size_t i = kMaxSummonedFamiliars; i < FamiliarRoster().size();
       ++i) {
    Familiar& other = *book.add_familiars();
    other.set_name(FamiliarRoster()[i].name);
    rest.push_back(&other);
  }
  int left = band->total_levels - kMaxSummonedFamiliars * band->main_level;
  for (int step = 0; left > 0; ++step, --left) {
    Familiar& next = *rest[step % rest.size()];
    next.set_level(std::min(kFamiliarMaxLevel, next.level() + 1));
  }
  // A familiar never levelled has no entry, as in a played book.
  FamiliarBook levelled;
  for (const Familiar& familiar : book.familiars()) {
    if (familiar.level() > 0) {
      *levelled.add_familiars() = familiar;
    }
  }
  return levelled;
}

const Mob* MaxBossTarget(const std::map<std::string, Boss>& bosses,
                         const std::map<std::string, Mob>& mobs, int level) {
  return NominalTarget(bosses, mobs, level, Activity::kBossing);
}

double MaxBossSwingRate(CharacterInstance& character,
                        const std::map<std::string, Skill>& skills,
                        const std::map<std::string, Boss>& bosses,
                        const std::map<std::string, Mob>& mobs) {
  return MaxHyperRate(character, skills, Activity::kBossing,
                      MaxBossTarget(bosses, mobs, character.proto().level()));
}

}  // namespace ms
