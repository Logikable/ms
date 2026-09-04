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

// The level-1 Beginner every character starts from, before any leveling.
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

// How many Level-Up items the workbench opens with. Enough to carry the
// character it starts as past every gate in the unlock table, and to keep
// earning AP past kTrialLevelCap, since LevelUp is not bounded by it.
constexpr int kTestLevelUpItems = 199;

// Everything the workbench dresses a job in: the best of each thing it carries
// that its starting level can wear. Advancing hands over gear for the level it
// happens at, and the workbench starts at the TOP of an advancement, so a level
// 60 Fighter would otherwise swing the axe they were given at 30.
//
// Two weapons means the better one by //analysis:weapon_sim, but a Rogue gets
// all three: which of the dagger and the claw is held decides what they swing.
//
// Long, and stays long: one row per job. The static_assert is the tripwire --
// Clang cannot check the switch itself, because -Wswitch over a proto enum
// demands the two DO_NOT_USE sentinels as well.
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
    // The 2nd jobs, at level 60, each with its off-hand -- a secondary belongs
    // to a branch, so the 1st jobs above have none. The three magician branches
    // swing the same staff but read from three different books.
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
    // Level 50 stars: the ladder's next rung is out of a level 60's reach.
    case JOB_ASSASSIN:
      return {"steely_throwing_knives", "dark_gigantic", "evil_ender_charm"};
    case JOB_BANDIT:
      return {"deadly_fin", "vanishing_shadow"};
    // The 3rd jobs, at level 100: the best gear meso alone reaches, since the
    // Frozen tier below is bought with tokens. Each takes the better of its
    // line's two weapons on //analysis:weapon_sim, and keeps its 2nd job's
    // off-hand -- a 3rd job opens no new slot. The Crusader's axe beats the
    // sword of the same tier on Weapon Mastery's axe bonus alone.
    case JOB_BERSERKER:
      return {"pinaka", "berserk_chain"};
    // The 4th job, at the level cap, so its gear is the Frozen tier a token
    // buys rather than the last one meso reaches.
    case JOB_DARK_KNIGHT:
      return {"frozen_spear", "frozen_chain"};
    case JOB_PALADIN:
      return {"frozen_maul", "frozen_rosary"};
    // The axe over the sword, for the reason the Crusader takes one: the two
    // weigh the same in the damage chain and Weapon Mastery pays 5% more for
    // an axe.
    case JOB_HERO:
      return {"frozen_two_handed_axe", "frozen_medal"};
    // The arrows are the one thing the Frozen tier has no answer for, so the
    // bow line keeps buying its ammunition off the shelf.
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
    // The stars are worn, not thrown: a claw with none in the slot swings for
    // nothing at all. See the Assassin above.
    case JOB_HERMIT:
      return {"balanced_fury", "red_craven", "death_sender_charm"};
    case JOB_CHIEF_BANDIT:
      return {"blood_dagger", "slashing_shadow"};
    default:
      return StarterEquipsFor(job);
  }
}

// The best trace of one type for `proto`, at the longest odds it is written
// at. The odds cost nothing here -- every slot passes -- so the biggest is the
// one to take. Null when nothing of that type is written for the item.
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

// The spell trace the workbench scrolls `proto` with: the one that raises the
// stat this character fights with, or, where a slot takes no stat trace at
// all, the one that raises the attack they swing with. Gloves and hearts are
// the second case -- nothing but ATT and M.ATT is written for either, so
// asking only for the stat left them unscrolled and, with a slot still open,
// unstarred. Returns nullptr for an item nothing is written for, ammunition
// and off-hands among them.
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

