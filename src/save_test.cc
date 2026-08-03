#include "src/save.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>

#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
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
    std::map<std::string, EquipPrototype> equips{{"Sword", sword_}};
    std::map<std::string, ItemPrototype> items{{"Green Snail Shell", shell_}};
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
TEST_F(SaveTest, ABareProgramNameFallsBackToTheWorkingDirectory) {
  EXPECT_EQ(SavePathFor("ms"), "ms.save");
}

// --- round trip through the file ---

TEST_F(SaveTest, WritesAndReadsBackACharacter) {
  std::unique_ptr<GameState> saved = MakeState();
  saved->character.LevelUp();
  saved->character.AddMeso(1234);
  saved->character.PickUp(std::make_unique<EquipInstance>(sword_));
  saved->character.AddStackable(shell_, 17);
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
  EXPECT_EQ(loaded->current_map, "lith");
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

}  // namespace
}  // namespace ms
