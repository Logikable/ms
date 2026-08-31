#ifndef MS_SRC_FRONTEND_WIDGETS_PANEL_TEST_BASE_H_
#define MS_SRC_FRONTEND_WIDGETS_PANEL_TEST_BASE_H_

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/frontend/widgets/screen_text.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Wide enough for the widest list a panel draws. The game runs wider still;
// a screen that clipped a column would fail tests over something no player
// ever sees.
constexpr int kTestScreenWidth = 100;

// The rows of `element` that put text hard against its RIGHT border with no
// column of clearance, drawn at the width the element asks for rather than at
// the test screen's. Empty is the passing answer.
//
// The mistake this catches is a card that measures its own width from its
// widest row and then forgets to ask for a margin, so the value comes out
// welded to the border. The LEFT is not asked about: the column inside the
// left border belongs to the cursor, and every list in the game draws its
// caret there.
//
// Rules are skipped -- a rule is drawn border to border on purpose -- and so
// is a card whose right column is a scroll bar, where the bar is the margin.
inline std::vector<std::string> RowsTouchingTheRightBorder(
    ftxui::Element element) {
  element->ComputeRequirement();
  int width = element->requirement().min_x;
  int height = element->requirement().min_y;
  if (width < 4) {
    return {};
  }
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                               ftxui::Dimension::Fixed(height));
  ftxui::Render(screen, element);
  std::vector<std::string> touching;
  for (int y = 1; y + 1 < height; ++y) {
    std::string row = ScreenRow(screen, y);
    std::string margin = ScreenRow(screen, y, width - 2, width - 1);
    if (margin != " " && margin != "─" && margin != "┃" && margin != "│") {
      touching.push_back(row);
    }
  }
  return touching;
}

// Shared fixture for panel tests. Provides c_ (level-1 Beginner character)
// and sword_ (primary weapon slot, required level 10, Warrior only).
class PanelTest : public testing::Test {
 protected:
  CharacterInstance MakeCharacter(int level = 1, int ap = 0) {
    Character proto;
    proto.set_level(level);
    proto.set_ap(ap);
    proto.set_job(JOB_BEGINNER);
    return CharacterInstance(rng_, std::move(proto));
  }

  static std::string RenderElement(ftxui::Element element) {
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(kTestScreenWidth), ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, element);
    return screen.ToString();
  }

  // The color of a panel's top-left border cell. Read off the pixel because
  // RenderElement goes through Screen::ToString, which is where color goes to
  // die: a gold border and a steel-blue one produce the same string.
  static ftxui::Color BorderColor(ftxui::Element element) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, element);
    return screen.PixelAt(0, 0).foreground_color;
  }

  // The colors of every divider rule inside a panel, top to bottom: the rows
  // drawn as a box-drawing horizontal line, minus the window's own top and
  // bottom borders. A lit panel has to go gold all the way through, and a rule
  // left steel-blue across the middle of a gold window reads as a seam, which
  // BorderColor cannot see.
  static std::vector<ftxui::Color> InnerRuleColors(ftxui::Element element) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, element);
    std::vector<ftxui::Color> colors;
    // Sampled at x=1 rather than x=0, which is the window's left border: a
    // vertical line on every row, including the ones a rule crosses. The first
    // and last rows are the window's own borders, which BorderColor covers.
    for (int y = 1; y + 1 < screen.dimy(); ++y) {
      if (screen.PixelAt(1, y).character == "─") {
        colors.push_back(screen.PixelAt(1, y).foreground_color);
      }
    }
    return colors;
  }

  // The topmost inner rule's color, for a panel that only has one worth
  // asking about. Color::Default when the panel has no inner rule at all,
  // which no expected color equals.
  static ftxui::Color InnerRuleColor(ftxui::Element element) {
    std::vector<ftxui::Color> colors = InnerRuleColors(std::move(element));
    return colors.empty() ? ftxui::Color::Default : colors.front();
  }

  // The foreground color of the first cell of `label` in a rendered element,
  // for asking what color a tab chip came out. Returns Color::Default when the
  // label is not on screen, which no expected color equals.
  static ftxui::Color LabelColor(ftxui::Element element,
                                 const std::string& label) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, element);
    return ColorOf(screen, label);
  }

  // The rendered component as plain characters. Use this to ask what the
  // screen says; use RenderComponent only when the styling is what is being
  // asserted. See screen_text.h for why ToString will not do.
  static std::string RenderComponentText(ftxui::Component component) {
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(kTestScreenWidth), ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, component->Render());
    return ScreenText(screen);
  }

  static std::string RenderComponent(ftxui::Component component) {
    ftxui::Screen screen = ftxui::Screen::Create(
        ftxui::Dimension::Fixed(kTestScreenWidth), ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, component->Render());
    return screen.ToString();
  }

  // Levels `c_` to `level`. The item menu's entries are level-gated, so a test
  // that means to exercise one has to have reached it -- and, just as easily
  // missed, a test asserting an entry is ABSENT proves nothing at level 1,
  // where every gated entry is absent anyway. Ask UnlockLevel(Feature::...)
  // for the level rather than writing a number: they have moved before.
  void LevelTo(int level) {
    while (c_.proto().level() < level) {
      c_.LevelUp();
    }
  }

  void SetUp() override {
    sword_.set_name("Sword");
    sword_.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_.set_required_level(10);
    sword_.add_equip_job_categories(EQUIP_JOB_CATEGORY_WARRIOR);
  }

  std::mt19937 rng_{0};
  CharacterInstance c_ = MakeCharacter();
  // The account every panel under test shares. Empty, so the unlocks a test
  // asks about are the character's own climb.
  AccountInstance account_;
  EquipPrototype sword_;
  int panel_focus_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_PANEL_TEST_BASE_H_
