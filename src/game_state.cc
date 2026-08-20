#include "src/game_state.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>

#include "src/character/character.h"
#include "src/character/exp_table.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

namespace {

// The level-1 Beginner every character starts from, before any leveling.
Character MakeBaseBeginnerProto() {
  Character proto;
  proto.set_level(1);
  proto.set_job(JOB_BEGINNER);
  proto.set_ap(0);
  proto.mutable_allocated_stats()->set_str(13);
  proto.mutable_allocated_stats()->set_dex(4);
  proto.mutable_allocated_stats()->set_int_(4);
  proto.mutable_allocated_stats()->set_luk(4);
  proto.mutable_allocated_stats()->set_hp(50);
  proto.mutable_allocated_stats()->set_mp(15);
  return proto;
}

// How many Level-Up items the workbench opens with. Enough to carry the
// character it starts as past every gate in the unlock table, and to keep
// earning AP and SP past kTrialLevelCap, since LevelUp is not bounded by it.
constexpr int kTestLevelUpItems = 199;

// Where the workbench's character stands when --job says nothing: the top of
// the Hero line as far as it is written, holding the whole of what this game
// has to hand out. It moves up with the line rather than staying put -- a
// workbench with an advancement still waiting is one the tester has to finish
// before they can look at anything.
constexpr JobAdvancement kTestAdvancement = JOB_ADVANCEMENT_HERO;

// Everything the workbench dresses a job in: the best of each thing it carries
// that its starting level can wear. Advancing hands over gear for the level it
// happens at, and the workbench starts at the TOP of an advancement, so a level
// 60 Fighter would otherwise swing the axe they were given at 30.
//
// Two weapons means the better one by //analysis:weapon_sim, but a Rogue gets
// all three: which of the dagger and the claw is held decides what they swing.
std::vector<std::string> WorkbenchGearFor(Job job) {
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

// Puts a copy of the named equip in the bag, or does nothing if the catalog
// has no such entry. Lets a GameState be built for a test without the game's
// data files behind it.
void GiveEquip(GameState& state, const std::string& name) {
  std::map<std::string, EquipPrototype>::const_iterator it =
      state.equips.find(name);
  if (it == state.equips.end()) {
    return;
  }
  state.character.PickUp(std::make_unique<EquipInstance>(it->second));
}

// Puts each of `names` on, from the row it lands on, so a Rogue's three reach
// three slots -- and whatever a later one displaces goes back to the bag for
// the tester to swap in.
void WearAll(GameState& state, const std::vector<std::string>& names) {
  for (const std::string& name : names) {
    int row = static_cast<int>(state.character.inventory().size());
    GiveEquip(state, name);
    if (static_cast<int>(state.character.inventory().size()) > row) {
      state.character.Equip(row);
    }
  }
}

// The only armour there is. Universal, so it fits whoever the workbench is.
std::vector<std::string> FrozenArmour() {
  return {"frozen_hat", "frozen_top", "frozen_bottom", "frozen_cape"};
}

// Passed as `unspent_stage` to spend every point the climb earns.
constexpr int kSpendEveryStage = 0;

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
    character.LevelUp();
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

// Climbs to the top of `advancement`: the job it names, at the last level
// before the next advancement would be offered, having taken every earlier
// advancement on the way to it.
void GrowToJob(GameState& state, JobAdvancement advancement,
               int unspent_stage) {
  Job job = JobForAdvancement(advancement);
  int stage = StageForAdvancement(advancement);
  std::vector<Job> path;
  for (int i = 1; i <= stage; ++i) {
    path.push_back(JobForAdvancement(AdvancementForJobStage(job, i)));
  }
  // Held to the cap: the 5th advancement's level is above it, so the 4th job
  // stops where the EXP table does rather than climbing past the end.
  GrowTo(state, std::min(NextAdvancementLevel(stage), kTrialLevelCap), path,
         unspent_stage);
  // The job's own gear, worn rather than carried, since there is no
  // advancement moment here to put it on at.
  WearAll(state, WorkbenchGearFor(job));
  // The Frozen set on top, from the 3rd job up. It drops rather than sells, so
  // a workbench is the only character that will ever be seen in the whole of
  // it -- and every piece is inside a 3rd job's level 100, which is what makes
  // their four slots four. A 4th job adds the two the token shelf armed them
  // with above, for six.
  if (stage >= 3) {
    WearAll(state, FrozenArmour());
  }
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

// The workbench. Everything here exists to reach a screen without playing up
// to it. `chosen` is --job: unset takes kTestAdvancement and buys its whole
// book, so the default workbench is finished rather than half-built.
void SeedTest(GameState& state, JobAdvancement chosen) {
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

  bool chose_job = chosen != JOB_ADVANCEMENT_UNSPECIFIED;
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
  // Only the book the chosen job is standing in is left unspent. The ones
  // behind them are not what --job was asked for, and leaving those unbought
  // would put the tester through two allocation screens to reach one.
  GrowToJob(state, chose_job ? chosen : kTestAdvancement,
            chose_job ? StageForAdvancement(chosen) : kSpendEveryStage);

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
                     JobAdvancement test_job, std::optional<unsigned int> seed)
    : equips(std::move(equips_arg)),
      scrolls(std::move(scrolls_arg)),
      items(std::move(items_arg)),
      mobs(std::move(mobs_arg)),
      maps(std::move(maps_arg)),
      skills(std::move(skills_arg)),
      rng(seed.has_value() ? *seed : std::random_device{}()),
      // Both modes start at level 1. The workbench used to start at 10,
      // standing at its first advancement, but the game reveals itself a
      // level at a time now and starting part way up would skip the half of
      // it worth watching. SeedTest hands it Level-Up items instead, so a
      // tester climbs the ladder on demand rather than beginning above it.
      character(rng, MakeBaseBeginnerProto()),
      created_unix_seconds(static_cast<int64_t>(std::time(nullptr))) {
  if (mode == GameMode::kTest) {
    SeedTest(*this, test_job);
  } else {
    SeedPlay(*this);
  }
}

}  // namespace ms
