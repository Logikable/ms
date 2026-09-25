#include "src/frontend/screens/boss_select_panel.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/inner_ability.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/colors.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

Mob BossMob(const std::string& name, int level, int64_t max_hp, int pdr) {
  Mob mob;
  mob.set_name(name);
  mob.set_level(level);
  mob.set_max_hp(max_hp);
  mob.set_pdr(pdr);
  mob.set_boss(true);
  return mob;
}

void AddPhase(BossDifficulty* difficulty, const std::string& mob, int count) {
  Spawn* spawn = difficulty->add_phases()->add_spawns();
  spawn->set_mob(mob);
  spawn->set_count(count);
}

Boss Zakum() {
  Boss boss;
  boss.set_name("Zakum");
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_reset(RESET_PERIOD_DAILY);
  normal->set_time_limit_seconds(300);
  AddPhase(normal, "zakum_arm", 8);
  AddPhase(normal, "zakum", 1);
  return boss;
}

// A second, smaller boss, for checking the list order.
Boss Balrog() {
  Boss boss;
  boss.set_name("Balrog");
  BossDifficulty* easy = boss.add_difficulties();
  easy->set_name("Easy");
  easy->set_reset(RESET_PERIOD_WEEKLY);
  easy->set_time_limit_seconds(600);
  AddPhase(easy, "balrog", 1);
  BossDifficulty* normal = boss.add_difficulties();
  normal->set_name("Normal");
  normal->set_reset(RESET_PERIOD_WEEKLY);
  normal->set_time_limit_seconds(600);
  AddPhase(normal, "balrog", 1);
  return boss;
}

std::unique_ptr<GameState> WithBosses(bool two = false) {
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{},
      std::map<std::string, Mob>{
          {"zakum_arm", BossMob("Zakum's Arm", 110, 700000, 40)},
          {"zakum", BossMob("Zakum", 110, 7000000, 40)},
          {"balrog", BossMob("Balrog", 80, 100000, 20)}},
      std::map<std::string, MapData>{});
  state->bosses["zakum"] = Zakum();
  if (two) {
    state->bosses["balrog"] = Balrog();
  }
  return state;
}

// Raises `state`'s character to `level`, for fights that unlock partway up.
void LevelTo(GameState& state, int level) {
  while (state.character.proto().level() < level) {
    state.character.LevelUp();
  }
}

// The time the scrolling names are read at. The epoch shows every name from the
// start, where it stays for the first second.
constexpr std::chrono::steady_clock::time_point kHead;

// The width the panel actually takes, for checking whether a row pushed it
// wider.
int Width(const BossSelectPanel& panel) {
  ftxui::Element element = panel.Render(kHead);
  // Fit clamps to the terminal unless told not to, and this screen is wider
  // than the 80 columns a test terminal reports.
  return ftxui::Dimension::Fit(element, /*extend_beyond_screen=*/true).dimx;
}

// The panel's rows as text, for checking which comes before which.
std::vector<std::string> RenderRows(
    const BossSelectPanel& panel, int height = 32,
    std::chrono::steady_clock::time_point now = kHead) {
  ftxui::Element element = ftxui::hbox({panel.Render(now), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(height));
  ftxui::Render(screen, element);
  return ScreenRows(screen);
}

std::string Render(const BossSelectPanel& panel, int height = 32,
                   std::chrono::steady_clock::time_point now = kHead) {
  std::string out;
  for (const std::string& row : RenderRows(panel, height, now)) {
    out += row + "\n";
  }
  return out;
}

// The row containing `needle`, or -1 if the panel doesn't show it.
int RowOf(const std::vector<std::string>& rows, const std::string& needle) {
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].find(needle) != std::string::npos) {
      return i;
    }
  }
  return -1;
}

