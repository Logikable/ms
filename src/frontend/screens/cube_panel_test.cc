#include "src/frontend/screens/cube_panel.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/item/equip_instance.h"
#include "src/item/potential.h"
#include "src/protos/equip.pb.h"

namespace ms {
namespace {

class CubePanelTest : public PanelTest {
 protected:
  // A weapon carrying one Rare line, which is enough to read the window by.
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

// Both windows measure themselves, so both have to ask for the margin.
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
  // The purse and the price, in that order: what the reroll leaves is read
  // before what it takes, and the window is the only place either is said.
  size_t held = rendered.find("60,000,000");
  size_t cost = rendered.find("12,000,000");
  ASSERT_NE(held, std::string::npos) << rendered;
  ASSERT_NE(cost, std::string::npos) << rendered;
  EXPECT_LT(held, cost);
}

// An item with nothing on it yet is not being asked the same question.
TEST_F(CubePanelTest, AnItemWithNoPotentialIsAskedToBeGrantedOne) {
  EquipInstance item(sword_);
  CubePanel panel = Open(item, 5 * kCubeCost);
  std::string rendered = RenderElement(panel.RenderConfirm());
  EXPECT_NE(rendered.find("Grant potential?"), std::string::npos);
  EXPECT_EQ(rendered.find("Reroll these lines?"), std::string::npos);
}

// The one Confirm in the game that leaves its window up: the caller rerolls
// and the same question is asked again over the new lines.
TEST_F(CubePanelTest, ConfirmDoesNotCloseTheQuestion) {
  EquipInstance item = Cubed();
  CubePanel panel = Open(item, 5 * kCubeCost);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);
  EXPECT_TRUE(panel.IsConfirming());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), ConfirmChoice::kCancelled);
  EXPECT_FALSE(panel.IsConfirming());
}

// Rerolled dry: the price goes red, Confirm cannot be pressed, and the cursor
// is taken off it.
TEST_F(CubePanelTest, AnEmptiedPurseStopsTheReroll) {
  EquipInstance item = Cubed();
  CubePanel panel = Open(item, kCubeCost);
  ASSERT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kConfirmed);

  panel.SetItem(&item, 0);
  EXPECT_EQ(LabelColor(panel.RenderConfirm(), "12,000,000"), kRed);
  EXPECT_NE(LabelColor(panel.RenderConfirm(), "Held"), kRed);
  // The cursor is on Cancel, so Enter leaves rather than doing nothing.
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);
}

// A cube the purse cannot cover is still walked onto and still opens its
// window -- the same one a player rerolls their way into.
TEST_F(CubePanelTest, AnUnaffordableCubeStillOpensItsQuestion) {
  EquipInstance item = Cubed();
  CubePanel panel = Open(item, 0);
  ASSERT_TRUE(panel.IsConfirming());
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), ConfirmChoice::kCancelled);
}

// The shelf is a ring, and the question holds the arrows while it is up.
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

}  // namespace
}  // namespace ms
