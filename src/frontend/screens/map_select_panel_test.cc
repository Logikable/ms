#include "src/frontend/screens/map_select_panel.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/color.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/format.h"
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

// More pages than the list will ever have, so ChangePage stops at the last band
// however many bands there are.
constexpr int kPastEveryBand = 99;

void AddSpawn(MapData* map, const std::string& mob, int count) {
  Spawn* spawn = map->add_spawns();
  spawn->set_mob(mob);
  spawn->set_count(count);
}

// Three maps whose display order isn't alphabetical: Green and Mixed both come
// out at level 1 (Green first by name), and Horny, alphabetically in the
// middle, sorts last at level 8. Mixed is mostly snails, so the counts keep it
// next to Green.
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

Mob KnightMob() {
  Mob mob;
  mob.set_name("Official Knight C");
  mob.set_level(172);
  return mob;
}

Mob ErdaMob() {
  Mob mob;
  mob.set_name("Raging Erda");
  mob.set_level(201);
  return mob;
}

Mob SpiritMob() {
  Mob mob;
  mob.set_name("Snow Cloud Spirit");
  mob.set_level(233);
  return mob;
}

Mob FireSpiritMob() {
  Mob mob;
  mob.set_name("Fire Spirit");
  mob.set_level(261);
  return mob;
}

// One map in every band, so a test can page across the whole list: Green (level
// 1) and Horny (level 8) in the 1-10 band, then Temple (15) in 11-30, Cave (40)
// in 31-60, Meadow (86) in 61-100, Nest (106) in 101-140, Road (141) in
// 141-170, District (172) in 171-200, Zone (201) in 201-220, Rage (201,
// requiring Arcane Force) in Arcane River P1, Clearing (233) in P2, and
// Ramparts (261, requiring Sacred Power) in Grandis, each alone in its band.
//
// Adding a band to kLevelBands means adding a map here, or paging to the end
// lands on an empty band and the tests below check nothing.
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
  MapData district;
  district.set_name("District");
  AddSpawn(&district, "knight", 3);
  MapData zone;
  zone.set_name("Zone");
  AddSpawn(&zone, "erda", 3);
  MapData rage;
  rage.set_name("Rage");
  AddSpawn(&rage, "erda", 3);
  rage.set_arcane_force(30);
  MapData clearing;
  clearing.set_name("Clearing");
  AddSpawn(&clearing, "spirit", 3);
  clearing.set_arcane_force(320);
  MapData ramparts;
  ramparts.set_name("Ramparts");
  AddSpawn(&ramparts, "fire_spirit", 3);
  ramparts.set_sacred_power(30);
  return GameState({}, {}, {},
                   {{"snail", SnailMob()},
                    {"mushroom", MushroomMob()},
                    {"golem", GolemMob()},
                    {"drake", DrakeMob()},
                    {"hare", HareMob()},
                    {"harp", HarpMob()},
                    {"monk", MonkMob()},
                    {"knight", KnightMob()},
                    {"erda", ErdaMob()},
                    {"spirit", SpiritMob()},
                    {"fire_spirit", FireSpiritMob()}},
                   {{"green_field", green},
                    {"horny_field", horny},
                    {"temple", temple},
                    {"cave", cave},
                    {"meadow", meadow},
                    {"nest", nest},
                    {"road", road},
                    {"district", district},
                    {"zone", zone},
                    {"rage", rage},
                    {"clearing", clearing},
                    {"ramparts", ramparts}});
}

int Width(const MapSelectPanel& panel) {
  ftxui::Element element = panel.Render();
  return ftxui::Dimension::Fit(element).dimx;
}

std::string Render(const MapSelectPanel& panel) {
  ftxui::Element element = ftxui::hbox({panel.Render(), ftxui::filler()});
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
                                               ftxui::Dimension::Fixed(12));
  ftxui::Render(screen, element);
  return screen.ToString();
}

// The band chip drawn as active. Screen::ToString drops both inversion and
// colour, so this reads pixels: an unfocused bar marks its chip with ftxui's
// inversion, and a focused one paints it white instead.
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

// Whether the chip bar has the cursor, which it shows by turning white.
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

