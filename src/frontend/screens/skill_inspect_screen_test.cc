#include "src/frontend/screens/skill_inspect_screen.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/testing/panel_test_base.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

Skill Named(const std::string& name, const std::string& description) {
  Skill skill;
  skill.set_name(name);
  skill.set_description(description);
  skill.set_kind(SKILL_KIND_ACTIVE);
  skill.set_max_level(20);
  return skill;
}

class SkillInspectScreenTest : public PanelTest {
 protected:
  SkillInspectScreenTest()
      : node_(Named("Alpha Boost", "Strengthens two attacks.")),
        alpha_(Named("Alpha", "The first attack.")),
        beta_(Named("Beta", "The second attack.")) {
    node_.set_kind(SKILL_KIND_PASSIVE);
    node_.set_max_level(60);
    for (const char* name : {"Alpha", "Beta"}) {
      SkillBoost* boost = node_.add_boost();
      boost->set_skill_name(name);
      boost->mutable_effect()->set_final_dmg_pct(0.02);
    }
    screen_.SetSkills({&node_, 10, 0}, {{&alpha_, 5, 0}, {&beta_, 0, 0}});
    screen_.SetSize(120, 30);
  }

  // Columns [from, to) of every row of the screen at its set size.
  std::vector<std::string> Lines(int from = 0, int to = 120) const {
    ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                                 ftxui::Dimension::Fixed(30));
    ftxui::Render(screen, screen_.Render());
    std::vector<std::string> lines;
    for (int y = 0; y < screen.dimy(); ++y) {
      std::string line;
      for (int x = from; x < to; ++x) {
        const std::string& glyph = screen.PixelAt(x, y).character;
        line += glyph.empty() ? " " : glyph;
      }
      lines.push_back(line);
    }
    return lines;
  }

  std::string Rendered() const {
    std::string all;
    for (const std::string& line : Lines()) {
      all += line + "\n";
    }
    return all;
  }

  // The column of the right card's top-left corner.
  int RightCardColumn() const {
    for (int x = 1; x < 120; ++x) {
      if (Lines(x, x + 1)[0] == "╭") {
        return x;
      }
    }
    return -1;
  }

  // The column just past the right card's top-right corner.
  int RightCardEnd() const {
    for (int x = 119; x > 0; --x) {
      if (Lines(x, x + 1)[0] == "╮") {
        return x + 1;
      }
    }
    return -1;
  }

  Skill node_;
  Skill alpha_;
  Skill beta_;
  SkillInspectScreen screen_;
};

// Side by side, with a tab per boosted skill and the first one's card showing.
// Too narrow for two, it is the node's card alone.
TEST_F(SkillInspectScreenTest, ShowsTheBoostedSkillBesideTheBoost) {
  ASSERT_TRUE(screen_.Split());
  std::string rendered = Rendered();
  EXPECT_NE(rendered.find("Strengthens two attacks."), std::string::npos);
  EXPECT_NE(rendered.find("The first attack."), std::string::npos);
  EXPECT_EQ(rendered.find("The second attack."), std::string::npos);
  EXPECT_NE(rendered.find(" Beta "), std::string::npos) << "a tab per skill";
  EXPECT_GT(RightCardColumn(), 0);

  screen_.SetSize(90, 30);
  EXPECT_FALSE(screen_.Split());
  rendered = Rendered();
  EXPECT_NE(rendered.find("Strengthens two attacks."), std::string::npos);
  EXPECT_EQ(rendered.find("The first attack."), std::string::npos);
  EXPECT_TRUE(screen_.OnEvent(ftxui::Event::Tab));
  EXPECT_FALSE(screen_.boosted_focused()) << "nothing to switch to";
}

// Left and Right turn the tabs only while the boosted card has focus, and the
// card keeps its place and width whichever tab is showing.
TEST_F(SkillInspectScreenTest, TabMovesTheKeysBetweenTheCards) {
  const int column = RightCardColumn();
  const int end = RightCardEnd();
  EXPECT_TRUE(screen_.OnEvent(ftxui::Event::ArrowRight));
  EXPECT_EQ(screen_.tab(), 0) << "the node's card has the keys";

  EXPECT_TRUE(screen_.OnEvent(ftxui::Event::Tab));
  EXPECT_TRUE(screen_.boosted_focused());
  screen_.OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(screen_.tab(), 1);
  EXPECT_NE(Rendered().find("The second attack."), std::string::npos);
  EXPECT_EQ(RightCardColumn(), column);
  EXPECT_EQ(RightCardEnd(), end);
  screen_.OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(screen_.tab(), 0) << "wraps, like every tab bar";

  screen_.OnEvent(ftxui::Event::TabReverse);
  EXPECT_FALSE(screen_.boosted_focused());
  EXPECT_FALSE(screen_.OnEvent(ftxui::Event::Return));
  screen_.Reset();
  EXPECT_EQ(screen_.tab(), 0);
}

// Up and Down scroll the focused card and leave the other where it was.
TEST_F(SkillInspectScreenTest, OnlyTheFocusedCardScrolls) {
  screen_.SetSize(120, 9);
  const int split = RightCardColumn();
  const std::vector<std::string> left = Lines(0, split);
  const std::vector<std::string> right = Lines(split, 120);
  screen_.OnEvent(ftxui::Event::Tab);
  screen_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(Lines(0, split), left);
  EXPECT_NE(Lines(split, 120), right);
  screen_.OnEvent(ftxui::Event::Tab);
  screen_.OnEvent(ftxui::Event::ArrowDown);
  EXPECT_NE(Lines(0, split), left);
}

TEST(BoostedSkillNamesTest, EachSkillOnceInOrderNeverItself) {
  Skill skill;
  skill.set_name("Self");
  for (const char* name : {"B", "A", "B", "Self"}) {
    skill.add_boost()->set_skill_name(name);
  }
  skill.mutable_buff()->add_boost()->set_skill_name("C");
  EXPECT_EQ(BoostedSkillNames(skill),
            (std::vector<std::string>{"B", "A", "C"}));
}

}  // namespace
}  // namespace ms
