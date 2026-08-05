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
#include "src/character/character.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

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
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
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
    for (int y = 0; y < screen.dimy(); ++y) {
      std::string row;
      for (int x = 0; x < screen.dimx(); ++x) {
        const std::string& cell = screen.PixelAt(x, y).character;
        // One cell per character here, so the index into `row` is the column.
        row += cell.empty() ? " " : cell;
      }
      size_t at = row.find(label);
      if (at != std::string::npos) {
        return screen.PixelAt(static_cast<int>(at), y).foreground_color;
      }
    }
    return ftxui::Color::Default;
  }

  static std::string RenderComponent(ftxui::Component component) {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                                 ftxui::Dimension::Fixed(20));
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
  EquipPrototype sword_;
  int panel_focus_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_PANEL_TEST_BASE_H_
