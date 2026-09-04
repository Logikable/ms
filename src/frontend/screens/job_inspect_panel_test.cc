#include "src/frontend/screens/job_inspect_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

Skill MakeSkill(const std::string& name, JobAdvancement advancement, int order,
                int max_level, SkillKind kind = SKILL_KIND_PASSIVE) {
  Skill skill;
  skill.set_name(name);
  skill.set_job_advancement(advancement);
  skill.set_skill_order(order);
  skill.set_max_level(max_level);
  skill.set_kind(kind);
  return skill;
}

Skill Node(const std::string& name, JobAdvancement advancement, int order,
           VNodeKind kind, int max_level) {
  Skill skill = MakeSkill(name, advancement, order, max_level);
  skill.set_v_node(kind);
  return skill;
}

// A block `rows` tall, standing in for a card or a book of that height.
ftxui::Element Block(int rows) {
  std::vector<ftxui::Element> lines;
  for (int i = 0; i < rows; ++i) {
    lines.push_back(ftxui::text("x"));
  }
  return ftxui::vbox(std::move(lines));
}

int Rows(ftxui::Element element) {
  element->ComputeRequirement();
  return element->requirement().min_y;
}

int Cols(ftxui::Element element) {
  element->ComputeRequirement();
  return element->requirement().min_x;
}

class JobInspectPanelTest : public PanelTest {
 protected:
  // A Fighter's book and a Page's, so a test can prove one job's page is not
  // showing the other's, plus the Swordman book both of them grew out of.
  std::map<std::string, Skill> Catalog() {
    return {
        {"brandish", MakeSkill("Brandish", JOB_ADVANCEMENT_FIGHTER, 1, 20,
                               SKILL_KIND_ATTACK)},
        {"agile_arms", MakeSkill("Agile Arms", JOB_ADVANCEMENT_FIGHTER, 3, 5)},
        {"weapon_mastery",
         MakeSkill("Weapon Mastery", JOB_ADVANCEMENT_FIGHTER, 2, 10)},
        {"divine_swing",
         MakeSkill("Divine Swing", JOB_ADVANCEMENT_PAGE, 1, 20)},
        {"slash_blast",
         MakeSkill("Slash Blast", JOB_ADVANCEMENT_SWORDMAN, 1, 20)},
        {"puncture", MakeSkill("Puncture", JOB_ADVANCEMENT_HERO, 1, 30)},
        // The Hero's matrix: a common node, and one of each kind the job
        // itself holds. Boost nodes go to 60 where the other two stop at 30.
        {"rope_lift",
         Node("Rope Lift", JOB_ADVANCEMENT_COMMON, 1, V_NODE_KIND_COMMON, 30)},
        {"radiant_evil",
         Node("Radiant Evil", JOB_ADVANCEMENT_HERO_V, 1, V_NODE_KIND_JOB, 30)},
        {"puncture_boost", Node("Puncture Boost", JOB_ADVANCEMENT_HERO_V, 2,
                                V_NODE_KIND_BOOST, 60)},
    };
  }

  // The rendered line holding `needle`, or "" when no line does. Rows carry
  // colour escapes between their columns, so a whole-render search for two
  // columns side by side finds nothing.
  static std::string LineWith(const std::string& rendered,
                              const std::string& needle) {
    size_t start = 0;
    while (start <= rendered.size()) {
      size_t end = rendered.find('\n', start);
      if (end == std::string::npos) {
        end = rendered.size();
      }
      std::string line = rendered.substr(start, end - start);
      if (line.find(needle) != std::string::npos) {
        return line;
      }
      start = end + 1;
    }
    return "";
  }

  JobInspectPanel PanelOn(Job job, int stage) {
    JobInspectPanel panel(Catalog());
    panel.SetJob(job, stage);
    return panel;
  }
};

TEST_F(JobInspectPanelTest, TitlesItselfWithTheJobsFullName) {
  EXPECT_NE(RenderElement(PanelOn(JOB_FIGHTER, 2).Render()).find("Fighter"),
            std::string::npos);
}

