#include "src/frontend/screens/cube_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/item/equip_instance.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class CubePanelTest : public PanelTest {
 protected:
  // A weapon with one Rare line, which is enough to test the window.
  EquipInstance Cubed() {
    Equip state;
    Potential* potential = state.mutable_main_potential();
    potential->set_rank(POTENTIAL_RANK_RARE);
    PotentialLine* line = potential->add_lines();
    line->set_type(POTENTIAL_LINE_TYPE_STR);
    line->set_rank(POTENTIAL_RANK_RARE);
    return EquipInstance(sword_, state);
  }

  CubePanel Open(const EquipInstance& item, int64_t meso) {
    CubePanel panel;
    panel.Reset();
    panel.SetItem(&item, meso);
    panel.OnEvent(ftxui::Event::Return);
    return panel;
  }
};

TEST_F(CubePanelTest, TheShelfNamesTheCubeItsTrackAndItsPrice) {
  EquipInstance item = Cubed();
  CubePanel panel;
  panel.Reset();
  panel.SetItem(&item, 5 * kCubeCost);
  std::string rendered = RenderElement(panel.Render(true));
  EXPECT_NE(rendered.find("Cube Selection"), std::string::npos);
  EXPECT_NE(rendered.find("Name"), std::string::npos);
  EXPECT_NE(rendered.find("Red Cube"), std::string::npos);
  EXPECT_NE(rendered.find("Main"), std::string::npos);
  EXPECT_NE(rendered.find("12,000,000"), std::string::npos);
}

// One cube, six rows: the shelf is the same height whatever is on it, so the
// card beside it never moves as cubes are added.
TEST_F(CubePanelTest, TheShelfKeepsItsHeight) {
  EquipInstance item = Cubed();
  CubePanel panel;
  panel.Reset();
  panel.SetItem(&item, kCubeCost);
  ftxui::Element shelf = panel.Render(false);
  shelf->ComputeRequirement();
  // Two borders, the header, its rule, and six rows.
  EXPECT_EQ(shelf->requirement().min_y, 10);
}

// The Green Cube isn't on the shelf until bonus potential unlocks, and is gold
// while its trail leads there.
TEST_F(CubePanelTest, TheGreenCubeWaitsForItsUnlock) {
  EquipInstance item = Cubed();
  CubePanel panel;
  panel.Reset();
  panel.SetItem(&item, 5 * kGreenCubeCost);
  EXPECT_EQ(RenderElement(panel.Render(true)).find("Green Cube"),
            std::string::npos);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_cube(), CubeType::kRed) << "one cube to wrap over";

  panel.SetShelf({{CubeType::kRed}, {CubeType::kGreen, /*lead=*/true}});
  std::string rendered = RenderElement(panel.Render(true));
  EXPECT_NE(rendered.find("Green Cube"), std::string::npos);
  EXPECT_NE(rendered.find("Bonus"), std::string::npos);
  EXPECT_NE(rendered.find("24,000,000"), std::string::npos);
  EXPECT_EQ(LabelColor(panel.Render(true), "Green Cube"), kGold);
  EXPECT_NE(LabelColor(panel.Render(true), "Red Cube"), kGold);
  panel.SetShelf({{CubeType::kRed}, {CubeType::kGreen}});
  EXPECT_NE(LabelColor(panel.Render(true), "Green Cube"), kGold);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_cube(), CubeType::kGreen);
}

// The question shows the lines the selected cube rerolls: none yet for bonus
// potential on an item only red cubes have touched.
TEST_F(CubePanelTest, TheQuestionShowsTheSelectedCubesPotential) {
  EquipInstance item = Cubed();
  CubePanel panel;
  panel.Reset();
  panel.SetShelf({{CubeType::kRed}, {CubeType::kGreen}});
  panel.SetItem(&item, 5 * kGreenCubeCost);
  panel.MoveCursor(1);
  panel.OnEvent(ftxui::Event::Return);
  std::string rendered = RenderElement(panel.RenderConfirm());
  EXPECT_NE(rendered.find("Grant bonus potential?"), std::string::npos)
      << rendered;
  EXPECT_NE(rendered.find("Green Cube"), std::string::npos);
  EXPECT_NE(rendered.find("24,000,000"), std::string::npos);
  EXPECT_EQ(rendered.find("STR"), std::string::npos);
  int dashes = 0;
  for (size_t at = rendered.find("—"); at != std::string::npos;
       at = rendered.find("—", at + 1)) {
    ++dashes;
  }
  EXPECT_EQ(dashes, kPotentialLines) << "a placeholder for every line";
}

