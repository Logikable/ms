#ifndef MS_SRC_FRONTEND_TESTING_PANEL_TEST_BASE_H_
#define MS_SRC_FRONTEND_TESTING_PANEL_TEST_BASE_H_

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
#include "src/character/exp_table.h"
#include "src/frontend/testing/screen_text.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Wide enough for the widest list a panel draws. The game runs wider still; a
// screen that clipped a column would fail tests over something no player sees.
constexpr int kTestScreenWidth = 100;

// Where a band of background colour landed: the row and the columns it covers.
// Tests check the span, not just the row, because a band stopping short of the
// borders looks like a column that isn't part of the row.
struct BandSpan {
  int y = 0;
  int first = 0;
  int last = 0;
};

inline std::vector<BandSpan> BandSpans(const ftxui::Screen& screen,
                                       ftxui::Color color) {
  std::vector<BandSpan> spans;
  for (int y = 0; y < screen.dimy(); ++y) {
    BandSpan span = {y, -1, -1};
    for (int x = 0; x < screen.dimx(); ++x) {
      if (screen.PixelAt(x, y).background_color == color) {
        if (span.first < 0) {
          span.first = x;
        }
        span.last = x;
      }
    }
    if (span.first >= 0) {
      spans.push_back(span);
    }
  }
  return spans;
}

// Shared fixture for panel tests. Provides c_ (level-1 Beginner character) and
// sword_ (primary weapon slot, required level 10, Warrior only).
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

  // The colour of a panel's top-left border cell. Read from the pixel because
  // RenderElement goes through Screen::ToString, which loses colour: a gold
  // border and a steel-blue one give the same string.
  static ftxui::Color BorderColor(ftxui::Element element) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, element);
    return screen.PixelAt(0, 0).foreground_color;
  }

  // The colours of every divider line inside a panel, top to bottom. A lit
  // panel is gold throughout, and a steel-blue line across a gold window looks
  // like a seam, which BorderColor can't detect.
  static std::vector<ftxui::Color> InnerRuleColors(ftxui::Element element) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, element);
    std::vector<ftxui::Color> colors;
    // Sampled at x=1, not x=0, because x=0 is the window's left border, a
    // vertical line on every row including those a divider crosses. The first
    // and last rows are the window's borders, which BorderColor covers.
    for (int y = 1; y + 1 < screen.dimy(); ++y) {
      if (screen.PixelAt(1, y).character == "─") {
        colors.push_back(screen.PixelAt(1, y).foreground_color);
      }
    }
    return colors;
  }

  // The topmost inner divider's colour, for a panel with only one worth
  // checking. Color::Default if there is none, which no expected colour equals.
  static ftxui::Color InnerRuleColor(ftxui::Element element) {
    std::vector<ftxui::Color> colors = InnerRuleColors(std::move(element));
    return colors.empty() ? ftxui::Color::Default : colors.front();
  }

  // The foreground colour of the first cell of `label` in a rendered element,
  // for checking a tab's colour. Returns Color::Default if the label isn't on
  // screen, which no expected colour equals.
  static ftxui::Color LabelColor(ftxui::Element element,
                                 const std::string& label) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
    ftxui::Render(screen, element);
    return ColorOf(screen, label);
  }

  // The rendered component as plain text. Use this to check what the screen
  // says, and RenderComponent only when checking styling. See screen_text.h for
  // why ToString doesn't work.
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

  // Levels `c_` to `level`. Item menu entries are level-gated, so a test
  // checking that one is absent proves nothing at level 1, where they all are.
  // Use UnlockLevel(Feature::...) rather than a hard-coded number.
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

  // Unlocks every level-gated feature for the account, for tests about what a
  // panel shows once everything is unlocked rather than about the gate itself.
  void UnlockEverything() {
    account_.RecordProgress(kMaxLevel, /*job_stage=*/4);
  }

  std::mt19937 rng_{0};
  CharacterInstance c_ = MakeCharacter();
  // The account every panel test shares. It is empty, so any unlocks come from
  // the character's own level.
  AccountInstance account_;
  EquipPrototype sword_;
  int panel_focus_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_TESTING_PANEL_TEST_BASE_H_
