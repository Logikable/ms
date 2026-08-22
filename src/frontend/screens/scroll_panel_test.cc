#include "src/frontend/screens/scroll_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

class ScrollPanelTest : public PanelTest {
 protected:
  // The rates are GMS's own, because the price table only prices those four.
  // A made-up rate has no price and stops the panel.
  static std::map<std::string, Scroll> MakeScrolls() {
    std::map<std::string, Scroll> scrolls;
    Scroll& a = scrolls["AAA Scroll"];
    a.set_name("AAA Scroll");
    a.set_success_rate(30);
    a.set_tier(SCROLL_TIER_1);
    a.set_scroll_type(SCROLL_TYPE_ATT);
    a.mutable_stats()->set_attack(5);
    a.set_target(SCROLL_TARGET_WEAPON);
    a.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
    Scroll& z = scrolls["ZZZ Scroll"];
    z.set_name("ZZZ Scroll");
    z.set_success_rate(70);
    z.set_tier(SCROLL_TIER_2);
    z.set_scroll_type(SCROLL_TYPE_DEX);
    z.set_target(SCROLL_TARGET_WEAPON);
    z.add_applicable_job_categories(EQUIP_JOB_CATEGORY_BOWMAN);
    return scrolls;
  }

  static std::string Render(ScrollPanel& panel) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(10));
    ftxui::Render(screen, panel.Render());
    return screen.ToString();
  }

  // How tall the panel asks to be, which is what says whether a window is
  // stacked below the list or floating over it.
  static int FitHeight(ScrollPanel& panel) {
    ftxui::Element element = panel.Render();
    return ftxui::Screen::Create(ftxui::Dimension::Fit(element),
                                 ftxui::Dimension::Fit(element))
        .dimy();
  }

  // Traces the character is carrying, so a test can put the balance where it
  // needs it before the panel reads it.
  void GiveTraces(int count) {
    ItemPrototype trace;
    trace.set_name(kSpellTraceName);
    trace.set_category(ITEM_CATEGORY_ETC);
    trace.set_max_stack(30000);
    c_.AddStackable(trace, count);
  }

  // A level to price against, and what the two scrolls cost there. The price
  // comes from the item, so a test that reads a Cost has to name a level.
  static constexpr int kSwordLevel = 100;
  static constexpr int kAaaCost = 34;  // 30% weapon, level 100 band
  static constexpr int kZzzCost = 28;  // 70% weapon, same band

  // Moves the cursor onto `name` and pins it, the way the menu will. The
  // render first is what lets the Menu take an arrow key at all.
  void PinByName(ScrollPanel* panel, const std::string& name) {
    Render(*panel);
    for (int i = 0; i < 32 && panel->selected_scroll().name() != name; ++i) {
      panel->OnEvent(ftxui::Event::ArrowDown);
    }
    EXPECT_EQ(panel->selected_scroll().name(), name) << "no such row";
    c_.ToggleScrollPin(panel->PinKeyOfSelected());
    panel->Resort();
  }

  // Enter opens the menu, and Enter again on Scroll asks the CALLER to open
  // the confirm -- the panel does not, because only the caller knows whether
  // the item has a slot left. Tests stand in for that caller here.
  void OpenConfirmThroughTheMenu(ScrollPanel* panel) {
    Render(*panel);
    panel->OnEvent(ftxui::Event::Return);
    EXPECT_TRUE(panel->IsMenuOpen());
    panel->OnEvent(ftxui::Event::Return);
    EXPECT_TRUE(panel->TakeScrollChosen());
    panel->OpenConfirm();
  }

  std::map<std::string, Scroll> scrolls_ = MakeScrolls();
  ScrollPanel panel_{c_, scrolls_};
};

// AAA is SCROLL_TYPE_ATT (1) and ZZZ is SCROLL_TYPE_DEX (4), so the type sort
// opens on AAA despite its longer odds.
TEST_F(ScrollPanelTest, OpensOnTheFirstScrollAndNamesItsOddsAndStat) {
  EXPECT_EQ(panel_.selected(), 0);
  EXPECT_EQ(panel_.selected_scroll().name(), "AAA Scroll");
  std::string drawn = Render(panel_);
  EXPECT_NE(drawn.find("AAA Scroll"), std::string::npos);
  EXPECT_NE(drawn.find("30%"), std::string::npos);
  EXPECT_NE(drawn.find("+5 ATT"), std::string::npos);
}

