#include "src/frontend/screens/boss_select_panel.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
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

Mob BossMob(const std::string& name, int level, int max_hp, int pdr) {
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

// A second boss, lower level, to check the list sorts by the level fought at.
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

// Raises `state`'s character to `level`, for the fights that only open partway
// up the ladder.
void LevelTo(GameState& state, int level) {
  while (state.character.proto().level() < level) {
    state.character.LevelUp();
  }
}

// The columns the panel actually takes, for asking whether a row inside it
// pushed it wider.
int Width(const BossSelectPanel& panel) {
  ftxui::Element element = panel.Render();
  return ftxui::Dimension::Fit(element).dimx;
}

std::string Render(const BossSelectPanel& panel, int height = 16) {
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(height));
  ftxui::Render(screen, element);
  return screen.ToString();
}

// The colour the first character of the row holding `needle` is drawn in.
// Read off the pixel, because ToString() is where colour goes to die: a red
// row and a white one produce the same string.
ftxui::Color RowColor(const BossSelectPanel& panel, const std::string& needle) {
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(24));
  ftxui::Render(screen, element);
  for (int y = 0; y < screen.dimy(); ++y) {
    std::string row;
    for (int x = 0; x < screen.dimx(); ++x) {
      const std::string& cell = screen.PixelAt(x, y).character;
      row += cell.empty() ? " " : cell;
    }
    size_t at = row.find(needle);
    if (at != std::string::npos) {
      return screen.PixelAt(static_cast<int>(at), y).foreground_color;
    }
  }
  return ftxui::Color::Default;
}

// One row of the last render as plain text, since ToString() threads escape
// codes between the cells and a padding column cannot be seen through them.
std::string Row(const ftxui::Screen& screen, int y) {
  std::string row;
  for (int x = 0; x < screen.dimx(); ++x) {
    const std::string& cell = screen.PixelAt(x, y).character;
    row += cell.empty() ? " " : cell;
  }
  return row;
}

TEST(BossSelectPanelTest, TheDetailPanelDescribesTheFight) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  BossSelectPanel panel(state);
  std::string out = Render(panel);
  EXPECT_NE(out.find("Normal Zakum"), std::string::npos);
  EXPECT_NE(out.find("110"), std::string::npos);
  // Phase 1 is all eight arms together, phase 2 the body.
  EXPECT_NE(out.find("5,600,000"), std::string::npos);
  EXPECT_NE(out.find("7,000,000"), std::string::npos);
  EXPECT_NE(out.find("40%"), std::string::npos);
  EXPECT_NE(out.find("5:00"), std::string::npos);
  EXPECT_NE(out.find("Daily"), std::string::npos);
  // Nothing drops yet, and an empty list says so and nothing else.
  EXPECT_NE(out.find("(empty)"), std::string::npos);
}

// The rewards a clear pays, which is what the player is choosing between when
// there is more than one fight. An equip is named out of its own catalog: the
// list read only the stackables before Zakum dropped anything.
TEST(BossSelectPanelTest, TheRewardsListNamesTheMesoAndEveryDrop) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  EquipPrototype crystal;
  crystal.set_name("Condensed Power Crystal");
  state.equips["condensed_power_crystal"] = crystal;
  ItemPrototype shard;
  shard.set_name("Zakum's Soul Shard");
  state.items["zakums_soul_shard"] = shard;
  BossDifficulty* normal = state.bosses["zakum"].mutable_difficulties(0);
  normal->set_meso(3062500);
  MobDrop* equip_drop = normal->add_drops();
  equip_drop->set_equip("condensed_power_crystal");
  equip_drop->set_per_kill(0.5);
  MobDrop* item_drop = normal->add_drops();
  item_drop->set_item("zakums_soul_shard");
  item_drop->set_per_kill(1.0);

  BossSelectPanel panel(state);
  std::string out = Render(panel, 24);
  EXPECT_NE(out.find("3,062,500"), std::string::npos);
  EXPECT_NE(out.find("50%"), std::string::npos);
  // A name too long for the panel wraps rather than being cut, and the chance
  // rides the last line of it. One that fits keeps its own row whole.
  EXPECT_NE(out.find("Condensed"), std::string::npos);
  EXPECT_NE(out.find("Power Crystal"), std::string::npos);
  EXPECT_EQ(out.find("Condensed Power Crystal"), std::string::npos);
  EXPECT_NE(out.find("Zakum's Soul Shard"), std::string::npos);
  EXPECT_NE(out.find("100%"), std::string::npos);
  EXPECT_EQ(out.find("(empty)"), std::string::npos);
}