// The one thing a player cannot read off the skills: most of a book names no
// weapon at all, and neither warrior line's 3rd job names one.
TEST_F(JobInspectPanelTest, NamesTheWeaponsTheJobIsBuiltAround) {
  EXPECT_NE(RenderElement(PanelOn(JOB_FIGHTER, 2).Render()).find("Sword / Axe"),
            std::string::npos);
  EXPECT_NE(
      RenderElement(PanelOn(JOB_CRUSADER, 3).Render()).find("Sword / Axe"),
      std::string::npos);
  EXPECT_NE(RenderElement(PanelOn(JOB_PRIEST, 3).Render()).find("Staff"),
            std::string::npos);
}

// This job's own book, not the line's: a player choosing between a Fighter and
// a Page already holds the Swordman's skills.
TEST_F(JobInspectPanelTest, ListsThisJobsBookAndNoOther) {
  std::string rendered = RenderElement(PanelOn(JOB_FIGHTER, 2).Render());
  EXPECT_NE(rendered.find("Brandish"), std::string::npos);
  EXPECT_NE(rendered.find("Weapon Mastery"), std::string::npos);
  EXPECT_EQ(rendered.find("Divine Swing"), std::string::npos);
  EXPECT_EQ(rendered.find("Slash Blast"), std::string::npos);
}

TEST_F(JobInspectPanelTest, SkillsAreInBookOrderWithTheirTagAndMaxLevel) {
  JobInspectPanel panel = PanelOn(JOB_FIGHTER, 2);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_LT(rendered.find("Brandish"), rendered.find("Weapon Mastery"));
  EXPECT_LT(rendered.find("Weapon Mastery"), rendered.find("Agile Arms"));
  // The tag, the name and the max level share a row. Asserted per line rather
  // than over the whole render, which carries the colour escapes between them.
  EXPECT_NE(LineWith(rendered, "Brandish").find("A:"), std::string::npos);
  EXPECT_NE(LineWith(rendered, "Brandish").find("Max 20"), std::string::npos);
  EXPECT_NE(LineWith(rendered, "Weapon Mastery").find("P:"), std::string::npos);
  EXPECT_NE(LineWith(rendered, "Weapon Mastery").find("Max 10"),
            std::string::npos);
}

// Every list in the game marks its selected row with a caret, so this one
// does too -- an inverted name would read as a button, and nothing here is
// pressed.
TEST_F(JobInspectPanelTest, TheSelectedRowWearsTheCursor) {
  JobInspectPanel panel = PanelOn(JOB_FIGHTER, 2);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(LineWith(rendered, "Brandish").find(">"), std::string::npos);
  EXPECT_EQ(LineWith(rendered, "Weapon Mastery").find(">"), std::string::npos);

  panel.MoveCursor(1);
  rendered = RenderElement(panel.Render());
  EXPECT_EQ(LineWith(rendered, "Brandish").find(">"), std::string::npos);
  EXPECT_NE(LineWith(rendered, "Weapon Mastery").find(">"), std::string::npos);
}

// The longest name in the game is half again the column it sits in. It is cut
// to the column and slides under it while selected, rather than widening the
// panel or being lost.
TEST_F(JobInspectPanelTest, ALongNameIsCutToItsColumnAndNotPastIt) {
  const std::string kLongest = "Expert Throwing Star Handling";
  std::map<std::string, Skill> catalog = {
      {"expert", MakeSkill(kLongest, JOB_ADVANCEMENT_FIGHTER, 1, 20)}};
  JobInspectPanel panel(catalog);
  panel.SetJob(JOB_FIGHTER, 2);
  std::string rendered = RenderElement(panel.Render());

  EXPECT_EQ(rendered.find(kLongest), std::string::npos)
      << "the whole name fits, so this test proves nothing";
  EXPECT_NE(rendered.find(kLongest.substr(0, 19)), std::string::npos);
  EXPECT_EQ(Cols(panel.Render()), Cols(PanelOn(JOB_FIGHTER, 2).Render()))
      << "a long name widened the panel";
}