// The rendered screen, for something ToString() can't show: the dim flag, which
// isn't in the text.
ftxui::Screen RenderScreen(const BossSelectPanel& panel) {
  ftxui::Element element = ftxui::hbox({panel.Render(kHead), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(32));
  ftxui::Render(screen, element);
  return screen;
}

// The colour of the first character on the row containing `needle`. Read from
// the pixel, because ToString() loses colour: a red row and a white one give
// the same string.
ftxui::Color RowColor(const BossSelectPanel& panel, const std::string& needle) {
  ftxui::Element element = ftxui::hbox({panel.Render(kHead), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(32));
  ftxui::Render(screen, element);
  return ColorOf(screen, needle);
}

TEST(BossSelectPanelTest, TheDetailPanelDescribesTheFight) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  BossSelectPanel panel(state);
  std::string out = Render(panel);
  EXPECT_NE(out.find("Normal Zakum"), std::string::npos);
  EXPECT_NE(out.find("110"), std::string::npos);
  // Phase 1 is all eight arms together and phase 2 is the body, and a fight
  // with two phases numbers them.
  EXPECT_NE(out.find("P1 HP"), std::string::npos);
  EXPECT_NE(out.find("5.6M"), std::string::npos);
  EXPECT_NE(out.find("P2 HP"), std::string::npos);
  EXPECT_NE(out.find("7M"), std::string::npos);
  EXPECT_NE(out.find("40%"), std::string::npos);
  EXPECT_NE(out.find("5:00"), std::string::npos);
  EXPECT_NE(out.find("Daily"), std::string::npos);
  // Nothing drops yet, and an empty list says only that. A fight that pays no
  // EXP has no EXP row instead of one reading zero.
  EXPECT_NE(out.find("(empty)"), std::string::npos);
  EXPECT_EQ(out.find("EXP"), std::string::npos);
}

// The rewards a clear pays, which is what the player compares when there is
// more than one fight. An equip's name comes from the equip catalog.
TEST(BossSelectPanelTest, TheRewardsListNamesWhatAClearPays) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  EquipPrototype shoulder;
  shoulder.set_name("Royal Black Metal Shoulder");
  state.equips["royal_black_metal_shoulder"] = shoulder;
  ItemPrototype shard;
  shard.set_name("Zakum's Soul Shard");
  state.items["zakums_soul_shard"] = shard;
  BossDifficulty* normal = state.bosses["zakum"].mutable_difficulties(0);
  normal->set_meso(3062500);
  normal->set_exp(4750740);
  MobDrop* equip_drop = normal->add_drops();
  equip_drop->set_equip("royal_black_metal_shoulder");
  equip_drop->set_per_kill(0.5);
  MobDrop* item_drop = normal->add_drops();
  item_drop->set_item("zakums_soul_shard");
  item_drop->set_per_kill(1.0);

  BossSelectPanel panel(state);
  std::string out = Render(panel);
  EXPECT_NE(out.find("3,062,500"), std::string::npos);
  EXPECT_NE(out.find("EXP"), std::string::npos);
  EXPECT_NE(out.find("4,750,740"), std::string::npos);
  EXPECT_NE(out.find("50%"), std::string::npos);
  // A name too long for its column is cut to fit and scrolls; at the start of
  // the scroll its end isn't shown yet. A name that fits is shown whole.
  EXPECT_NE(out.find("Royal Black Metal"), std::string::npos);
  EXPECT_EQ(out.find("Royal Black Metal Shoulder"), std::string::npos);
  EXPECT_NE(out.find("Zakum's Soul Shard"), std::string::npos);
  EXPECT_NE(out.find("100%"), std::string::npos);
  EXPECT_EQ(out.find("(empty)"), std::string::npos);
}

// Honor is the third thing every clear with a reset pays, and it is listed with
// the meso and EXP, above the drops, which are chances.
TEST(BossSelectPanelTest, TheRewardsListNamesTheHonorAClearPays) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  LevelTo(state, kInnerAbilityUnlockLevel);
  BossDifficulty* normal = state.bosses["zakum"].mutable_difficulties(0);
  normal->set_meso(3062500);
  normal->set_exp(4750740);
  BossSelectPanel panel(state);
  std::vector<std::string> rows = RenderRows(panel);
  EXPECT_LT(RowOf(rows, "Meso"), RowOf(rows, "EXP"));
  EXPECT_LT(RowOf(rows, "EXP"), RowOf(rows, "Honor"));
  EXPECT_NE(rows[RowOf(rows, "Honor")].find("1,500"), std::string::npos);
}

// Nothing uses honor before Inner Ability unlocks, so it isn't shown.
TEST(BossSelectPanelTest, TheHonorRowWaitsForInnerAbility) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  BossSelectPanel panel(state);
  EXPECT_EQ(Render(panel).find("Honor"), std::string::npos);
}

// A fight with no reset pays no honor, so none is shown.
TEST(BossSelectPanelTest, AFightWithNoLockoutNamesNoHonor) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  LevelTo(state, kInnerAbilityUnlockLevel);
  state.bosses["zakum"].mutable_difficulties(0)->clear_reset();
  BossSelectPanel panel(state);
  EXPECT_EQ(Render(panel).find("Honor"), std::string::npos);
}