// The armour scroll that raises all four stats. Four cells saying the same
// number is what the option is named for, and it does not fit the row anyway.
TEST_F(ScrollPanelTest, FourStatsThatAgreeReadAsAllStats) {
  EquipStats* stats = scrolls_["AAA Scroll"].mutable_stats();
  stats->set_attack(0);
  stats->set_str(2);
  stats->set_dex(2);
  stats->set_int_(2);
  stats->set_luk(2);
  stats->set_def(7);
  ScrollPanel panel(c_, scrolls_);
  std::string drawn = Render(panel);
  EXPECT_NE(drawn.find("+2 All Stats"), std::string::npos);
  EXPECT_EQ(drawn.find("+2 STR"), std::string::npos);
  EXPECT_NE(drawn.find("+7 DEF"), std::string::npos);
}

// Four stats that disagree are four stats, whatever the scroll is called.
TEST_F(ScrollPanelTest, StatsThatDisagreeStayApart) {
  EquipStats* stats = scrolls_["AAA Scroll"].mutable_stats();
  stats->set_attack(0);
  stats->set_str(2);
  stats->set_dex(2);
  stats->set_int_(2);
  stats->set_luk(3);
  ScrollPanel panel(c_, scrolls_);
  EXPECT_NE(Render(panel).find("+2 STR"), std::string::npos);
}

// The list is the whole screen, with no tab bar over it, so the ends meet.
TEST_F(ScrollPanelTest, TheCursorWalksTheListAsARing) {
  Render(panel_);  // populate entries_ so the menu knows its size
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected(), 1);
  EXPECT_EQ(panel_.selected_scroll().name(), "ZZZ Scroll");
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected(), 0);
  panel_.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel_.selected(), 1);
}

TEST_F(ScrollPanelTest, SetFilterChangesSelectedScroll) {
  std::vector<const Scroll*> filter = {&scrolls_["ZZZ Scroll"]};
  panel_.SetFilter(filter, kSwordLevel, SCROLL_TARGET_WEAPON);
  Render(panel_);
  EXPECT_EQ(panel_.selected_scroll().name(), "ZZZ Scroll");
}

TEST_F(ScrollPanelTest, SetFilterResetsSelectionToZero) {
  Render(panel_);
  panel_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel_.selected(), 1);

  std::vector<const Scroll*> filter = {&scrolls_["AAA Scroll"]};
  panel_.SetFilter(filter, kSwordLevel, SCROLL_TARGET_WEAPON);
  EXPECT_EQ(panel_.selected(), 0);
}

TEST_F(ScrollPanelTest, SetFilterForPrototypeReturnsTrueForMatch) {
  EquipPrototype proto;
  proto.set_required_level(1);  // tier 1
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_TRUE(panel_.SetFilterForPrototype(proto));
  Render(panel_);
  EXPECT_EQ(panel_.selected_scroll().name(), "AAA Scroll");
}

TEST_F(ScrollPanelTest, SetFilterRejectsAnItemWithNoScrolls) {
  EquipPrototype proto;
  proto.set_required_level(1);  // tier 1
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.add_equip_job_categories(
      EQUIP_JOB_CATEGORY_PIRATE);  // no pirate scrolls
  EXPECT_FALSE(panel_.SetFilterForPrototype(proto));
  // Filter unchanged; original first entry still selected.
  EXPECT_EQ(panel_.selected_scroll().name(), "AAA Scroll");
}

TEST_F(ScrollPanelTest, SetFilterRejectsATierMismatch) {
  EquipPrototype proto;
  proto.set_required_level(75);  // tier 2; only ZZZ is tier 2 but it's bowman
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_FALSE(panel_.SetFilterForPrototype(proto));
}

// A hat lists the same job category a weapon does, so the categories alone
// would hand it every weapon scroll in the catalog.
TEST_F(ScrollPanelTest, SetFilterRejectsAWeaponScrollForArmour) {
  EquipPrototype hat;
  hat.set_required_level(1);  // tier 1, where AAA lives
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  hat.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  EXPECT_FALSE(panel_.SetFilterForPrototype(hat));

  scrolls_["AAA Scroll"].set_target(SCROLL_TARGET_ARMOUR);
  ScrollPanel armour_panel(c_, scrolls_);
  EXPECT_TRUE(armour_panel.SetFilterForPrototype(hat));
}

