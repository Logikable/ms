#include "analysis/yardstick.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "src/character/character_stats.h"
#include "src/character/skill_placement.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

EquipPrototype Sword(AttackSpeed speed, int attack,
                     const std::string& name = "Sword") {
  EquipPrototype sword;
  sword.set_name(name);
  sword.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.set_attack_speed(speed);
  // Both halves, so the swing lands whatever job the starting character is.
  sword.mutable_base_stats()->set_attack(attack);
  sword.mutable_base_stats()->set_magic_attack(attack);
  return sword;
}

Skill SlashBlast() {
  Skill slash;
  slash.set_name("Slash Blast");
  slash.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(slash, JOB_ADVANCEMENT_SWORDMAN);
  slash.set_max_level(20);
  slash.set_max_enemies(6);
  slash.mutable_base()->set_skill_pct(1.83);
  return slash;
}

// A character standing on a field of snails with a sword in hand and one
// swing in their book: enough of a fight that a yardstick has something to
// measure.
//
// The skill is not decoration. A strand is looked up by NAME in the catalog,
// so the bare poke every character has carries none -- a character with an
// empty book has an empty yardstick and ranks every candidate equal.
std::unique_ptr<GameState> ArmedOnAField(
    AttackSpeed speed = ATTACK_SPEED_AVERAGE, int attack = 100) {
  Skill slash = SlashBlast();
  auto state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{},
      std::map<std::string, Mob>{{"snail", SnailMob()}},
      std::map<std::string, MapData>{{"field", SnailMap()}},
      std::map<std::string, Skill>{{"slash_blast", slash}});
  state->current_map = "field";
  state->character.PickUp(
      std::make_unique<EquipInstance>(Sword(speed, attack)));
  state->character.Equip(0);
  while (state->character.proto().job_stage() < 1) {
    if (state->character.CanAdvanceJob()) {
      state->character.AdvanceJob(JOB_SWORDMAN);
    } else {
      state->character.LevelUp();
    }
  }
  while (state->character.sp(1) < 1) {
    state->character.LevelUp();
  }
  state->character.LearnSkill(slash, 1);
  return state;
}

// --- YardstickFor ---

// The fallback: a character below the first boss has no fight to aim at, and
// still has to rank gear by damage rather than by nothing.
TEST(YardstickForTest, StandsInAMonsterOfTheirOwnLevel) {
  std::unique_ptr<GameState> state = ArmedOnAField();
  state->character.LevelUp();
  state->character.LevelUp();

  Yardstick yard = YardstickFor(*state);
  EXPECT_EQ(yard.target.level(), state->character.proto().level());
  EXPECT_FALSE(yard.target.boss());
  // Nothing bought is wasted against it: no defence to be ignored.
  EXPECT_EQ(yard.target.pdr(), 0);
}

TEST(YardstickForTest, CarriesTheSwingsTheFightReallyLands) {
  std::unique_ptr<GameState> state = ArmedOnAField();

  Yardstick yard = YardstickFor(*state);
  ASSERT_FALSE(yard.strands.empty());
  for (const Strand& strand : yard.strands) {
    EXPECT_NE(strand.swing, nullptr);
    EXPECT_GT(strand.per_second, 0.0) << "a strand nothing swings is no strand";
  }
}

// The rate is SOLVED off the played fight, so a weapon swung twice as often
// has to come back at a higher rate for the same swing.
TEST(YardstickForTest, AFasterWeaponLandsTheSameSwingMoreOften) {
  std::unique_ptr<GameState> slow = ArmedOnAField(ATTACK_SPEED_SLOWER);
  std::unique_ptr<GameState> fast = ArmedOnAField(ATTACK_SPEED_FASTEST_3);

  Yardstick slow_yard = YardstickFor(*slow);
  Yardstick fast_yard = YardstickFor(*fast);
  ASSERT_FALSE(slow_yard.strands.empty());
  ASSERT_FALSE(fast_yard.strands.empty());
  EXPECT_GT(fast_yard.strands.front().per_second,
            slow_yard.strands.front().per_second);
}

// --- WorthOf ---

TEST(WorthOfTest, RisesWithTheAttackItIsHanded) {
  std::unique_ptr<GameState> state = ArmedOnAField();
  Yardstick yard = YardstickFor(*state);
  DerivedStats derived = DerivedStatsFor(state->character, state->skills);
  PassiveOffense passives = PassiveOffenseFor(derived);

  EquipStats worn = TotalEquipStats(state->character, derived);
  EquipStats better = worn;
  better.set_attack(worn.attack() + 50);

  double before = WorthOf(*state, yard, worn, passives);
  EXPECT_GT(before, 0.0);
  EXPECT_GT(WorthOf(*state, yard, better, passives), before);
}

// A character with no attack at all ranks every candidate equal, which is
// correct: nothing they buy changes a damage they cannot deal.
TEST(WorthOfTest, IsNothingWithoutAStrand) {
  std::unique_ptr<GameState> state = ArmedOnAField();
  DerivedStats derived = DerivedStatsFor(state->character, state->skills);

  Yardstick bare;
  bare.target = YardstickFor(*state).target;
  EXPECT_EQ(WorthOf(*state, bare, TotalEquipStats(state->character, derived),
                    PassiveOffenseFor(derived)),
            0.0);
}

// --- HeldYardstick ---
//
// The whole point of holding one is that it is NOT re-taken per purchase, so
// a kit change the key misses serves a stale yardstick to every candidate for
// the rest of the pass -- and the numbers stay plausible while being about a
// character who is no longer there. Each of these moves one thing the key
// claims to watch.

TEST(HeldYardstickTest, RetakesWhenTheCharacterLevels) {
  std::unique_ptr<GameState> state = ArmedOnAField();
  HeldYardstick held;
  int before = held.For(*state).target.level();

  state->character.LevelUp();
  EXPECT_EQ(held.For(*state).target.level(), before + 1);
}

TEST(HeldYardstickTest, RetakesWhenTheWeaponChanges) {
  std::unique_ptr<GameState> state = ArmedOnAField(ATTACK_SPEED_SLOWER);
  HeldYardstick held;
  ASSERT_FALSE(held.For(*state).strands.empty());
  double before = held.For(*state).strands.front().per_second;

  // A different prototype, as a real swap is: the key names what is worn, so
  // two items sharing a name are one kit to it.
  state->character.PickUp(std::make_unique<EquipInstance>(
      Sword(ATTACK_SPEED_FASTEST_3, 100, "Quick Sword")));
  state->character.Equip(0);
  EXPECT_GT(held.For(*state).strands.front().per_second, before);
}

TEST(HeldYardstickTest, HoldsWhatItTookWhileTheKitStands) {
  std::unique_ptr<GameState> state = ArmedOnAField();
  HeldYardstick held;
  const Yardstick& first = held.For(*state);
  int level = first.target.level();
  std::size_t strands = first.strands.size();

  // Meso is not the kit, so it must not cost a fight to re-play.
  state->character.AddMeso(1'000'000);
  const Yardstick& again = held.For(*state);
  EXPECT_EQ(again.target.level(), level);
  EXPECT_EQ(again.strands.size(), strands);
}

}  // namespace
}  // namespace ms