// The gear is separated by a rule from what every clear pays and the shard, and
// listed below it most common first, so the rarest drop is the last line.
TEST(BossSelectPanelTest, TheGearIsRuledOffAndOrderedByChance) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  EquipPrototype crystal;
  crystal.set_name("Crystal");
  state.equips["crystal"] = crystal;
  EquipPrototype eye;
  eye.set_name("Eye");
  state.equips["eye"] = eye;
  ItemPrototype shard;
  shard.set_name("Shard");
  state.items["shard"] = shard;
  BossDifficulty* normal = state.bosses["zakum"].mutable_difficulties(0);
  // Added rarest first, and with the equip, to show that neither decides the
  // order.
  MobDrop* rare = normal->add_drops();
  rare->set_equip("eye");
  rare->set_per_kill(0.1);
  MobDrop* common = normal->add_drops();
  common->set_equip("crystal");
  common->set_per_kill(0.5);
  MobDrop* item = normal->add_drops();
  item->set_item("shard");
  item->set_per_kill(1.0);

  BossSelectPanel panel(state);
  std::vector<std::string> rows = RenderRows(panel);
  int shard_row = RowOf(rows, "Shard");
  int crystal_row = RowOf(rows, "Crystal");
  EXPECT_LT(shard_row, crystal_row);
  EXPECT_LT(crystal_row, RowOf(rows, "Eye"));
  // The rule between them, and no other rule between the shard and the gear.
  EXPECT_EQ(crystal_row, shard_row + 2);
  EXPECT_NE(rows[shard_row + 1].find("\u2500"), std::string::npos);
}

// A token buys a piece of gear, so it goes below the rule with the gear rather
// than being read as something every clear pays.
TEST(BossSelectPanelTest, ATokenIsRuledOffWithTheGear) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  ItemPrototype shard;
  shard.set_name("Shard");
  shard.set_kind(ITEM_KIND_SOUL_SHARD);
  state.items["shard"] = shard;
  ItemPrototype token;
  token.set_name("Token");
  token.set_kind(ITEM_KIND_TOKEN);
  state.items["token"] = token;
  BossDifficulty* normal = state.bosses["zakum"].mutable_difficulties(0);
  MobDrop* token_drop = normal->add_drops();
  token_drop->set_item("token");
  token_drop->set_per_kill(1.0);
  MobDrop* shard_drop = normal->add_drops();
  shard_drop->set_item("shard");
  shard_drop->set_per_kill(1.0);

  BossSelectPanel panel(state);
  std::vector<std::string> rows = RenderRows(panel);
  int shard_row = RowOf(rows, "Shard");
  ASSERT_EQ(RowOf(rows, "Token"), shard_row + 2);
  EXPECT_NE(rows[shard_row + 1].find("\u2500"), std::string::npos);
}

// The reward names are the longest text on this screen. A name longer than the
// label and value columns must not push the panel wider; it scrolls inside its
// column, and the end appears once the start has been shown long enough to
// read.
TEST(BossSelectPanelTest, ALongRewardNameSlidesRatherThanWidenThePanel) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  EquipPrototype crystal;
  crystal.set_name("Aquatic Letter Eye Accessory");
  state.equips["aquatic_letter_eye_accessory"] = crystal;
  BossSelectPanel bare(state);
  int narrow = Width(bare);

  BossDifficulty* normal = state.bosses["zakum"].mutable_difficulties(0);
  MobDrop* drop = normal->add_drops();
  drop->set_equip("aquatic_letter_eye_accessory");
  drop->set_per_kill(0.5);
  BossSelectPanel wide(state);
  EXPECT_EQ(Width(wide), narrow);
  EXPECT_NE(Render(wide).find("Aquatic Letter"), std::string::npos);
  EXPECT_EQ(Render(wide).find("Eye Accessory"), std::string::npos);
  // Past the pause at the start and the 600ms scroll, inside the pause at the
  // far end with the end of the name showing.
  std::chrono::steady_clock::time_point slid =
      kHead + std::chrono::milliseconds(2000);
  EXPECT_NE(Render(wide, 32, slid).find("Eye Accessory"), std::string::npos);
}

// A single monster is just "HP", since there is no other phase to confuse it
// with.
TEST(BossSelectPanelTest, AOnePhaseFightLabelsItsHpPlainly) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossSelectPanel panel(state);  // Balrog sorts first: one phase.
  std::string out = Render(panel);
  EXPECT_NE(out.find("HP"), std::string::npos);
  EXPECT_EQ(out.find("P1 HP"), std::string::npos);
}