// The state a piece of the workbench's gear arrives in, one flag at a time:
// hammers driven in, upgrade slots passed, stars set. Each is asked for on its
// own, so a tester can name the exact configuration they want.
//
// Written straight into the state rather than rolled through Scroll() and
// StarForce(): the tester asked for the finished item, not for the odds.
Equip UpgradedState(const GameState& state, const EquipPrototype& proto,
                    const GearSetup& equips) {
  Equip built;
  built.set_equip_name(proto.name());
  built.set_remaining_upgrade_slots(proto.upgrade_slots());
  // Hammers first, so the wider shelf is the one the scrolls then fill.
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
  // Stars go on an item with nothing left to scroll, which is the rule the
  // upgrade screen holds to as well -- so --sf without --scrolled leaves an
  // item that has slots unstarred.
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

// Puts a copy of the named equip in the bag, or does nothing if the catalog
// has no such entry. Lets a GameState be built for a test without the game's
// data files behind it.
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

// Puts each of `names` on, from the row it lands on, so a Rogue's three reach
// three slots -- and whatever a later one displaces goes back to the bag for
// the tester to swap in. A piece the character is too low to wear, or whose
// branch is not theirs, is handed over anyway and stays in the bag: the four
// Cygnus shoulders are one slot fought over by four branches, and Equip itself
// asks neither question.
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

// Every stage's gear, the job's own last. A character standing below the
// level the top tier asks for is still armed off the tier under it: a Hero's
// Frozen axe opens at 120, so a Lv110 one would otherwise meet a boss with
// nothing to swing.
//
// Only the slots the job's own gear names are filled this way, and only when
// the level cannot reach what it offers there. A Bandit is not handed the
// Rogue's throwing stars because the branch stopped carrying them, and a
// character at the top of their advancement carries no spares at all.
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
      // Two of one slot in one stage is a choice the workbench offers -- the
      // Rogue's dagger and claw -- so a stage is filtered whole rather than
      // item by item.
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

// The only universal armour there is, so it fits whoever the workbench is.
// The gloves and the boots ask for level 140, which a 3rd job standing at 100
// carries rather than wears.
std::vector<std::string> FrozenArmour() {
  return {"frozen_hat",  "frozen_top",    "frozen_bottom",
          "frozen_cape", "frozen_gloves", "frozen_boots"};
}

// The level the Chaos Root Abyss opens at, and so the earliest anybody can own
// what its Pieces buy. The gear itself is worn at 150; nothing pays for a
// piece of it until 200, and a character dressed here is one the game could
// really have produced.
constexpr int kRootAbyssLevel = 200;

// The Root Abyss set the branch wears, worn over the Frozen tier: three pieces
// of armour and the weapon, which supersede the Frozen hat, top, bottom and
// weapon. Nothing here fills the off-hand, so the Frozen secondary stays on
// and the Frozen set keeps paying at four pieces.
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

// The Root Abyss weapon a 4th job swings: the same line's choice the Frozen
// tier makes in WorkbenchGearFor. Empty for anybody below the 4th job, who
// reaches level 200 only if a tester asks for it by hand.
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

// What the bosses pay, which is the only thing that fills the accessory and
// pocket slots. A boss drop is a long way to walk for a screen, so the
// workbench starts in it -- and the crystal asks for level 110, which a 3rd
// job standing at 100 carries rather than wears.
//
// Two slots hold a pair, Pink Bean's alternate for the eye and the pocket
// after the piece it supersedes: worn in this order, a 3rd job at 100 keeps
// the older one and a 4th at the cap swaps to the newer.
std::vector<std::string> BossAccessories() {
  return {"aquatic_letter_eye_accessory",
          "black_bean_mark",
          "condensed_power_crystal",
          "stone_of_eternal_life",
          "pink_holy_cup",
          "silver_blossom_ring",
          "chaos_horntail_necklace",
          "dominator_pendant",
          "dea_sidus_earring",
          "will_o_the_wisps",
          "royal_black_metal_shoulder",
          "golden_clover_belt",
          "crystal_ventus_badge"};
}

// What the shop's own Equips shelf fills the same slots with. Handed over
// rather than bought, like everything else here: the workbench is a character
// who already went shopping. The Meister Ring asks for 140, which a 3rd job
// carries rather than wears.
//
// The four Cygnus shoulders come last, after the boss drop they supersede:
// each names one branch, so whichever the workbench is wears one of them and
// carries the other three. `cygnus_shoulders` leaves them out, which is what
// a character measured against a boss roster wants -- the shoulder is bought
// with a token off Cygnus, and Cygnus is the fight nobody has won yet.
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

// Passed as `unspent_stage` to spend every point the climb earns.
constexpr int kSpendEveryStage = 0;

// Arcane River opens at 200, and it opens with a symbol in hand: without one
// the first map there would take the full penalty, which is a wall rather than
// an introduction.
constexpr int kArcaneRiverLevel = 200;
constexpr char kStarterSymbol[] = "symbol_vanishing_journey";

// Puts on the Arcane Symbol the climb past 200 handed over. Worn rather than
// carried: a symbol in the bag is worth no Arcane Force, and a workbench at
// the cap is standing on the maps that ask for it.
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

// Climbs to `level` the way a player gets there, taking each advancement in
// `path` as it is offered. Thirty hours of grinding, handed over.
//
// AP is always spent, into the primary stat: a hundred points in the pool is a
// hundred keypresses between the tester and the screen they came for. SP is
// spent below `unspent_stage` only, leaving the book they are standing in --
// usually the question -- to spend by hand.
void GrowTo(GameState& state, int level, const std::vector<Job>& path,
            int unspent_stage) {
  CharacterInstance& character = state.character;
  int taken = 0;
  while (character.proto().level() < level) {
    int before = character.proto().level();
    character.LevelUp();
    GrantLevelRewards(state, before, character.proto().level());
    if (character.CanAdvanceJob() && taken < static_cast<int>(path.size())) {
      character.AdvanceJob(path[taken++]);
    }
    // After the advancement, not before: it puts every allocated point back in
    // the pool and re-spends it for the new job.
    while (character.AllocateStat(PrimaryStatField(character.proto().job()))) {
    }
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      int stage = StageForAdvancement(entry.second.job_advancement());
      if (unspent_stage != kSpendEveryStage && stage >= unspent_stage) {
        continue;
      }
      while (character.LearnSkill(entry.second)) {
      }
    }
  }
}

