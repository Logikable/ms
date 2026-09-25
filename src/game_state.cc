#include "src/game_state.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/character/consumables.h"
#include "src/character/exp_table.h"
#include "src/character/honor.h"
#include "src/character/job_branch.h"
#include "src/character/job_name.h"
#include "src/character/link.h"
#include "src/character/max_character.h"
#include "src/character/stat_preset.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
#include "src/item/potential.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

namespace {

// The level-1 Beginner every character starts as.
Character MakeBaseBeginnerProto() {
  Character proto;
  proto.set_name(kDefaultUsername);
  proto.set_level(1);
  proto.set_job(JOB_BEGINNER);
  proto.set_ap(0);
  proto.mutable_allocated_stats()->set_str(kBeginnerStr);
  proto.mutable_allocated_stats()->set_dex(kBaseStat);
  proto.mutable_allocated_stats()->set_int_(kBaseStat);
  proto.mutable_allocated_stats()->set_luk(kBaseStat);
  proto.mutable_allocated_stats()->set_hp(50);
  proto.mutable_allocated_stats()->set_mp(15);
  return proto;
}

// The gear the workbench gives each job: the best of each item it offers that
// the job's starting level can wear. The workbench starts at the top of an
// advancement, so without this a level 60 Fighter would still use the axe they
// got at 30. A Rogue gets all three weapons, and the dagger or claw decides
// what they attack with.
//
// This is long and stays long: one row per job. The static_assert catches a
// missing job, since Clang can't check the switch (-Wswitch over a proto enum
// also demands the two DO_NOT_USE sentinels).
std::vector<std::string> WorkbenchGearFor(Job job) {
  static_assert(Job_ARRAYSIZE == 36, "a new job needs a row in this table");
  switch (job) {
    // The 1st jobs, at level 30.
    case JOB_SWORDMAN:
      return {"gladius"};
    case JOB_ARCHER:
      return {"ryden", "quality_arrow_for_bow"};
    case JOB_MAGICIAN:
      return {"circle_winded_staff"};
    case JOB_ROGUE:
      return {"kumbi_throwing_stars", "reef_claw", "steel_guards"};
    // The 2nd jobs, at level 60, each with its secondary weapon. Secondaries
    // belong to branches, so the 1st jobs above have none. The three magician
    // branches use the same staff but different books.
    case JOB_FIGHTER:
      return {"the_shining", "orders_medallion"};
    case JOB_PAGE:
      return {"the_blessing", "divine_rosary"};
    case JOB_SPEARMAN:
      return {"holy_spear", "dark_chain"};
    case JOB_HUNTER:
      return {"asianic_bow", "gusty_feather", "strong_arrow_for_bow"};
    case JOB_CROSSBOWMAN:
      return {"golden_crow", "sure_shot", "strong_arrow_for_crossbow"};
    case JOB_FIRE_POISON_WIZARD:
      return {"frantic_crow_staff", "rusty_book_antistrophe"};
    case JOB_ICE_LIGHTNING_WIZARD:
      return {"frantic_crow_staff", "metallic_blue_book_antistrophe"};
    case JOB_CLERIC:
      return {"frantic_crow_staff", "white_gold_book_antistrophe"};
    // Level 50 stars: the next tier is out of reach at level 60.
    case JOB_ASSASSIN:
      return {"steely_throwing_knives", "dark_gigantic", "evil_ender_charm"};
    case JOB_BANDIT:
      return {"deadly_fin", "vanishing_shadow"};
    // The 3rd jobs, at level 100: the best gear meso alone can buy, since the
    // Frozen tier below costs tokens. Each takes the better of its line's two
    // weapons on //analysis:bench_sim and keeps its 2nd job secondary, since a
    // 3rd job opens no new slot. The Crusader's axe beats the same-tier sword
    // only because Weapon Mastery gives axes a bonus.
    case JOB_BERSERKER:
      return {"pinaka", "berserk_chain"};
    // The 4th job, at the level cap, so its gear is the Frozen tier bought with
    // tokens instead of the last tier meso can buy.
    case JOB_DARK_KNIGHT:
      return {"frozen_spear", "frozen_chain"};
    case JOB_PALADIN:
      return {"frozen_maul", "frozen_rosary"};
    // The axe over the sword, for the Crusader's reason: both are equal in the
    // damage chain, and Weapon Mastery gives 5% more for an axe.
    case JOB_HERO:
      return {"frozen_two_handed_axe", "frozen_medal"};
    // The Frozen tier has no arrows, so the bow line keeps buying ammunition
    // from the shop.
    case JOB_BOW_MASTER:
      return {"frozen_longbow", "frozen_feather", "titanium_arrow_for_bow"};
    case JOB_MARKSMAN:
      return {"frozen_crossbow", "frozen_true_shot",
              "titanium_arrow_for_crossbow"};
    case JOB_CRUSADER:
      return {"tavar", "virtues_medallion"};
    case JOB_WHITE_KNIGHT:
      return {"golden_smith_hammer", "sacred_rosary"};
    case JOB_RANGER:
      return {"dark_nisrock", "blasted_feather", "titanium_arrow_for_bow"};
    case JOB_SNIPER:
      return {"dark_neschere", "true_shot", "titanium_arrow_for_crossbow"};
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return {"frozen_staff", "frozen_metallic_book"};
    case JOB_FIRE_POISON_ARCH_MAGE:
      return {"frozen_staff", "frozen_rusty_book"};
    case JOB_BISHOP:
      return {"frozen_staff", "frozen_white_gold_book"};
    case JOB_NIGHT_LORD:
      return {"frozen_steer", "balanced_fury", "frozen_death_sender_charm"};
    case JOB_SHADOWER:
      return {"frozen_cutter", "frozen_shadow"};
    case JOB_ICE_LIGHTNING_MAGE:
      return {"crimson_arcanon", "metallic_blue_book_epode"};
    case JOB_FIRE_POISON_MAGE:
      return {"crimson_arcanon", "rusty_book_epode"};
    case JOB_PRIEST:
      return {"crimson_arcanon", "white_gold_book_epode"};
    // The stars go in the slot, not thrown: a claw with no stars equipped deals
    // nothing. See the Assassin above.
    case JOB_HERMIT:
      return {"balanced_fury", "red_craven", "death_sender_charm"};
    case JOB_CHIEF_BANDIT:
      return {"blood_dagger", "slashing_shadow"};
    default:
      return StarterEquipsFor(job);
  }
}

// The best trace of one type for `proto`, at its lowest success rate. The odds
// don't matter here, since every slot succeeds, so the biggest bonus wins. Null
// if no trace of that type exists for the item.
const Scroll* BestScrollOfType(const GameState& state,
                               const EquipPrototype& proto, ScrollTarget target,
                               ScrollType type) {
  std::set<int> item_categories(proto.equip_job_categories().begin(),
                                proto.equip_job_categories().end());
  const Scroll* best = nullptr;
  for (const std::pair<const std::string, Scroll>& entry : state.scrolls) {
    const Scroll& scroll = entry.second;
    if (scroll.scroll_type() != type || scroll.target() != target ||
        scroll.tier() != TierForLevel(proto.required_level())) {
      continue;
    }
    bool fits = false;
    for (int category : scroll.applicable_job_categories()) {
      fits = fits || item_categories.count(category) > 0;
    }
    if (fits &&
        (best == nullptr || scroll.success_rate() < best->success_rate())) {
      best = &scroll;
    }
  }
  return best;
}

// The spell trace the workbench uses on `proto`: the one raising this
// character's main stat, or, where a slot takes no stat trace, the one raising
// attack. Gloves and hearts are the second case; asking only for the stat left
// them unscrolled and so without stars.
const Scroll* BestScrollFor(const GameState& state,
                            const EquipPrototype& proto) {
  StatField primary = PrimaryStatField(state.character.proto().job());
  ScrollType wanted = SCROLL_TYPE_UNSPECIFIED;
  switch (primary) {
    case STAT_FIELD_STR:
      wanted = SCROLL_TYPE_STR;
      break;
    case STAT_FIELD_DEX:
      wanted = SCROLL_TYPE_DEX;
      break;
    case STAT_FIELD_INT:
      wanted = SCROLL_TYPE_INT;
      break;
    case STAT_FIELD_LUK:
      wanted = SCROLL_TYPE_LUK;
      break;
    default:
      return nullptr;
  }
  ScrollTarget target = TargetForSlot(proto.equip_slot());
  if (target == SCROLL_TARGET_UNSPECIFIED) {
    return nullptr;
  }
  const Scroll* best = BestScrollOfType(state, proto, target, wanted);
  if (best != nullptr) {
    return best;
  }
  ScrollType attack =
      primary == STAT_FIELD_INT ? SCROLL_TYPE_MATT : SCROLL_TYPE_ATT;
  return BestScrollOfType(state, proto, target, attack);
}

// The upgrade state of one workbench item, set flag by flag so a tester can
// name an exact configuration. Written directly instead of rolled through
// Scroll() and StarForce(), because the tester wants the finished item, not the
// odds.
Equip UpgradedState(const GameState& state, const EquipPrototype& proto,
                    const GearSetup& equips) {
  Equip built;
  built.set_equip_name(proto.name());
  built.set_remaining_upgrade_slots(proto.upgrade_slots());
  // Hammers first, so the scrolls fill the widened set of slots.
  if (equips.hammered && TakesUpgradeSlots(proto)) {
    built.set_hammers(kMaxHammers);
    built.set_remaining_upgrade_slots(TotalUpgradeSlots(proto, built));
  }
  const Scroll* scroll = BestScrollFor(state, proto);
  if (equips.scrolled && scroll != nullptr && TakesUpgradeSlots(proto)) {
    int slots = built.remaining_upgrade_slots();
    std::vector<EquipStats> passes(slots, scroll->stats());
    *built.mutable_scroll_stats() = SumEquipStats(passes);
    built.set_scroll_successes(slots);
    built.set_remaining_upgrade_slots(0);
  }
  // Stars only go on an item with no slots left to scroll, the same rule as the
  // upgrade screen, so --sf without --scrolled leaves an item with slots
  // unstarred.
  const int wanted =
      proto.equip_slot() == EQUIP_SLOT_PRIMARY_WEAPON && equips.weapon_stars > 0
          ? equips.weapon_stars
          : equips.stars;
  if (wanted > 0 && built.remaining_upgrade_slots() == 0 &&
      Supports(proto, UPGRADE_STAR_FORCE)) {
    built.set_stars(std::min(
        wanted, EquipTabItem::MaxStarsForLevel(proto.required_level())));
  }
  return built;
}

// Puts a copy of the named equip in the bag, or does nothing if the catalog has
// no such entry. Lets a test build a GameState without the game's data files.
void GiveEquip(GameState& state, const std::string& name,
               const GearSetup& equips = GearSetup()) {
  std::map<std::string, EquipPrototype>::const_iterator it =
      state.equips.find(name);
  if (it == state.equips.end()) {
    return;
  }
  state.character.PickUp(std::make_unique<EquipInstance>(
      it->second, UpgradedState(state, it->second, equips)));
}

// Equips each of `names`, with anything it displaces going back to the bag. A
// piece too high-level to wear, or for another branch, is still given and stays
// in the bag: the four Cygnus shoulders compete for one slot across four
// branches.
void WearAll(GameState& state, const std::vector<std::string>& names,
             const GearSetup& equips) {
  for (const std::string& name : names) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(name);
    if (it == state.equips.end()) {
      continue;
    }
    int row = static_cast<int>(state.character.inventory().size());
    GiveEquip(state, name, equips);
    if (static_cast<int>(state.character.inventory().size()) > row &&
        state.character.MeetsLevel(it->second) &&
        state.character.MeetsJob(it->second)) {
      state.character.Equip(row);
    }
  }
}