TEST_F(JobInspectPanelTest, TheCursorStartsAtTheTopAndWrapsBothWays) {
  JobInspectPanel panel = PanelOn(JOB_FIGHTER, 2);
  ASSERT_NE(panel.selected_skill(), nullptr);
  EXPECT_EQ(panel.selected_skill()->name(), "Brandish");
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_skill()->name(), "Weapon Mastery");
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_skill()->name(), "Agile Arms");
  panel.MoveCursor(1);  // off the end, back to the top
  EXPECT_EQ(panel.selected_skill()->name(), "Brandish");
  panel.MoveCursor(-1);  // and off the top, back to the end
  EXPECT_EQ(panel.selected_skill()->name(), "Agile Arms");
}

// Opening the panel on another job puts the cursor back at the top of its
// book: the old row number means nothing in a book it did not come from.
TEST_F(JobInspectPanelTest, ANewJobStartsTheCursorOver) {
  JobInspectPanel panel = PanelOn(JOB_FIGHTER, 2);
  panel.MoveCursor(1);
  panel.SetJob(JOB_PAGE, 2);
  ASSERT_NE(panel.selected_skill(), nullptr);
  EXPECT_EQ(panel.selected_skill()->name(), "Divine Swing");
}

// The 5th hands over a matrix rather than a book, and the matrix is the
// commons as well as the job's own -- every kind of node, boosts included,
// which go to 60 where the other two stop at 30. Listing only the job's own
// would have shown one node where advancing gives three.
TEST_F(JobInspectPanelTest, AFifthAdvancementListsItsWholeMatrix) {
  JobInspectPanel panel = PanelOn(JOB_HERO, 5);
  std::vector<const Skill*> nodes = panel.Skills();
  ASSERT_EQ(nodes.size(), 3u);
  EXPECT_EQ(nodes[0]->name(), "Rope Lift") << "the commons lead the matrix";
  EXPECT_EQ(nodes[1]->name(), "Radiant Evil");
  EXPECT_EQ(nodes[2]->name(), "Puncture Boost");

  EXPECT_NE(RenderElement(panel.Render()).find("Max 60"), std::string::npos)
      << "a boost node goes twice as far as the rest";
}

// The 4th job is the last stage a job can sit at, and asking only about the
// stages below it left every one of these pages blank.
TEST_F(JobInspectPanelTest, AFourthJobListsItsOwnBook) {
  JobInspectPanel panel = PanelOn(JOB_HERO, 4);
  ASSERT_NE(panel.selected_skill(), nullptr);
  EXPECT_EQ(panel.selected_skill()->name(), "Puncture");
  std::string rendered = RenderElement(panel.Render());
  EXPECT_NE(rendered.find("Puncture"), std::string::npos);
  EXPECT_EQ(rendered.find("(empty)"), std::string::npos);
}

TEST_F(JobInspectPanelTest, AJobWithNoBookSaysSoRatherThanCrashing) {
  JobInspectPanel panel =
      PanelOn(JOB_HERMIT, 3);  // no Hermit skills in Catalog()
  EXPECT_EQ(panel.selected_skill(), nullptr);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_skill(), nullptr);
  EXPECT_NE(RenderElement(panel.Render()).find("(empty)"), std::string::npos);
}

// The whole point of passing the tallest card in: the screen is the same
// height whichever skill the cursor is on.
TEST_F(JobInspectPanelTest, TheScreenStandsStillUnderAShortCard) {
  constexpr int kTallest = 20;
  EXPECT_EQ(Rows(JobInspectScreen(Block(9), Block(kTallest), kTallest)),
            kTallest);
  EXPECT_EQ(Rows(JobInspectScreen(Block(9), Block(6), kTallest)), kTallest);
}

TEST_F(JobInspectPanelTest, ABookTallerThanEveryCardIsNotClipped) {
  EXPECT_EQ(Rows(JobInspectScreen(Block(30), Block(6), 20)), 30);
}

// A card that measures its own width has to ask for its right margin.
TEST_F(JobInspectPanelTest, EveryRowKeepsAColumnClearOfTheRightBorder) {
  std::vector<std::string> touching =
      RowsTouchingTheRightBorder(PanelOn(JOB_FIGHTER, 2).Render());
  EXPECT_TRUE(touching.empty()) << (touching.empty() ? "" : touching[0]);
}

}  // namespace
}  // namespace ms
