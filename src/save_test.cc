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
#include <vector>

#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/keybinds.pb.h"
#include "src/protos/save.pb.h"
#include "src/protos/skill.pb.h"

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
  std::unique_ptr<GameState> MakeState(
      std::map<std::string, Skill> skills = {}) {
    // Keyed by data-file stem like the real catalogs, not by display name --
    // a save refers to items by the name the player sees, and a fixture that
    // conflated the two would hide a lookup against the wrong key.
    std::map<std::string, EquipPrototype> equips{{"sword", sword_}};
    std::map<std::string, ItemPrototype> items{{"green_snail_shell", shell_}};
    return std::make_unique<GameState>(equips, std::map<std::string, Scroll>{},
                                       items, std::map<std::string, Mob>{},
                                       std::map<std::string, MapData>{},
                                       std::move(skills));
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

  // Writes `save` as the file a version 1 build would have left, whatever it
  // was carrying.
  void WriteV1(SaveGameV1 save) {
    save.set_format_version(1);
    std::string bytes;
    ASSERT_TRUE(save.SerializeToString(&bytes));
    WriteRaw(path_, bytes);
  }

  // A version 1 character with `keys` in seen_tabs, which was field 14. The
  // field is reserved now, so the only way to write one is the way the wire
  // held it.
  static Character WithSeenTabs(const std::vector<std::string>& keys) {
    Character character;
    for (const std::string& key : keys) {
      character.GetReflection()
          ->MutableUnknownFields(&character)
          ->AddLengthDelimited(14, key);
    }
    return character;
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
  auto potted = std::make_unique<EquipInstance>(sword_);
  std::mt19937 cube_rng(1);
  ASSERT_TRUE(potted->Cube(CubeType::kRed, cube_rng));
  const Potential rolled = potted->potential();
  saved->character.PickUp(std::move(potted));
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
  // A rolled potential is worth more than the item under it, so it had better
  // come back with it.
  const EquipInstance* back = loaded->character.inventory().equip_instance(0);
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->potential().rank(), rolled.rank());
  ASSERT_EQ(back->potential().lines_size(), rolled.lines_size());
  EXPECT_EQ(back->potential().lines(0).type(), rolled.lines(0).type());
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

// A toggle is a standing choice about how the character fights, not a thing
// about the session it was flicked in, so it comes back switched on.
TEST_F(SaveTest, WritesAndReadsBackASwitchedOnToggle) {
  Skill toggle;
  toggle.set_name("Righteously Indignant");
  toggle.set_kind(SKILL_KIND_ACTIVE);
  toggle.set_job_advancement(JOB_ADVANCEMENT_BISHOP);
  toggle.set_max_level(1);
  toggle.set_hyper(true);
  toggle.set_required_level(140);
  toggle.set_toggle(true);
  std::map<std::string, Skill> skills{{"righteously_indignant", toggle}};

  std::unique_ptr<GameState> saved = MakeState(skills);
  Character bishop;
  bishop.set_level(140);
  bishop.set_job(JOB_BISHOP);
  bishop.set_job_stage(4);
  bishop.set_hyper_sp(1);
  saved->character.RestoreFrom(bishop, saved->equips, saved->items);
  ASSERT_TRUE(saved->character.LearnSkill(toggle));
  ASSERT_TRUE(saved->character.ToggleSkill(toggle));
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState(skills);
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_TRUE(loaded->character.SkillToggledOn("Righteously Indignant"));
}

TEST_F(SaveTest, WritesAndReadsBackBothHyperStatPresets) {
  std::unique_ptr<GameState> saved = MakeState();
  Character grown;
  grown.set_level(160);
  saved->character.RestoreFrom(grown, saved->equips, saved->items);
  ASSERT_TRUE(saved->character.AllocateHyperStat(HYPER_STAT_FIELD_EXP,
                                                 StatPreset::kFarming, 4));
  ASSERT_TRUE(saved->character.AllocateHyperStat(HYPER_STAT_FIELD_BOSS_DAMAGE,
                                                 StatPreset::kBossing, 5));
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.hyper_stat_level(HYPER_STAT_FIELD_EXP,
                                               StatPreset::kFarming),
            4);
  EXPECT_EQ(loaded->character.hyper_stat_level(HYPER_STAT_FIELD_BOSS_DAMAGE,
                                               StatPreset::kBossing),
            5);
  EXPECT_EQ(loaded->character.hyper_stat_points_left(StatPreset::kFarming),
            75 - 15);
}

