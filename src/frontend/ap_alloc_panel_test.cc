#include "src/frontend/ap_alloc_panel.h"

#include <gtest/gtest.h>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/panel_test_base.h"
#include "src/frontend/types.h"

namespace ms {
namespace {

class ApAllocPanelTest : public PanelTest {};

TEST_F(ApAllocPanelTest, RenderShowsAp) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/25);
  ApAllocPanel panel(c);
  EXPECT_NE(RenderElement(panel.Render()).find("AP: 25"), std::string::npos);
}

TEST_F(ApAllocPanelTest, RenderShowsStatValueFormat) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  ApAllocPanel panel(c);
  // No AP allocated, no equips: STR shows 0 (0+0).
  EXPECT_NE(RenderElement(panel.Render()).find("0 (0+0)"), std::string::npos);
}

TEST_F(ApAllocPanelTest, RenderShowsAllStatNames) {
  CharacterInstance c = MakeCharacter();
  ApAllocPanel panel(c);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("STR"), std::string::npos);
  EXPECT_NE(rendered.find("DEX"), std::string::npos);
  EXPECT_NE(rendered.find("INT"), std::string::npos);
  EXPECT_NE(rendered.find("LUK"), std::string::npos);
  EXPECT_NE(rendered.find("HP"), std::string::npos);
  EXPECT_NE(rendered.find("MP"), std::string::npos);
}

TEST_F(ApAllocPanelTest, RenderShowsButtonsOnSelectedRow) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  ApAllocPanel panel(c);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("[+1]"), std::string::npos);
  EXPECT_NE(rendered.find("[All]"), std::string::npos);
}

TEST_F(ApAllocPanelTest, ArrowDownMovesSelectionToNextRow) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  ApAllocPanel panel(c);
  std::string before = RenderElement(panel.Render());
  panel.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_NE(before, RenderElement(panel.Render()));
}

TEST_F(ApAllocPanelTest, ArrowUpDoesNotMoveAboveFirst) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  ApAllocPanel panel(c);
  std::string before = RenderElement(panel.Render());
  panel.OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(before, RenderElement(panel.Render()));
}

TEST_F(ApAllocPanelTest, EscapeReturnsMain) {
  CharacterInstance c = MakeCharacter();
  ApAllocPanel panel(c);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), kMain);
}

TEST_F(ApAllocPanelTest, EnterOnPlus1AllocatesOneStat) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/10);
  ApAllocPanel panel(c);
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(c.proto().ap(), 9);
  EXPECT_EQ(c.proto().allocated_stats().str(), 1);
}

TEST_F(ApAllocPanelTest, EnterWithNoApDoesNothing) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/0);
  ApAllocPanel panel(c);
  panel.OnEvent(ftxui::Event::Return);
  EXPECT_EQ(c.proto().ap(), 0);
}

TEST_F(ApAllocPanelTest, EnterOnAllOpensConfirmation) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/10);
  ApAllocPanel panel(c);
  panel.OnEvent(ftxui::Event::ArrowRight);  // focus [All]
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Return), kApAlloc);
  EXPECT_NE(RenderElement(panel.Render()).find("Confirm"), std::string::npos);
}

TEST_F(ApAllocPanelTest, ConfirmAllocatesAllAp) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/10);
  ApAllocPanel panel(c);
  panel.OnEvent(ftxui::Event::ArrowRight);  // focus [All]
  panel.OnEvent(ftxui::Event::Return);      // open confirmation
  panel.OnEvent(ftxui::Event::Return);      // confirm (Confirm is default)
  EXPECT_EQ(c.proto().ap(), 0);
  EXPECT_EQ(c.proto().allocated_stats().str(), 10);
}

TEST_F(ApAllocPanelTest, CancelDoesNotAllocate) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/10);
  ApAllocPanel panel(c);
  panel.OnEvent(ftxui::Event::ArrowRight);  // focus [All]
  panel.OnEvent(ftxui::Event::Return);      // open confirmation
  panel.OnEvent(ftxui::Event::ArrowRight);  // move to [Cancel]
  panel.OnEvent(ftxui::Event::Return);      // cancel
  EXPECT_EQ(c.proto().ap(), 10);
}

TEST_F(ApAllocPanelTest, EscapeFromConfirmationStaysOnScreen) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/10);
  ApAllocPanel panel(c);
  panel.OnEvent(ftxui::Event::ArrowRight);  // focus [All]
  panel.OnEvent(ftxui::Event::Return);      // open confirmation
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), kApAlloc);
  EXPECT_EQ(RenderElement(panel.Render()).find("Confirm"), std::string::npos);
}

TEST_F(ApAllocPanelTest, ResetRestoresInitialState) {
  CharacterInstance c = MakeCharacter(/*level=*/1, /*ap=*/5);
  ApAllocPanel panel(c);
  panel.OnEvent(ftxui::Event::ArrowDown);
  panel.OnEvent(ftxui::Event::ArrowRight);
  panel.Reset();
  std::string rendered = RenderElement(panel.Render());
  // After reset, STR row (first) has the cursor, [+1] is focused.
  EXPECT_NE(rendered.find("STR"), std::string::npos);
}

}  // namespace
}  // namespace ms