// The level a character stops at when nothing names one: the top of the job's
// own band, held to the cap the EXP table pays up to. The last advancement the
// game has written has no band above it -- the 6th job's level is a number in
// the table and nothing else -- so that one climbs to the cap instead.
int LevelForJob(JobAdvancement advancement, int level) {
  if (level > 0) {
    return level;
  }
  int stage = StageForAdvancement(advancement);
  return stage >= kLastJobStage
             ? kTrialLevelCap
             : std::min(NextAdvancementLevel(stage), kTrialLevelCap);
}

// Climbs into `advancement`: the job it names, having taken every earlier
// advancement on the way to it. `level` is where the climb stops, or 0 for the
// last level before the next advancement would be offered.
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
  // The job's own gear, worn rather than carried, since there is no
  // advancement moment here to put it on at -- and the stages under it for
  // whatever the level cannot reach yet.
  WearThePath(state, path, equips);
  // The Frozen set on top, from the 3rd job up. It drops rather than sells, so
  // a workbench is the only character that will ever be seen in the whole of
  // it -- and every piece is inside a 3rd job's level 100, which is what makes
  // their four slots four. A 4th job adds the two the token shelf armed them
  // with above, for six.
  if (stage >= 3) {
    WearAll(state, FrozenArmour(), equips);
    WearAll(state, BossAccessories(), equips);
    WearAll(state, ShopAccessories(cygnus_shoulders), equips);
  }
  // Last, so it displaces the Frozen pieces it supersedes rather than the
  // other way round.
  if (state.character.proto().level() >= kRootAbyssLevel) {
    WearAll(state, RootAbyssGear(state.character.proto().job()), equips);
  }
  WearStarterSymbol(state);
}

// A player starts armed and with nothing else: the Sword is worn rather than
// carried, so the bag really is empty.
void SeedPlay(GameState& state) {
  GiveEquip(state, "sword");
  if (!state.character.inventory().empty()) {
    state.character.Equip(0);
  }
  state.current_map = kHomeMap;
}

// What the workbench multiplies combat EXP by. High enough that the early
// levels go by while the tester watches, which is what makes the level-gated
// features reachable without farming for them.
constexpr int kTestExpMultiplier = 5;

// How many of each token the workbench opens with: enough to buy a shelf's
// worth and still have one left to buy with.
constexpr int kTestTokens = 20;

// A full stack of spell traces -- 30,000 is the item's own max_stack, so this
// is one row of the Etc tab and the most the tester can be handed without a
// second. Carried rather than bought: the shop counts them out 5,000 meso at a
// time, which is a long walk to reach the scroll screen.
constexpr int kTestSpellTraces = 30000;

// `advancement`'s job name as a username: letters, digits and spaces only, so
// "I/L Arch Mage" arrives as "IL Arch Mage".
std::string UsernameFor(JobAdvancement advancement) {
  std::string name;
  for (char c : ShortJobName(JobForAdvancement(advancement))) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ') {
      name += c;
    }
  }
  return name.substr(0, kMaxUsernameLength);
}

// Spare Vanishing Journey symbols the workbench carries. A symbol levels by
// absorbing duplicates and its first level asks for 12, so this is one level
// up on the Symbols tab and change -- enough to watch the ladder move without
// farming Vanishing Journey for it.
constexpr int kTestSymbols = 15;