// Nothing on this screen moves as the cursor moves: both panels are 25 rows
// whatever fight is selected, and the options row below makes 28.
TEST(BossSelectPanelTest, TheScreenIsTheSameSizeForEveryFight) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossDifficulty* normal = state.bosses["zakum"].mutable_difficulties(0);
  normal->set_meso(3062500);
  ItemPrototype shard;
  shard.set_name("Zakum's Soul Shard");
  state.items["zakums_soul_shard"] = shard;
  for (int i = 0; i < 4; ++i) {
    MobDrop* drop = normal->add_drops();
    drop->set_item("zakums_soul_shard");
    drop->set_per_kill(1.0);
  }
  BossSelectPanel panel(state);
  ftxui::Element zakum = panel.Render(kHead);
  int width = Width(panel);
  EXPECT_EQ(ftxui::Dimension::Fit(zakum, true).dimy, 28);
  panel.MoveCursor(1);
  ftxui::Element balrog = panel.Render(kHead);
  EXPECT_EQ(ftxui::Dimension::Fit(balrog, true).dimy, 28);
  EXPECT_EQ(Width(panel), width);
}

TEST(BossSelectPanelTest, PhaseHpAndLevelReadOffTheMobs) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  const BossDifficulty& normal = state.bosses["zakum"].difficulties(0);
  EXPECT_EQ(PhaseHp(state, normal.phases(0)), 5600000);
  EXPECT_EQ(PhaseHp(state, normal.phases(1)), 7000000);
  EXPECT_EQ(BossLevel(state, normal), 110);
}

TEST(BossSelectPanelTest, TheListSortsByLevelAndTheCursorWraps) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossSelectPanel panel(state);
  EXPECT_EQ(panel.selected_boss(), "balrog");  // level 80 sorts first
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_boss(), "zakum");
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_boss(), "balrog");
  panel.MoveCursor(-1);
  EXPECT_EQ(panel.selected_boss(), "zakum");
}

// The column belongs to the grid, not to a fight: moving down in the second
// column stays in the second column. A fight with fewer difficulties is the one
// exception, and it doesn't lose the column.
TEST(BossSelectPanelTest, TheColumnIsHeldAcrossTheGridAndClampsToItsEnds) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossSelectPanel panel(state);
  ASSERT_EQ(panel.selected_boss(), "balrog");
  panel.ChangeDifficulty(1);
  EXPECT_EQ(panel.selected_title(), "Normal Balrog");
  // Moving past the last difficulty stays on it, since the ladder has a top.
  panel.ChangeDifficulty(1);
  EXPECT_EQ(panel.selected_difficulty(), 1);

  // Zakum has only one difficulty, so its row falls back to it...
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_title(), "Normal Zakum");
  EXPECT_EQ(panel.selected_difficulty(), 0);

  // ...and the column is still the second one on the way back.
  panel.MoveCursor(-1);
  EXPECT_EQ(panel.selected_title(), "Normal Balrog");
  EXPECT_EQ(panel.selected_difficulty(), 1);

  panel.ChangeDifficulty(-1);
  EXPECT_EQ(panel.selected_title(), "Easy Balrog");
  panel.ChangeDifficulty(-1);  // and a bottom
  EXPECT_EQ(panel.selected_difficulty(), 0);

  // Opening the screen starts again on the easiest.
  panel.ChangeDifficulty(1);
  panel.Reset();
  EXPECT_EQ(panel.selected_title(), "Easy Balrog");
}

// Every difficulty of a fight is shown on its row, and the cursor is shown by
// lighting the cell rather than with a caret. Both panels keep a blank column
// inside each border.
TEST(BossSelectPanelTest, TheGridShowsEveryDifficultyAndLightsTheChosenOne) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossSelectPanel panel(state);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(16));
  ftxui::Render(screen, panel.Render());
  std::string out = screen.ToString();
  EXPECT_EQ(out.find(">"), std::string::npos) << "no caret";
  std::string balrog = ScreenRow(screen, 3);
  EXPECT_NE(balrog.find("Easy"), std::string::npos);
  EXPECT_NE(balrog.find("Normal"), std::string::npos)
      << "both columns stand there, whichever is chosen";
  EXPECT_NE(out.find("\033[7mEasy"), std::string::npos)
      << "the chosen cell is inverted";
  panel.ChangeDifficulty(1);
  ftxui::Screen moved = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                              ftxui::Dimension::Fixed(16));
  ftxui::Render(moved, panel.Render());
  out = moved.ToString();
  EXPECT_EQ(out.find("\033[7mEasy"), std::string::npos);
  EXPECT_NE(out.find("\033[7mNormal"), std::string::npos)
      << "Right lights the next column instead of replacing the name";
  std::string header = ScreenRow(screen, 1);
  EXPECT_NE(header.find("│ Name"), std::string::npos)
      << "a column of clearance";
  EXPECT_NE(header.find(" │"), std::string::npos) << "on both sides";
  EXPECT_NE(ScreenRow(screen, 3).find(" │"), std::string::npos)
      << "the detail panel too";
}