// Removes the weakest item in `proto`'s slot family when every slot in it is
// full and `proto` is better than one of them. Otherwise a piece arriving after
// the family is full replaces whatever is in the first slot, which reflects
// equip order instead of value.
void MakeRoomFor(GameState& state, const EquipPrototype& proto) {
  EquipSlot weakest = EQUIP_SLOT_UNSPECIFIED;
  int lowest = proto.required_level();
  for (EquipSlot slot : SlotFamily(proto.equip_slot())) {
    const EquipInstance* worn =
        state.character.WornAt(StatPreset::kFirst, slot);
    if (worn == nullptr) {
      return;  // Free space for it, so nothing has to be removed.
    }
    if (worn->prototype().required_level() < lowest) {
      lowest = worn->prototype().required_level();
      weakest = slot;
    }
  }
  if (weakest != EQUIP_SLOT_UNSPECIFIED) {
    state.character.Unequip(weakest);
  }
}

// Every stage's gear, with the job's own last, so a character below the top
// tier's level still gets the tier below. Only slots the job's own gear names
// are filled this way, and only where the level can't wear the job's item, so a
// Bandit doesn't get the Rogue's throwing stars.
void WearThePath(GameState& state, const std::vector<Job>& path,
                 const GearSetup& equips) {
  if (path.empty()) {
    return;
  }
  std::set<EquipSlot> wanted;
  for (const std::string& name : WorkbenchGearFor(path.back())) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(name);
    if (it != state.equips.end()) {
      wanted.insert(it->second.equip_slot());
    }
  }
  std::set<EquipSlot> filled;
  std::vector<std::vector<std::string>> by_stage(path.size());
  for (int i = static_cast<int>(path.size()) - 1; i >= 0; --i) {
    std::set<EquipSlot> reached;
    for (const std::string& name : WorkbenchGearFor(path[i])) {
      std::map<std::string, EquipPrototype>::const_iterator it =
          state.equips.find(name);
      if (it == state.equips.end() ||
          wanted.count(it->second.equip_slot()) == 0 ||
          filled.count(it->second.equip_slot()) > 0) {
        continue;
      }
      // Two items for one slot in one stage (the Rogue's dagger and claw) are a
      // choice the workbench offers, so a whole stage is filtered at once
      // instead of item by item.
      by_stage[i].push_back(name);
      if (state.character.MeetsLevel(it->second)) {
        reached.insert(it->second.equip_slot());
      }
    }
    filled.insert(reached.begin(), reached.end());
  }
  for (const std::vector<std::string>& stage : by_stage) {
    WearAll(state, stage, equips);
  }
}

