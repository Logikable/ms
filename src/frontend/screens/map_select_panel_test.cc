#include "src/frontend/screens/map_select_panel.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/color.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/game_state.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

Mob SnailMob() {
  Mob mob;
  mob.set_name("Snail");
  mob.set_level(1);
  return mob;
}

Mob MushroomMob() {
  Mob mob;
  mob.set_name("Horny Mushroom");
  mob.set_level(8);
  return mob;
}

// More pages than the list will ever hold, so ChangePage clamps to the last
// band whatever the band count has grown to.
constexpr int kPastEveryBand = 99;

void AddSpawn(MapData* map, const std::string& mob, int count) {
  MapData::Spawn* spawn = map->add_spawns();
  spawn->set_mob(mob);
  spawn->set_count(count);
}

// Three maps whose display order is not their alphabetical order: Green and
// Mixed both weigh in at level 1 (Green first by name), and Horny --
// alphabetically in the middle -- sorts last at level 8. Mixed is mostly
// snails, so the count is what holds it down next to Green.
GameState ThreeMaps() {
  MapData green;
  green.set_name("Green Field");
  AddSpawn(&green, "snail", 4);
  MapData mixed;
  mixed.set_name("Mixed Field");
  AddSpawn(&mixed, "snail", 9);
  AddSpawn(&mixed, "mushroom", 1);
  MapData horny;
  horny.set_name("Horny Field");
  AddSpawn(&horny, "mushroom", 6);
  return GameState(
      {}, {}, {}, {{"snail", SnailMob()}, {"mushroom", MushroomMob()}},
      {{"green_field", green}, {"mixed_field", mixed}, {"horny_field", horny}});
}

Mob GolemMob() {
  Mob mob;
  mob.set_name("Stone Golem");
  mob.set_level(15);
  return mob;
}

Mob DrakeMob() {
  Mob mob;
  mob.set_name("Drake");
  mob.set_level(40);
  return mob;
}

Mob HareMob() {
  Mob mob;
  mob.set_name("Brown Hare");
  mob.set_level(86);
  return mob;
}

Mob HarpMob() {
  Mob mob;
  mob.set_name("Harp");
  mob.set_level(106);
  return mob;
}

Mob MonkMob() {
  Mob mob;
  mob.set_name("Oblivion Monk");
  mob.set_level(141);
  return mob;
}

// One map on every band, so a test can page across the whole list: Green
// (level 1) and Horny (level 8) on the 1-10 band, then Temple (15) on 11-30,
// Cave (40) on 31-60, Meadow (86) on 61-100, Nest (106) on 101-140 and Road
// (141) on 141-170, each alone on its own.
// **Adding a band to kLevelBands means adding a map here**, or paging to the
// end lands on an empty band and the tests below say nothing.
GameState EveryBand() {
  MapData green;
  green.set_name("Green Field");
  AddSpawn(&green, "snail", 4);
  MapData horny;
  horny.set_name("Horny Field");
  AddSpawn(&horny, "mushroom", 6);
  MapData temple;
  temple.set_name("Temple");
  AddSpawn(&temple, "golem", 3);
  MapData cave;
  cave.set_name("Cave");
  AddSpawn(&cave, "drake", 3);
  MapData meadow;
  meadow.set_name("Meadow");
  AddSpawn(&meadow, "hare", 3);
  MapData nest;
  nest.set_name("Nest");
  AddSpawn(&nest, "harp", 3);
  MapData road;
  road.set_name("Road");
  AddSpawn(&road, "monk", 3);
  return GameState({}, {}, {},
                   {{"snail", SnailMob()},
                    {"mushroom", MushroomMob()},
                    {"golem", GolemMob()},
                    {"drake", DrakeMob()},
                    {"hare", HareMob()},
                    {"harp", HarpMob()},
                    {"monk", MonkMob()}},
                   {{"green_field", green},
                    {"horny_field", horny},
                    {"temple", temple},
                    {"cave", cave},
                    {"meadow", meadow},
                    {"nest", nest},
                    {"road", road}});
}

std::string Render(const MapSelectPanel& panel) {
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(12));
  ftxui::Render(screen, element);
  return screen.ToString();
}

