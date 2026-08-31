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

#include "src/character/arcane_force.h"
#include "src/character/character.h"
#include "src/character/exp_table.h"
#include "src/character/honor.h"
#include "src/character/job_name.h"
#include "src/item/equip_instance.h"
#include "src/item/equip_stats.h"
#include "src/item/inventory.h"
#include "src/item/item.h"
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
    // The 3rd jobs, at level 100 -- the top of the game, so this is the best
    // gear there is. Each takes the better of its line's two weapons on
    // //analysis:weapon_sim, and keeps its 2nd job's off-hand: a 3rd job opens
    // no new slot. The Crusader's axe beats the sword of the same tier on
    // Weapon Mastery's axe bonus alone.
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
    case JOB_FIRE_POISON_ARCH_MAGE:
      return {"frozen_staff", "frozen_metallic_book"};
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
                    const TestEquips& equips) {
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
  if (equips.stars > 0 && built.remaining_upgrade_slots() == 0 &&
      Supports(proto, UPGRADE_STAR_FORCE)) {
    built.set_stars(std::min(
        equips.stars, EquipTabItem::MaxStarsForLevel(proto.required_level())));
  }
  return built;
}

// Puts a copy of the named equip in the bag, or does nothing if the catalog
// has no such entry. Lets a GameState be built for a test without the game's
// data files behind it.
void GiveEquip(GameState& state, const std::string& name,
               const TestEquips& equips = TestEquips()) {
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
// the tester to swap in. A piece the character is too low to wear is handed
// over anyway, and stays in the bag until they are.
void WearAll(GameState& state, const std::vector<std::string>& names,
             const TestEquips& equips) {
  for (const std::string& name : names) {
    std::map<std::string, EquipPrototype>::const_iterator it =
        state.equips.find(name);
    if (it == state.equips.end()) {
      continue;
    }
    int row = static_cast<int>(state.character.inventory().size());
    GiveEquip(state, name, equips);
    if (static_cast<int>(state.character.inventory().size()) > row &&
        state.character.MeetsLevel(it->second)) {
      state.character.Equip(row);
    }
  }
}

// The only armour there is. Universal, so it fits whoever the workbench is.
// The gloves and the boots ask for level 140, which a 3rd job standing at 100
// carries rather than wears.
std::vector<std::string> FrozenArmour() {
  return {"frozen_hat",  "frozen_top",    "frozen_bottom",
          "frozen_cape", "frozen_gloves", "frozen_boots"};
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
std::vector<std::string> ShopAccessories() {
  return {"lightning_god_ring", "meister_ring", "gold_maple_leaf_emblem",
          "master_adventurer"};
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

// Climbs into `advancement`: the job it names, having taken every earlier
// advancement on the way to it. `level` is where the climb stops, or 0 for the
// last level before the next advancement would be offered.
void GrowToJob(GameState& state, JobAdvancement advancement, int level,
               int unspent_stage, const TestEquips& equips) {
  Job job = JobForAdvancement(advancement);
  int stage = StageForAdvancement(advancement);
  std::vector<Job> path;
  for (int i = 1; i <= stage; ++i) {
    path.push_back(JobForAdvancement(AdvancementForJobStage(job, i)));
  }
  // Held to the cap: the 5th advancement's level is above it, so the 4th job
  // stops where the EXP table does rather than climbing past the end.
  GrowTo(
      state,
      level > 0 ? level : std::min(NextAdvancementLevel(stage), kTrialLevelCap),
      path, unspent_stage);
  // The job's own gear, worn rather than carried, since there is no
  // advancement moment here to put it on at.
  WearAll(state, WorkbenchGearFor(job), equips);
  // The Frozen set on top, from the 3rd job up. It drops rather than sells, so
  // a workbench is the only character that will ever be seen in the whole of
  // it -- and every piece is inside a 3rd job's level 100, which is what makes
  // their four slots four. A 4th job adds the two the token shelf armed them
  // with above, for six.
  if (stage >= 3) {
    WearAll(state, FrozenArmour(), equips);
    WearAll(state, BossAccessories(), equips);
    WearAll(state, ShopAccessories(), equips);
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

// The awkward items, for the upgrade screens: a fully scrolled weapon at high
// star force, a trace to recover, and the base item recovering it consumes.
void GiveUpgradeItems(GameState& state) {
  std::map<std::string, EquipPrototype>::const_iterator fafnir =
      state.equips.find("fafnir_mistilteinn");
  if (fafnir == state.equips.end()) {
    return;
  }
  Equip scrolled;
  scrolled.set_equip_name("Fafnir Mistilteinn");
  scrolled.set_remaining_upgrade_slots(0);
  scrolled.set_scroll_successes(8);
  scrolled.set_stars(20);
  scrolled.mutable_scroll_stats()->set_attack(40);
  scrolled.mutable_scroll_stats()->set_str(16);
  state.character.PickUp(
      std::make_unique<EquipInstance>(fafnir->second, scrolled));

  // The same at 22 stars but destroyed: the source trace for recovery.
  Equip trace = scrolled;
  trace.set_stars(22);
  state.character.PickUp(std::make_unique<EquipTrace>(fafnir->second, trace));

  // Fresh Fafnir -- the base item that recovering the trace consumes.
  state.character.PickUp(std::make_unique<EquipInstance>(fafnir->second));
}

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
    // The rungs below the default job's weapon, to swap between on the equip
    // screens. A job named by --job brings only its own. The first is worn
    // straight away: it is what the character holds until the climb below
    // hands them their job's weapon, and a catalog without that weapon -- a
    // test's, say -- leaves them holding this rather than nothing.
    GiveEquip(state, "sword");
    if (!state.character.inventory().empty()) {
      state.character.Equip(0);
    }
    GiveEquip(state, "long_sword");
    GiveEquip(state, "machete");
  }
  GiveUpgradeItems(state);
  // Only the book the job is standing in answers to --skills. The ones behind
  // it are bought either way: they are not what the tester picked the job for,
  // and leaving them unbought would put two allocation screens between them
  // and the one they came for.
  JobAdvancement advancement = chose_job ? test.job : kTestAdvancement;
  // Named after the job it was built for, so several workbenches in a party
  // are told apart without anybody typing a name.
  state.character.SetUsername(UsernameFor(advancement));
  GrowToJob(state, advancement, test.level,
            test.skills == TestSkills::kZero ? StageForAdvancement(advancement)
                                             : kSpendEveryStage,
            test.equips);

  // The weakest hunting ground there is; the tester picks anywhere else from
  // the map select.
  state.current_map = "right_around_lith_harbor";
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