// The only armour every job can wear, so it fits any workbench. The gloves and
// boots need level 140, so a 3rd job at 100 carries them instead of wearing
// them.
std::vector<std::string> FrozenArmour() {
  return {"frozen_hat",  "frozen_top",    "frozen_bottom",
          "frozen_cape", "frozen_gloves", "frozen_boots"};
}

// The level the Chaos Root Abyss opens at, and so the earliest anyone can own
// what its Pieces buy. The gear is wearable at 150, but nothing pays for a
// piece until 200, so a character dressed here is one the game could really
// produce.
constexpr int kRootAbyssLevel = 200;

// The branch's Root Abyss set, worn over the Frozen tier: three armour pieces
// and the weapon, which replace the Frozen hat, top, bottom and weapon. The
// secondary isn't included; Princess No's secondary, from a fight opening at
// the same level, fills it below.
std::vector<std::string> RootAbyssArmour(Job job) {
  switch (BranchOf(job)) {
    case JobBranch::kWarrior:
      return {"royal_warrior_helm", "eagle_eye_warrior_armor",
              "trixter_warrior_pants"};
    case JobBranch::kArcher:
      return {"royal_ranger_beret", "eagle_eye_ranger_cowl",
              "trixter_ranger_pants"};
    case JobBranch::kMagician:
      return {"royal_dunwitch_hat", "eagle_eye_dunwitch_robe",
              "trixter_dunwitch_pants"};
    case JobBranch::kRogue:
      return {"royal_assassin_hood", "eagle_eye_assassin_shirt",
              "trixter_assassin_pants"};
    default:
      return {};
  }
}

// The Root Abyss weapon for a 4th job, following the same line choice as the
// Frozen tier in WorkbenchGearFor. Empty below the 4th job, which only reaches
// level 200 if a tester sets it by hand.
std::string RootAbyssWeapon(Job job) {
  switch (job) {
    case JOB_HERO:
      return "fafnir_battle_cleaver";
    case JOB_PALADIN:
      return "fafnir_lightning_striker";
    case JOB_DARK_KNIGHT:
      return "fafnir_brionak";
    case JOB_BOW_MASTER:
      return "fafnir_wind_chaser";
    case JOB_MARKSMAN:
      return "fafnir_windwing_shooter";
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
    case JOB_FIRE_POISON_ARCH_MAGE:
    case JOB_BISHOP:
      return "fafnir_mana_crown";
    case JOB_NIGHT_LORD:
      return "fafnir_risk_holder";
    case JOB_SHADOWER:
      return "fafnir_damascus";
    default:
      return "";
  }
}

std::vector<std::string> RootAbyssGear(Job job) {
  std::vector<std::string> names = RootAbyssArmour(job);
  std::string weapon = RootAbyssWeapon(job);
  if (!weapon.empty()) {
    names.push_back(std::move(weapon));
  }
  return names;
}

// The secondary Princess No's fragments buy, the first to replace the Frozen
// secondary. She opens at 200 like the Chaos Root Abyss, so one level gate
// covers both. It belongs to no set, so the Frozen set loses a piece. Empty
// below the 4th job, like the Root Abyss weapon.
std::vector<std::string> PrincessNoSecondary(Job job) {
  switch (job) {
    case JOB_HERO:
      return {"princess_nos_medal"};
    case JOB_PALADIN:
      return {"princess_nos_rosary"};
    case JOB_DARK_KNIGHT:
      return {"princess_nos_flower_chain"};
    case JOB_FIRE_POISON_ARCH_MAGE:
      return {"princess_nos_flaming_book"};
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return {"princess_nos_damp_book"};
    case JOB_BISHOP:
      return {"princess_nos_golden_book"};
    case JOB_BOW_MASTER:
      return {"princess_nos_feather"};
    case JOB_MARKSMAN:
      return {"princess_nos_wreath"};
    case JOB_NIGHT_LORD:
      return {"princess_nos_charm"};
    case JOB_SHADOWER:
      return {"princess_nos_purple_shadow"};
    default:
      return {};
  }
}

// The level Damien and Lotus open at, and so the earliest anyone can own what
// their coins buy. The same rule as the Root Abyss one tier down: the gear is
// wearable at 160, but nothing pays for a piece until 210.
constexpr int kAbsoLabLevel = 210;

// The level the Guardian Angel Slime opens at, and so the earliest anyone can
// own the ring she drops. The same rule as the token tiers, for a fight that
// drops gear directly instead of a coin: it's wearable at 160, but nothing
// gives one out until 220.
constexpr int kGuardianAngelSlimeLevel = 220;