// The one scroll that asks nothing of the item but its tier.
TEST_F(ScrollPanelTest, ACleanSlateIsOfferedForEveryKindOfItem) {
  scrolls_["AAA Scroll"].set_scroll_category(SCROLL_CATEGORY_CLEAN_SLATE);
  scrolls_["AAA Scroll"].set_target(SCROLL_TARGET_UNSPECIFIED);
  ScrollPanel panel(c_, scrolls_);

  EquipPrototype hat;
  hat.set_required_level(1);
  hat.set_equip_slot(EQUIP_SLOT_HAT);
  EXPECT_TRUE(panel.SetFilterForPrototype(hat));
}

TEST_F(ScrollPanelTest, SetFilterResetsTheSelection) {
  Render(panel_);
  panel_.OnEvent(ftxui::Event::ArrowDown);

  EquipPrototype proto;
  proto.set_required_level(1);
  proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  proto.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  panel_.SetFilterForPrototype(proto);
  EXPECT_EQ(panel_.selected(), 0);
}

// --- the Cost column and the balance ---

// Display columns, not bytes: the scroll glyph is four bytes wide and two
// columns wide, which is exactly the confusion this file has to keep out of
// the Cost column.
//
// Screen::ToString keeps the colour escapes, so a styled cell puts bytes on
// the line that occupy no columns at all. Skipping them is what lets a red
// cost be measured against a heading that is not red.
int DisplayColumns(const std::string& s) {
  int width = 0;
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '\x1b') {
      while (i < s.size() && s[i] != 'm') {
        ++i;
      }
      ++i;  // the 'm' that ends it
      continue;
    }
    unsigned char c = s[i];
    int bytes = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
    width += bytes == 4 ? 2 : 1;  // the emoji is the only wide glyph here
    i += bytes;
  }
  return width;
}

TEST_F(ScrollPanelTest, EachRowCarriesItsTraceCost) {
  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  std::string rendered = Render(panel_);
  EXPECT_NE(rendered.find("34 📜"), std::string::npos);
  EXPECT_NE(rendered.find("Cost"), std::string::npos);
}

// --- pins ---

// A pinned scroll rides at the top of the list, and says so in its own column.
TEST_F(ScrollPanelTest, APinnedScrollSitsAtTheTop) {
  std::vector<const Scroll*> both = {&scrolls_["AAA Scroll"],
                                     &scrolls_["ZZZ Scroll"]};
  panel_.SetFilter(both, kSwordLevel, SCROLL_TARGET_WEAPON);
  ASSERT_EQ(panel_.selected_scroll().name(), "AAA Scroll");

  PinByName(&panel_, "ZZZ Scroll");

  EXPECT_EQ(panel_.selected(), 0) << "the cursor follows the scroll it was on";
  EXPECT_EQ(panel_.selected_scroll().name(), "ZZZ Scroll");
  EXPECT_TRUE(panel_.SelectedIsPinned());
  EXPECT_NE(Render(panel_).find("📌"), std::string::npos);
}

// Pinning changes what is at the top, not the order of anything else: the two
// halves of the list each keep the order they had.
TEST_F(ScrollPanelTest, PinnedScrollsKeepTheUsualOrderAmongThemselves) {
  std::map<std::string, Scroll> four;
  const int kRates[] = {100, 70, 30};
  for (int i = 0; i < 3; ++i) {
    Scroll& s = four["s" + std::to_string(i)];
    s.set_name("Scroll " + std::to_string(kRates[i]));
    s.set_success_rate(kRates[i]);
    s.set_tier(SCROLL_TIER_1);
    s.set_scroll_type(SCROLL_TYPE_ATT);
    s.set_target(SCROLL_TARGET_WEAPON);
    s.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  }
  ScrollPanel panel(c_, four);
  panel.SetFilter({&four["s0"], &four["s1"], &four["s2"]}, kSwordLevel,
                  SCROLL_TARGET_WEAPON);
  // Pin the 30%, the last of the three, and then the 70% above it.
  PinByName(&panel, "Scroll 30");
  PinByName(&panel, "Scroll 70");

  // 70 before 30 among the pinned, as they were before either was pinned.
  std::string rendered = Render(panel);
  EXPECT_LT(rendered.find("Scroll 70"), rendered.find("Scroll 30"));
  EXPECT_LT(rendered.find("Scroll 30"), rendered.find("Scroll 100"));
}