// Green for a fight that can be entered, red for one waiting on its reset.
TEST(BossSelectPanelTest, AClearedFightSaysSoAndIsNotAvailable) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  BossSelectPanel panel(state);
  EXPECT_TRUE(panel.selected_available());
  EXPECT_EQ(panel.selected_reset(), RESET_PERIOD_DAILY);
  EXPECT_NE(Render(panel).find("Available"), std::string::npos);

  state.character.RecordBossClear("zakum", "Normal",
                                  static_cast<int64_t>(std::time(nullptr)));
  EXPECT_FALSE(panel.selected_available());
  EXPECT_NE(Render(panel).find("Cleared"), std::string::npos);
}

// Beating a boss at any difficulty counts as beating the boss: the difficulty
// next to the one that was beaten also reads Cleared and waits for the same
// reset.
TEST(BossSelectPanelTest, AClearOfOneDifficultyClosesTheOthers) {
  std::unique_ptr<GameState> owner = WithBosses(true);
  GameState& state = *owner;
  BossSelectPanel panel(state);
  ASSERT_EQ(panel.selected_boss(), "balrog") << "the smaller fight sorts first";
  state.character.RecordBossClear("balrog", "Normal",
                                  static_cast<int64_t>(std::time(nullptr)));

  // The cursor is still on Easy, which nobody has beaten.
  EXPECT_EQ(panel.selected()->name(), "Easy");
  EXPECT_FALSE(panel.selected_available());
  EXPECT_NE(Render(panel).find("Cleared"), std::string::npos);

  // The other fight on the list is unaffected.
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_boss(), "zakum");
  EXPECT_TRUE(panel.selected_available());
}

TEST(BossSelectPanelTest, AnEmptyCatalogDrawsWithoutAFight) {
  GameState state(
      std::map<std::string, EquipPrototype>{}, std::map<std::string, Scroll>{},
      std::map<std::string, ItemPrototype>{}, std::map<std::string, Mob>{},
      std::map<std::string, MapData>{});
  BossSelectPanel panel(state);
  EXPECT_TRUE(panel.selected_boss().empty());
  EXPECT_EQ(panel.selected(), nullptr);
  EXPECT_FALSE(panel.selected_available());
  panel.MoveCursor(1);
  panel.ChangeDifficulty(1);
  EXPECT_NE(Render(panel).find("(empty)"), std::string::npos);
}

// --- a difficulty that opens partway up the ladder ---

// The row is below the fight's own level: what the player faces, then what it
// takes to enter. Red while they are below it.
TEST(BossSelectPanelTest, ALockedFightNamesTheLevelItWants) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  state.bosses["zakum"].mutable_difficulties(0)->set_unlock_level(130);
  BossSelectPanel panel(state);
  EXPECT_NE(Render(panel).find("Unlock Level"), std::string::npos);
  EXPECT_NE(Render(panel).find("130"), std::string::npos);
  EXPECT_FALSE(panel.selected_unlocked());
  EXPECT_EQ(panel.selected_unlock_level(), 130);
}

// Neither "Available" nor "Cleared" applies to a fight that can't be entered at
// all.
TEST(BossSelectPanelTest, ALockedFightReadsLockedRatherThanAvailable) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  state.bosses["zakum"].mutable_difficulties(0)->set_unlock_level(130);
  BossSelectPanel panel(state);
  std::string out = Render(panel);
  EXPECT_NE(out.find("Locked"), std::string::npos);
  EXPECT_EQ(out.find("Available"), std::string::npos);
}