// The AbsoLab weapon for a 4th job, following the same line choice as the two
// tiers below. Empty below the 4th job, which only reaches level 210 if a
// tester sets it by hand.
std::string AbsoLabWeapon(Job job) {
  switch (job) {
    case JOB_HERO:
      return "absolab_broad_axe";
    case JOB_PALADIN:
      return "absolab_broad_hammer";
    case JOB_DARK_KNIGHT:
      return "absolab_piercing_spear";
    case JOB_BOW_MASTER:
      return "absolab_sureshot_bow";
    case JOB_MARKSMAN:
      return "absolab_crossbow";
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
    case JOB_FIRE_POISON_ARCH_MAGE:
    case JOB_BISHOP:
      return "absolab_spellsong_staff";
    case JOB_NIGHT_LORD:
      return "absolab_revenge_guard";
    case JOB_SHADOWER:
      return "absolab_blade_lord";
    default:
      return "";
  }
}

// The branch's AbsoLab armour, worn over the Root Abyss tier: six pieces, which
// replace the Root Abyss hat, top and bottom and the Frozen cape, gloves and
// boots. The shoulder takes Cygnus's slot, so the Boss Accessory Set loses a
// piece to it.
std::vector<std::string> AbsoLabArmour(Job job) {
  switch (BranchOf(job)) {
    case JobBranch::kWarrior:
      return {"absolab_knight_helm",    "absolab_knight_armor",
              "absolab_knight_pants",   "absolab_knight_shoes",
              "absolab_knight_gloves",  "absolab_knight_cape",
              "absolab_knight_shoulder"};
    case JobBranch::kArcher:
      return {"absolab_archer_hood",    "absolab_archer_armor",
              "absolab_archer_pants",   "absolab_archer_shoes",
              "absolab_archer_gloves",  "absolab_archer_cape",
              "absolab_archer_shoulder"};
    case JobBranch::kMagician:
      return {"absolab_mage_crown",   "absolab_mage_armor",
              "absolab_mage_pants",   "absolab_mage_shoes",
              "absolab_mage_gloves",  "absolab_mage_cape",
              "absolab_mage_shoulder"};
    case JobBranch::kRogue:
      return {"absolab_bandit_cap",     "absolab_bandit_armor",
              "absolab_bandit_pants",   "absolab_bandit_shoes",
              "absolab_bandit_gloves",  "absolab_bandit_cape",
              "absolab_bandit_shoulder"};
    default:
      return {};
  }
}

std::vector<std::string> AbsoLabGear(Job job) {
  std::vector<std::string> names = AbsoLabArmour(job);
  std::string weapon = AbsoLabWeapon(job);
  if (!weapon.empty()) {
    names.push_back(std::move(weapon));
  }
  return names;
}

// Boss drops, the only things that fill the accessory and pocket slots. They
// take a long time to reach just to see a screen, so the workbench starts with
// them. Two slots hold a pair, equipped oldest first, so a 3rd job keeps the
// older one and a 4th job at the cap swaps to the newer.
std::vector<std::string> BossAccessories() {
  return {"aquatic_letter_eye_accessory",
          "black_bean_mark",
          "papulatus_mark",
          "condensed_power_crystal",
          "stone_of_eternal_life",
          "pink_holy_cup",
          "silver_blossom_ring",
          "kannas_treasure",
          "chaos_horntail_necklace",
          "dominator_pendant",
          "dea_sidus_earring",
          "will_o_the_wisps",
          "royal_black_metal_shoulder",
          "hayatos_treasure",
          "ayames_treasure",
          "golden_clover_belt",
          "crystal_ventus_badge"};
}

// What the shop's Equips shelf puts in the same slots, given instead of bought,
// since the workbench is a character who already went shopping. The four Cygnus
// shoulders come last, after the boss drop they replace. `cygnus_shoulders`
// leaves them out, which suits a character measured against a boss roster,
// since nobody has beaten Cygnus.
std::vector<std::string> ShopAccessories(bool cygnus_shoulders) {
  std::vector<std::string> names = {"lightning_god_ring", "meister_ring",
                                    "gold_maple_leaf_emblem",
                                    "master_adventurer"};
  if (cygnus_shoulders) {
    names.insert(
        names.end(),
        {"lionheart_battle_shoulder", "dragon_tail_mage_shoulder",
         "falcon_wing_sentinel_shoulder", "raven_horn_chaser_shoulder"});
  }
  return names;
}

// Passed as `unspent_stage` to spend every point earned.
constexpr int kSpendEveryStage = 0;

// Arcane River opens at 200, and it opens with a symbol provided: without one,
// the first map there would apply the full penalty, a wall instead of an
// introduction.
constexpr int kArcaneRiverLevel = 200;
constexpr char kStarterSymbol[] = "symbol_vanishing_journey";

// Equips the Arcane Symbol given on reaching 200. Worn, not carried: a symbol
// in the bag gives no Arcane Force, and a workbench at the cap is on maps that
// require it.
void WearStarterSymbol(GameState& state) {
  const InventoryInstance& bag = state.character.inventory();
  for (int i = 0; i < bag.size(); ++i) {
    const EquipInstance* item = bag.equip_instance(i);
    if (item != nullptr && IsArcaneSymbol(item->prototype())) {
      state.character.Equip(i);
      return;
    }
  }
}

// Whether levelling up leaves `skill` for the tester to buy. The book of the
// character's current job is theirs to spend, and for a 5th job that book is
// the whole V Matrix, including common nodes, since they use the same points
// and belong to no stage.
bool LeaveUnbought(const Skill& skill, int unspent_stage) {
  if (unspent_stage == kSpendEveryStage) {
    return false;
  }
  if (unspent_stage >= kFifthJobStage) {
    return skill.v_node() != V_NODE_KIND_UNSPECIFIED;
  }
  return StageForAdvancement(BookOf(skill)) >= unspent_stage;
}