// The names are the longest strings on this screen, and a row that ran past
// the label and value columns used to push the whole panel wider than the
// detail rows it stands among.
TEST(BossSelectPanelTest, ALongRewardNameDoesNotWidenThePanel) {
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

// A difficulty is chosen per fight and remembered, so walking the list and
// coming back does not put it back to the easiest.
TEST(BossSelectPanelTest, DifficultyIsPerFightAndClampsToItsEnds) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossSelectPanel panel(state);
  ASSERT_EQ(panel.selected_boss(), "balrog");
  panel.ChangeDifficulty(1);
  EXPECT_EQ(panel.selected_title(), "Normal Balrog");
  // Past the last difficulty stays on it -- the ladder has a top.
  panel.ChangeDifficulty(1);
  EXPECT_EQ(panel.selected_difficulty(), 1);

  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_title(), "Normal Zakum");
  panel.ChangeDifficulty(-1);  // Zakum has only the one
  EXPECT_EQ(panel.selected_difficulty(), 0);

  panel.MoveCursor(-1);
  EXPECT_EQ(panel.selected_title(), "Normal Balrog");
}

// The cursor is the lit difficulty rather than a caret: Left and Right act on
// that cell. Both panels keep a column of clearance inside each border.
TEST(BossSelectPanelTest, TheDifficultyIsTheHighlight) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossSelectPanel panel(state);
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(16));
  ftxui::Render(screen, panel.Render());
  std::string out = screen.ToString();
  EXPECT_EQ(out.find(">"), std::string::npos) << "no caret";
  EXPECT_NE(out.find("\033[7mEasy"), std::string::npos)
      << "the highlighted fight's difficulty is inverted";
  std::string header = Row(screen, 1);
  EXPECT_NE(header.find("│ Name"), std::string::npos)
      << "a column of clearance";
  EXPECT_NE(header.find("Difficulty │"), std::string::npos) << "on both sides";
  EXPECT_NE(Row(screen, 3).find("Level"), std::string::npos);
  EXPECT_NE(Row(screen, 3).find(" │"), std::string::npos)
      << "the detail panel too";
}

// Green for a fight that can be taken, red for one waiting on its reset.
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

// The row is under the fight's own level: what the player is up against, then
// what it takes to stand there. Red while they are short of it.
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

// Neither "Available" nor "Cleared" is true of a fight that cannot be entered
// at all.
TEST(BossSelectPanelTest, ALockedFightReadsLockedRatherThanAvailable) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  state.bosses["zakum"].mutable_difficulties(0)->set_unlock_level(130);
  BossSelectPanel panel(state);
  std::string out = Render(panel);
  EXPECT_NE(out.find("Locked"), std::string::npos);
  EXPECT_EQ(out.find("Available"), std::string::npos);
}

// Red is the reason: the one value the player falls short of. Both cells that
// name it go red, and neither is red once the level is reached.
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

// And the level opens it, row and all.
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

// A fight with no gate of its own says nothing at all, rather than carrying a
// row reading 0.
TEST(BossSelectPanelTest, AnUngatedFightHasNoUnlockRow) {
  std::unique_ptr<GameState> owner = WithBosses();
  BossSelectPanel panel(*owner);
  EXPECT_EQ(Render(panel).find("Unlock Level"), std::string::npos);
}

// The list is the ladder: a fight that opens later comes later, whatever its
// name and whatever level it is fought at.
TEST(BossSelectPanelTest, TheListSortsByTheLevelAFightOpensAt) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  // Balrog is the lower-level fight, so without a gate it leads. Gating it
  // past Zakum has to move it below.
  state.bosses["balrog"].mutable_difficulties(0)->set_unlock_level(130);
  BossSelectPanel panel(state);
  EXPECT_EQ(panel.selected_boss(), "zakum");
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_boss(), "balrog");
}

}  // namespace
}  // namespace ms