// A pin is filed under the kind of equipment it was set on, so the weapons a
// player pins do not follow them onto their armour.
TEST_F(ScrollPanelTest, PinsAreKeptPerKindOfEquipment) {
  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  c_.ToggleScrollPin(panel_.PinKeyOfSelected());
  ASSERT_TRUE(panel_.SelectedIsPinned());

  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_ARMOUR);
  EXPECT_FALSE(panel_.SelectedIsPinned());
}

// The pin holds as the player outgrows a tier: it names the stat and the rate,
// not the file, and the same scroll met again at the next tier is still theirs.
TEST_F(ScrollPanelTest, APinHoldsAcrossTiers) {
  Scroll higher = scrolls_["AAA Scroll"];
  higher.set_tier(SCROLL_TIER_2);
  scrolls_["Higher"] = higher;

  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  c_.ToggleScrollPin(panel_.PinKeyOfSelected());

  panel_.SetFilter({&scrolls_["Higher"]}, 150, SCROLL_TARGET_WEAPON);
  EXPECT_TRUE(panel_.SelectedIsPinned());
}

// Unpinning drops it back among the rest.
TEST_F(ScrollPanelTest, UnpinningPutsTheScrollBack) {
  std::vector<const Scroll*> both = {&scrolls_["AAA Scroll"],
                                     &scrolls_["ZZZ Scroll"]};
  panel_.SetFilter(both, kSwordLevel, SCROLL_TARGET_WEAPON);
  PinByName(&panel_, "ZZZ Scroll");
  ASSERT_EQ(panel_.selected_scroll().name(), "ZZZ Scroll");
  ASSERT_EQ(panel_.selected(), 0);

  c_.ToggleScrollPin(panel_.PinKeyOfSelected());
  panel_.Resort();
  EXPECT_EQ(panel_.selected_scroll().name(), "ZZZ Scroll");
  EXPECT_EQ(panel_.selected(), 1) << "back where it sorts";
  EXPECT_EQ(Render(panel_).find("📌"), std::string::npos);
}

// The list answers "what can I afford" on its face, in the same red the
// confirm window uses, so a player need not open a row to find out.
TEST_F(ScrollPanelTest, ARowsCostGoesRedWhenItCannotBePaid) {
  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  EXPECT_EQ(LabelColor(panel_.Render(), "34"), kRed);

  GiveTraces(kAaaCost);
  EXPECT_NE(LabelColor(panel_.Render(), "34"), kRed);
}

// The change this screen exists to show: one scroll, two prices, because the
// price is the item's. A Cost read off the scroll alone would not move.
TEST_F(ScrollPanelTest, TheSameScrollCostsMoreOnABetterItem) {
  std::vector<const Scroll*> one = {&scrolls_["AAA Scroll"]};
  panel_.SetFilter(one, 30, SCROLL_TARGET_WEAPON);
  int cheap = panel_.CostOfSelected();
  panel_.SetFilter(one, kSwordLevel, SCROLL_TARGET_WEAPON);
  EXPECT_GT(panel_.CostOfSelected(), cheap);
  EXPECT_EQ(panel_.CostOfSelected(), kAaaCost);
  EXPECT_NE(Render(panel_).find("34 📜"), std::string::npos);
}

// The one that catches a byte-padded cost cell: the heading and the number
// under it have to end in the same column.
TEST_F(ScrollPanelTest, TheCostColumnLinesUpWithItsHeading) {
  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  std::string rendered = Render(panel_);
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= rendered.size()) {
    size_t end = rendered.find('\n', start);
    if (end == std::string::npos) {
      end = rendered.size();
    }
    lines.push_back(rendered.substr(start, end - start));
    start = end + 1;
  }
  std::string header;
  std::string row;
  for (const std::string& line : lines) {
    if (line.find("Cost") != std::string::npos) {
      header = line;
    }
    if (line.find("34 📜") != std::string::npos) {
      row = line;
    }
  }
  ASSERT_FALSE(header.empty());
  ASSERT_FALSE(row.empty());
  EXPECT_EQ(DisplayColumns(header.substr(0, header.find("Cost") + 4)),
            DisplayColumns(row.substr(0, row.find("📜") + 4)));
}

TEST_F(ScrollPanelTest, TheTitleShowsWhatThePlayerOwns) {
  EXPECT_NE(Render(panel_).find("0 📜"), std::string::npos);
  GiveTraces(1240);
  EXPECT_NE(Render(panel_).find("1,240 📜"), std::string::npos);
}