// Red marks the reason: the one value the player falls short of. Both cells
// showing it are red, and neither is once the level is reached.
TEST(BossSelectPanelTest, TheLevelAndTheStatusGoRedWhileItIsOutOfReach) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  state.bosses["zakum"].mutable_difficulties(0)->set_unlock_level(130);
  BossSelectPanel locked(state);
  EXPECT_EQ(RowColor(locked, "Unlock Level"), kRed);
  EXPECT_EQ(RowColor(locked, "Locked"), kRed);

  LevelTo(state, 130);
  BossSelectPanel open(state);
  EXPECT_NE(RowColor(open, "Unlock Level"), kRed)
      << "a level already reached is not something to warn about";
}

// Reaching the level unlocks it, row and all.
TEST(BossSelectPanelTest, ReachingTheLevelUnlocksTheFight) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  state.bosses["zakum"].mutable_difficulties(0)->set_unlock_level(130);
  LevelTo(state, 130);
  BossSelectPanel panel(state);
  EXPECT_TRUE(panel.selected_unlocked());
  std::string out = Render(panel);
  EXPECT_NE(out.find("Unlock Level"), std::string::npos)
      << "the row stays once passed -- it is a fact about the fight";
  EXPECT_NE(out.find("Available"), std::string::npos);
  EXPECT_EQ(out.find("Locked"), std::string::npos);
}

// A fight with no unlock level of its own shows no row, instead of one reading
// 0.
TEST(BossSelectPanelTest, AnUngatedFightHasNoUnlockRow) {
  std::unique_ptr<GameState> owner = WithBosses();
  BossSelectPanel panel(*owner);
  EXPECT_EQ(Render(panel).find("Unlock Level"), std::string::npos);
}

// With neither fight gated, the list is ordered by the easiest difficulty's HP,
// whatever the fight is called.
TEST(BossSelectPanelTest, TheListSortsByTheHpOfTheEasiestDifficulty) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossSelectPanel small(state);
  EXPECT_EQ(small.selected_boss(), "balrog") << "100k against Zakum's 12.6M";
  // Raising his HP above Zakum's arms and body combined must move him below.
  state.mobs["balrog"].set_max_hp(20000000);
  BossSelectPanel big(state);
  EXPECT_EQ(big.selected_boss(), "zakum");
  big.MoveCursor(1);
  EXPECT_EQ(big.selected_boss(), "balrog");
}

// Adds a Chaos difficulty to Zakum that is defined but not built, with a body
// large enough to need a 64-bit HP.
BossDifficulty* AddChaosZakum(GameState& state) {
  state.mobs["chaos_zakum"] = BossMob("Chaos Zakum", 180, 84000000000LL, 100);
  BossDifficulty* chaos = state.bosses["zakum"].add_difficulties();
  chaos->set_name("Chaos");
  chaos->set_coming_soon(true);
  AddPhase(chaos, "chaos_zakum", 1);
  return chaos;
}

// The whole panel for a fight that isn't built yet: the "coming soon" line, a
// blank line, the HP, and nothing that hasn't been decided.
TEST(BossSelectPanelTest, AComingSoonFightShowsItsHpAndNothingElse) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  AddChaosZakum(state);
  BossSelectPanel panel(state);
  panel.ChangeDifficulty(1);
  ASSERT_TRUE(panel.selected_coming_soon());
  std::string out = Render(panel);
  EXPECT_NE(out.find("Coming soon!"), std::string::npos);
  EXPECT_NE(out.find("84B"), std::string::npos) << "the HP still reads";
  EXPECT_EQ(out.find("Time Limit"), std::string::npos);
  EXPECT_EQ(out.find("Reset"), std::string::npos);
  EXPECT_EQ(out.find("Rewards"), std::string::npos);
  EXPECT_EQ(out.find("Available"), std::string::npos);
  EXPECT_EQ(out.find("PDR"), std::string::npos);
  // Gold says the fight is coming rather than refused, which is why it isn't
  // the red used for a shortfall.
  EXPECT_EQ(RowColor(panel, "Coming soon!"), kYellow);
}

// The blank row keeps the HP apart from the "coming soon" line so the two don't
// read as one sentence.
TEST(BossSelectPanelTest, ABlankRowSeparatesThePromiseFromTheHp) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  AddChaosZakum(state);
  BossSelectPanel panel(state);
  panel.ChangeDifficulty(1);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(24));
  ftxui::Render(screen, ftxui::hbox({panel.Render(), ftxui::filler()}));
  int at = -1;
  for (int y = 0; y < screen.dimy(); ++y) {
    if (ScreenRow(screen, y).find("Coming soon!") != std::string::npos) {
      at = y;
    }
  }
  ASSERT_GE(at, 0);
  EXPECT_EQ(ScreenRow(screen, at + 1).find("HP"), std::string::npos)
      << "a blank row comes between";
  EXPECT_NE(ScreenRow(screen, at + 2).find("HP"), std::string::npos);
}

