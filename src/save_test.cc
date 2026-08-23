#include "src/save.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>

#include "src/character/character.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/keybinds.pb.h"
#include "src/protos/save.pb.h"

namespace ms {
namespace {

class SaveTest : public testing::Test {
 protected:
  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_upgrade_slots(7);
    shell_.set_name("Green Snail Shell");
    shell_.set_category(ITEM_CATEGORY_ETC);

    dir_ = std::filesystem::temp_directory_path() /
           ("ms_save_test_" +
            std::to_string(
                testing::UnitTest::GetInstance()->current_test_info()->line()));
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
    path_ = (dir_ / "ms.save").string();
  }

  void TearDown() override {
    std::filesystem::remove_all(dir_);
  }

  // A state carrying the catalogs a save refers to by name. Built for tests,
  // so it does not need the game's data files.
  std::unique_ptr<GameState> MakeState() {
    // Keyed by data-file stem like the real catalogs, not by display name --
    // a save refers to items by the name the player sees, and a fixture that
    // conflated the two would hide a lookup against the wrong key.
    std::map<std::string, EquipPrototype> equips{{"sword", sword_}};
    std::map<std::string, ItemPrototype> items{{"green_snail_shell", shell_}};
    return std::make_unique<GameState>(equips, std::map<std::string, Scroll>{},
                                       items, std::map<std::string, Mob>{},
                                       std::map<std::string, MapData>{});
  }

  std::string ReadRaw(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
  }

  void WriteRaw(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }

  EquipPrototype sword_;
  ItemPrototype shell_;
  std::filesystem::path dir_;
  std::string path_;
};

// --- where the file goes ---

TEST_F(SaveTest, TheSaveSitsBesideTheExecutable) {
  EXPECT_EQ(SavePathFor("/opt/games/ms/ms"), "/opt/games/ms/ms.save");
  EXPECT_EQ(SavePathFor("./ms"), "./ms.save");
}

// Run by bare name off the PATH there is no directory to work from, so the
// working directory is all that is left.
TEST_F(SaveTest, ABareProgramNameUsesTheWorkingDir) {
  EXPECT_EQ(SavePathFor("ms"), "ms.save");
}

// --- round trip through the file ---

TEST_F(SaveTest, WritesAndReadsBackACharacter) {
  std::unique_ptr<GameState> saved = MakeState();
  saved->character.LevelUp();
  saved->character.AddMeso(1234);
  saved->character.PickUp(std::make_unique<EquipInstance>(sword_));
  saved->character.AddStackable(shell_, 17);
  saved->character.ToggleScrollPin("2:1:30");
  saved->character.RecordBossClear("zakum", "Normal", 1755000000);
  saved->current_map = "lith";
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  LoadResult result = LoadGameFromFile(*loaded, path_);
  EXPECT_EQ(result.status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.proto().level(),
            saved->character.proto().level());
  EXPECT_EQ(loaded->character.meso(), 1234);
  EXPECT_EQ(loaded->character.inventory().size(), 1);
  ASSERT_EQ(loaded->character.stackables(ITEM_CATEGORY_ETC).size(), 1u);
  EXPECT_EQ(loaded->character.stackables(ITEM_CATEGORY_ETC)[0].count(), 17);
  // A pinned scroll is a standing preference, so it rides the save.
  EXPECT_TRUE(loaded->character.ScrollPinned("2:1:30"));
  EXPECT_FALSE(loaded->character.ScrollPinned("2:1:70"));
  // A boss clear is what holds the daily back, so losing it on a restart
  // would hand the player another run.
  EXPECT_EQ(loaded->character.BossClearedAt("zakum", "Normal"), 1755000000);
  EXPECT_EQ(loaded->character.BossClearedAt("zakum", "Chaos"), 0);
  EXPECT_EQ(loaded->current_map, "lith");
}

TEST_F(SaveTest, WritesAndReadsBackTheUsername) {
  std::unique_ptr<GameState> saved = MakeState();
  EXPECT_EQ(saved->character.username(), kDefaultUsername);
  saved->character.SetUsername("Logikable");
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.username(), "Logikable");
}

// A save written before characters had names. It comes forward with the
// invitation to set one rather than with a blank row.
TEST_F(SaveTest, ASaveWithNoNameLoadsWithTheDefault) {
  SaveGame old;
  old.set_format_version(kSaveFormatVersion);
  old.mutable_character()->set_level(1);
  std::string bytes;
  ASSERT_TRUE(old.SerializeToString(&bytes));
  WriteRaw(path_, bytes);

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.username(), kDefaultUsername);
}