// The skills available to buy at every level of the climb, in catalog order.
// Which book a skill belongs to only changes at an advancement, so this is
// checked once per advancement instead of once per level. The catalog has 450
// skills and a book about forty, and checking all of it at every level was the
// slowest thing in the tests.
std::vector<const Skill*> BuyableSkills(const CharacterInstance& character,
                                        const GameState& state,
                                        int unspent_stage) {
  std::vector<const Skill*> buyable;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    // The two kinds LearnSkill rejects without checking the books: a form
    // bought by buying the skill it replaces, and a node bought from the
    // matrix.
    if (!skill.replaces_skill_name().empty() ||
        LeaveUnbought(skill, unspent_stage)) {
      continue;
    }
    if (skill.v_node() != V_NODE_KIND_UNSPECIFIED ||
        character.HasBookFor(skill)) {
      buyable.push_back(&skill);
    }
  }
  return buyable;
}

// Levels to `level` the way a player would, taking each advancement in `path`
// when offered. AP always goes into the primary stat (a hundred points would be
// a hundred keypresses for the tester), and SP is spent only below
// `unspent_stage`, leaving the current book for the tester to spend by hand.
void GrowTo(GameState& state, int level, const std::vector<Job>& path,
            int unspent_stage) {
  CharacterInstance& character = state.character;
  std::vector<const Skill*> buyable =
      BuyableSkills(character, state, unspent_stage);
  int taken = 0;
  while (character.proto().level() < level) {
    int before = character.proto().level();
    character.LevelUp();
    GrantLevelRewards(state, before, character.proto().level());
    if (character.CanAdvanceJob() && taken < static_cast<int>(path.size())) {
      character.AdvanceJob(path[taken++]);
      buyable = BuyableSkills(character, state, unspent_stage);
    }
    // After the advancement, not before: the advancement refunds every
    // allocated point and re-spends it for the new job.
    while (character.AllocateStat(PrimaryStatField(character.proto().job()))) {
    }
    for (const Skill* skill : buyable) {
      while (character.LearnSkill(*skill)) {
      }
    }
  }
}

// The level a character stops at when none is given: the top of the job's level
// band, capped at the EXP table's maximum. The last advancement written has no
// band above it (the 6th job's level is only a number in the table), so it
// levels to the cap instead.
int LevelForJob(JobAdvancement advancement, int level) {
  if (level > 0) {
    return level;
  }
  int stage = StageForAdvancement(advancement);
  return stage >= kLastJobStage
             ? kTrialLevelCap
             : std::min(NextAdvancementLevel(stage), kTrialLevelCap);
}

// The highest advancement `level` unlocks in the same line: --job names the
// line and the level says how far along it. A stage with no branch chosen stops
// the climb: a Swordman at 200 stays a Swordman, since nothing says which of
// the three they became.
JobAdvancement HighestAdvancementAt(JobAdvancement advancement, int level) {
  Job job = JobForAdvancement(advancement);
  int stage = StageForAdvancement(advancement);
  while (stage < kLastJobStage && level >= NextAdvancementLevel(stage) &&
         AdvancementForJobStage(job, stage + 1) !=
             JOB_ADVANCEMENT_UNSPECIFIED) {
    ++stage;
  }
  return AdvancementForJobStage(job, stage);
}

// Levels into `advancement`: its job, having taken every earlier advancement on
// the way. `level` is where to stop, or 0 for the last level before the next
// advancement is offered.
void GrowToJob(GameState& state, JobAdvancement advancement, int level,
               int unspent_stage, const GearSetup& equips,
               bool cygnus_shoulders = true) {
  Job job = JobForAdvancement(advancement);
  int stage = StageForAdvancement(advancement);
  std::vector<Job> path;
  for (int i = 1; i <= stage; ++i) {
    path.push_back(JobForAdvancement(AdvancementForJobStage(job, i)));
  }
  GrowTo(state, LevelForJob(advancement, level), path, unspent_stage);
  // The job's own gear, worn instead of carried, since there's no advancement
  // moment here to equip it. Lower stages' gear fills whatever the level can't
  // yet wear.
  WearThePath(state, path, equips);
  // The Frozen set on top, from the 3rd job up. It drops instead of selling, so
  // only a workbench will ever wear the whole set. Every piece fits a 3rd job
  // at level 100, giving four slots; a 4th job adds the two from the token shop
  // above, for six.
  if (stage >= 3) {
    WearAll(state, FrozenArmour(), equips);
    WearAll(state, BossAccessories(), equips);
    WearAll(state, ShopAccessories(cygnus_shoulders), equips);
  }
  // Last, so it replaces the Frozen pieces it supersedes and not the other way
  // round.
  if (state.character.proto().level() >= kRootAbyssLevel) {
    WearAll(state, RootAbyssGear(state.character.proto().job()), equips);
    WearAll(state, PrincessNoSecondary(state.character.proto().job()), equips);
  }
  if (state.character.proto().level() >= kAbsoLabLevel) {
    WearAll(state, AbsoLabGear(state.character.proto().job()), equips);
  }
  if (state.character.proto().level() >= kGuardianAngelSlimeLevel) {
    std::map<std::string, EquipPrototype>::const_iterator ring =
        state.equips.find("guardian_angel_ring");
    if (ring != state.equips.end()) {
      MakeRoomFor(state, ring->second);
      WearAll(state, {"guardian_angel_ring"}, equips);
    }
  }
  WearStarterSymbol(state);
}

// Resets to the level-1 Beginner every mode starts from. The CharacterInstance
// holds a reference to the state's RNG, so it can't be replaced outright; kMax
// builds a whole account through this function.
void ResetToBeginner(GameState& state) {
  state.character.RestoreFrom(MakeBaseBeginnerProto(), state.equips,
                              state.items);
}

// A new player starts with a weapon and nothing else: the Sword is worn, so the
// bag is empty.
void SeedPlay(GameState& state) {
  GiveEquip(state, "sword");
  if (!state.character.inventory().empty()) {
    state.character.Equip(0);
  }
  state.current_map = kHomeMap;
}

// The workbench's combat EXP multiplier. High enough that the early levels pass
// while the tester watches, so level-gated features can be reached without
// farming.
constexpr int kTestExpMultiplier = 5;

// How many of each currency the workbench starts with: enough tokens to buy a
// shelf's worth with one left over, and enough shards to fill the Token tab's
// column.
constexpr int kTestTokens = 20;

// Enough spell traces to scroll every slot several times. Given instead of
// bought, since the shop sells them 5,000 meso at a time and that's a long way
// to the scroll screen.
constexpr int kTestSpellTraces = 30000;

// Enough V Points to fill the whole matrix twice: the workbench is for looking
// at nodes, not farming the sixty days one costs.
constexpr int64_t kTestVPoints = 5000;