// The band chip drawn as the active one. Screen::ToString drops both the
// invert and the colours, so this has to read pixels: an unfocused bar marks
// its chip with ftxui's invert, and a focused one paints it white instead.
std::string ActiveBand(const MapSelectPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(14));
  ftxui::Render(screen, ftxui::hbox({panel.Render(), ftxui::filler()}));
  std::string label;
  for (int y = 0; y < screen.dimy() && label.empty(); ++y) {
    for (int x = 0; x < screen.dimx(); ++x) {
      const ftxui::Pixel& pixel = screen.PixelAt(x, y);
      bool marked =
          pixel.inverted || pixel.background_color == ftxui::Color::White;
      if (marked && pixel.character != " " && !pixel.character.empty()) {
        label += pixel.character;
      }
    }
  }
  return label;
}

// Whether the chip bar is holding the cursor, which it says by going white.
bool BarHasTheCursor(const MapSelectPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(14));
  ftxui::Render(screen, ftxui::hbox({panel.Render(), ftxui::filler()}));
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x < screen.dimx(); ++x) {
      if (screen.PixelAt(x, y).background_color == ftxui::Color::White) {
        return true;
      }
    }
  }
  return false;
}

// Steps the cursor up onto the chip bar, which is the stop above the first map
// and the only place Left and Right do anything.
void GoToTheBar(MapSelectPanel* panel) {
  while (!BarHasTheCursor(*panel)) {
    panel->MoveCursor(-1);
  }
}

// The column the mob table's top-right corner lands on -- the right edge of
// the pair. Read off the pixels because Screen::ToString keeps the colour
// escapes, so a byte offset into a line is not the column it looks like.
int MobTableRightEdge(const MapSelectPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(14));
  ftxui::Render(screen, ftxui::hbox({panel.Render(), ftxui::filler()}));
  for (int x = screen.dimx() - 1; x >= 0; --x) {
    if (screen.PixelAt(x, 0).character == "╮") {
      return x;
    }
  }
  return -1;
}

// The column the map list's own top-right corner lands on -- the first of the
// two the pair draws.
int MapListRightEdge(const MapSelectPanel& panel) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(14));
  ftxui::Render(screen, ftxui::hbox({panel.Render(), ftxui::filler()}));
  for (int x = 0; x < screen.dimx(); ++x) {
    if (screen.PixelAt(x, 0).character == "╮") {
      return x;
    }
  }
  return -1;
}

// Runs of spaces collapsed to one, so assertions can name the columns without
// pinning their widths.
std::string Squeeze(const std::string& line) {
  std::string squeezed;
  bool in_spaces = false;
  for (char c : line) {
    if (c == ' ') {
      in_spaces = true;
      continue;
    }
    if (in_spaces && !squeezed.empty()) {
      squeezed += ' ';
    }
    in_spaces = false;
    squeezed += c;
  }
  return squeezed;
}

// The first rendered line containing `needle`. The map list and the mob table
// share a line, since they sit side by side.
std::string LineWith(const std::string& rendered, const std::string& needle) {
  std::istringstream lines(rendered);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.find(needle) != std::string::npos) {
      return Squeeze(line);
    }
  }
  return "";
}

// A map's own row in the list. A map name is on screen twice -- the mob table
// labels itself with it, up on the chip line -- and the row is the lower of
// the two, so this takes the last match rather than the first.
std::string MapRow(const std::string& rendered, const std::string& name) {
  std::istringstream lines(rendered);
  std::string line;
  std::string found;
  while (std::getline(lines, line)) {
    if (line.find(name) != std::string::npos) {
      found = Squeeze(line);
    }
  }
  return found;
}

TEST(MapSelectPanelTest, ListsMapsByWeightedLevelThenName) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);

  size_t green = rendered.find("Green Field");
  size_t mixed = rendered.find("Mixed Field");
  size_t horny = rendered.find("Horny Field");
  ASSERT_NE(horny, std::string::npos);
  EXPECT_LT(green, mixed);
  EXPECT_LT(mixed, horny);
}

TEST(MapSelectPanelTest, ShowsWeightedLevelRoundedDown) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);

  EXPECT_NE(MapRow(rendered, "Green Field").find("Green Field 1"),
            std::string::npos);
  // Mixed Field spawns nine level 1 mobs and one level 8: 1.7, down to 1.
  // Ignoring the counts would say 4.5, and rounding to nearest would say 2.
  EXPECT_NE(MapRow(rendered, "Mixed Field").find("Mixed Field 1"),
            std::string::npos);
}

// A town rather than a hunting ground. There is no mob to average, so the
// level column has to say 0 rather than divide by nothing.
TEST(MapSelectPanelTest, AMapWithNoMobsShowsLevelZero) {
  MapData green;
  green.set_name("Green Field");
  AddSpawn(&green, "snail", 4);
  MapData town;
  town.set_name("Maple Island");
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"green_field", green}, {"maple_island", town}});
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);
  EXPECT_NE(MapRow(rendered, "Maple Island").find("Maple Island 0"),
            std::string::npos);
}