// The most cubes one workbench slot will take reaching for its rank. Rare to
// Legendary is about 65 rolls on average; this is far enough past that the
// guard never fires in practice.
constexpr int kMaxSeedCubes = 100000;

void GiveSymbols(GameState& state) {
  for (int i = 0; i < kTestSymbols; ++i) {
    GiveEquip(state, kStarterSymbol);
  }
}

// Potential on the workbench's gear. Every rank is meant to be on screen at
// once, so the cubeable slots are shuffled and dealt the four ranks in turn
// rather than each rolling its own -- a run where nothing came out Legendary
// would leave a quarter of the display untested.
//
// A rank is reached by cubing until the item arrives at it, which is how a
// player reaches one too: the odds are short but the loop is only tens of
// rolls, since nothing here is paying for them.
void SeedPotentials(GameState& state) {
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, EquipInstance>& kv :
       state.character.equipped()) {
    if (kv.second.CanCube()) {
      slots.push_back(kv.first);
    }
  }
  std::shuffle(slots.begin(), slots.end(), state.rng);

  constexpr PotentialRank kRanks[] = {POTENTIAL_RANK_RARE, POTENTIAL_RANK_EPIC,
                                      POTENTIAL_RANK_UNIQUE,
                                      POTENTIAL_RANK_LEGENDARY};
  for (size_t i = 0; i < slots.size(); ++i) {
    const PotentialRank want = kRanks[i % std::size(kRanks)];
    // Guarded rather than trusted to the odds: a cube that stopped ranking up
    // would otherwise hang the workbench on startup.
    for (int tries = 0; tries < kMaxSeedCubes; ++tries) {
      if (!state.character.CubeWorn(slots[i], CubeType::kRed)) {
        break;
      }
      const std::map<EquipSlot, EquipInstance>::const_iterator it =
          state.character.equipped().find(slots[i]);
      if (it->second.potential().rank() >= want) {
        break;
      }
    }
  }
}

// The workbench. Everything here exists to reach a screen without playing up
// to it. `chosen` is --job: unset takes kTestAdvancement and buys its whole
// book, so the default workbench is finished rather than half-built.
void SeedTest(GameState& state, const TestOptions& test) {
  state.exp_multiplier = kTestExpMultiplier;

  // Enough to buy anything the shop stocks, several times over, so the buying
  // screens can be exercised without grinding for the meso first. A hundred
  // billion because star force is what really spends it: one attempt at the
  // top of the ladder runs to nine figures, so a billion bought a tester about
  // sixteen presses of the button.
  state.character.AddMeso(100000000000);

  // The ladder in a bag. Skipped when the catalog has no such item, as every
  // other piece of seeding is.
  std::map<std::string, ItemPrototype>::const_iterator level_up =
      state.items.find("level_up");
  if (level_up != state.items.end()) {
    state.character.AddStackable(level_up->second, kTestLevelUpItems);
  }

  std::map<std::string, ItemPrototype>::const_iterator trace =
      state.items.find("spell_trace");
  if (trace != state.items.end()) {
    state.character.AddStackable(trace->second, kTestSpellTraces);
  }

  // A handful of every token, so the shop's token shelves can be bought from
  // without farming the mobs that drop them.
  for (const std::pair<const std::string, ItemPrototype>& entry : state.items) {
    if (!entry.second.currency_mark().empty()) {
      state.character.AddStackable(entry.second, kTestTokens);
    }
  }

  bool chose_job = test.job != JOB_ADVANCEMENT_UNSPECIFIED;
  if (!chose_job) {
    // Worn straight away: it is what the character holds until the climb below
    // hands them their job's weapon, and a catalog without that weapon -- a
    // test's, say -- leaves them holding this rather than nothing.
    GiveEquip(state, "sword");
    if (!state.character.inventory().empty()) {
      state.character.Equip(0);
    }
  }
  // Only the book the job is standing in answers to --skills. The ones behind
  // it are bought either way: they are not what the tester picked the job for,
  // and leaving them unbought would put two allocation screens between them
  // and the one they came for.
  JobAdvancement advancement = chose_job ? test.job : kTestAdvancement;
  // Named after the job it was built for, so several workbenches in a party
  // are told apart without anybody typing a name.
  state.character.SetUsername(UsernameFor(advancement));
  // --skills=0 leaves the book the character stands in unbought, held to the
  // last one SP buys: a 5th job's is bought with V Points, and leaving it is
  // what would put the tester in front of an empty pool.
  int unspent = std::min(StageForAdvancement(advancement), kLastSpJobStage);
  GrowToJob(state, advancement, test.level,
            test.skills == TestSkills::kZero ? unspent : kSpendEveryStage,
            test.equips);

  // Everything above dresses the character; nothing is meant to be carried.
  // What the climb leaves in the bag is the leftovers -- gear a level gate
  // says is carried not worn, the branches' shoulders the workbench is not,
  // the weapon a later one displaced -- and a tester opening the Equip tab
  // should see what they put there, not that.
  state.character.ClearEquipInventory();
  GiveSymbols(state);
  SeedPotentials(state);

  // The weakest hunting ground there is; the tester picks anywhere else from
  // the map select.
  state.current_map = "right_around_lith_harbor";
}