// `advancement`'s job name as a username, using only letters, digits and
// spaces, so "I/L Arch Mage" becomes "IL Arch Mage".
std::string UsernameFor(JobAdvancement advancement) {
  std::string name;
  for (char c : ShortJobName(JobForAdvancement(advancement))) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ') {
      name += c;
    }
  }
  return name.substr(0, kMaxUsernameLength);
}

// Spare Vanishing Journey symbols for the workbench. A symbol levels up by
// absorbing duplicates and its first level needs 12, so this is one level on
// the Symbols tab with some left over: enough to see it progress without
// farming.
constexpr int kTestSymbols = 15;

// The most cubes one workbench slot will use trying to reach its rank. Rare to
// Legendary averages about 65 rolls, so in practice this limit never triggers.
constexpr int kMaxSeedCubes = 100000;

void GiveSymbols(GameState& state) {
  for (int i = 0; i < kTestSymbols; ++i) {
    GiveEquip(state, kStarterSymbol);
  }
}

// Potential on the workbench's gear. Every rank should be on screen at once, so
// the cubeable slots are assigned the four ranks in turn instead of each
// rolling its own. A rank is reached by cubing until the item gets there, like
// a player would, which costs only tens of rolls.
void SeedPotentials(GameState& state) {
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, const EquipInstance*>& kv :
       state.character.equipped()) {
    if (kv.second->CanCube()) {
      slots.push_back(kv.first);
    }
  }
  std::shuffle(slots.begin(), slots.end(), state.rng);

  constexpr PotentialRank kRanks[] = {POTENTIAL_RANK_RARE, POTENTIAL_RANK_EPIC,
                                      POTENTIAL_RANK_UNIQUE,
                                      POTENTIAL_RANK_LEGENDARY};
  for (size_t i = 0; i < slots.size(); ++i) {
    // Capped instead of trusting the odds: a cube that stopped ranking up would
    // otherwise hang the workbench on startup.
    state.character.CubeWornUpTo(slots[i], CubeType::kRed,
                                 kRanks[i % std::size(kRanks)], kMaxSeedCubes);
  }
}

// The workbench, where everything exists to reach a screen without playing up
// to it. `chosen` is --job: unset uses kTestAdvancement and buys its whole
// book, so the default workbench is complete instead of half-built.
void SeedTest(GameState& state, const TestOptions& test) {
  state.exp_multiplier = kTestExpMultiplier;
  // Both settings on: the workbench has two allocations at once, which is what
  // autoswap is for, and the music is what a tester hears for hours.
  state.account.SetAutoswapPresets(true);
  state.account.SetJukeboxMode(JUKEBOX_MODE_SHUFFLE);

  // Enough to buy anything in the shop several times over, so buying screens
  // can be tested without farming meso. A hundred billion because star force is
  // the real cost: one attempt near the top costs nine figures, so a billion
  // gave a tester about sixteen presses.
  state.character.AddMeso(100000000000);

  std::map<std::string, ItemPrototype>::const_iterator trace =
      state.items.find("spell_trace");
  if (trace != state.items.end()) {
    state.character.AddItem(trace->second, kTestSpellTraces);
  }
  state.character.AddVPoints(kTestVPoints);

  // Some of every currency, so the shop's token shelves can be used without
  // farming the mobs that drop them, and both columns of the bag's Token tab
  // are filled without clearing every boss.
  for (const std::pair<const std::string, ItemPrototype>& entry : state.items) {
    if (entry.second.kind() == ITEM_KIND_TOKEN ||
        entry.second.kind() == ITEM_KIND_SOUL_SHARD) {
      state.character.AddItem(entry.second, kTestTokens);
    }
  }

  bool chose_job = test.job != JOB_ADVANCEMENT_UNSPECIFIED;
  if (!chose_job) {
    // Worn immediately: it's the character's weapon until levelling provides
    // their job's weapon, and with a catalog missing that weapon (a test's, for
    // example) they keep this instead of nothing.
    GiveEquip(state, "sword");
    if (!state.character.inventory().empty()) {
      state.character.Equip(0);
    }
  }
  // Only the current job's book follows --skills. The earlier books are bought
  // either way: they aren't why the tester chose the job, and leaving them
  // unbought would add two allocation screens before the one they want.
  JobAdvancement advancement = chose_job ? test.job : kTestAdvancement;
  // Named after its job, so several workbenches in a party can be told apart
  // without typing names.
  state.character.SetUsername(UsernameFor(advancement));
  GrowToJob(state, advancement, test.level,
            test.skills == TestSkills::kZero ? StageForAdvancement(advancement)
                                             : kSpendEveryStage,
            test.equips);

  // Everything above dresses the character, and nothing is meant to be carried.
  // The bag holds only leftovers (gear a level gate says to carry instead of
  // wear, other branches' shoulders, weapons replaced later), and a tester
  // opening the Equip tab should see only what they put there.
  state.character.ClearEquipInventory();
  GiveSymbols(state);
  SeedPotentials(state);

  // The weakest hunting ground; the tester can pick any other on map select.
  state.current_map = "right_around_lith_harbor";
}

// What the max character is left with. The climb's whole income is spent by now
// (see max_character.cc for the math), so this is pocket change, not a purse to
// shop with.
constexpr int64_t kMaxLeftoverMeso = 50000000;

// Every permanent potion the character's level has unlocked. Each is paid for
// at its own price, so the remaining meso is correct and doesn't include
// potions the level can't reach yet. Switched on, because a player who bought
// one uses it.
void BuyMaxConsumables(GameState& state) {
  for (const ConsumableInfo& potion : AllConsumables()) {
    if (state.character.proto().level() < potion.unlock_level) {
      continue;
    }
    state.character.AddMeso(potion.permanent_price);
    if (state.character.BuyConsumable(potion.type)) {
      state.character.ToggleConsumable(potion.type);
    }
  }
}

// The level every Arcane Symbol of a max character is set to. A level-1 symbol
// gives 30 Arcane Force against rivers requiring 190 and up, the worst bracket
// (a tenth of the damage dealt and 2.8x taken). Level 10 gives 120 each, enough
// for the maps the same levels unlocked.
constexpr int kMaxModeSymbolLevel = 10;

