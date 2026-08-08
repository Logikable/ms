#include "src/character/job_advancement.h"

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

// Every job a character can advance into, with the level that advancement
// happens at -- the thresholds in character.cc's kAdvancementLevels. A stage
// with no branches written yet simply contributes nothing.
std::vector<std::pair<Job, int>> AdvanceableJobs() {
  const int kLevelForStage[] = {0, 10, 30};
  std::vector<std::pair<Job, int>> jobs;
  for (Job from :
       {JOB_BEGINNER, JOB_SWORDMAN, JOB_ARCHER, JOB_MAGICIAN, JOB_ROGUE}) {
    for (int stage = 1; stage <= 2; ++stage) {
      for (Job job : JobChoicesForStage(from, stage)) {
        jobs.push_back({job, kLevelForStage[stage]});
      }
    }
  }
  return jobs;
}

// The names in StarterEquipsFor are catalog keys, and nothing in the type
// system ties them to the files on disk -- renaming a textproto would leave a
// job silently advancing empty-handed.
TEST_F(JobAdvancementTest, EveryStarterEquipExistsInTheCatalog) {
  for (const std::pair<Job, int>& entry : AdvanceableJobs()) {
    std::vector<std::string> names = StarterEquipsFor(entry.first);
    EXPECT_FALSE(names.empty())
        << Job_Name(entry.first) << " advances with no weapon";
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
          {JOB_MAGICIAN, {EQUIP_TYPE_STAFF}},
          {JOB_ARCHER, {EQUIP_TYPE_BOW}},
          {JOB_ROGUE,
           {EQUIP_TYPE_DAGGER, EQUIP_TYPE_THROWING_STAR, EQUIP_TYPE_CLAW}},
          // A 2nd job gets what its Final Attack demands, which is what makes
          // the demand worth stating at all.
          {JOB_FIGHTER, {EQUIP_TYPE_TWO_HANDED_AXE}},
          {JOB_PAGE, {EQUIP_TYPE_TWO_HANDED_BLUNT}},
          {JOB_SPEARMAN, {EQUIP_TYPE_SPEAR}},
          // The archer's branch says the same thing about a bow: the Hunter's
          // whole book lapses without one.
          {JOB_HUNTER, {EQUIP_TYPE_BOW}},
          {JOB_CROSSBOWMAN, {EQUIP_TYPE_CROSSBOW}},
      };
  return *kTypes;
}

// Existence says nothing about what the weapon IS -- a job could advance into
// a full set of the wrong class's gear and every other test here would pass.
TEST_F(JobAdvancementTest, EachJobStartsWithItsOwnWeapons) {
  for (const std::pair<Job, int>& entry : AdvanceableJobs()) {
    std::multiset<EquipType> actual;
    for (const std::string& name : StarterEquipsFor(entry.first)) {
      actual.insert(state_.equips.at(name).equip_type());
    }
    EXPECT_EQ(actual, ExpectedStarterTypes().at(entry.first))
        << Job_Name(entry.first) << " does not advance with its own weapons";
  }
}

// "The weapon of the level the advancement happens at", not "a weapon that
// level can wear": gear that drifted either way would hand over something
// weaker than the tier, or something that cannot be held at all.
TEST_F(JobAdvancementTest, StarterEquipsAreTheirTiersWeapons) {
  for (const std::pair<Job, int>& entry : AdvanceableJobs()) {
    for (const std::string& name : StarterEquipsFor(entry.first)) {
      EXPECT_EQ(state_.equips.at(name).required_level(), entry.second)
          << name << " is not a level " << entry.second << " weapon";
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
  // Still wearing what they started the game in: advancing hands the gear
  // over, it does not put it on.
  ASSERT_TRUE(state_.character.equipped().count(EQUIP_SLOT_PRIMARY_WEAPON));
  EXPECT_EQ(state_.character.equipped()
                .at(EQUIP_SLOT_PRIMARY_WEAPON)
                .prototype()
                .name(),
            state_.equips.at("sword").name());
}

TEST_F(JobAdvancementTest, ARogueIsHandedBothWeaponsAndTheStars) {
  PerformJobAdvancement(state_, JOB_ROGUE);
  EXPECT_EQ(state_.character.inventory().size(), 3);
}

}  // namespace
}  // namespace ms