// Both windows measure themselves, so both have to request the margin.
TEST_F(CubePanelTest, NeitherWindowWeldsTextToItsBorder) {
  EquipInstance item = Cubed();
  CubePanel panel = Open(item, 5 * kCubeCost);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.Render(true)).empty());
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.RenderConfirm()).empty());
}

TEST_F(CubePanelTest, APriceOutOfReachIsRed) {
  EquipInstance item = Cubed();
  CubePanel panel;
  panel.Reset();
  panel.SetItem(&item, kCubeCost - 1);
  EXPECT_EQ(LabelColor(panel.Render(true), "12,000,000"), kRed);

  panel.SetItem(&item, kCubeCost);
  EXPECT_NE(LabelColor(panel.Render(true), "12,000,000"), kRed);
}

TEST_F(CubePanelTest, TheQuestionShowsTheLinesItWouldThrowAway) {
  EquipInstance item = Cubed();
  CubePanel panel = Open(item, 5 * kCubeCost);
  ASSERT_TRUE(panel.IsConfirming());
  std::string rendered = RenderElement(panel.RenderConfirm());
  EXPECT_NE(rendered.find("Red Cube"), std::string::npos);
  EXPECT_NE(rendered.find("Reroll these lines?"), std::string::npos);
  EXPECT_NE(rendered.find("STR"), std::string::npos);
  // The purse, then the price: what the reroll leaves is read before what it
  // costs, and the window is the only place either is shown.
  size_t held = rendered.find("60,000,000");
  size_t cost = rendered.find("12,000,000");
  ASSERT_NE(held, std::string::npos) << rendered;
  ASSERT_NE(cost, std::string::npos) << rendered;
  EXPECT_LT(held, cost);
}

// An item with no potential yet gets a different question.
TEST_F(CubePanelTest, AnItemWithNoPotentialIsAskedToBeGrantedOne) {
  EquipInstance item(sword_);
  CubePanel panel = Open(item, 5 * kCubeCost);
  std::string rendered = RenderElement(panel.RenderConfirm());
  EXPECT_NE(rendered.find("Grant potential?"), std::string::npos);
  EXPECT_EQ(rendered.find("Reroll these lines?"), std::string::npos);
}

// Confirm leaves the window open: the caller rerolls and the same question is
// asked again over the new lines.
TEST_F(CubePanelTest, ConfirmDoesNotCloseTheQuestion) {
  EquipInstance item = Cubed();
  CubePanel panel = Open(item, 5 * kCubeCost);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), CubeAction::kReroll);
  EXPECT_TRUE(panel.IsConfirming());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), CubeAction::kClosed);
  EXPECT_FALSE(panel.IsConfirming());
}

// Out of meso after a reroll: the price turns red, Confirm can't be pressed,
// and the cursor moves off it.
TEST_F(CubePanelTest, AnEmptiedPurseStopsTheReroll) {
  EquipInstance item = Cubed();
  CubePanel panel = Open(item, kCubeCost);
  ASSERT_EQ(panel.OnEvent(ftxui::Event::Return), CubeAction::kReroll);

  panel.SetItem(&item, 0);
  EXPECT_EQ(LabelColor(panel.RenderConfirm(), "12,000,000"), kRed);
  EXPECT_NE(LabelColor(panel.RenderConfirm(), "Held"), kRed);
  // The cursor is on Cancel, so Enter closes the window instead of doing
  // nothing.
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), CubeAction::kClosed);
}

// A cube the purse can't cover can still be selected and still opens its
// window, the same one a player rerolls their way into.
TEST_F(CubePanelTest, AnUnaffordableCubeStillOpensItsQuestion) {
  EquipInstance item = Cubed();
  CubePanel panel = Open(item, 0);
  ASSERT_TRUE(panel.IsConfirming());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), CubeAction::kClosed);
}

