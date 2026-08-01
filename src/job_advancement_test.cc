#include "src/job_advancement.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/game_state.h"
#include "src/proto_loader.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

std::map<std::string, EquipPrototype> LoadEquips() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<EquipPrototype>(runfiles->Rlocation("ms/data/equip"));
}

// A state holding the real equip catalog, so the starting gear is the gear the
// game actually ships.
class JobAdvancementTest : public testing::Test {
 protected:
  GameState state_{LoadEquips(), {}, {}, {}, {}};
};

// The names in StarterEquipsFor are catalog keys, and nothing in the type
// system ties them to the files on disk -- renaming a textproto would leave a
// job silently advancing empty-handed.
TEST_F(JobAdvancementTest, EveryStarterEquipExistsInTheCatalog) {
  for (Job job : JobChoicesForStage(1)) {
    std::vector<std::string> names = StarterEquipsFor(job);
    EXPECT_FALSE(names.empty()) << Job_Name(job) << " advances with no weapon";
    for (const std::string& name : names) {
      EXPECT_NE(state_.equips.find(name), state_.equips.end())
          << name << " is not in data/equip";
    }
  }
}

// The weapons each job is supposed to walk away with. Spelled out as types
// rather than catalog keys so that swapping which sword a Swordman starts with
// stays a data decision, while handing one a bow does not.
const std::map<Job, std::multiset<EquipType>>& ExpectedStarterTypes() {
  static const std::map<Job, std::multiset<EquipType>>* kTypes =
      new std::map<Job, std::multiset<EquipType>>{
          {JOB_SWORDMAN, {EQUIP_TYPE_ONE_HANDED_SWORD}},
          {JOB_MAGICIAN, {EQUIP_TYPE_WAND}},
          {JOB_ARCHER, {EQUIP_TYPE_BOW}},
          {JOB_ROGUE,
           {EQUIP_TYPE_DAGGER, EQUIP_TYPE_THROWING_STAR, EQUIP_TYPE_CLAW}},
      };
  return *kTypes;
}

// A level-10 weapon or the character cannot hold what it is handed.
TEST_F(JobAdvancementTest, StarterEquipsAreWearableAtTen) {
  for (Job job : JobChoicesForStage(1)) {
    for (const std::string& name : StarterEquipsFor(job)) {
      EXPECT_LE(state_.equips.at(name).required_level(), 10)
          << name << " cannot be worn by the character it is given to";
    }
  }
}

// Existence and a wearable level say nothing about what the weapon IS -- a job
// could advance into a full set of the wrong class's gear and every other test
// here would pass.
TEST_F(JobAdvancementTest, EachJobStartsWithItsOwnWeapons) {
  for (Job job : JobChoicesForStage(1)) {
    std::multiset<EquipType> actual;
    for (const std::string& name : StarterEquipsFor(job)) {
      actual.insert(state_.equips.at(name).equip_type());
    }
    EXPECT_EQ(actual, ExpectedStarterTypes().at(job))
        << Job_Name(job) << " does not advance with its own weapons";
  }
}

// "The level 10 weapon", not "a weapon a level 10 can wear": starting gear that
// drifted below the advancement level would quietly hand out a weaker weapon.
TEST_F(JobAdvancementTest, StarterEquipsAreTheLevelTenWeapons) {
  for (Job job : JobChoicesForStage(1)) {
    for (const std::string& name : StarterEquipsFor(job)) {
      EXPECT_EQ(state_.equips.at(name).required_level(), 10)
          << name << " is not a level 10 weapon";
    }
  }
}

TEST_F(JobAdvancementTest, AdvancingSetsTheJobAndItsStage) {
  // Relative to where the starting character already is, so this test does not
  // ride the testing knob in game_state.cc.
  int before = state_.character.proto().job_stage();
  PerformJobAdvancement(state_, JOB_ARCHER);
  EXPECT_EQ(state_.character.proto().job(), JOB_ARCHER);
  EXPECT_EQ(state_.character.proto().job_stage(), before + 1);
}

TEST_F(JobAdvancementTest, AdvancingReseatsTheStats) {
  PerformJobAdvancement(state_, JOB_MAGICIAN);
  EXPECT_EQ(state_.character.proto().allocated_stats().int_(), 25);
  EXPECT_EQ(state_.character.proto().allocated_stats().str(), 4);
}

// The gear goes in the bag, not on the character: equipping it is the first
// thing the game asks a new job to do.
TEST_F(JobAdvancementTest, StarterGearLandsInTheBag) {
  PerformJobAdvancement(state_, JOB_SWORDMAN);
  ASSERT_EQ(state_.character.inventory().size(), 1);
  EXPECT_EQ(state_.character.inventory().equip_instance(0)->prototype().name(),
            state_.equips.at("long_sword").name());
  EXPECT_TRUE(state_.character.equipped().empty());
}

TEST_F(JobAdvancementTest, ARogueIsHandedBothWeaponsAndTheStars) {
  PerformJobAdvancement(state_, JOB_ROGUE);
  EXPECT_EQ(state_.character.inventory().size(), 3);
}

}  // namespace
}  // namespace ms