// Dimming marks a fight that can't be entered, and this one never can at any
// level. The cursor takes priority: a selected cell is lit even when the fight
// is closed.
TEST(BossSelectPanelTest, AComingSoonFightIsDimAndNeverEnterable) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  AddChaosZakum(state);
  LevelTo(state, 200);
  BossSelectPanel panel(state);
  EXPECT_FALSE(panel.selected_coming_soon()) << "Normal is built";

  auto render = [&panel]() {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                                 ftxui::Dimension::Fixed(16));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  };
  EXPECT_NE(render().find("\033[2m"), std::string::npos)
      << "Chaos is dimmed while the cursor is on Normal";

  panel.ChangeDifficulty(1);
  EXPECT_TRUE(panel.selected_coming_soon());
  EXPECT_EQ(render().find("\033[2m"), std::string::npos)
      << "and lit rather than dimmed once the cursor is on it";
}

// The three windows form one ring, and the screen opens on the grid.
TEST(BossSelectPanelTest, TabWalksTheThreeWindows) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  BossSelectPanel panel(state);
  EXPECT_EQ(panel.focus(), BossPanel::kList);
  panel.SwitchPanel(1);
  EXPECT_EQ(panel.focus(), BossPanel::kFight);
  panel.SwitchPanel(1);
  EXPECT_EQ(panel.focus(), BossPanel::kOptions);
  panel.SwitchPanel(1);
  EXPECT_EQ(panel.focus(), BossPanel::kList);
  panel.SwitchPanel(-1);
  EXPECT_EQ(panel.focus(), BossPanel::kOptions);
  panel.Reset();
  EXPECT_EQ(panel.focus(), BossPanel::kList);
}

// A fight card with more rewards than fit scrolls them with a bar, and the
// arrows that scroll it don't move the grid's cursor. They stop at both ends,
// since a list at its bottom has nowhere further to go.
TEST(BossSelectPanelTest, TheFightCardScrollsItsRewards) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  ItemPrototype shard;
  shard.set_name("Zakum's Soul Shard");
  state.items["zakums_soul_shard"] = shard;
  BossDifficulty* normal = state.bosses["zakum"].mutable_difficulties(0);
  for (int i = 0; i < 20; ++i) {
    MobDrop* drop = normal->add_drops();
    drop->set_item("zakums_soul_shard");
    drop->set_per_kill(1.0 / (i + 1));
  }
  BossSelectPanel panel(state);
  panel.MoveCursor(1);  // Balrog sorts first, being the smaller fight.
  std::string top = Render(panel);
  // The bar is drawn only once something is below the window.
  EXPECT_NE(top.find("\u2503"), std::string::npos);
  EXPECT_NE(top.find("100%"), std::string::npos);

  panel.SwitchPanel(1);
  panel.MoveCursor(3);
  EXPECT_EQ(panel.selected_boss(), "zakum");
  std::string scrolled = Render(panel);
  EXPECT_EQ(scrolled.find("100%"), std::string::npos);
  // Left and Right belong to the grid, and a card with the keys doesn't change
  // its column.
  panel.ChangeDifficulty(1);
  EXPECT_EQ(panel.selected_difficulty(), 0);

  panel.MoveCursor(-9);
  EXPECT_EQ(Render(panel), top);
  for (int i = 0; i < 30; ++i) {
    panel.MoveCursor(1);
  }
  std::string foot = Render(panel);
  panel.MoveCursor(1);
  EXPECT_EQ(Render(panel), foot);
}

// The rewards reset to the top when the cursor moves: a fight with two drops
// has nothing to show at another fight's offset.
TEST(BossSelectPanelTest, MovingTheCursorPutsTheRewardsBackAtTheTop) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  ItemPrototype shard;
  shard.set_name("Zakum's Soul Shard");
  state.items["zakums_soul_shard"] = shard;
  BossDifficulty* normal = state.bosses["zakum"].mutable_difficulties(0);
  for (int i = 0; i < 20; ++i) {
    MobDrop* drop = normal->add_drops();
    drop->set_item("zakums_soul_shard");
    drop->set_per_kill(1.0 / (i + 1));
  }
  BossSelectPanel panel(state);
  std::string top = Render(panel);
  panel.SwitchPanel(1);
  panel.MoveCursor(5);
  panel.SwitchPanel(-1);
  panel.MoveCursor(1);
  panel.MoveCursor(-1);
  EXPECT_EQ(Render(panel), top);
}