// Level 0 puts it below every hunting ground, so the list still runs low to
// high with a town on it.
TEST(MapSelectPanelTest, AMapWithNoMobsSortsFirst) {
  MapData green;
  green.set_name("Green Field");
  AddSpawn(&green, "snail", 4);
  MapData town;
  town.set_name("Maple Island");
  GameState state({}, {}, {}, {{"snail", SnailMob()}},
                  {{"green_field", green}, {"maple_island", town}});
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);
  EXPECT_LT(rendered.find("Maple Island"), rendered.find("Green Field"));
}

TEST(MapSelectPanelTest, ResetPutsTheCursorOnTheMapBeingFarmed) {
  GameState state = ThreeMaps();
  state.current_map = "horny_field";
  MapSelectPanel panel(state);
  panel.Reset();

  EXPECT_EQ(panel.selected_map(), "horny_field");
  std::string rendered = Render(panel);
  EXPECT_NE(MapRow(rendered, "Horny Field").find("> Horny Field"),
            std::string::npos);
  EXPECT_EQ(MapRow(rendered, "Green Field").find(">"), std::string::npos);
}

// The mob table names the map it is showing, in the row the band chips fill
// next door. Without it the two tables sit one line out of step, since they
// share a screen and one has a bar the other has not.
TEST(MapSelectPanelTest, TheMobTableNamesTheMapItIsShowing) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);

  int seen = 0;
  for (size_t at = rendered.find("Green Field"); at != std::string::npos;
       at = rendered.find("Green Field", at + 1)) {
    ++seen;
  }
  EXPECT_EQ(seen, 2) << "once in the map list, once over its mobs";
}

// The pin for that alignment: both column headers land on one line.
TEST(MapSelectPanelTest, TheTwoTablesShareAHeaderLine) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  std::string header = LineWith(Render(panel), "Count");
  ASSERT_FALSE(header.empty());
  EXPECT_NE(header.find("Name"), header.rfind("Name"))
      << "both tables' headers belong on one line, and this one reads: "
      << header;
}

// There are more bands than fit beside the maps, so the bar is held to the
// rows' width and scrolls under them. Left to itself it would take the window
// out past 48 columns and the maps would sit in a window sized by its tabs.
TEST(MapSelectPanelTest, TheBandBarDoesNotWidenTheMapList) {
  GameState state = EveryBand();
  MapSelectPanel panel(state);
  panel.Reset();

  EXPECT_LT(MapListRightEdge(panel), 47);
  // And it says so, rather than just dropping the bands it cannot draw.
  EXPECT_NE(Render(panel).find("›"), std::string::npos);
}

// The mob table wears the selected map's name over its columns, and the pair
// of windows is centered. A name wider than the columns would push that window
// out and walk both of them sideways every time the cursor moved.
TEST(MapSelectPanelTest, ALongMapNameDoesNotWidenTheMobTable) {
  Mob beetle;
  beetle.set_name("Beetle");
  beetle.set_level(103);
  MapData nest;
  nest.set_name("Nest");
  AddSpawn(&nest, "beetle", 23);
  MapData battlefield;
  battlefield.set_name("Battlefield of Fire and Darkness");  // 32 characters
  AddSpawn(&battlefield, "beetle", 23);
  GameState state({}, {}, {}, {{"beetle", beetle}},
                  {{"nest", nest}, {"battlefield", battlefield}});
  state.current_map = "nest";
  MapSelectPanel panel(state);
  panel.Reset();

  int narrow = MobTableRightEdge(panel);
  ASSERT_GT(narrow, 0);
  panel.MoveCursor(-1);
  ASSERT_EQ(panel.selected_map(), "battlefield");
  EXPECT_EQ(MobTableRightEdge(panel), narrow);
  // And the name is all there, not clipped to buy that width. The label shares
  // its line with the chip bar, so that line is the one to read -- the map's
  // own row further down carries the name too, and is wider.
  std::string label = LineWith(Render(panel), "1-10");
  EXPECT_NE(label.find("Battlefield of Fire and Darkness"), std::string::npos)
      << "the mob table's label is clipped: " << label;
}