TEST_F(ScrollPanelTest, AffordabilityFollowsTheBalance) {
  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  ASSERT_EQ(panel_.CostOfSelected(), kAaaCost);
  EXPECT_FALSE(panel_.CanAffordSelected());
  GiveTraces(kAaaCost - 1);
  EXPECT_FALSE(panel_.CanAffordSelected());
  GiveTraces(1);  // exactly the price
  EXPECT_TRUE(panel_.CanAffordSelected());
}

// The name column gave up its width to Cost, so a name past it is cut rather
// than allowed to shove the other columns along. That it also SLIDES while
// selected is ScrollingWindow's promise and is tested with it, in marquee_test
// -- catching it here would mean sleeping out the marquee's pause.
TEST_F(ScrollPanelTest, ALongNameIsCutToItsColumn) {
  std::map<std::string, Scroll> scrolls;
  Scroll& s = scrolls["long"];
  s.set_name("100% Clean Slate");
  s.set_success_rate(100);
  s.set_tier(SCROLL_TIER_1);
  s.set_scroll_category(SCROLL_CATEGORY_CLEAN_SLATE);
  s.set_trace_cost(20);
  s.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  ScrollPanel panel(c_, scrolls);
  std::string rendered = Render(panel);
  EXPECT_NE(rendered.find("100% Clean"), std::string::npos);
  EXPECT_EQ(rendered.find("100% Clean Slate"), std::string::npos);
}

// The panel is the first of two refusals -- the controller will not spend what
// is not there either -- so this has to be asserted here, where the controller
// cannot cover for it.
TEST_F(ScrollPanelTest, TheConfirmWindowWillNotAnswerYesUnpaid) {
  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  OpenConfirmThroughTheMenu(&panel_);
  ASSERT_TRUE(panel_.IsConfirming());
  panel_.OnEvent(ftxui::Event::Return);  // answer yes with nothing to pay with
  EXPECT_FALSE(panel_.TakeConfirmed());

  // The refused answer closed the window, so paying for it means walking the
  // menu again.
  GiveTraces(kAaaCost);
  OpenConfirmThroughTheMenu(&panel_);
  panel_.OnEvent(ftxui::Event::Return);
  EXPECT_TRUE(panel_.TakeConfirmed());
}

TEST_F(ScrollPanelTest, TheConfirmWindowShowsTheCost) {
  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  GiveTraces(100);
  OpenConfirmThroughTheMenu(&panel_);
  std::string rendered = Render(panel_);
  EXPECT_NE(rendered.find("Confirm"), std::string::npos);
  EXPECT_NE(rendered.find("AAA Scroll"), std::string::npos);
  EXPECT_NE(rendered.find("34 📜"), std::string::npos) << "the cost";
}