// A save whose allocation outspends the points the level pays -- an older set
// of rules, or a hand-edited file. Loading trims it rather than letting the
// character keep what they never earned.
TEST_F(SaveTest, ASaveWithOverspentHyperStatsIsTrimmed) {
  std::unique_ptr<GameState> saved = MakeState();
  Character grown;
  grown.set_level(145);
  (*grown.mutable_hyper_stats()
        ->mutable_farming()
        ->mutable_levels())[HYPER_STAT_FIELD_DAMAGE] = 10;
  saved->character.RestoreFrom(grown, saved->equips, saved->items);
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_LE(loaded->character.hyper_stat_level(HYPER_STAT_FIELD_DAMAGE), 5);
  EXPECT_GE(loaded->character.hyper_stat_points_left(), 0);
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
  old.add_characters()->mutable_character()->set_level(1);
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
  ASSERT_EQ(on_disk.characters(0).character().ap(), 45);
  on_disk.mutable_characters(0)->mutable_character()->set_ap(0);
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
  Keybind* row = saved->account.mutable_keybinds()->add_binds();
  row->set_action(KEY_ACTION_UP);
  row->add_keys("Up");
  row->add_keys("W");
  row->add_keys("");
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  ASSERT_EQ(loaded->account.keybinds().binds_size(), 1);
  EXPECT_EQ(loaded->account.keybinds().binds(0).action(), KEY_ACTION_UP);
  EXPECT_EQ(loaded->account.keybinds().binds(0).keys(1), "W");
}

TEST_F(SaveTest, WritesAndReadsBackTheMultiplayerAccount) {
  std::unique_ptr<GameState> saved = MakeState();
  saved->account.SetMultiplayerAccount("0123456789abcdef", "a-token");
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->account.multiplayer_account_id(), "0123456789abcdef");
  EXPECT_EQ(loaded->account.multiplayer_token(), "a-token");
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
  WriteV1(SaveGameV1());

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

// The write stamps itself, so what comes back is when the file was written
// rather than anything the state was carrying. This is the clock offline
// progress is measured from.
TEST_F(SaveTest, TheSaveStampsWhenItWasWritten) {
  std::unique_ptr<GameState> saved = MakeState();
  saved->last_seen_unix_seconds = 1500000000;  // overwritten by the write
  std::int64_t before = static_cast<std::int64_t>(std::time(nullptr));
  ASSERT_TRUE(SaveGameToFile(*saved, path_));
  std::int64_t after = static_cast<std::int64_t>(std::time(nullptr));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_GE(loaded->last_seen_unix_seconds, before);
  EXPECT_LE(loaded->last_seen_unix_seconds, after);
}

// A save written before the field existed has no stamp, which credits no
// absence rather than one measured from the epoch.
TEST_F(SaveTest, AnOldSaveHasNoLastSeenStamp) {
  WriteV1(SaveGameV1());

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->last_seen_unix_seconds, 0);
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

// --- more than one character ---

// The characters a session is not playing are never unpacked, so the write has
// to put them back exactly as they arrived, in the slots they came from.
TEST_F(SaveTest, TheCharactersNotBeingPlayedSurviveASave) {
  std::unique_ptr<GameState> state = MakeState();
  state->character.SetUsername("Second");
  state->active_character = 1;
  CharacterSave first;
  first.mutable_character()->set_name("First");
  first.mutable_character()->set_level(42);
  first.set_current_map("lith");
  state->inactive_characters.push_back(first);
  ASSERT_TRUE(SaveGameToFile(*state, path_));

  SaveGame on_disk;
  ASSERT_TRUE(on_disk.ParseFromString(ReadRaw(path_)));
  ASSERT_EQ(on_disk.characters_size(), 2);
  EXPECT_EQ(on_disk.characters(0).character().name(), "First");
  EXPECT_EQ(on_disk.characters(0).current_map(), "lith");
  EXPECT_EQ(on_disk.characters(1).character().name(), "Second");
  EXPECT_EQ(on_disk.active_character(), 1);
}