// And a name past even that is clipped rather than allowed to push the window
// out. Which of the two the columns can hold is a number that will move; that
// the label never sets the width is not.
TEST(MapSelectPanelTest, AMapNamePastTheMobColumnsIsClipped) {
  Mob beetle;
  beetle.set_name("Beetle");
  beetle.set_level(103);
  MapData nest;
  nest.set_name("Nest");
  AddSpawn(&nest, "beetle", 23);
  MapData sprawl;
  sprawl.set_name("A Map Whose Name Runs Well Past The Mob Columns");
  AddSpawn(&sprawl, "beetle", 23);
  GameState state({}, {}, {}, {{"beetle", beetle}},
                  {{"nest", nest}, {"sprawl", sprawl}});
  state.current_map = "nest";
  MapSelectPanel panel(state);
  panel.Reset();

  int narrow = MobTableRightEdge(panel);
  ASSERT_GT(narrow, 0);
  panel.MoveCursor(-1);
  ASSERT_EQ(panel.selected_map(), "sprawl");
  EXPECT_EQ(MobTableRightEdge(panel), narrow);
}

TEST(MapSelectPanelTest, MobTableFollowsTheCursor) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);

  // Green Field, the first row, holds only snails.
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Snail"), std::string::npos);
  EXPECT_EQ(rendered.find("Horny Mushroom"), std::string::npos);

  panel.MoveCursor(1);  // Mixed Field, which holds both
  rendered = Render(panel);
  EXPECT_NE(rendered.find("Snail"), std::string::npos);
  EXPECT_NE(rendered.find("Horny Mushroom"), std::string::npos);
}

TEST(MapSelectPanelTest, MobTableShowsLevelAndCount) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  std::string rendered = Render(panel);

  // Green Field, the first row, spawns four snails.
  EXPECT_NE(LineWith(rendered, "Snail").find("Snail 1 4"), std::string::npos);
}

// PadRight truncates, so a name past the column loses its last letters rather
// than pushing the table wider. The longest shipped name is 19.
TEST(MapSelectPanelTest, TheMobColumnFitsTheLongestName) {
  Mob monster;
  monster.set_name("Muddy Swamp Monster");  // 19 characters
  monster.set_level(49);
  MapData swamp;
  swamp.set_name("Swamp");
  AddSpawn(&swamp, "monster", 18);
  GameState state({}, {}, {}, {{"monster", monster}}, {{"swamp", swamp}});
  // Reset opens on the farmed map's own band, whichever band that turns out to
  // be. Paging to the end would land on whatever band is last today.
  state.current_map = "swamp";
  MapSelectPanel panel(state);
  panel.Reset();

  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Muddy Swamp Monster"), std::string::npos);
  // And still a gap before the level, rather than running into it.
  EXPECT_NE(LineWith(rendered, "Muddy Swamp Monster").find("Monster 49 18"),
            std::string::npos);
}

// The rows and the chip bar over them are one ring: Up off the first map
// reaches the bar, Up again wraps to the last map, and Down comes back round.
TEST(MapSelectPanelTest, TheCursorRingRunsThroughTheChipBar) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  ASSERT_EQ(panel.selected_map(), "green_field") << "the first row";
  ASSERT_FALSE(BarHasTheCursor(panel));

  panel.MoveCursor(-1);
  EXPECT_TRUE(BarHasTheCursor(panel)) << "the stop above the first map";
  // The row is held, not moved, so stepping back down returns to it.
  EXPECT_EQ(panel.selected_map(), "green_field");
  EXPECT_EQ(LineWith(Render(panel), "Green Field").find(">"), std::string::npos)
      << "no row cursor while the bar has it";

  panel.MoveCursor(-1);
  EXPECT_FALSE(BarHasTheCursor(panel));
  EXPECT_EQ(panel.selected_map(), "horny_field") << "the last row";

  panel.MoveCursor(1);
  EXPECT_TRUE(BarHasTheCursor(panel)) << "round through the bar again";

  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_map(), "green_field");
  EXPECT_FALSE(BarHasTheCursor(panel));
}

// The bar carries one chip per band, and the one on show is the marked one.
TEST(MapSelectPanelTest, TheBarShowsEveryBandAndMarksTheOneOnShow) {
  GameState state = EveryBand();
  MapSelectPanel panel(state);
  panel.Reset();
  std::string rendered = Render(panel);

  EXPECT_NE(rendered.find("1-10"), std::string::npos);
  EXPECT_NE(rendered.find("11-30"), std::string::npos);
  EXPECT_NE(rendered.find("31-60"), std::string::npos);
  EXPECT_NE(rendered.find("61-100"), std::string::npos);
  EXPECT_EQ(ActiveBand(panel), "1-10");
}