// Rendered lines, so a test can say which row sits under which.
std::vector<std::string> Lines(const std::string& rendered) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= rendered.size()) {
    size_t end = rendered.find('\n', start);
    if (end == std::string::npos) {
      end = rendered.size();
    }
    lines.push_back(rendered.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

// Lines carrying a horizontal rule. A rule inside a window is drawn into that
// window's own side borders, so it always brings a left and a right end with
// it -- which is what tells a rule from the plain rows around it.
int RuleLines(const std::vector<std::string>& lines) {
  int count = 0;
  for (const std::string& line : lines) {
    if (line.find("├") != std::string::npos &&
        line.find("┤") != std::string::npos) {
      ++count;
    }
  }
  return count;
}

int LineIndex(const std::vector<std::string>& lines,
              const std::string& needle) {
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (lines[i].find(needle) != std::string::npos) {
      return i;
    }
  }
  return -1;
}

// A blank column between them. At one space the two headings read as the one
// phrase "Cost Pin".
TEST_F(ScrollPanelTest, TheCostAndPinHeadingsStandApart) {
  std::string rendered = Render(panel_);
  EXPECT_NE(rendered.find("Cost  Pin"), std::string::npos);
  EXPECT_EQ(rendered.find("Cost Pin"), std::string::npos);
}

// And the pin stays in its column rather than drifting into the gap. Right
// edges, because the glyph is two columns and the heading is three -- the same
// way the costs sit under "Cost".
TEST_F(ScrollPanelTest, ThePinEndsWhereItsHeadingEnds) {
  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  PinByName(&panel_, "AAA Scroll");
  std::vector<std::string> lines = Lines(Render(panel_));
  int header = LineIndex(lines, "Cost");
  int row = LineIndex(lines, "📌");
  ASSERT_GE(header, 0);
  ASSERT_GT(row, header);

  const std::string& head = lines[header];
  const std::string& pinned = lines[row];
  int head_end = DisplayColumns(head.substr(0, head.find("Pin") + 3));
  // Two columns for the glyph itself, which the substring stops short of.
  int pin_end = DisplayColumns(pinned.substr(0, pinned.find("📌"))) + 2;
  EXPECT_EQ(head_end, pin_end);
}

// Three blocks, ruled off: what is going on what, then what it does and costs,
// then the answer. Without the rules the four rows read as one list.
TEST_F(ScrollPanelTest, TheConfirmWindowRulesOffItsBlocks) {
  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  GiveTraces(100);
  OpenConfirmThroughTheMenu(&panel_);
  std::vector<std::string> lines = Lines(Render(panel_));

  int effect = LineIndex(lines, "+5 ATT");
  int cost = LineIndex(lines, "Cost 34");
  int buttons = LineIndex(lines, "[Confirm]");
  ASSERT_GT(effect, 0);
  ASSERT_GT(buttons, 0);

  // One rule belongs to the list behind, and two to the window over it.
  EXPECT_EQ(RuleLines(lines), 3);
  EXPECT_EQ(cost, effect + 1) << "the cost belongs with what it buys";
  EXPECT_LT(effect, buttons);
}

// The window is a pop-up over the list: opening it must not make the panel
// taller, which is what it did when it sat below.
//
// Needs a list the window can fit inside. Against the two-scroll fixture the
// window is the taller of the two and the panel grows whatever it is doing,
// so the assertion would say nothing about floating.
TEST_F(ScrollPanelTest, TheConfirmWindowDoesNotGrowThePanel) {
  std::map<std::string, Scroll> many;
  for (int i = 0; i < 10; ++i) {
    Scroll& s = many["scroll " + std::to_string(i)];
    s.set_name("Scroll " + std::to_string(i));
    s.set_success_rate(100);
    s.set_tier(SCROLL_TIER_1);
    s.set_scroll_type(SCROLL_TYPE_ATT);
    s.set_target(SCROLL_TARGET_WEAPON);
    s.mutable_stats()->set_attack(1);
    s.add_applicable_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  }
  ScrollPanel panel(c_, many);
  GiveTraces(1000);

  int closed = FitHeight(panel);
  OpenConfirmThroughTheMenu(&panel);
  ASSERT_TRUE(panel.IsConfirming());
  EXPECT_EQ(FitHeight(panel), closed);
}

// Said in red and in the greyed Confirm, and in no words at all: the cost row
// is the thing they cannot pay, so it is the thing that turns.
TEST_F(ScrollPanelTest, TheCostGoesRedWhenTheTracesFallShort) {
  panel_.SetFilter({&scrolls_["AAA Scroll"]}, kSwordLevel,
                   SCROLL_TARGET_WEAPON);
  GiveTraces(5);
  OpenConfirmThroughTheMenu(&panel_);
  EXPECT_EQ(LabelColor(panel_.Render(), "Cost 34"), kRed);

  GiveTraces(100);
  EXPECT_NE(LabelColor(panel_.Render(), "Cost 34"), kRed);
}

// The window names the item as well as the scroll, which is the whole reason
// it replaced a bare button row.
TEST_F(ScrollPanelTest, TheConfirmWindowNamesTheItemBeingScrolled) {
  EquipPrototype sword;
  sword.set_name("Long Sword");
  sword.set_required_level(10);
  sword.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
  sword.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  ASSERT_TRUE(panel_.SetFilterForPrototype(sword));
  OpenConfirmThroughTheMenu(&panel_);
  EXPECT_NE(Render(panel_).find("Long Sword"), std::string::npos);
}

// A landed scroll is what the traces were spent for, so the window goes gold
// -- border and rules alike. A failure leaves the frame alone.
TEST_F(ScrollPanelTest, TheResultWindowGoesGoldOnSuccess) {
  ScrollResult r;
  r.equip_name = "Sword";
  r.scroll_name = "AAA Scroll";
  r.slots_remaining = 3;

  r.outcome = kScrollSuccess;
  EXPECT_EQ(BorderColor(panel_.RenderResult(r)), kYellow);
  EXPECT_EQ(InnerRuleColor(panel_.RenderResult(r)), kYellow);

  r.outcome = kScrollFail;
  EXPECT_EQ(BorderColor(panel_.RenderResult(r)), kTheme);
}

}  // namespace
}  // namespace ms