// AP is only ever moved between the pool and the stats, so a save whose books
// do not add up was written under older rules. Loading puts them back rather
// than handing the player a character short of what they earned.
TEST_F(SaveTest, ASaveWithTheWrongApIsPutBackOnItsBooks) {
  std::unique_ptr<GameState> saved = MakeState();
  for (int i = 0; i < 9; ++i) {
    saved->character.LevelUp();
  }
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  // Rewritten as a file that recorded none of the AP those levels paid.
  SaveGame on_disk;
  ASSERT_TRUE(on_disk.ParseFromString(ReadRaw(path_)));
  ASSERT_EQ(on_disk.character().ap(), 45);
  on_disk.mutable_character()->set_ap(0);
  std::string bytes;
  ASSERT_TRUE(on_disk.SerializeToString(&bytes));
  WriteRaw(path_, bytes);

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.proto().ap(), 45);
}

TEST_F(SaveTest, SavingTwiceReplacesRatherThanAppends) {
  std::unique_ptr<GameState> state = MakeState();
  state->character.AddMeso(5);
  ASSERT_TRUE(SaveGameToFile(*state, path_));
  state->character.AddMeso(95);
  ASSERT_TRUE(SaveGameToFile(*state, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.meso(), 100);
}

// The write is not readable as text, which is the whole point of the format.
TEST_F(SaveTest, TheSaveIsNotPlainText) {
  std::unique_ptr<GameState> state = MakeState();
  state->character.AddMeso(1234567);
  ASSERT_TRUE(SaveGameToFile(*state, path_));

  EXPECT_EQ(ReadRaw(path_).find("1234567"), std::string::npos);
}

// --- playtime and creation time ---

TEST_F(SaveTest, WritesAndReadsBackPlaytimeAndCreationTime) {
  std::unique_ptr<GameState> saved = MakeState();
  saved->playtime_seconds = 3725.5;
  saved->created_unix_seconds = 1700000000;
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->created_unix_seconds, 1700000000);
  // Whole seconds on disk, so the half second is the one thing not kept.
  EXPECT_EQ(loaded->playtime_seconds, 3725.0);
}

// A fresh character is created now, not at the epoch -- the state stamps
// itself, so there is a real date to show before the first save is written.
TEST_F(SaveTest, WritesAndReadsBackKeybinds) {
  std::unique_ptr<GameState> saved = MakeState();
  Keybind* row = saved->keybinds.add_binds();
  row->set_action(KEY_ACTION_UP);
  row->add_keys("Up");
  row->add_keys("W");
  row->add_keys("");
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  ASSERT_EQ(loaded->keybinds.binds_size(), 1);
  EXPECT_EQ(loaded->keybinds.binds(0).action(), KEY_ACTION_UP);
  EXPECT_EQ(loaded->keybinds.binds(0).keys(1), "W");
}

TEST_F(SaveTest, ANewStateIsStampedWithTheCurrentTime) {
  std::int64_t before = static_cast<std::int64_t>(std::time(nullptr));
  std::unique_ptr<GameState> state = MakeState();
  std::int64_t after = static_cast<std::int64_t>(std::time(nullptr));

  EXPECT_GE(state->created_unix_seconds, before);
  EXPECT_LE(state->created_unix_seconds, after);
  EXPECT_EQ(state->playtime_seconds, 0.0);
}

// The shape of a save written before either field existed: neither is set, so
// playtime starts from nothing and the creation time falls back to now.
TEST_F(SaveTest, AnOldSaveLoadsWithZeroPlaytime) {
  SaveGame old;
  old.set_format_version(kSaveFormatVersion);
  std::string bytes;
  ASSERT_TRUE(old.SerializeToString(&bytes));
  WriteRaw(path_, bytes);

  std::int64_t before = static_cast<std::int64_t>(std::time(nullptr));
  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);

  EXPECT_EQ(loaded->playtime_seconds, 0.0);
  EXPECT_GE(loaded->created_unix_seconds, before)
      << "an absent creation time reads as now, not as the epoch";
}

