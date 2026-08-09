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

// One job a character can advance into: which job, at which level, and which
// stage that advancement is.
struct Advanceable {
  Job job = JOB_UNSPECIFIED;
  int level = 0;
  int stage = 0;
};

// Every job a character can advance into. The levels are the thresholds in
// character.cc's kAdvancementLevels. A stage with no branches written yet
// simply contributes nothing.
std::vector<Advanceable> AdvanceableJobs() {
  const int kLevelForStage[] = {0, 10, 30};
  std::vector<Advanceable> jobs;
  for (Job from :
       {JOB_BEGINNER, JOB_SWORDMAN, JOB_ARCHER, JOB_MAGICIAN, JOB_ROGUE}) {
    for (int stage = 1; stage <= 2; ++stage) {
      for (Job job : JobChoicesForStage(from, stage)) {
        jobs.push_back({job, kLevelForStage[stage], stage});
      }
    }
  }
  return jobs;
}

// The names in StarterEquipsFor are catalog keys, and nothing in the type
// system ties them to the files on disk -- renaming a textproto would leave a
// job silently advancing empty-handed.
TEST_F(JobAdvancementTest, EveryStarterEquipExistsInTheCatalog) {
  for (const Advanceable& entry : AdvanceableJobs()) {
    std::vector<std::string> names = StarterEquipsFor(entry.job);
    EXPECT_FALSE(names.empty())
        << Job_Name(entry.job) << " advances with nothing";
    for (const std::string& name : names) {
      EXPECT_NE(state_.equips.find(name), state_.equips.end())
          << name << " is not in data/equip";
    }
  }
}

// A 2nd job is handed one thing, and it is not a weapon: it arrives armed and
// able to afford the tier, so a free weapon would take the choice of which to
// buy away. The off-hand is different -- the advancement is what opens that
// slot, and nothing else would ever put anything in it.
TEST_F(JobAdvancementTest, ASecondJobIsHandedItsOffHandAndNoWeapon) {
  for (const Advanceable& entry : AdvanceableJobs()) {
    if (entry.stage == 1) {
      continue;
    }
    std::vector<std::string> names = StarterEquipsFor(entry.job);
    ASSERT_EQ(names.size(), 1u)
        << Job_Name(entry.job) << " advances with the wrong number of items";
    EXPECT_EQ(state_.equips.at(names[0]).equip_slot(), EQUIP_SLOT_SECONDARY)
        << Job_Name(entry.job) << " is handed something it should have bought";
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
          {JOB_FIGHTER, {EQUIP_TYPE_MEDALLION}},
          {JOB_PAGE, {EQUIP_TYPE_ROSARY}},
          {JOB_SPEARMAN, {EQUIP_TYPE_IRON_CHAIN}},
          {JOB_HUNTER, {EQUIP_TYPE_ARROW_FLETCHING}},
          {JOB_CROSSBOWMAN, {EQUIP_TYPE_BOW_THIMBLE}},
          {JOB_FIRE_POISON_WIZARD, {EQUIP_TYPE_MAGIC_BOOK_FIRE_POISON}},
          {JOB_ICE_LIGHTNING_WIZARD, {EQUIP_TYPE_MAGIC_BOOK_ICE_LIGHTNING}},
          {JOB_CLERIC, {EQUIP_TYPE_MAGIC_BOOK_HOLY}},
          {JOB_ASSASSIN, {EQUIP_TYPE_CHARM}},
          {JOB_BANDIT, {EQUIP_TYPE_DAGGER_SCABBARD}},
      };
  return *kTypes;
}

// Existence says nothing about what the gear IS -- a job could advance into a
// full set of the wrong class's, or the wrong branch's, and every other test
// here would pass.
TEST_F(JobAdvancementTest, EachJobStartsWithItsOwnGear) {
  for (const Advanceable& entry : AdvanceableJobs()) {
    std::multiset<EquipType> actual;
    for (const std::string& name : StarterEquipsFor(entry.job)) {
      actual.insert(state_.equips.at(name).equip_type());
    }
    EXPECT_EQ(actual, ExpectedStarterTypes().at(entry.job))
        << Job_Name(entry.job) << " does not advance with its own gear";
  }
}

// "The gear of the level the advancement happens at", not "gear that level can
// wear": either drift would hand over something weaker than the tier, or
// something that cannot be held at all.
TEST_F(JobAdvancementTest, StarterEquipsAreTheirTiers) {
  for (const Advanceable& entry : AdvanceableJobs()) {
    for (const std::string& name : StarterEquipsFor(entry.job)) {
      EXPECT_EQ(state_.equips.at(name).required_level(), entry.level)
          << name << " is not level " << entry.level << " gear";
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

TEST_F(JobAdvancementTest, TheFirstAdvancementReseatsTheStats) {
  PerformJobAdvancement(state_, JOB_MAGICIAN);
  EXPECT_EQ(state_.character.proto().allocated_stats().int_(), 25);
  EXPECT_EQ(state_.character.proto().allocated_stats().str(), 4);
}

// The second one leaves them where they are. It picks a branch of a category
// the character is already in, raising the same stat -- so re-seating would
// throw away every point spent since the first advancement and give the
// player AP to put back where it came from.
TEST_F(JobAdvancementTest, TheSecondAdvancementLeavesTheStatsAlone) {
  PerformJobAdvancement(state_, JOB_SWORDMAN);
  for (int i = 0; i < 5; ++i) {
    state_.character.LevelUp();
  }
  while (state_.character.AllocateStat(STAT_FIELD_STR)) {
  }
  int str = state_.character.proto().allocated_stats().str();
  int ap = state_.character.proto().ap();
  ASSERT_GT(str, 25) << "nothing was spent, so nothing could be taken back";

  PerformJobAdvancement(state_, JOB_FIGHTER);
  EXPECT_EQ(state_.character.proto().allocated_stats().str(), str);
  EXPECT_EQ(state_.character.proto().ap(), ap);
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