// What the ceiling is left holding. The climb's whole income is spent by the
// time it gets here -- see max_character.cc for the arithmetic -- and this is
// the change in the pocket rather than a purse to shop with.
constexpr int64_t kMaxLeftoverMeso = 50000000;

// Every permanent potion the character's level has opened. Each is paid for
// at its own price, so the purse the mode leaves behind is the change and not
// the price of a potion the level cannot reach yet. Switched on, because a
// player who bought one is using it.
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

// The same lines on every piece of one kind. Written rather than cubed for:
// what a real sheet holds is luck, and a fight measured against a character
// who is a little different every run says nothing. See MaxPotentialFor.
void DressMaxPotentials(GameState& state, const MaxGear& gear) {
  const StatField primary = PrimaryStatField(state.character.proto().job());
  std::vector<EquipSlot> slots;
  for (const std::pair<const EquipSlot, EquipInstance>& worn :
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

// The ceiling: the character a player who spent well is standing in at this
// level. Everything is written outright rather than played for, and every
// number is priced against what the climb pays by then -- max_character.cc
// carries the arithmetic band by band.
//
// Nothing of the workbench is here. No purse to spend, no Level-Up items, no
// EXP bonus and no spare gear: a fight measured against this character has to
// be measured against one the game could really produce.
void SeedMax(GameState& state, const TestOptions& options) {
  // The same default the workbench takes: the top of the line as far as the
  // game is written, which is where a boss roster is measured from.
  const JobAdvancement advancement = options.job != JOB_ADVANCEMENT_UNSPECIFIED
                                         ? options.job
                                         : kTestAdvancement;
  const int level = LevelForJob(advancement, options.level);
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
  // The leftovers of the climb: a piece a level gate says is carried rather
  // than worn, and the weapons a later one displaced.
  state.character.ClearEquipInventory();

  DressMaxPotentials(state, gear);
  SpendMaxHyperStats(state.character);
  if (state.character.inner_ability_unlocked()) {
    const StatField primary = PrimaryStatField(state.character.proto().job());
    state.character.SetAbility(MaxAbilityPreset(StatPreset::kFarming, primary),
                               StatPreset::kFarming);
    state.character.SetAbility(MaxAbilityPreset(StatPreset::kBossing, primary),
                               StatPreset::kBossing);
  }
  BuyMaxConsumables(state);
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
                     std::map<std::string, EquipSet> sets)
    : equips(std::move(equips_arg)),
      scrolls(std::move(scrolls_arg)),
      items(std::move(items_arg)),
      mobs(std::move(mobs_arg)),
      maps(std::move(maps_arg)),
      skills(std::move(skills_arg)),
      equip_sets(std::move(sets)),
      rng(seed.has_value() ? *seed : std::random_device{}()),
      // Both modes start at level 1. The workbench used to start at 10,
      // standing at its first advancement, but the game reveals itself a
      // level at a time now and starting part way up would skip the half of
      // it worth watching. SeedTest hands it Level-Up items instead, so a
      // tester climbs the ladder on demand rather than beginning above it.
      character(rng, MakeBaseBeginnerProto()),
      created_unix_seconds(static_cast<int64_t>(std::time(nullptr))) {
  if (mode == GameMode::kTest) {
    SeedTest(*this, test);
  } else if (mode == GameMode::kMax) {
    SeedMax(*this, test);
  } else {
    SeedPlay(*this);
  }
  character.UseEquipSets(equip_sets);
}

void GrantLevelRewards(GameState& state, int from_level, int to_level) {
  // Paid for every level in the span, whether or not the character can spend
  // it yet: Inner Ability opens at 160 onto a pool the climb has been filling
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

}  // namespace ms