// Every symbol whose area the level has reached, worn and levelled. Called
// before the bag is cleared, so the replaced level-1 starter is removed with
// the other leftovers.
void WearMaxSymbols(GameState& state) {
  const int level = state.character.proto().level();
  for (const std::pair<const std::string, EquipPrototype>& entry :
       state.equips) {
    const EquipPrototype& proto = entry.second;
    if (!IsArcaneSymbol(proto) || proto.arcane_symbol().area_level() > level) {
      continue;
    }
    Equip raised;
    raised.set_symbol_level(kMaxModeSymbolLevel);
    int row = static_cast<int>(state.character.inventory().size());
    state.character.PickUp(std::make_unique<EquipInstance>(proto, raised));
    if (static_cast<int>(state.character.inventory().size()) > row) {
      state.character.Equip(row);
    }
  }
}

// The same potential lines on every piece of one kind. Set directly instead of
// cubed for: real potential is luck, and a fight measured against a slightly
// different character every run tells us nothing. See MaxPotentialFor.
void DressMaxPotentials(GameState& state, const MaxGear& gear) {
  const StatField primary = PrimaryStatField(state.character.proto().job());
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
       state.character.equipped()) {
    slots.push_back(worn.first);
  }
  for (EquipSlot slot : slots) {
    const Potential potential = MaxPotentialFor(slot, gear, primary);
    if (potential.lines_size() > 0) {
      state.character.TakePotential(slot, potential);
    }
  }
}

// Every matrix node at its maximum level. The points are granted at each node's
// cost, leaving nothing in the pool: this mode creates a character who spent
// everything, not one holding points.
void MaxVMatrix(GameState& state) {
  CharacterInstance& character = state.character;
  if (!character.v_matrix_unlocked()) {
    return;
  }
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    const Skill& skill = entry.second;
    if (skill.v_node() == V_NODE_KIND_UNSPECIFIED ||
        !character.ReachesVNode(skill)) {
      continue;
    }
    int room = SkillMaxLevel(skill) - character.skill_level(skill);
    character.AddVPoints(character.VNodeCostFor(skill, room));
    character.LearnSkill(skill, room);
  }
}

// Every job the fourth advancement leads to, one per line. Read from the
// advancement table instead of listed, so a new line is included automatically.
std::vector<Job> EveryFourthJob() {
  std::vector<Job> jobs;
  for (Job first : JobChoicesForStage(JOB_BEGINNER, 1)) {
    for (Job second : JobChoicesForStage(first, 2)) {
      for (Job third : JobChoicesForStage(second, 3)) {
        for (Job fourth : JobChoicesForStage(third, 4)) {
          jobs.push_back(fourth);
        }
      }
    }
  }
  return jobs;
}

// The highest advancement `job`'s line reaches at `level`. Found through the
// 4th job, which covers every book in its line: HighestAdvancementAt reads the
// job back from the advancement, and a 1st job's doesn't say which branch it
// became.
JobAdvancement CeilingAdvancementFor(Job job, int level) {
  int stage = 1;
  while (stage < kLastJobStage && level >= NextAdvancementLevel(stage) &&
         AdvancementForJobStage(job, stage + 1) !=
             JOB_ADVANCEMENT_UNSPECIFIED) {
    ++stage;
  }
  return AdvancementForJobStage(job, stage);
}

// What the rest of a max account gives the character on `line`. Every slot is
// at the same level in a different line, so this is arithmetic instead of a
// loop over characters, which lets the roster be built before any character
// exists.
LinkTally CeilingTally(Job line, int level) {
  LinkTally tally;
  for (Job job : EveryFourthJob()) {
    if (LineOf(job) == line) {
      continue;
    }
    tally.Record(JobForAdvancement(CeilingAdvancementFor(job, level)), level);
  }
  return tally;
}

// Builds a max character onto whatever `state.character` holds, so the caller
// resets to a Beginner between characters. `tally` is what the rest of the
// account gives them, set before Hyper Stats are measured because a link
// skill's crit rate changes the best allocation.
void MaxOneCharacter(GameState& state, JobAdvancement advancement, int level,
                     const LinkTally& tally) {
  const MaxGear gear = MaxGearForLevel(level);
  GearSetup equips;
  equips.hammered = gear.hammered;
  equips.scrolled = true;
  equips.stars = gear.stars;
  equips.weapon_stars = gear.weapon_stars;

  state.character.SetUsername(UsernameFor(advancement));
  state.character.AddMeso(kMaxLeftoverMeso);
  GrowToJob(state, advancement, level, kSpendEveryStage, equips,
            /*cygnus_shoulders=*/false);
  WearMaxSymbols(state);
  // The climb's leftovers: pieces a level gate says to carry instead of wear,
  // and weapons replaced later.
  state.character.ClearEquipInventory();

  DressMaxPotentials(state, gear);
  MaxVMatrix(state);
  state.character.set_link_tally(tally);
  state.character.ReconcileLinkSkills(state.skills);
  SpendMaxHyperStats(state.character, state.skills, state.bosses, state.mobs);
  if (state.character.inner_ability_unlocked()) {
    const StatField primary = PrimaryStatField(state.character.proto().job());
    for (Activity activity : {Activity::kFarming, Activity::kBossing}) {
      state.character.SetAbility(MaxAbilityPreset(activity, primary),
                                 AutoswapSlotFor(activity));
    }
  }
  BuyMaxConsumables(state);
}

// The rest of a max account: a max character at the top of every other job
// line, at the played character's level, so the roster provides the account's
// link skills. Built before the played character, who is then the only one
// never converted to and from a proto.
void SeedMaxRoster(GameState& state, Job played_line, int level) {
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  state.inactive_characters.clear();
  for (Job job : EveryFourthJob()) {
    if (LineOf(job) == played_line) {
      continue;
    }
    ResetToBeginner(state);
    MaxOneCharacter(state, CeilingAdvancementFor(job, level), level,
                    CeilingTally(LineOf(job), level));
    CharacterSave& slot = state.inactive_characters.emplace_back();
    *slot.mutable_character() = state.character.ToProto();
    slot.set_current_map(kHomeMap);
    slot.set_created_unix_seconds(now);
    // Marked unplayed, so character select opens on the character in play
    // instead of the last one the loop built.
    slot.set_last_played_unix_seconds(0);
  }
}