// Left and Right belong to the bar. In the list they would change the list
// under the cursor on a key the player pressed to move within it.
TEST(MapSelectPanelTest, TheBandChangesOnlyFromTheBar) {
  GameState state = EveryBand();
  MapSelectPanel panel(state);
  panel.Reset();

  panel.ChangePage(1);
  EXPECT_EQ(ActiveBand(panel), "1-10") << "ignored from the list";
  EXPECT_EQ(panel.selected_map(), "green_field");

  GoToTheBar(&panel);
  panel.ChangePage(1);
  EXPECT_EQ(ActiveBand(panel), "11-30");
  EXPECT_EQ(panel.selected_map(), "temple");
}

// Wrapping stays inside the band. Bands are Left and Right, and rolling into
// the next one on Up would move two things on one key.
TEST(MapSelectPanelTest, WrappingDoesNotCarryIntoTheNextBand) {
  GameState state = EveryBand();
  MapSelectPanel panel(state);
  panel.Reset();
  ASSERT_EQ(ActiveBand(panel), "1-10");

  panel.MoveCursor(-1);
  panel.MoveCursor(-1);
  EXPECT_EQ(ActiveBand(panel), "1-10");
  EXPECT_EQ(panel.selected_map(), "horny_field") << "the last map of this band";
}

TEST(MapSelectPanelTest, OpensOnTheLowestBandByDefault) {
  GameState state = EveryBand();
  MapSelectPanel panel(state);
  panel.Reset();
  std::string rendered = Render(panel);

  EXPECT_EQ(ActiveBand(panel), "1-10");
  EXPECT_NE(rendered.find("Green Field"), std::string::npos);
  EXPECT_EQ(rendered.find("Temple"), std::string::npos);
}

TEST(MapSelectPanelTest, ChangingPageShowsTheNextBandFromItsTop) {
  GameState state = EveryBand();
  MapSelectPanel panel(state);
  panel.Reset();
  panel.MoveCursor(1);  // Horny Field, so the cursor has somewhere to fall from
  GoToTheBar(&panel);

  panel.ChangePage(1);
  std::string rendered = Render(panel);
  EXPECT_EQ(ActiveBand(panel), "11-30");
  EXPECT_NE(rendered.find("Temple"), std::string::npos);
  EXPECT_EQ(rendered.find("Green Field"), std::string::npos);
  EXPECT_EQ(panel.selected_map(), "temple");
}

TEST(MapSelectPanelTest, ResetOpensOnTheFarmedMapsBand) {
  GameState state = EveryBand();
  state.current_map = "temple";
  MapSelectPanel panel(state);
  panel.Reset();

  EXPECT_EQ(panel.selected_map(), "temple");
  EXPECT_EQ(ActiveBand(panel), "11-30");
}

TEST(MapSelectPanelTest, PagingStopsAtBothEndsOfTheBands) {
  GameState state = EveryBand();
  MapSelectPanel panel(state);
  panel.Reset();
  GoToTheBar(&panel);

  panel.ChangePage(-1);
  EXPECT_EQ(panel.selected_map(), "green_field");

  panel.ChangePage(kPastEveryBand);
  EXPECT_EQ(panel.selected_map(), "road");
}

TEST(MapSelectPanelTest, MapsPastTheLastBandShowOnIt) {
  // Nothing holds level 200 yet; it must not fall out of the list for that.
  Mob balrog;
  balrog.set_name("Balrog");
  balrog.set_level(200);
  MapData cave;
  cave.set_name("Deep Cave");
  AddSpawn(&cave, "balrog", 5);
  GameState state({}, {}, {}, {{"balrog", balrog}}, {{"deep_cave", cave}});
  MapSelectPanel panel(state);
  panel.Reset();
  GoToTheBar(&panel);
  panel.ChangePage(kPastEveryBand);

  EXPECT_EQ(panel.selected_map(), "deep_cave");
  EXPECT_NE(Render(panel).find("Deep Cave"), std::string::npos);
}

TEST(MapSelectPanelTest, HandlesAWorldWithNoMaps) {
  GameState state({}, {}, {}, {}, {});
  MapSelectPanel panel(state);
  panel.Reset();

  EXPECT_EQ(panel.selected_map(), "");
  EXPECT_NE(Render(panel).find("(empty)"), std::string::npos);
}

}  // namespace
}  // namespace ms
