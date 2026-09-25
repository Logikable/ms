// --job must give the best gear its level can wear. Weapons named in a switch
// go stale as soon as a tier or branch is added.
#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/character/character.h"
#include "src/character/exp_table.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/item.h"
#include "src/proto_loader.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "src/protos/skill.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

// Every advancement a character can start at the top of, read from the enum
// descriptor instead of listed by hand. A hand-written list only gets new jobs
// when someone remembers, which is how a new job ends up with no weapon.
std::vector<JobAdvancement> EveryAdvancement() {
  std::vector<JobAdvancement> all;
  for (int i = 1; i <= JobAdvancement_MAX; ++i) {
    JobAdvancement advancement = static_cast<JobAdvancement>(i);
    // The workbench can't start at any of these three: the common nodes' book,
    // the book every character starts with, and the link skills, which belong
    // to no job.
    if (advancement == JOB_ADVANCEMENT_COMMON ||
        advancement == JOB_ADVANCEMENT_BEGINNER ||
        advancement == JOB_ADVANCEMENT_LINK) {
      continue;
    }
    all.push_back(advancement);
  }
  return all;
}

// The level where the two token tiers can first be owned. That is where the
// bosses that pay for them open, not where the gear can be worn; see
// OwnedFromLevel in game_state.h.
constexpr int kRootAbyssOpens = 200;
constexpr int kAbsoLabOpens = 210;

// Every piece of the set has "Frozen" in its name, as the player sees. The
// armour has no set_family; only the two pieces the token shelf sells do, where
// the family stops a second one from being bought.
bool IsFrozen(const EquipPrototype& proto) {
  return proto.name().rfind("Frozen ", 0) == 0;
}

class WorkbenchGearTest : public ::testing::Test {
 protected:
  // Loaded once for the whole suite: the catalogs don't change during a test,
  // and parsing them per test was most of this file's run time.
  static void SetUpTestSuite() {
    std::string err;
    runfiles_.reset(Runfiles::CreateForTest(&err));
    ASSERT_NE(runfiles_, nullptr) << err;
    equips_ = LoadTextProtoDir<EquipPrototype>(Dir("equip"));
    scrolls_ = LoadTextProtoDir<Scroll>(Dir("scrolls"));
    items_ = LoadTextProtoDir<ItemPrototype>(Dir("items"));
    mobs_ = LoadTextProtoDir<Mob>(Dir("mobs"));
    maps_ = LoadTextProtoDir<MapData>(Dir("maps"));
    skills_ = LoadTextProtoDir<Skill>(Dir("skills"));
    ASSERT_FALSE(equips_.empty());
  }

  static std::string Dir(const std::string& name) {
    return runfiles_->Rlocation("ms/data/" + name);
  }

  // `level` 0 puts the character at the top of their advancement, where --job
  // leaves them.
  static GameState Workbench(JobAdvancement advancement, int level = 0) {
    TestOptions options;
    options.job = advancement;
    options.level = level;
    return GameState(equips_, scrolls_, items_, mobs_, maps_, skills_,
                     GameMode::kTest, options);
  }

  static std::unique_ptr<Runfiles> runfiles_;
  static std::map<std::string, EquipPrototype> equips_;
  static std::map<std::string, Scroll> scrolls_;
  static std::map<std::string, ItemPrototype> items_;
  static std::map<std::string, Mob> mobs_;
  static std::map<std::string, MapData> maps_;
  static std::map<std::string, Skill> skills_;
};

std::unique_ptr<Runfiles> WorkbenchGearTest::runfiles_;
std::map<std::string, EquipPrototype> WorkbenchGearTest::equips_;
std::map<std::string, Scroll> WorkbenchGearTest::scrolls_;
std::map<std::string, ItemPrototype> WorkbenchGearTest::items_;
std::map<std::string, Mob> WorkbenchGearTest::mobs_;
std::map<std::string, MapData> WorkbenchGearTest::maps_;
std::map<std::string, Skill> WorkbenchGearTest::skills_;

// The required levels on `worn`'s ladder among the items this character could
// equip, highest first. OwnedFromLevel checks what CanEquip can't: a token tier
// waits for the fight that pays for it.
//
// A ladder is a slot family plus a type: the type alone would merge a Fighter's
// swords and axes, the slot alone all four armour pieces.
std::vector<int> TiersOnLadder(
    const CharacterInstance& character, const EquipPrototype& worn,
    const std::map<std::string, EquipPrototype>& equips) {
  std::vector<int> levels;
  for (const std::pair<const std::string, EquipPrototype>& entry : equips) {
    const EquipPrototype& proto = entry.second;
    if (BaseSlot(proto.equip_slot()) == BaseSlot(worn.equip_slot()) &&
        proto.equip_type() == worn.equip_type() && character.CanEquip(proto) &&
        character.proto().level() >= OwnedFromLevel(proto)) {
      levels.push_back(proto.required_level());
    }
  }
  std::sort(levels.rbegin(), levels.rend());
  return levels;
}

