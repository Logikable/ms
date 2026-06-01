#include "src/frontend/character_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "src/frontend/panel_test_base.h"

namespace ms {
namespace {

class CharacterPanelTest : public PanelTest {};

TEST_F(CharacterPanelTest, ShowsLevel) {
  CharacterPanel panel(c_);
  EXPECT_NE(RenderElement(panel.Render()).find("Lv1"), std::string::npos);
}

TEST_F(CharacterPanelTest, ShowsJobName) {
  CharacterPanel panel(c_);
  EXPECT_NE(RenderElement(panel.Render()).find("Beginner"), std::string::npos);
}

}  // namespace
}  // namespace ms
