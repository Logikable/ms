#include "src/frontend/screens/boss_select_panel.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/inner_ability.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/screen_text.h"
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

// A second boss, smaller, to check the list sorts by how much there is to kill.
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

std::string Render(const BossSelectPanel& panel, int height = 30) {
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(height));
  ftxui::Render(screen, element);
  return screen.ToString();
}

// The panel's rows as text, for asking which of them comes before which.
std::vector<std::string> RenderRows(const BossSelectPanel& panel,
                                    int height = 30) {
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(height));
  ftxui::Render(screen, element);
  return ScreenRows(screen);
}

// Which row says `needle`, or -1 for a panel that does not.
int RowOf(const std::vector<std::string>& rows, const std::string& needle) {
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (rows[i].find(needle) != std::string::npos) {
      return i;
    }
  }
  return -1;
}

// The colour the first character of the row holding `needle` is drawn in.
// Read off the pixel, because ToString() is where colour goes to die: a red
// row and a white one produce the same string.
ftxui::Color RowColor(const BossSelectPanel& panel, const std::string& needle) {
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(24));
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
  // Phase 1 is all eight arms together, phase 2 the body, and a fight with
  // two of them numbers them.
  EXPECT_NE(out.find("P1 HP"), std::string::npos);
  EXPECT_NE(out.find("5.6M"), std::string::npos);
  EXPECT_NE(out.find("P2 HP"), std::string::npos);
  EXPECT_NE(out.find("7M"), std::string::npos);
  EXPECT_NE(out.find("40%"), std::string::npos);
  EXPECT_NE(out.find("5:00"), std::string::npos);
  EXPECT_NE(out.find("Daily"), std::string::npos);
  // Nothing drops yet, and an empty list says so and nothing else. A fight
  // paying no EXP has no row for it rather than a row reading zero.
  EXPECT_NE(out.find("(empty)"), std::string::npos);
  EXPECT_EQ(out.find("EXP"), std::string::npos);
}

// The rewards a clear pays, which is what the player is choosing between when
// there is more than one fight. An equip is named out of its own catalog: the
// list read only the stackables before Zakum dropped anything.
TEST(BossSelectPanelTest, TheRewardsListNamesWhatAClearPays) {
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
  normal->set_exp(4750740);
  MobDrop* equip_drop = normal->add_drops();
  equip_drop->set_equip("condensed_power_crystal");
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
  // A name too long for the panel wraps rather than being cut, and the chance
  // rides the last line of it. One that fits keeps its own row whole.
  EXPECT_NE(out.find("Condensed"), std::string::npos);
  EXPECT_NE(out.find("Power Crystal"), std::string::npos);
  EXPECT_EQ(out.find("Condensed Power Crystal"), std::string::npos);
  EXPECT_NE(out.find("Zakum's Soul Shard"), std::string::npos);
  EXPECT_NE(out.find("100%"), std::string::npos);
  EXPECT_EQ(out.find("(empty)"), std::string::npos);
}

// Honor is the third thing every gated clear pays, and it reads in the block
// with the meso and the EXP -- above the drops, which are chances.
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

// Nothing spends honor before Inner Ability opens, so nothing names it.
TEST(BossSelectPanelTest, TheHonorRowWaitsForInnerAbility) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  BossSelectPanel panel(state);
  EXPECT_EQ(Render(panel).find("Honor"), std::string::npos);
}

// A fight the calendar does not gate pays no honor, so it names none.
TEST(BossSelectPanelTest, AFightWithNoLockoutNamesNoHonor) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  LevelTo(state, kInnerAbilityUnlockLevel);
  state.bosses["zakum"].mutable_difficulties(0)->clear_reset();
  BossSelectPanel panel(state);
  EXPECT_EQ(Render(panel).find("Honor"), std::string::npos);
}

// The gear is ruled off from what the clear always pays and the shard, and
// stands under it commonest first: the rarest drop is the bottom line.
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
  // Listed rarest first, and by the equip, to show neither is what orders it.
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
  // The rule between them, and no other between the shard and the gear.
  EXPECT_EQ(crystal_row, shard_row + 2);
  EXPECT_NE(rows[shard_row + 1].find("\u2500"), std::string::npos);
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

// One monster is just "HP": there is no other phase for the number to be
// confused with.
TEST(BossSelectPanelTest, AOnePhaseFightLabelsItsHpPlainly) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossSelectPanel panel(state);  // Balrog sorts first: one phase.
  std::string out = Render(panel);
  EXPECT_NE(out.find("HP"), std::string::npos);
  EXPECT_EQ(out.find("P1 HP"), std::string::npos);
}