// Moves the cursor up onto the chip bar, the stop above the first map and the
// only place Left and Right do anything.
void GoToTheBar(MapSelectPanel* panel) {
  while (!BarHasTheCursor(*panel)) {
    panel->MoveCursor(-1);
  }
}

// The column of the mob table's top-right corner, the right edge of the pair.
// Read from the pixels because Screen::ToString keeps colour escapes, so a byte
// offset into a line isn't the column it appears to be.
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

// The column of the map list's own top-right corner, the first of the two
// windows.
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

// Runs of spaces collapsed to one, so checks can name the columns without
// fixing their widths.
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
// are side by side, so they share lines.
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

// A map's own row in the list. A map name appears twice on screen (the mob
// table's label on the chip line shows it too), and the row is the lower one,
// so this takes the last match.
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
  // Mixed Field spawns nine level 1 mobs and one level 8: 1.7, rounded down to
  // 1. Ignoring the counts would give 4.5, and rounding to nearest would give
  // 2.
  EXPECT_NE(MapRow(rendered, "Mixed Field").find("Mixed Field 1"),
            std::string::npos);
}

// A town rather than a hunting ground. There are no mobs to average, so the
// level column shows 0 instead of dividing by zero, which also puts the town
// below every hunting ground and keeps the list running low to high.
TEST(MapSelectPanelTest, AMapWithNoMobsShowsLevelZeroAndSortsFirst) {
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

// The mob table names the map it is showing, on the row where the band chips
// are next door. Without it the two tables would be a line out of step, since
// they share the screen and only one has a bar.
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

// The check for that alignment: both column headers are on one line.
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
// rows' width and scrolls: the window is 48 columns and its corner is at column
// 47. Otherwise the bar would push it wider and the maps would sit in a window
// sized by its tabs.
TEST(MapSelectPanelTest, TheBandBarDoesNotWidenTheMapList) {
  GameState state = EveryBand();
  MapSelectPanel panel(state);
  panel.Reset();

  EXPECT_LT(MapListRightEdge(panel), 48);
  // It shows that it scrolls, instead of just dropping the bands it can't draw.
  EXPECT_NE(Render(panel).find("›"), std::string::npos);
}

// The mob table shows the selected map's name above its columns, and the two
// windows are centred as a pair. A name wider than the columns would push that
// window out and shift both sideways whenever the cursor moved.
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
  // The name is shown in full, not clipped to save width. The label shares its
  // line with the chip bar, so that line is checked; the map's own row further
  // down also has the name and is wider.
  std::string label = LineWith(Render(panel), "1-10");
  EXPECT_NE(label.find("Battlefield of Fire and Darkness"), std::string::npos)
      << "the mob table's label is clipped: " << label;
}

// A name longer than even that is clipped instead of pushing the window out.
// Which names fit will change; that the label never sets the width won't.
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

  // Green Field, the first row, has four snails and nothing else.
  std::string rendered = Render(panel);
  EXPECT_NE(LineWith(rendered, "Snail").find("Snail 1 4"), std::string::npos);
  EXPECT_EQ(rendered.find("Horny Mushroom"), std::string::npos);

  panel.MoveCursor(1);  // Mixed Field, which has both
  rendered = Render(panel);
  EXPECT_NE(rendered.find("Snail"), std::string::npos);
  EXPECT_NE(rendered.find("Horny Mushroom"), std::string::npos);
}

// PadRight truncates, so a name longer than the column loses its last letters
// instead of widening the table. The longest name in the game is 19.
TEST(MapSelectPanelTest, TheMobColumnFitsTheLongestName) {
  Mob monster;
  monster.set_name("Muddy Swamp Monster");  // 19 characters
  monster.set_level(49);
  MapData swamp;
  swamp.set_name("Swamp");
  AddSpawn(&swamp, "monster", 18);
  GameState state({}, {}, {}, {{"monster", monster}}, {{"swamp", swamp}});
  // Reset opens on the farmed map's own band, whichever band that is. Paging to
  // the end would land on whatever band happens to be last.
  state.current_map = "swamp";
  MapSelectPanel panel(state);
  panel.Reset();

  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Muddy Swamp Monster"), std::string::npos);
  // Still a gap before the level, instead of running into it.
  EXPECT_NE(LineWith(rendered, "Muddy Swamp Monster").find("Monster 49 18"),
            std::string::npos);
}