// Checks that the character carries the best item their level can wear on each
// ladder (a slot family that can hold several items at once). Anything less and
// the tester sees a weaker character than the game has.
void ExpectTopOfEveryLadder(
    const CharacterInstance& character,
    const std::map<std::string, EquipPrototype>& equips) {
  ASSERT_TRUE(character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON))
      << "nothing in hand at all";
  // The levels worn on each ladder, keyed by the slot family and type that
  // define it, so rings are only compared with rings.
  std::map<std::pair<EquipSlot, EquipType>, std::vector<int>> worn_levels;
  std::map<std::pair<EquipSlot, EquipType>, const EquipPrototype*> example;
  for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
       character.equipped()) {
    const EquipPrototype& proto = worn.second->prototype();
    std::pair<EquipSlot, EquipType> ladder{BaseSlot(proto.equip_slot()),
                                           proto.equip_type()};
    worn_levels[ladder].push_back(proto.required_level());
    example[ladder] = &proto;
  }
  for (std::pair<const std::pair<EquipSlot, EquipType>, std::vector<int>>&
           entry : worn_levels) {
    std::vector<int>& worn = entry.second;
    std::sort(worn.rbegin(), worn.rend());
    std::vector<int> offered =
        TiersOnLadder(character, *example[entry.first], equips);
    ASSERT_GE(offered.size(), worn.size())
        << "wearing more than the ladder offers";
    offered.resize(worn.size());
    EXPECT_EQ(worn, offered)
        << "the " << EquipSlot_Name(entry.first.first) << " a level "
        << character.proto().level() << " wears is not the best on offer";
  }
}

// The check above only looks at what is worn, so it never sees an empty slot.
// That is how every 2nd job ended up with no off-hand without any test failing.
// This checks that the slot is filled.
void ExpectOffHand(const CharacterInstance& character,
                   JobAdvancement advancement) {
  bool branched = StageForAdvancement(advancement) >= 2;
  EXPECT_EQ(character.equipped().count(EQUIP_SLOT_SECONDARY) == 1, branched)
      << "a secondary belongs to a branch, and a 1st job is not in one";
}

// The Frozen set drops and isn't sold, so the workbench is where it's seen. A
// 3rd job at 100 wears the four pieces in range; a 4th at 200 keeps three; a
// 5th at the cap wears AbsoLab instead.
void ExpectFrozenSet(const CharacterInstance& character,
                     JobAdvancement advancement) {
  int frozen = 0;
  for (const std::pair<const EquipSlot, const EquipInstance*>& worn :
       character.equipped()) {
    frozen += IsFrozen(worn.second->prototype()) ? 1 : 0;
  }
  int stage = StageForAdvancement(advancement);
  EXPECT_EQ(frozen, stage == 3 ? 4 : stage == 4 ? 3 : 0);
}

// Each token tier waits for its fight: Chaos Root Abyss at 200, Damien and
// Lotus at 210. So a 5th job at the cap wears AbsoLab, a 4th at 200 Root Abyss,
// and a 3rd neither.
void ExpectTokenTier(const CharacterInstance& character,
                     JobAdvancement advancement) {
  if (StageForAdvancement(advancement) < 3) {
    return;
  }
  const EquipSlot kSlots[] = {EQUIP_SLOT_HAT, EQUIP_SLOT_TOP, EQUIP_SLOT_BOTTOM,
                              EQUIP_SLOT_PRIMARY_WEAPON};
  int level = character.proto().level();
  for (EquipSlot slot : kSlots) {
    WornGear::const_iterator worn = character.equipped().find(slot);
    ASSERT_NE(worn, character.equipped().end())
        << EquipSlot_Name(slot) << " is empty";
    int tier = worn->second->prototype().required_level();
    SCOPED_TRACE(EquipSlot_Name(slot) + (" holds " + worn->second->name()));
    if (level >= kAbsoLabOpens) {
      EXPECT_EQ(tier, 160);
    } else if (level >= kRootAbyssOpens) {
      EXPECT_EQ(tier, 150);
    } else {
      EXPECT_LT(tier, 150);
    }
  }
}

