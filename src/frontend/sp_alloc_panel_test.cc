#include "src/frontend/sp_alloc_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/panel_test_base.h"
#include "src/frontend/types.h"

namespace ms {
namespace {

class SpAllocPanelTest : public PanelTest {};

TEST_F(SpAllocPanelTest, RenderShowsTheHeaderAndPlaceholder) {
  CharacterInstance c = MakeCharacter();
  SpAllocPanel panel(c);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("SP Allocation"), std::string::npos);
  EXPECT_NE(rendered.find("No skills"), std::string::npos);
}

TEST_F(SpAllocPanelTest, ShowsTheCurrentStageSp) {
  CharacterInstance c = MakeCharacter(/*level=*/10);
  c.AdvanceJob(JOB_WARRIOR);  // stage 1, +1 SP
  SpAllocPanel panel(c);
  EXPECT_NE(RenderElement(panel.Render()).find("SP: 1"), std::string::npos);
}

TEST_F(SpAllocPanelTest, EscapeReturnsMain) {
  CharacterInstance c = MakeCharacter();
  SpAllocPanel panel(c);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::Escape), kMain);
}

TEST_F(SpAllocPanelTest, OtherKeysStayOnScreen) {
  CharacterInstance c = MakeCharacter();
  SpAllocPanel panel(c);
  EXPECT_EQ(panel.OnEvent(ftxui::Event::ArrowDown), kSpAlloc);
}

}  // namespace
}  // namespace ms