// Walking the list must not move the top of the screen, so the panel takes
// the same rows whichever fight is under the cursor -- and the tallest fight
// in the game has to fit in them.
TEST(BossSelectPanelTest, TheScreenIsTheSameHeightForEveryFight) {
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
  ftxui::Element zakum = panel.Render();
  int first = ftxui::Dimension::Fit(zakum).dimy;
  panel.MoveCursor(1);
  ftxui::Element balrog = panel.Render();
  EXPECT_EQ(ftxui::Dimension::Fit(balrog).dimy, first);
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

// The column belongs to the grid, not to a fight: moving down the second
// column stays in the second column. A fight with fewer difficulties than the
// widest is the one exception, and it does not cost the column.
TEST(BossSelectPanelTest, TheColumnIsHeldAcrossTheGridAndClampsToItsEnds) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossSelectPanel panel(state);
  ASSERT_EQ(panel.selected_boss(), "balrog");
  panel.ChangeDifficulty(1);
  EXPECT_EQ(panel.selected_title(), "Normal Balrog");
  // Past the last difficulty stays on it -- the ladder has a top.
  panel.ChangeDifficulty(1);
  EXPECT_EQ(panel.selected_difficulty(), 1);

  // Zakum has only the one, so its row falls back to it ...
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_title(), "Normal Zakum");
  EXPECT_EQ(panel.selected_difficulty(), 0);

  // ... and the column is still the second one on the way back.
  panel.MoveCursor(-1);
  EXPECT_EQ(panel.selected_title(), "Normal Balrog");
  EXPECT_EQ(panel.selected_difficulty(), 1);

  panel.ChangeDifficulty(-1);
  EXPECT_EQ(panel.selected_title(), "Easy Balrog");
  panel.ChangeDifficulty(-1);  // and a bottom
  EXPECT_EQ(panel.selected_difficulty(), 0);

  // Opening the screen starts over on the easiest.
  panel.ChangeDifficulty(1);
  panel.Reset();
  EXPECT_EQ(panel.selected_title(), "Easy Balrog");
}

// Every difficulty a fight has stands on its row at once, and the cursor is
// the lit cell rather than a caret. Both panels keep a column of clearance
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

// Beating a boss at any difficulty is beating the boss: the rung beside the
// one that was taken reads Cleared too, and waits for the same reset.
TEST(BossSelectPanelTest, AClearOfOneDifficultyClosesTheOthers) {
  std::unique_ptr<GameState> owner = WithBosses(true);
  GameState& state = *owner;
  BossSelectPanel panel(state);
  ASSERT_EQ(panel.selected_boss(), "balrog") << "the smaller fight sorts first";
  state.character.RecordBossClear("balrog", "Normal",
                                  static_cast<int64_t>(std::time(nullptr)));

  // The cursor is still on Easy, which nobody has taken.
  EXPECT_EQ(panel.selected()->name(), "Easy");
  EXPECT_FALSE(panel.selected_available());
  EXPECT_NE(Render(panel).find("Cleared"), std::string::npos);

  // The other fight on the list is untouched.
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

// The list is the ladder, and its rung is how much there is to kill on the
// easiest difficulty -- whatever the fight is named and whatever gate it
// carries.
TEST(BossSelectPanelTest, TheListSortsByTheHpOfTheEasiestDifficulty) {
  std::unique_ptr<GameState> owner = WithBosses(/*two=*/true);
  GameState& state = *owner;
  BossSelectPanel small(state);
  EXPECT_EQ(small.selected_boss(), "balrog") << "100k against Zakum's 12.6M";
  // Growing him past Zakum's arms and body together has to move him below.
  state.mobs["balrog"].set_max_hp(20000000);
  BossSelectPanel big(state);
  EXPECT_EQ(big.selected_boss(), "zakum");
  big.MoveCursor(1);
  EXPECT_EQ(big.selected_boss(), "balrog");
}

// Adds a Chaos difficulty to Zakum that is written down but not built, and
// gives it a body big enough to need a 64-bit HP.
BossDifficulty* AddChaosZakum(GameState& state) {
  state.mobs["chaos_zakum"] = BossMob("Chaos Zakum", 180, 84000000000LL, 100);
  BossDifficulty* chaos = state.bosses["zakum"].add_difficulties();
  chaos->set_name("Chaos");
  chaos->set_coming_soon(true);
  AddPhase(chaos, "chaos_zakum", 1);
  return chaos;
}

// The whole panel for a fight that is not built yet: the promise, a blank
// line, the HP, and nothing that has not been decided.
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
  // Gold says the fight is on its way rather than refused, which is why it is
  // not the red a shortfall gets.
  EXPECT_EQ(RowColor(panel, "Coming soon!"), kYellow);
}

// The blank row is the point of the pair: it holds the HP off the promise so
// the two do not read as one sentence.
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

// Dim is the door, and this one does not open at any level.
TEST(BossSelectPanelTest, AComingSoonFightIsDimAndNeverEnterable) {
  std::unique_ptr<GameState> owner = WithBosses();
  GameState& state = *owner;
  AddChaosZakum(state);
  LevelTo(state, 200);
  BossSelectPanel panel(state);
  EXPECT_FALSE(panel.selected_coming_soon()) << "Normal is built";
  panel.ChangeDifficulty(1);
  EXPECT_TRUE(panel.selected_coming_soon());
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(16));
  ftxui::Render(screen, panel.Render());
  EXPECT_NE(screen.ToString().find("\033[2m"), std::string::npos)
      << "the difficulty cell is dimmed";
}

}  // namespace
}  // namespace ms