// The rows and the chip bar above them form one ring: Up from the first map
// reaches the bar, Up again wraps to the last map, and Down wraps back.
TEST(MapSelectPanelTest, TheCursorRingRunsThroughTheChipBar) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  ASSERT_EQ(panel.selected_map(), "green_field") << "the first row";
  ASSERT_FALSE(BarHasTheCursor(panel));

  panel.MoveCursor(-1);
  EXPECT_TRUE(BarHasTheCursor(panel)) << "the stop above the first map";
  // The row is kept, not moved, so moving back down returns to it.
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

// The bar has one chip per band, and the band shown is the marked one: the
// lowest, where the panel opens.
TEST(MapSelectPanelTest, TheBarShowsEveryBandAndOpensOnTheLowest) {
  GameState state = EveryBand();
  MapSelectPanel panel(state);
  panel.Reset();
  std::string rendered = Render(panel);

  EXPECT_NE(rendered.find("1-10"), std::string::npos);
  EXPECT_NE(rendered.find("11-30"), std::string::npos);
  EXPECT_NE(rendered.find("31-60"), std::string::npos);
  EXPECT_NE(rendered.find("61-100"), std::string::npos);
  EXPECT_EQ(ActiveBand(panel), "1-10");
  EXPECT_NE(rendered.find("Green Field"), std::string::npos);
  EXPECT_EQ(rendered.find("Temple"), std::string::npos);
}

// Left and Right belong to the bar. In the list they would change the list
// under the cursor when the player meant to move within it.
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

// Wrapping stays inside the band. Bands change with Left and Right, and moving
// into the next band on Up would change two things with one key.
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
  EXPECT_EQ(panel.selected_map(), "ramparts");
}

// Arcane River splits at 230 and Grandis is separate, whatever the level: the
// tab is chosen by the force a map requires before its level.
TEST(MapSelectPanelTest, TheForcesHaveTabsOfTheirOwn) {
  GameState state = EveryBand();
  const struct {
    const char* map;
    const char* band;
  } kCases[] = {{"zone", "201-220"},
                {"rage", "ArcaneRiverP1"},
                {"clearing", "ArcaneRiverP2"},
                {"ramparts", "Grandis"}};
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.map);
    state.current_map = c.map;
    MapSelectPanel panel(state);
    panel.Reset();
    EXPECT_EQ(panel.selected_map(), c.map);
    EXPECT_EQ(ActiveBand(panel), c.band);
  }
}

TEST(MapSelectPanelTest, MapsPastTheLastBandShowOnIt) {
  // No map outside Arcane River and Grandis is above 220, but one that is must
  // not fall out of the list.
  Mob balrog;
  balrog.set_name("Balrog");
  balrog.set_level(300);
  MapData cave;
  cave.set_name("Deep Cave");
  AddSpawn(&cave, "balrog", 5);
  GameState state({}, {}, {}, {{"balrog", balrog}}, {{"deep_cave", cave}});
  state.current_map = "deep_cave";
  MapSelectPanel panel(state);
  panel.Reset();

  EXPECT_EQ(ActiveBand(panel), "201-220");
  EXPECT_EQ(panel.selected_map(), "deep_cave");
  EXPECT_NE(Render(panel).find("Deep Cave"), std::string::npos);
}

// --- The map context menu ---

TEST(MapSelectPanelTest, TheMenuOpensOverTheListAndSaysWhatItOffers) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  panel.Reset();
  EXPECT_FALSE(panel.menu_open());

  panel.OpenMenu();
  ASSERT_TRUE(panel.menu_open());
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("Move"), std::string::npos);
  EXPECT_NE(rendered.find("Inspect"), std::string::npos);
  EXPECT_NE(rendered.find("Close"), std::string::npos);
  // The list is still behind it, since the menu is about a map the player can
  // see.
  EXPECT_NE(rendered.find("Green Field"), std::string::npos);
}

