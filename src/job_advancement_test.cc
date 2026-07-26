#include "src/job_advancement.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
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

// A level-10 weapon or the character cannot hold what it is handed.
TEST_F(JobAdvancementTest, StarterEquipsAreWearableAtTen) {
  for (Job job : JobChoicesForStage(1)) {
    for (const std::string& name : StarterEquipsFor(job)) {
      EXPECT_LE(state_.equips.at(name).required_level(), 10)
          << name << " cannot be worn by the character it is given to";
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
