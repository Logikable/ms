// Checks the workbench against the shipped catalogs rather than any one
// function: --job starts its character at the top of an advancement, and what
// it puts in their hand has to be the top of the ladder that level reaches.
//
// Named weapons in a switch rot the moment a tier or a branch is added -- the
// thief branches arrived holding level 30 gear at level 60 and nothing said
// so. This is what says so.
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

// Every advancement --job accepts, which is every one a character can stand at
// the top of.
// Every advancement in the enum, taken from the descriptor rather than listed:
// a hardcoded list is one a new job joins only when somebody remembers to add
// it, which is exactly how a new job comes to stand there unarmed.
std::vector<JobAdvancement> EveryAdvancement() {
  std::vector<JobAdvancement> all;
  for (int i = 1; i <= JobAdvancement_MAX; ++i) {
    all.push_back(static_cast<JobAdvancement>(i));
  }
  return all;
}

// Every piece of the set is named for it, which is what the player reads too.
// The armour carries no set_family -- only the two the token shelf sells do,
// where the family is what stops a second of one being bought.
bool IsFrozen(const EquipPrototype& proto) {
  return proto.name().rfind("Frozen ", 0) == 0;
}

class WorkbenchGearTest : public ::testing::Test {
 protected:
  void SetUp() override {
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

  std::string Dir(const std::string& name) {
    return runfiles_->Rlocation("ms/data/" + name);
  }

  // `level` 0 stands the character at the top of their advancement, which is
  // where --job leaves them.
  GameState Workbench(JobAdvancement advancement, int level = 0) {
    TestOptions options;
    options.job = advancement;
    options.level = level;
    return GameState(equips_, scrolls_, items_, mobs_, maps_, skills_,
                     GameMode::kTest, options);
  }

  // The required levels the catalog offers on `worn`'s own ladder among the
  // items this character could put on, highest first -- CanEquip asks about
  // their level and their job together, which is the same question the shop
  // asks.
  //
  // A ladder is a slot family and a type together. The type alone would put a
  // Fighter's swords and axes on one, which is the choice the workbench makes;
  // the slot alone would put all four pieces of armour on one, and armour
  // names no type at all. The family rather than the slot, because a character
  // wears four rings and the four are one ladder, not four.
  std::vector<int> TiersOnLadder(const CharacterInstance& character,
                                 const EquipPrototype& worn) {
    std::vector<int> levels;
    for (const std::pair<const std::string, EquipPrototype>& entry : equips_) {
      const EquipPrototype& proto = entry.second;
      if (BaseSlot(proto.equip_slot()) == BaseSlot(worn.equip_slot()) &&
          proto.equip_type() == worn.equip_type() &&
          character.CanEquip(proto)) {
        levels.push_back(proto.required_level());
      }
    }
    std::sort(levels.rbegin(), levels.rend());
    return levels;
  }

  std::unique_ptr<Runfiles> runfiles_;
  std::map<std::string, EquipPrototype> equips_;
  std::map<std::string, Scroll> scrolls_;
  std::map<std::string, ItemPrototype> items_;
  std::map<std::string, Mob> mobs_;
  std::map<std::string, MapData> maps_;
  std::map<std::string, Skill> skills_;
};

// The whole claim, one advancement at a time: the workbench arms its character
// with the best of each thing they carry that their level can wear. Anything
// less and the tester is looking at a weaker character than the game has --
// which is exactly what a stale entry in the switch produces.
//
// Asked a ladder at a time rather than an item at a time, because a family of
// slots holds several at once: a character wearing three of the four rings the
// catalog offers should be wearing the best three, and only one of those can
// be the best one.
TEST_F(WorkbenchGearTest, EveryJobWearsTheTopTierItsLevelReaches) {
  for (JobAdvancement advancement : EveryAdvancement()) {
    GameState state = Workbench(advancement);
    const CharacterInstance& character = state.character;
    SCOPED_TRACE(JobAdvancement_Name(advancement));
    EXPECT_TRUE(character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON))
        << "nothing in hand at all";
    // The levels worn on each ladder, keyed by the family and type that name
    // it, so the rings meet each other and nothing else.
    std::map<std::pair<EquipSlot, EquipType>, std::vector<int>> worn_levels;
    std::map<std::pair<EquipSlot, EquipType>, const EquipPrototype*> example;
    for (const std::pair<const EquipSlot, EquipInstance>& worn :
         character.equipped()) {
      const EquipPrototype& proto = worn.second.prototype();
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
          TiersOnLadder(character, *example[entry.first]);
      ASSERT_GE(offered.size(), worn.size())
          << "wearing more than the ladder offers";
      offered.resize(worn.size());
      EXPECT_EQ(worn, offered)
          << "the " << EquipSlot_Name(entry.first.first) << " a level "
          << character.proto().level() << " wears is not the best on offer";
    }
  }
}

// The test above walks what is worn, so an empty slot is a slot it never
// reaches -- which is how every 2nd job came to stand there with no off-hand at
// all and nothing said so. This is the slot being filled at all.
TEST_F(WorkbenchGearTest, EveryJobPastTheFirstWearsAnOffHand) {
  for (JobAdvancement advancement : EveryAdvancement()) {
    GameState state = Workbench(advancement);
    SCOPED_TRACE(JobAdvancement_Name(advancement));
    bool branched = StageForAdvancement(advancement) >= 2;
    EXPECT_EQ(state.character.equipped().count(EQUIP_SLOT_SECONDARY) == 1,
              branched)
        << "a secondary belongs to a branch, and a 1st job is not in one";
  }
}