// The max mode character: what a player who spent well has at this level. Set
// directly instead of played for, with every number priced against the climb's
// income by then (max_character.cc has the math). Nothing from the workbench:
// no extra meso, no EXP bonus, no spare gear.
void SeedMax(GameState& state, const TestOptions& options) {
  state.character.set_link_skills_off(!options.link_skills);
  // A max character has two allocations at once, which is what autoswap is for,
  // whatever the state requested.
  state.account.SetAutoswapPresets(true);
  state.account.SetJukeboxMode(JUKEBOX_MODE_SHUFFLE);
  state.MirrorAccount();
  // The same default as the workbench: the highest advancement written in the
  // line, which is where boss rosters are measured from.
  const JobAdvancement chosen = options.job != JOB_ADVANCEMENT_UNSPECIFIED
                                    ? options.job
                                    : kTestAdvancement;
  const int level = LevelForJob(chosen, options.level);
  // --job names the line, not where to stop in it: a max character at a level
  // has taken every advancement that level allows.
  const JobAdvancement advancement = HighestAdvancementAt(chosen, level);
  const Job played_line = LineOf(JobForAdvancement(advancement));

  // The roster exists only for link skills, so a max character without them
  // stands alone, as in every sim.
  LinkTally tally;
  if (options.link_skills) {
    SeedMaxRoster(state, played_line, level);
    tally = CeilingTally(played_line, level);
  }
  ResetToBeginner(state);
  MaxOneCharacter(state, advancement, level, tally);
  // Recompute the tally from the roster: from now on the account provides it,
  // and removing a slot changes it.
  state.MirrorAccount();
  state.current_map = kHomeMap;
}

}  // namespace

GameState::GameState(std::map<std::string, EquipPrototype> equips_arg,
                     std::map<std::string, Scroll> scrolls_arg,
                     std::map<std::string, ItemPrototype> items_arg,
                     std::map<std::string, Mob> mobs_arg,
                     std::map<std::string, MapData> maps_arg,
                     std::map<std::string, Skill> skills_arg, GameMode mode,
                     TestOptions test, std::optional<unsigned int> seed,
                     std::map<std::string, EquipSet> sets,
                     std::map<std::string, Boss> bosses_arg)
    : equips(std::move(equips_arg)),
      scrolls(std::move(scrolls_arg)),
      items(std::move(items_arg)),
      mobs(std::move(mobs_arg)),
      maps(std::move(maps_arg)),
      skills(std::move(skills_arg)),
      equip_sets(std::move(sets)),
      bosses(std::move(bosses_arg)),
      rng(seed.has_value() ? *seed : std::random_device{}()),
      // Every mode builds its character up from a level-1 Beginner. --level
      // says where to stop, and the seeding below levels up to it.
      character(rng, MakeBaseBeginnerProto()),
      last_played_unix_seconds(static_cast<int64_t>(std::time(nullptr))),
      created_unix_seconds(static_cast<int64_t>(std::time(nullptr))) {
  FillTokenShelves(equips, items);
  // Before seeding: a max character's allocations are measured by playing the
  // fight, and the fight reads this setting.
  account.SetAutoswapPresets(test.autoswap_presets);
  MirrorAccount();
  if (mode == GameMode::kTest) {
    SeedTest(*this, test);
  } else if (mode == GameMode::kMax) {
    SeedMax(*this, test);
  } else {
    SeedPlay(*this);
  }
  character.UseEquipSets(equip_sets);
  MirrorAccount();
}

void SeedNewCharacter(GameState& state) {
  ResetToBeginner(state);
  SeedPlay(state);
}

void GameState::MirrorAccount() {
  MirrorAccountOnto(character, inactive_characters, /*theirs=*/-1);
}

void GameState::MirrorAccountOnto(CharacterInstance& into,
                                  const std::vector<CharacterSave>& roster,
                                  int theirs) const {
  into.set_autoswap_presets(account.autoswap_presets());
  into.set_account_max_level(account.max_level());
  // Only the others: a character provides their own line's link skill
  // themselves, since their level can change mid-session while a slot's can't.
  LinkTally tally;
  for (int slot = 0; slot < static_cast<int>(roster.size()); ++slot) {
    if (slot == theirs) {
      continue;
    }
    tally.Record(roster[slot].character().job(),
                 roster[slot].character().level());
  }
  into.set_link_tally(std::move(tally));
}

namespace {

// Every other character on the account, which Burning compares the played one
// against.
std::vector<int> OtherCharacterLevels(const GameState& state) {
  std::vector<int> levels;
  levels.reserve(state.inactive_characters.size());
  for (const CharacterSave& save : state.inactive_characters) {
    levels.push_back(save.character().level());
  }
  return levels;
}

}  // namespace

void AwardExp(GameState& state, int64_t amount) {
  int before = state.character.proto().level();
  state.character.AddExp(amount, OtherCharacterLevels(state));
  GrantLevelRewards(state, before, state.character.proto().level());
}

void GrantLevelRewards(GameState& state, int from_level, int to_level) {
  // Paid for every level in the range, whether or not the character can spend
  // it yet: Inner Ability opens at 160 with a pool the climb has been filling
  // all along.
  state.character.AddHonor(HonorForLevels(from_level, to_level));
  if (from_level >= kArcaneRiverLevel || to_level < kArcaneRiverLevel) {
    return;
  }
  std::map<std::string, EquipPrototype>::const_iterator symbol =
      state.equips.find(kStarterSymbol);
  if (symbol == state.equips.end()) {
    return;
  }
  state.character.PickUp(std::make_unique<EquipInstance>(symbol->second));
}

int OwnedFromLevel(const EquipPrototype& proto) {
  if (proto.token_item() == "absolab_coin") {
    return kAbsoLabLevel;
  }
  // The four Chaos Root Abyss Pieces, the only tokens named this way, and
  // Princess No's fragment, from a fight opening at the same level. The Frozen
  // and Cygnus tokens come from fights open well below the gear they buy, so
  // they gate nothing.
  if (proto.token_item().rfind("piece_of_", 0) == 0 ||
      proto.token_item() == "captivating_fragment") {
    return kRootAbyssLevel;
  }
  // The one piece of gear a fight drops directly instead of through a token,
  // and the only one whose fight opens above the level it's worn at.
  if (proto.name() == "Guardian Angel Ring") {
    return kGuardianAngelSlimeLevel;
  }
  return proto.required_level();
}

}  // namespace ms