// A reroll that raises the potential's rank turns the whole window gold,
// including its rules, since a steel-blue rule across a gold window looks like
// a seam. It lasts two seconds and until a key, whichever is later.
TEST_F(CubePanelTest, ARankUpIsGoldForTwoSecondsAndAKey) {
  EquipInstance item = Cubed();
  CubePanel panel = Open(item, 5 * kCubeCost);
  EXPECT_EQ(BorderColor(panel.RenderConfirm()), kTheme);

  panel.RaiseRankUp();
  EXPECT_EQ(BorderColor(panel.RenderConfirm()), kYellow);
  for (ftxui::Color color : InnerRuleColors(panel.RenderConfirm())) {
    EXPECT_EQ(color, kYellow);
  }
  panel.TouchRankUp();
  panel.AdvanceRankUp(kRankUpSeconds / 2);
  EXPECT_TRUE(panel.rank_up()) << "a key alone is too soon";
  panel.AdvanceRankUp(kRankUpSeconds / 2);
  EXPECT_FALSE(panel.rank_up());

  panel.RaiseRankUp();
  panel.AdvanceRankUp(2 * kRankUpSeconds);
  EXPECT_TRUE(panel.rank_up()) << "the clock alone is too soon";
  panel.TouchRankUp();
  EXPECT_EQ(BorderColor(panel.RenderConfirm()), kTheme);
}

// Opening the screen starts it steel blue, whatever the last item's last reroll
// did.
TEST_F(CubePanelTest, ResetPutsTheGoldOut) {
  EquipInstance item = Cubed();
  CubePanel panel = Open(item, 5 * kCubeCost);
  panel.RaiseRankUp();
  panel.Reset();
  panel.SetItem(&item, 5 * kCubeCost);
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(BorderColor(panel.RenderConfirm()), kTheme);
}

// The shelf wraps, and the question keeps the arrows while it is open.
TEST_F(CubePanelTest, TheCursorWrapsAndTheQuestionHoldsIt) {
  EquipInstance item = Cubed();
  CubePanel panel;
  panel.Reset();
  panel.SetItem(&item, kCubeCost);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_cube(), CubeType::kRed);
  panel.OnEvent(ftxui::Event::Return);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_cube(), CubeType::kRed);
}

// The Black Cube's question over an item and its roll.
class ChoosingCubeTest : public CubePanelTest {
 protected:
  CubePanel OpenBlack(const EquipInstance& item, int64_t meso) {
    CubePanel panel;
    panel.Reset();
    panel.SetShelf({{CubeType::kRed}, {CubeType::kBlack}});
    panel.SetItem(&item, meso);
    panel.MoveCursor(1);
    panel.OnEvent(ftxui::Event::Return);
    return panel;
  }

  Potential Roll() {
    Potential roll;
    roll.set_rank(POTENTIAL_RANK_EPIC);
    for (int i = 0; i < kPotentialLines; ++i) {
      PotentialLine* line = roll.add_lines();
      line->set_type(POTENTIAL_LINE_TYPE_DEX_PCT);
      line->set_rank(POTENTIAL_RANK_EPIC);
    }
    return roll;
  }

  // The rendered rows of the question, one string each, without the colour
  // codes, so a column can be counted.
  std::vector<std::string> Rows(const CubePanel& panel) {
    std::vector<std::string> rows;
    std::string rendered;
    const std::string coloured = RenderElement(panel.RenderConfirm());
    for (size_t i = 0; i < coloured.size(); ++i) {
      if (coloured[i] == '\x1b') {
        i = coloured.find('m', i);
        continue;
      }
      rendered += coloured[i];
    }
    size_t start = 0;
    for (size_t end = rendered.find('\n'); end != std::string::npos;
         end = rendered.find('\n', start)) {
      rows.push_back(rendered.substr(start, end - start));
      start = end + 1;
    }
    rows.push_back(rendered.substr(start));
    return rows;
  }

  // The terminal column `text` starts at in `row`.
  int Column(const std::string& row, const std::string& text) {
    return TextColumns(row.substr(0, row.find(text)));
  }

  // The first row containing `text`, or -1.
  int RowOf(const std::vector<std::string>& rows, const std::string& text) {
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
      if (rows[i].find(text) != std::string::npos) {
        return i;
      }
    }
    return -1;
  }
};

