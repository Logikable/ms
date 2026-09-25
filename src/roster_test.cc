#include "src/roster.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/character/character.h"
#include "src/character/stat_preset.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/save.pb.h"

namespace ms {
namespace {

class RosterTest : public testing::Test {
 protected:
  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    state_ = MakeState();
  }

  std::unique_ptr<GameState> MakeState() {
    std::map<std::string, EquipPrototype> equips{{"sword", sword_}};
    return std::make_unique<GameState>(equips, std::map<std::string, Scroll>{},
                                       std::map<std::string, ItemPrototype>{},
                                       std::map<std::string, Mob>{},
                                       std::map<std::string, MapData>{});
  }

  // A slot for someone not being played, last played `stamp` seconds after the
  // epoch.
  CharacterSave Slot(const std::string& name, int level, Job job,
                     int64_t stamp) {
    CharacterSave slot;
    slot.mutable_character()->set_name(name);
    slot.mutable_character()->set_level(level);
    slot.mutable_character()->set_job(job);
    slot.set_current_map("cave");
    slot.set_last_played_unix_seconds(stamp);
    return slot;
  }

  EquipPrototype sword_;
  std::unique_ptr<GameState> state_;
};

TEST_F(RosterTest, ANewGameIsOneCharacterHoldingTheCheck) {
  std::vector<RosterEntry> rows = Roster(*state_);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_TRUE(rows[0].played);
  EXPECT_TRUE(rows[0].offline);
  EXPECT_EQ(rows[0].level, 1);
  EXPECT_EQ(rows[0].job, "Beginner");
}

TEST_F(RosterTest, TheRosterListsMostRecentlyPlayedFirst) {
  state_->character.SetUsername("Played");
  state_->inactive_characters.push_back(Slot("Old", 30, JOB_FIGHTER, 100));
  state_->inactive_characters.push_back(Slot("Newer", 40, JOB_PAGE, 200));
  state_->played_slot = 1;

  std::vector<RosterEntry> rows = Roster(*state_);
  ASSERT_EQ(rows.size(), 3u);
  // The played character was stamped when put in, so they're always the newest.
  EXPECT_EQ(rows[0].name, "Played");
  EXPECT_EQ(rows[1].name, "Newer");
  EXPECT_EQ(rows[2].name, "Old");
  EXPECT_EQ(rows[0].slot, 1);
  EXPECT_EQ(rows[1].slot, 2);
  EXPECT_EQ(rows[2].slot, 0);
}

TEST_F(RosterTest, PlayingSomebodyElseWritesTheFirstCharacterBack) {
  state_->character.SetUsername("First");
  state_->character.AddMeso(500);
  state_->current_map = "field";
  state_->inactive_characters.push_back(Slot("Second", 30, JOB_FIGHTER, 100));

  EXPECT_TRUE(PlayCharacter(*state_, 1));
  EXPECT_EQ(state_->character.username(), "Second");
  EXPECT_EQ(state_->played_slot, 1);
  EXPECT_EQ(state_->current_map, "cave") << "their own map comes back";
  // The offline check stays put: playing someone isn't choosing them for it.
  EXPECT_EQ(state_->offline_slot, 0);

  EXPECT_TRUE(PlayCharacter(*state_, 0));
  EXPECT_EQ(state_->character.username(), "First");
  EXPECT_EQ(state_->character.meso(), 500);
  EXPECT_EQ(state_->current_map, "field");

  // Choosing the character already in play, or an empty slot, changes nothing
  // and returns false; on that row Play means resume.
  EXPECT_FALSE(PlayCharacter(*state_, 0));
  EXPECT_FALSE(PlayCharacter(*state_, 7));
  EXPECT_EQ(state_->character.username(), "First");
  EXPECT_EQ(state_->played_slot, 0);
}

TEST_F(RosterTest, CreateMakesAnArmedBeginnerAndPlaysThem) {
  state_->character.SetUsername("First");
  CreateCharacter(*state_);

  EXPECT_EQ(state_->played_slot, 1);
  EXPECT_EQ(state_->character.proto().level(), 1);
  EXPECT_EQ(state_->character.proto().job(), JOB_BEGINNER);
  EXPECT_EQ(state_->current_map, kHomeMap);
  EXPECT_NE(
      state_->character.WornAt(StatPreset::kFirst, EQUIP_SLOT_PRIMARY_WEAPON),
      nullptr)
      << "a new character starts armed";
  EXPECT_EQ(state_->offline_slot, 0) << "the check does not follow a new one";
  ASSERT_EQ(state_->inactive_characters.size(), 1u);
  EXPECT_EQ(state_->inactive_characters[0].character().name(), "First");
}

// New characters' unlocks are checked against the account, and only a switch
// can tell it what the departing character reached.
TEST_F(RosterTest, TheAccountTakesInWhoeverLeavesPlay) {
  while (state_->character.proto().level() < 120) {
    state_->character.LevelUp();
  }
  ASSERT_EQ(state_->account.max_level(), 0) << "nothing has recorded it yet";

  CreateCharacter(*state_);
  EXPECT_EQ(state_->account.max_level(), 120);
  EXPECT_EQ(state_->character.proto().level(), 1);
}

TEST_F(RosterTest, TheLastCharacterCannotBeDeleted) {
  EXPECT_FALSE(DeleteCharacter(*state_, 0));
  EXPECT_EQ(Roster(*state_).size(), 1u);
}

TEST_F(RosterTest, DeletingSomebodyElseLeavesThePlayedCharacterAlone) {
  state_->character.SetUsername("Played");
  state_->inactive_characters.push_back(Slot("Doomed", 30, JOB_FIGHTER, 100));
  state_->inactive_characters.push_back(Slot("Kept", 40, JOB_PAGE, 200));
  state_->played_slot = 2;
  state_->offline_slot = 2;

  EXPECT_TRUE(DeleteCharacter(*state_, 0));
  EXPECT_EQ(state_->character.username(), "Played");
  // Both markers shifted down along with the removed slot.
  EXPECT_EQ(state_->played_slot, 1);
  EXPECT_EQ(state_->offline_slot, 1);
  ASSERT_EQ(state_->inactive_characters.size(), 1u);
  EXPECT_EQ(state_->inactive_characters[0].character().name(), "Kept");
}

TEST_F(RosterTest, DeletingThePlayedCharacterSwapsTheOfflineOneIn) {
  state_->character.SetUsername("Played");
  state_->inactive_characters.push_back(Slot("Farmer", 30, JOB_FIGHTER, 100));
  state_->played_slot = 1;
  state_->offline_slot = 0;

  EXPECT_TRUE(DeleteCharacter(*state_, 1));
  EXPECT_EQ(state_->character.username(), "Farmer");
  EXPECT_EQ(state_->played_slot, 0);
  EXPECT_EQ(state_->offline_slot, 0);
  EXPECT_TRUE(state_->inactive_characters.empty());
}

TEST_F(RosterTest, DeletingTheOfflineCharacterHandsTheCheckToWhoIsPlayed) {
  state_->character.SetUsername("Played");
  state_->inactive_characters.push_back(Slot("Farmer", 30, JOB_FIGHTER, 100));
  state_->played_slot = 1;
  state_->offline_slot = 0;

  EXPECT_TRUE(DeleteCharacter(*state_, 0));
  EXPECT_EQ(state_->character.username(), "Played");
  EXPECT_EQ(state_->played_slot, 0);
  EXPECT_EQ(state_->offline_slot, 0);
}

TEST_F(RosterTest, TheCheckMovesWhereItIsPutAndNowhereElse) {
  state_->inactive_characters.push_back(Slot("Farmer", 30, JOB_FIGHTER, 100));
  SetOfflineCharacter(*state_, 1);
  EXPECT_EQ(state_->offline_slot, 1);
  // Out of range is rejected instead of clamped: a missing slot is a caller's
  // mistake, not a choice.
  SetOfflineCharacter(*state_, 7);
  EXPECT_EQ(state_->offline_slot, 1);
}

TEST_F(RosterTest, AllCharactersFoldsThePlayedOneIntoTheirSlot) {
  state_->character.SetUsername("Played");
  state_->inactive_characters.push_back(Slot("Before", 30, JOB_FIGHTER, 100));
  state_->inactive_characters.push_back(Slot("After", 40, JOB_PAGE, 200));
  state_->played_slot = 1;

  std::vector<CharacterSave> all = AllCharacters(*state_);
  ASSERT_EQ(all.size(), 3u);
  EXPECT_EQ(all[0].character().name(), "Before");
  EXPECT_EQ(all[1].character().name(), "Played");
  EXPECT_EQ(all[2].character().name(), "After");
}

}  // namespace
}  // namespace ms