TEST_F(SaveTest, TheActiveSlotIsTheOneLoaded) {
  std::unique_ptr<GameState> saved = MakeState();
  saved->character.SetUsername("Second");
  saved->character.AddMeso(90);
  saved->active_character = 1;
  CharacterSave first;
  first.mutable_character()->set_name("First");
  first.set_playtime_seconds(1200);
  saved->inactive_characters.push_back(first);
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.username(), "Second");
  EXPECT_EQ(loaded->character.meso(), 90);
  EXPECT_EQ(loaded->active_character, 1);
  ASSERT_EQ(loaded->inactive_characters.size(), 1u);
  EXPECT_EQ(loaded->inactive_characters[0].character().name(), "First");
  EXPECT_EQ(loaded->playtime_seconds, 0.0) << "the played character's clock";
}

// A hand-edited file, or one whose active slot was deleted. The first
// character is a better answer than refusing to load the save at all.
TEST_F(SaveTest, AnActiveSlotOutOfRangeLoadsTheFirst) {
  SaveGame save;
  save.set_format_version(kSaveFormatVersion);
  save.add_characters()->mutable_character()->set_name("First");
  save.set_active_character(7);
  std::string bytes;
  ASSERT_TRUE(save.SerializeToString(&bytes));
  WriteRaw(path_, bytes);

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.username(), "First");
  EXPECT_EQ(loaded->active_character, 0);
}

// A save with an account and no characters is not a save this build wrote.
TEST_F(SaveTest, ASaveWithNoCharactersIsRefused) {
  SaveGame save;
  save.set_format_version(kSaveFormatVersion);
  save.mutable_account()->set_max_level(30);
  std::string bytes;
  ASSERT_TRUE(save.SerializeToString(&bytes));
  WriteRaw(path_, bytes);

  std::unique_ptr<GameState> state = MakeState();
  EXPECT_EQ(LoadGameFromFile(*state, path_).status, LoadStatus::kUnreadable);
}

// --- what the account keeps ---

TEST_F(SaveTest, WritesAndReadsBackTheSeenKeys) {
  std::unique_ptr<GameState> saved = MakeState();
  saved->account.MarkSeen("shop");
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_TRUE(loaded->account.Seen("shop"));
  EXPECT_FALSE(loaded->account.Seen("skills"));
}

// The watermark is what a second character inherits, so the write has to fold
// in how far the played character has climbed since the file was last read.
TEST_F(SaveTest, TheWriteRecordsTheClimbOnTheAccount) {
  std::unique_ptr<GameState> saved = MakeState();
  for (int i = 0; i < 20; ++i) {
    saved->character.LevelUp();
  }
  saved->character.AdvanceJob(JOB_SWORDMAN);
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->account.max_level(), 21);
  EXPECT_EQ(loaded->account.max_job_stage(), 1);
}

// A character the player is not on still counts: the account opened what they
// opened, whichever of them did it.
TEST_F(SaveTest, TheUnlocksTakeInEveryCharacterInTheFile) {
  std::unique_ptr<GameState> saved = MakeState();
  CharacterSave veteran;
  veteran.mutable_character()->set_level(120);
  veteran.mutable_character()->set_job_stage(3);
  saved->inactive_characters.push_back(veteran);
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->account.max_level(), 120);
  EXPECT_EQ(loaded->account.max_job_stage(), 3);
}

// --- reading a version 1 save ---

TEST_F(SaveTest, AVersion1SaveLoadsAsTheOneCharacter) {
  SaveGameV1 old;
  old.mutable_character()->set_name("Only");
  old.mutable_character()->set_level(45);
  old.mutable_character()->set_meso(5000);
  old.set_current_map("lith");
  old.set_created_unix_seconds(1500000000);
  old.set_playtime_seconds(3600);
  old.set_last_seen_unix_seconds(1700000000);
  WriteV1(old);

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.username(), "Only");
  EXPECT_EQ(loaded->character.meso(), 5000);
  EXPECT_EQ(loaded->current_map, "lith");
  EXPECT_EQ(loaded->created_unix_seconds, 1500000000);
  EXPECT_EQ(loaded->playtime_seconds, 3600.0);
  EXPECT_EQ(loaded->last_seen_unix_seconds, 1700000000);
  EXPECT_EQ(loaded->active_character, 0);
  EXPECT_TRUE(loaded->inactive_characters.empty());
}