// The Frozen set drops rather than sells, so the workbench is the only place
// so much of it is ever seen. A 3rd job at 100 reaches the four armour pieces
// inside its level and keeps its meso weapon and off-hand. A 4th job at the
// cap adds the two that ask for 140 and the off-hand the token shelf sells,
// but hands the hat, top, bottom and weapon over to the Root Abyss set below
// -- four apiece. Under the 3rd job, none.
TEST_F(WorkbenchGearTest, TheThirdJobUpWearsTheFrozenSet) {
  for (JobAdvancement advancement : EveryAdvancement()) {
    GameState state = Workbench(advancement);
    SCOPED_TRACE(JobAdvancement_Name(advancement));
    int frozen = 0;
    for (const std::pair<const EquipSlot, EquipInstance>& worn :
         state.character.equipped()) {
      frozen += IsFrozen(worn.second.prototype()) ? 1 : 0;
    }
    EXPECT_EQ(frozen, StageForAdvancement(advancement) < 3 ? 0 : 4);
  }
}

// The Root Abyss set is bought with what the Chaos Root Abyss bosses drop, and
// they open at 200 -- so a 4th job standing at the cap wears all four pieces
// and the 3rd job under it, which wears the same four slots, wears none.
TEST_F(WorkbenchGearTest, OnlyTheCapWearsTheRootAbyssSet) {
  const EquipSlot kSlots[] = {EQUIP_SLOT_HAT, EQUIP_SLOT_TOP, EQUIP_SLOT_BOTTOM,
                              EQUIP_SLOT_PRIMARY_WEAPON};
  for (JobAdvancement advancement : EveryAdvancement()) {
    int stage = StageForAdvancement(advancement);
    if (stage < 3) {
      continue;  // wears no armour at all; the Frozen test above says so
    }
    GameState state = Workbench(advancement);
    SCOPED_TRACE(JobAdvancement_Name(advancement));
    for (EquipSlot slot : kSlots) {
      std::map<EquipSlot, EquipInstance>::const_iterator worn =
          state.character.equipped().find(slot);
      ASSERT_NE(worn, state.character.equipped().end())
          << EquipSlot_Name(slot) << " is empty";
      EXPECT_EQ(worn->second.prototype().required_level() == 150, stage >= 4)
          << EquipSlot_Name(slot) << " holds " << worn->second.name();
    }
  }
}

// The boss accessories fill the slots nothing else in the catalog does, and a
// boss drop is a long walk for a tester. Their levels decide who wears what:
// Zakum's eye piece asks for 100 and his crystal for 110, so a 3rd job
// standing at 100 wears the one and carries the other.
TEST_F(WorkbenchGearTest, TheThirdJobUpWearsWhatTheBossesDrop) {
  GameState second = Workbench(JOB_ADVANCEMENT_BANDIT);
  EXPECT_EQ(second.character.equipped().count(EQUIP_SLOT_EYE_ACCESSORY), 0u);
  EXPECT_EQ(second.character.equipped().count(EQUIP_SLOT_FACE_ACCESSORY), 0u);

  GameState third = Workbench(JOB_ADVANCEMENT_BERSERKER);
  EXPECT_EQ(third.character.equipped().count(EQUIP_SLOT_EYE_ACCESSORY), 1u);
  EXPECT_EQ(third.character.equipped().count(EQUIP_SLOT_FACE_ACCESSORY), 0u);

  // At the cap it wears all nine slots the Boss Accessory Set spans, and where
  // a later piece is an alternate for a slot an earlier one filled, the later
  // one is worn. The second pendant slot takes the one the first does not, and
  // the shoulder is the one of the four Cygnus sells that names this branch.
  GameState fourth = Workbench(JOB_ADVANCEMENT_DARK_KNIGHT);
  const std::map<EquipSlot, EquipInstance>& worn = fourth.character.equipped();
  const std::map<EquipSlot, std::string> kExpected = {
      {EQUIP_SLOT_EYE_ACCESSORY, "Black Bean Mark"},
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
    EXPECT_EQ(worn.at(want.first).prototype().name(), want.second);
  }
}

// The tier a branch finishes in can open above the level the branch does: a
// Hero advances at 100 and the Frozen axe they end up holding asks for 120.
// Standing in between they hold the tier under it rather than nothing, which
// is what --mode=max needs to measure a boss at every level one opens at.
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
// shows up here as itself rather than as a weapon that looks wrong.
TEST_F(WorkbenchGearTest, EachJobStartsAtTheTopOfItsOwnBand) {
  EXPECT_EQ(Workbench(JOB_ADVANCEMENT_ROGUE).character.proto().level(), 30);
  EXPECT_EQ(Workbench(JOB_ADVANCEMENT_BANDIT).character.proto().level(), 60);
  EXPECT_EQ(Workbench(JOB_ADVANCEMENT_BERSERKER).character.proto().level(),
            100);
  // The 4th job stops at the cap rather than at the 5th advancement's level,
  // which is above everything the EXP table describes.
  EXPECT_EQ(Workbench(JOB_ADVANCEMENT_DARK_KNIGHT).character.proto().level(),
            kTrialLevelCap);
}

}  // namespace
}  // namespace ms