TEST(MapSelectPanelTest, TheMenuSaysWhichEntryWasTaken) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  panel.Reset();

  panel.OpenMenu();
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return), kMain);  // Move leads
  EXPECT_FALSE(panel.menu_open());

  panel.OpenMenu();
  panel.OnMenuEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return), kMobInspect);

  panel.OpenMenu();
  panel.OnMenuEvent(ftxui::Event::ArrowUp);  // wraps onto Close
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Return), kMapSelect);

  panel.OpenMenu();
  EXPECT_EQ(panel.OnMenuEvent(ftxui::Event::Escape), kMapSelect);
  EXPECT_FALSE(panel.menu_open());
}

// The bar isn't a map, so there is nothing to open a menu about.
TEST(MapSelectPanelTest, TheBarOpensNoMenu) {
  GameState state = ThreeMaps();
  MapSelectPanel panel(state);
  panel.Reset();
  GoToTheBar(&panel);

  panel.OpenMenu();
  EXPECT_FALSE(panel.menu_open());
  EXPECT_EQ(Render(panel).find("Inspect"), std::string::npos);
}

TEST(MapSelectPanelTest, HandlesAWorldWithNoMaps) {
  GameState state({}, {}, {}, {}, {});
  MapSelectPanel panel(state);
  panel.Reset();

  EXPECT_EQ(panel.selected_map(), "");
  EXPECT_NE(Render(panel).find("(empty)"), std::string::npos);
}

// A map in Arcane River, which puts it in the second river band.
GameState ArcaneMaps() {
  MapData clearing;
  clearing.set_name("Snow Cloud Clearing");
  AddSpawn(&clearing, "spirit", 36);
  // 320 rather than a rounder number, so a test looking for the cell doesn't
  // match one of the band chips, which read "11-30", "101-140" and so on.
  clearing.set_arcane_force(320);
  MapData plain;
  plain.set_name("Green Field");
  AddSpawn(&plain, "snail", 4);
  return GameState({}, {}, {}, {{"spirit", SpiritMob()}, {"snail", SnailMob()}},
                   {{"clearing", clearing}, {"green_field", plain}});
}

// The column header appears only on bands that require force. Outside Arcane
// River no map requires any, so the header would sit over a column of blanks.
TEST(MapSelectPanelTest, TheArcaneForceColumnFollowsTheBand) {
  GameState state = ArcaneMaps();
  MapSelectPanel panel(state);
  panel.Reset();
  std::string rendered = Render(panel);
  // The first band has the plain map, which requires no force.
  EXPECT_EQ(rendered.find("AF"), std::string::npos) << rendered;
  EXPECT_EQ(rendered.find("320"), std::string::npos) << rendered;
  int plain_width = Width(panel);

  GoToTheBar(&panel);
  panel.ChangePage(kPastEveryBand);
  panel.ChangePage(-1);  // Grandis is last, and empty here
  std::string arcane = Render(panel);
  EXPECT_NE(arcane.find("Snow Cloud Clearing"), std::string::npos);
  EXPECT_NE(arcane.find("AF"), std::string::npos) << arcane;
  EXPECT_NE(arcane.find("320"), std::string::npos) << arcane;
  // The column keeps its width without its header, so the window doesn't shift
  // sideways as the player pages.
  EXPECT_EQ(Width(panel), plain_width);
}

// Grandis maps put Sacred Power in the same column, under their own header.
TEST(MapSelectPanelTest, GrandisHeadsTheColumnSac) {
  MapData ramparts;
  ramparts.set_name("Cernium Eastern City Ramparts 1");
  AddSpawn(&ramparts, "spirit", 39);
  ramparts.set_sacred_power(30);
  GameState state({}, {}, {}, {{"spirit", SpiritMob()}},
                  {{"ramparts", ramparts}});
  MapSelectPanel panel(state);
  panel.Reset();
  GoToTheBar(&panel);
  panel.ChangePage(kPastEveryBand);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("SAC"), std::string::npos) << rendered;
  EXPECT_EQ(rendered.find("AF "), std::string::npos) << rendered;
}

// The band bar, the map list and the mob table beside it are in one window
// sized to the longest row of any of them.
TEST(MapSelectPanelTest, NoRowWeldsItselfToTheRightBorder) {
  GameState state = EveryBand();
  MapSelectPanel panel(state);
  panel.Reset();
  for (int i = 0; i < 12; ++i) {
    EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render()).empty());
    panel.ChangePage(1);
  }
}
}  // namespace
}  // namespace ms