// The options row runs under both panels and keeps its space even when empty,
// so filling it doesn't move them.
TEST(BossSelectPanelTest, AnOptionsRowRunsUnderBothPanels) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  BossSelectPanel panel(state);
  std::vector<std::string> rows = RenderRows(panel);
  int options = RowOf(rows, "Options");
  EXPECT_GT(options, RowOf(rows, "Bosses"));
  EXPECT_GT(options, RowOf(rows, "Rewards"));
  EXPECT_EQ(options, 25);
}

// A fight's name scrolls inside the grid's column instead of reaching the
// Difficulty beside it, whichever window has the keys.
TEST(BossSelectPanelTest, ALongFightNameSlidesUnderItsColumn) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  state.bosses["zakum"].set_name("Zakum The Everlasting Flame");
  BossSelectPanel panel(state);
  EXPECT_NE(Render(panel).find("Zakum The Everlasting"), std::string::npos);
  EXPECT_EQ(Render(panel).find("Flame "), std::string::npos);
  // Past the pause at the start and the 750ms scroll.
  std::chrono::steady_clock::time_point slid =
      kHead + std::chrono::milliseconds(2200);
  EXPECT_NE(Render(panel, 32, slid).find("Flame "), std::string::npos);
  panel.SwitchPanel(2);
  EXPECT_NE(Render(panel, 32, slid).find("Flame "), std::string::npos);
}

// The row draws the switch in its current state, and Left and Right stay on the
// only switch there is.
TEST(BossSelectPanelTest, TheOptionsRowDrawsThePracticeSwitch) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  BossSelectPanel panel(state);
  std::vector<std::string> rows = RenderRows(panel);
  EXPECT_NE(rows[RowOf(rows, "Options") + 1].find("[ ] Practice"),
            std::string::npos);

  state.boss_options.set_practice(true);
  EXPECT_TRUE(panel.practice());
  rows = RenderRows(panel);
  EXPECT_NE(rows[RowOf(rows, "Options") + 1].find("[✓] Practice"),
            std::string::npos);

  panel.SwitchPanel(2);
  ASSERT_EQ(panel.focus(), BossPanel::kOptions);
  panel.ChangeDifficulty(1);
  EXPECT_EQ(panel.selected_option(), 0);
  panel.ChangeDifficulty(-1);
  EXPECT_EQ(panel.selected_option(), 0);
  // Left and Right didn't move the grid's own cursor.
  EXPECT_EQ(panel.selected_difficulty(), 0);
}

// Practice ignores the reset, so "Cleared" no longer stands between the player
// and the fight, and the rewards it won't pay are dimmed.
TEST(BossSelectPanelTest, PracticeRestatesTheStatusAndDimsTheRewards) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  state.bosses["zakum"].mutable_difficulties(0)->set_meso(3062500);
  state.character.RecordBossClear("zakum", "Normal", std::time(nullptr));
  BossSelectPanel panel(state);
  EXPECT_NE(Render(panel).find("Cleared"), std::string::npos);
  EXPECT_FALSE(PixelOf(RenderScreen(panel), "Meso").dim);

  state.boss_options.set_practice(true);
  std::string out = Render(panel);
  EXPECT_NE(out.find("Practice"), std::string::npos);
  EXPECT_EQ(out.find("Cleared"), std::string::npos);
  EXPECT_EQ(RowColor(panel, "Status"), kYellow);
  // tmux capture-pane loses the dim flag, so it can only be checked here.
  EXPECT_TRUE(PixelOf(RenderScreen(panel), "Meso").dim);
  EXPECT_TRUE(PixelOf(RenderScreen(panel), "Rewards").dim);
  // A fight the character's level is too low for stays locked: practice skips
  // the reset, not the level requirement.
  state.bosses["zakum"].mutable_difficulties(0)->set_unlock_level(300);
  EXPECT_NE(Render(panel).find("Locked"), std::string::npos);
}

// The list, the detail panel beside it and the options row are each fitted to
// their own rows.
TEST(BossSelectPanelTest, NoPanelWeldsARowToItsRightBorder) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  BossSelectPanel panel(*owner);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render(kHead)).empty());
  panel.SwitchPanel(1);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render(kHead)).empty());
}
}  // namespace
}  // namespace ms