// The creation time is the character's, not the file's: re-saving must carry
// the original date rather than stamp the moment of the write.
TEST_F(SaveTest, ResavingKeepsTheCreationTime) {
  std::unique_ptr<GameState> first = MakeState();
  first->created_unix_seconds = 1500000000;
  ASSERT_TRUE(SaveGameToFile(*first, path_));

  std::unique_ptr<GameState> second = MakeState();
  ASSERT_EQ(LoadGameFromFile(*second, path_).status, LoadStatus::kLoaded);
  ASSERT_TRUE(SaveGameToFile(*second, path_));

  std::unique_ptr<GameState> third = MakeState();
  ASSERT_EQ(LoadGameFromFile(*third, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(third->created_unix_seconds, 1500000000);
}

// Playtime is a running total across sessions, so a session that loads a save
// and plays on has to add to what was there rather than replace it.
TEST_F(SaveTest, PlaytimeAccumulatesAcrossSessions) {
  std::unique_ptr<GameState> first = MakeState();
  first->playtime_seconds = 600.0;
  ASSERT_TRUE(SaveGameToFile(*first, path_));

  std::unique_ptr<GameState> second = MakeState();
  ASSERT_EQ(LoadGameFromFile(*second, path_).status, LoadStatus::kLoaded);
  ASSERT_EQ(second->playtime_seconds, 600.0) << "the previous total, restored";
  second->playtime_seconds += 90.0;  // what this session played
  ASSERT_TRUE(SaveGameToFile(*second, path_));

  std::unique_ptr<GameState> third = MakeState();
  ASSERT_EQ(LoadGameFromFile(*third, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(third->playtime_seconds, 690.0);
}

// A refused load must not leave a half-applied playtime behind it either.
TEST_F(SaveTest, ARefusedLoadLeavesPlaytimeAlone) {
  WriteRaw(path_, "not a save");
  std::unique_ptr<GameState> state = MakeState();
  state->playtime_seconds = 1234.0;
  state->created_unix_seconds = 1600000000;

  ASSERT_EQ(LoadGameFromFile(*state, path_).status, LoadStatus::kUnreadable);
  EXPECT_EQ(state->playtime_seconds, 1234.0);
  EXPECT_EQ(state->created_unix_seconds, 1600000000);
}

// --- the file layer's failure modes ---

TEST_F(SaveTest, AMissingFileIsNotAnError) {
  std::unique_ptr<GameState> state = MakeState();
  LoadResult result = LoadGameFromFile(*state, path_);
  EXPECT_EQ(result.status, LoadStatus::kMissing);
  EXPECT_TRUE(result.message.empty()) << "nothing to tell the player about";
}

TEST_F(SaveTest, GarbageIsRefusedRatherThanRead) {
  WriteRaw(path_, "this is not a save file, it is a sentence");
  std::unique_ptr<GameState> state = MakeState();
  LoadResult result = LoadGameFromFile(*state, path_);
  EXPECT_EQ(result.status, LoadStatus::kUnreadable);
  EXPECT_NE(result.message.find(path_), std::string::npos)
      << "the player has to be told which file to move";
}

// The shape a half-finished write would take if one could reach the real file.
TEST_F(SaveTest, ATruncatedSaveIsRefused) {
  std::unique_ptr<GameState> state = MakeState();
  state->character.PickUp(std::make_unique<EquipInstance>(sword_));
  state->character.AddStackable(shell_, 42);
  ASSERT_TRUE(SaveGameToFile(*state, path_));
  std::string whole = ReadRaw(path_);
  ASSERT_GT(whole.size(), 4u);
  WriteRaw(path_, whole.substr(0, whole.size() / 2));

  std::unique_ptr<GameState> loaded = MakeState();
  EXPECT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kUnreadable);
}

TEST_F(SaveTest, ASaveFromANewerBuildIsRefused) {
  SaveGame future;
  future.set_format_version(kSaveFormatVersion + 1);
  std::string bytes;
  ASSERT_TRUE(future.SerializeToString(&bytes));
  WriteRaw(path_, bytes);

  std::unique_ptr<GameState> state = MakeState();
  LoadResult result = LoadGameFromFile(*state, path_);
  EXPECT_EQ(result.status, LoadStatus::kFromTheFuture);
  EXPECT_NE(result.message.find(path_), std::string::npos);
}

// A refused load must not have half-applied itself on the way to refusing.
TEST_F(SaveTest, ARefusedLoadLeavesTheCharacterAlone) {
  WriteRaw(path_, "not a save");
  std::unique_ptr<GameState> state = MakeState();
  state->character.AddMeso(777);
  state->current_map = "field";

  ASSERT_EQ(LoadGameFromFile(*state, path_).status, LoadStatus::kUnreadable);
  EXPECT_EQ(state->character.meso(), 777);
  EXPECT_EQ(state->current_map, "field");
}

// --- atomicity ---

// The bytes go somewhere else first, so an interrupted write cannot be
// mistaken for a save. Checked by leaving a stale temp file behind and showing
// the real save is still the one that loads.
TEST_F(SaveTest, AStaleTempFileIsNotTheSave) {
  std::unique_ptr<GameState> state = MakeState();
  state->character.AddMeso(4242);
  ASSERT_TRUE(SaveGameToFile(*state, path_));
  WriteRaw((dir_ / "ms.save.writing").string(), "half a write");

  std::unique_ptr<GameState> loaded = MakeState();
  EXPECT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.meso(), 4242);
}

// Nothing is left lying around after an ordinary save, or every autosave would
// leave a copy of the game behind it.
TEST_F(SaveTest, AFinishedSaveLeavesNoTempFile) {
  std::unique_ptr<GameState> state = MakeState();
  ASSERT_TRUE(SaveGameToFile(*state, path_));
  EXPECT_FALSE(std::filesystem::exists(dir_ / "ms.save.writing"));
}

// The previous save has to survive a write that cannot start -- which is what
// makes autosaving under a player who may close the window safe.
TEST_F(SaveTest, AFailedWriteLeavesThePreviousSaveIntact) {
  std::unique_ptr<GameState> state = MakeState();
  state->character.AddMeso(999);
  ASSERT_TRUE(SaveGameToFile(*state, path_));

  // A directory where the temp file wants to be: the open fails, and the save
  // never gets as far as the rename.
  std::filesystem::create_directory(dir_ / "ms.save.writing");
  state->character.AddMeso(1);
  EXPECT_FALSE(SaveGameToFile(*state, path_));

  std::filesystem::remove(dir_ / "ms.save.writing");
  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.meso(), 999) << "the old save, not a ruined one";
}

// --- SavePolicy ---

using Clock = std::chrono::steady_clock;

// An empty path turns saving off entirely, which is what keeps the workbench
// off a player's file.
TEST_F(SaveTest, NoPathWritesNothingAtAll) {
  std::unique_ptr<GameState> state = MakeState();
  Clock::time_point start = Clock::now();
  SavePolicy policy("", start);
  EXPECT_FALSE(policy.saving());
  EXPECT_FALSE(policy.Save(*state, start));
  EXPECT_FALSE(policy.AutosaveIfDue(*state, start + kAutosaveInterval * 10));
  EXPECT_FALSE(std::filesystem::exists(path_));
}

TEST_F(SaveTest, SaveWritesWhateverTheClockSays) {
  std::unique_ptr<GameState> state = MakeState();
  state->character.AddMeso(4242);
  SavePolicy policy(path_, Clock::now());
  ASSERT_TRUE(policy.saving());
  ASSERT_TRUE(policy.Save(*state, Clock::now()));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.meso(), 4242);
}