// The three things that moved to the account. The keys were the character's in
// version 1, and a player who has walked a gold trail once has walked it.
TEST_F(SaveTest, AVersion1SaveMovesItsAccountStateOff) {
  SaveGameV1 old;
  *old.mutable_character() = WithSeenTabs({"shop", "lead_action:scrolling"});
  old.mutable_character()->set_level(75);
  old.mutable_character()->set_job_stage(2);
  Keybind* row = old.mutable_keybinds()->add_binds();
  row->set_action(KEY_ACTION_UP);
  row->add_keys("W");
  WriteV1(old);

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_TRUE(loaded->account.Seen("shop"));
  EXPECT_TRUE(loaded->account.Seen("lead_action:scrolling"));
  EXPECT_FALSE(loaded->account.Seen("skills"));
  ASSERT_EQ(loaded->account.keybinds().binds_size(), 1);
  EXPECT_EQ(loaded->account.keybinds().binds(0).keys(0), "W");
  EXPECT_EQ(loaded->account.max_level(), 75)
      << "the account inherits what the one character opened";
  EXPECT_EQ(loaded->account.max_job_stage(), 2);
}

// Loading an old save and saving again writes the new format, and the upgrade
// is not run a second time.
TEST_F(SaveTest, AnUpgradedSaveIsWrittenBackAtTheNewVersion) {
  SaveGameV1 old;
  old.mutable_character()->set_name("Only");
  old.mutable_character()->set_level(45);
  WriteV1(old);

  std::unique_ptr<GameState> loaded = MakeState();
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  ASSERT_TRUE(SaveGameToFile(*loaded, path_));

  SaveGame on_disk;
  ASSERT_TRUE(on_disk.ParseFromString(ReadRaw(path_)));
  EXPECT_EQ(on_disk.format_version(), kSaveFormatVersion);
  ASSERT_EQ(on_disk.characters_size(), 1);
  EXPECT_EQ(on_disk.characters(0).character().name(), "Only");
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

// The skills' own door, beside the AP one above. A book whose maximums have
// come down since the save was written is put back inside them, and the levels
// it takes off are spent again rather than lost.
TEST_F(SaveTest, ASaveTaughtPastAMaximumIsPutBackInsideItsBook) {
  std::map<std::string, Skill> skills;
  for (const std::pair<std::string, int> entry :
       {std::pair<std::string, int>{"Slash Blast", 10},
        std::pair<std::string, int>{"Iron Body", 20}}) {
    Skill& skill = skills[entry.first];
    skill.set_name(entry.first);
    skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
    skill.set_max_level(entry.second);
  }
  std::unique_ptr<GameState> saved = MakeState(skills);
  saved->character.AdvanceJob(JOB_SWORDMAN);
  ASSERT_TRUE(SaveGameToFile(*saved, path_));

  // Rewritten as a save from when Slash Blast still went to 20.
  SaveGame on_disk;
  ASSERT_TRUE(on_disk.ParseFromString(ReadRaw(path_)));
  (*on_disk.mutable_characters(0)
        ->mutable_character()
        ->mutable_skill_levels())["Slash Blast"] = 20;
  std::string bytes;
  ASSERT_TRUE(on_disk.SerializeToString(&bytes));
  WriteRaw(path_, bytes);

  std::unique_ptr<GameState> loaded = MakeState(skills);
  ASSERT_EQ(LoadGameFromFile(*loaded, path_).status, LoadStatus::kLoaded);
  EXPECT_EQ(loaded->character.skill_level(skills.at("Slash Blast")), 10);
  EXPECT_EQ(loaded->character.skill_level(skills.at("Iron Body")), 10)
      << "the ten it gave up went nowhere";
}

}  // namespace
}  // namespace ms