// Top to bottom: the prompt, Before over the item's lines, the Keep row, After
// over three placeholders until a roll, the price, then Confirm. Before and
// After sit where the window's title does.
TEST_F(ChoosingCubeTest, TheQuestionStacksBeforeKeepAfterAndPrice) {
  EquipInstance item = Cubed();
  CubePanel panel = OpenBlack(item, 5 * kBlackCubeCost);
  ASSERT_EQ(panel.selected_cube(), CubeType::kBlack);
  const std::vector<std::string> rows = Rows(panel);
  const int title = RowOf(rows, " Black Cube ");
  const int prompt = RowOf(rows, "Reroll these lines?");
  const int before = RowOf(rows, " Before ");
  const int str = RowOf(rows, "STR");
  const int keep = RowOf(rows, "[Keep ↑]");
  const int after = RowOf(rows, " After ");
  const int held = RowOf(rows, "Held");
  const int confirm = RowOf(rows, "[Confirm]");
  ASSERT_GE(title, 0);
  EXPECT_LT(title, prompt);
  EXPECT_EQ(prompt + 1, before);
  EXPECT_LT(before, str);
  EXPECT_LT(str, keep);
  EXPECT_EQ(keep + 1, after);
  EXPECT_LT(after, held);
  EXPECT_LT(held, confirm);
  EXPECT_EQ(Column(rows[before], " Before "),
            Column(rows[title], " Black Cube "));
  EXPECT_EQ(Column(rows[after], " After "),
            Column(rows[title], " Black Cube "));
  EXPECT_EQ(Column(rows[keep], "[Keep ↑]"), Column(rows[confirm], "[Confirm]"))
      << "the buttons are a square";
  EXPECT_EQ(Column(rows[keep], "[Keep ↓]"), Column(rows[confirm], "[Cancel]"));
  int dashes = 0;
  for (int row = after; row < held; ++row) {
    dashes += rows[row].find("—") != std::string::npos;
  }
  EXPECT_EQ(dashes, kPotentialLines);
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel.RenderConfirm()).empty());
}

// Confirm stays on Confirm to reroll again; up reaches the Keep button above.
TEST_F(ChoosingCubeTest, UpFromConfirmKeepsBefore) {
  EquipInstance item = Cubed();
  CubePanel panel = OpenBlack(item, 5 * kBlackCubeCost);
  panel.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), CubeAction::kReroll)
      << "nothing to keep yet, so up stays on Confirm";
  panel.SetAfter(Roll());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), CubeAction::kReroll);
  EXPECT_NE(RenderElement(panel.RenderConfirm()).find("DEX"),
            std::string::npos);
  panel.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), CubeAction::kKeepBefore);
  EXPECT_FALSE(panel.after().has_value());
  EXPECT_TRUE(panel.IsConfirming());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), CubeAction::kReroll)
      << "back on Confirm";
}

// Up from Cancel is Keep ↓; left, right and down walk the square.
TEST_F(ChoosingCubeTest, TheArrowsWalkTheSquare) {
  EquipInstance item = Cubed();
  CubePanel panel = OpenBlack(item, 5 * kBlackCubeCost);
  panel.SetAfter(Roll());
  panel.OnEvent(ftxui::Event::ArrowRight);
  panel.OnEvent(ftxui::Event::ArrowUp);
  panel.OnEvent(ftxui::Event::ArrowLeft);
  panel.OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), CubeAction::kKeepAfter);
  EXPECT_EQ(panel.TakeAfter().rank(), POTENTIAL_RANK_EPIC);
  EXPECT_FALSE(panel.after().has_value());

  panel.SetAfter(Roll());
  panel.OnEvent(ftxui::Event::ArrowUp);
  panel.OnEvent(ftxui::Event::ArrowRight);
  panel.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), CubeAction::kClosed)
      << "down from Keep ↓ is Cancel";
}

// Escape keeps what the item has and leaves the shelf cursor where it was.
TEST_F(ChoosingCubeTest, EscapeDropsTheRoll) {
  EquipInstance item = Cubed();
  CubePanel panel = OpenBlack(item, 5 * kBlackCubeCost);
  panel.SetAfter(Roll());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), CubeAction::kClosed);
  EXPECT_FALSE(panel.IsConfirming());
  EXPECT_FALSE(panel.after().has_value());
  EXPECT_EQ(panel.selected_cube(), CubeType::kBlack);
}

}  // namespace
}  // namespace ms