// The autosave clock starts at construction, so the first one is a whole
// interval in rather than on the first tick.
TEST_F(SaveTest, AutosaveWaitsOutTheWholeInterval) {
  std::unique_ptr<GameState> state = MakeState();
  Clock::time_point start = Clock::now();
  SavePolicy policy(path_, start);

  EXPECT_FALSE(policy.AutosaveIfDue(*state, start));
  EXPECT_FALSE(policy.AutosaveIfDue(
      *state, start + kAutosaveInterval - Clock::duration(1)));
  EXPECT_FALSE(std::filesystem::exists(path_));

  EXPECT_TRUE(policy.AutosaveIfDue(*state, start + kAutosaveInterval));
  EXPECT_TRUE(std::filesystem::exists(path_));
}

// Every write restarts the wait, the ones asked for outside the clock
// included -- a save on the way out is not a reason to write again a moment
// later.
TEST_F(SaveTest, EveryWriteRestartsTheWait) {
  std::unique_ptr<GameState> state = MakeState();
  Clock::time_point start = Clock::now();
  SavePolicy policy(path_, start);

  ASSERT_TRUE(policy.AutosaveIfDue(*state, start + kAutosaveInterval));
  EXPECT_FALSE(policy.AutosaveIfDue(
      *state, start + kAutosaveInterval * 2 - Clock::duration(1)));
  EXPECT_TRUE(policy.AutosaveIfDue(*state, start + kAutosaveInterval * 2));

  policy.Save(*state, start + kAutosaveInterval * 2 + kAutosaveInterval / 2);
  EXPECT_FALSE(policy.AutosaveIfDue(*state, start + kAutosaveInterval * 3));
}

// A write that cannot start is logged and swallowed: the game does not come
// down over it, and the previous save is still there.
TEST_F(SaveTest, AFailedWriteIsReportedAndSurvived) {
  std::unique_ptr<GameState> state = MakeState();
  SavePolicy policy(path_, Clock::now());
  std::filesystem::create_directory(dir_ / "ms.save.writing");
  EXPECT_FALSE(policy.Save(*state, Clock::now()));
  std::filesystem::remove(dir_ / "ms.save.writing");
}

}  // namespace
}  // namespace ms
