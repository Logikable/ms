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

  GameState Workbench(JobAdvancement advancement) {
    return GameState(equips_, scrolls_, items_, mobs_, maps_, skills_,
                     GameMode::kTest, advancement);
  }

  // The highest required level the catalog offers on `worn`'s own ladder among
  // the items this character could put on -- CanEquip asks about their level
  // and their job together, which is the same question the shop asks.
  //
  // A ladder is a slot and a type together. The type alone would put a
  // Fighter's swords and axes on one, which is the choice the workbench makes;
  // the slot alone would put all four pieces of armour on one, and armour
  // names no type at all.
  int BestTier(const CharacterInstance& character, const EquipPrototype& worn) {
    int best = 0;
    for (const std::pair<const std::string, EquipPrototype>& entry : equips_) {
      const EquipPrototype& proto = entry.second;
      if (proto.equip_slot() == worn.equip_slot() &&
          proto.equip_type() == worn.equip_type() &&
          character.CanEquip(proto)) {
        best = std::max(best, proto.required_level());
      }
    }
    return best;
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
TEST_F(WorkbenchGearTest, EveryJobWearsTheTopTierItsLevelReaches) {
  for (JobAdvancement advancement : EveryAdvancement()) {
    GameState state = Workbench(advancement);
    const CharacterInstance& character = state.character;
    SCOPED_TRACE(JobAdvancement_Name(advancement));
    EXPECT_TRUE(character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON))
        << "nothing in hand at all";
    for (const std::pair<const EquipSlot, EquipInstance>& worn :
         character.equipped()) {
      const EquipPrototype& proto = worn.second.prototype();
      EXPECT_EQ(proto.required_level(), BestTier(character, proto))
          << worn.second.name() << " is not the best "
          << EquipSlot_Name(proto.equip_slot()) << " a level "
          << character.proto().level() << " can wear";
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
// the whole of it is ever seen. A 3rd job at 100 reaches all four armour
// pieces and keeps its meso weapon and off-hand; a 4th job at the cap adds the
// two the token shelf sells, for six. Below that, none.
TEST_F(WorkbenchGearTest, TheThirdJobUpWearsTheFrozenSet) {
  for (JobAdvancement advancement : EveryAdvancement()) {
    GameState state = Workbench(advancement);
    SCOPED_TRACE(JobAdvancement_Name(advancement));
    int frozen = 0;
    for (const std::pair<const EquipSlot, EquipInstance>& worn :
         state.character.equipped()) {
      frozen += IsFrozen(worn.second.prototype()) ? 1 : 0;
    }
    int stage = StageForAdvancement(advancement);
    EXPECT_EQ(frozen, stage < 3 ? 0 : (stage == 3 ? 4 : 6));
  }
}

// Zakum's two accessories fill the slots nothing else in the catalog does, and
// a boss drop is a long walk for a tester. The eye piece asks for level 100
// and the crystal for 110, so a 3rd job standing at 100 wears the one and
// carries the other.
TEST_F(WorkbenchGearTest, TheThirdJobUpWearsWhatZakumDrops) {
  GameState second = Workbench(JOB_ADVANCEMENT_BANDIT);
  EXPECT_EQ(second.character.equipped().count(EQUIP_SLOT_EYE_ACCESSORY), 0u);
  EXPECT_EQ(second.character.equipped().count(EQUIP_SLOT_FACE_ACCESSORY), 0u);

  GameState third = Workbench(JOB_ADVANCEMENT_BERSERKER);
  EXPECT_EQ(third.character.equipped().count(EQUIP_SLOT_EYE_ACCESSORY), 1u);
  EXPECT_EQ(third.character.equipped().count(EQUIP_SLOT_FACE_ACCESSORY), 0u);

  GameState fourth = Workbench(JOB_ADVANCEMENT_DARK_KNIGHT);
  EXPECT_EQ(fourth.character.equipped().count(EQUIP_SLOT_EYE_ACCESSORY), 1u);
  EXPECT_EQ(fourth.character.equipped().count(EQUIP_SLOT_FACE_ACCESSORY), 1u);
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