// Checks everything the workbench gives a character, for every advancement. It
// is one loop instead of four because building a state is a whole climb, and
// all four checks need the same one.
TEST_F(WorkbenchGearTest, EveryJobIsDressedForItsBand) {
  for (JobAdvancement advancement : EveryAdvancement()) {
    SCOPED_TRACE(JobAdvancement_Name(advancement));
    GameState state = Workbench(advancement);
    const CharacterInstance& character = state.character;
    ExpectTopOfEveryLadder(character, equips_);
    ExpectOffHand(character, advancement);
    ExpectFrozenSet(character, advancement);
    ExpectTokenTier(character, advancement);
  }
}

// Boss accessories fill slots nothing else does. Zakum's eye accessory requires
// 100 and his crystal 110, so a 3rd job at 100 wears the first and carries the
// second.
TEST_F(WorkbenchGearTest, TheThirdJobUpWearsWhatTheBossesDrop) {
  GameState second = Workbench(JOB_ADVANCEMENT_BANDIT);
  EXPECT_EQ(second.character.equipped().count(EQUIP_SLOT_EYE_ACCESSORY), 0u);
  EXPECT_EQ(second.character.equipped().count(EQUIP_SLOT_FACE_ACCESSORY), 0u);

  GameState third = Workbench(JOB_ADVANCEMENT_BERSERKER);
  EXPECT_EQ(third.character.equipped().count(EQUIP_SLOT_EYE_ACCESSORY), 1u);
  EXPECT_EQ(third.character.equipped().count(EQUIP_SLOT_FACE_ACCESSORY), 0u);

  // At the cap it wears all nine Boss Accessory Set slots, preferring the later
  // piece where two fit one slot. The shoulder is the Cygnus one matching this
  // branch.
  GameState fourth = Workbench(JOB_ADVANCEMENT_DARK_KNIGHT);
  const WornGear& worn = fourth.character.equipped();
  const std::map<EquipSlot, std::string> kExpected = {
      {EQUIP_SLOT_EYE_ACCESSORY, "Papulatus Mark"},
      {EQUIP_SLOT_FACE_ACCESSORY, "Condensed Power Crystal"},
      {EQUIP_SLOT_POCKET, "Pink Holy Cup"},
      {EQUIP_SLOT_RING, "Silver Blossom Ring"},
      {EQUIP_SLOT_PENDANT, "Chaos Horntail Necklace"},
      {EQUIP_SLOT_PENDANT_2, "Dominator Pendant"},
      {EQUIP_SLOT_EARRINGS, "Will o' the Wisps"},
      {EQUIP_SLOT_SHOULDER, "Lionheart Battle Shoulder"},
      {EQUIP_SLOT_BELT, "Golden Clover Belt"},
      {EQUIP_SLOT_BADGE, "Crystal Ventus Badge"}};
  for (const std::pair<const EquipSlot, std::string>& want : kExpected) {
    ASSERT_EQ(worn.count(want.first), 1u) << EquipSlot_Name(want.first);
    EXPECT_EQ(worn.at(want.first)->prototype().name(), want.second);
  }
}

// A Hero advances at 100 but their Frozen axe requires 120, so in between they
// hold the tier below. --mode=max measures a boss at every level where one
// opens.
TEST_F(WorkbenchGearTest, TheFourthJobIsArmedAtEveryLevelABossOpensAt) {
  for (JobAdvancement advancement : EveryAdvancement()) {
    if (StageForAdvancement(advancement) != 4) {
      continue;
    }
    SCOPED_TRACE(JobAdvancement_Name(advancement));
    for (int level = 110; level <= kTrialLevelCap; level += 10) {
      GameState state = Workbench(advancement, level);
      SCOPED_TRACE(level);
      EXPECT_EQ(state.character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON), 1u)
          << "nothing in hand";
      EXPECT_EQ(state.character.equipped().count(EQUIP_SLOT_SECONDARY), 1u)
          << "no off-hand";
    }
  }
}

// The level the gear is checked against, so a change to the advancement levels
// shows up here directly instead of as a weapon that looks wrong.
TEST_F(WorkbenchGearTest, EachJobStartsAtTheTopOfItsOwnBand) {
  EXPECT_EQ(Workbench(JOB_ADVANCEMENT_ROGUE).character.proto().level(), 30);
  EXPECT_EQ(Workbench(JOB_ADVANCEMENT_BANDIT).character.proto().level(), 60);
  EXPECT_EQ(Workbench(JOB_ADVANCEMENT_BERSERKER).character.proto().level(),
            100);
  EXPECT_EQ(Workbench(JOB_ADVANCEMENT_DARK_KNIGHT).character.proto().level(),
            200);
  // The 5th job goes to the cap: it is the last advancement, so no band above
  // it stops it.
  EXPECT_EQ(Workbench(JOB_ADVANCEMENT_DARK_KNIGHT_V).character.proto().level(),
            kTrialLevelCap);
}

}  // namespace
}  // namespace ms
