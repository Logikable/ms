#include "src/frontend/screens/job_inspect_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/skill_placement.h"
#include "src/frontend/testing/panel_test_base.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

Skill MakeSkill(const std::string& name, JobAdvancement advancement, int order,
                int max_level, SkillKind kind = SKILL_KIND_PASSIVE) {
  Skill skill;
  skill.set_name(name);
  PlaceIn(skill, advancement, order);
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

// A block `rows` tall, standing in for a card or book of that height.
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
  // A Fighter's book and a Page's, so a test can show one job's page doesn't
  // list the other's skills, plus the Swordman book both come from.
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
        // The Hero's matrix: a common node, and one of each kind the job has.
        // Boost nodes go to 60 while the other two stop at 30.
        {"rope_lift",
         Node("Rope Lift", JOB_ADVANCEMENT_COMMON, 1, V_NODE_KIND_COMMON, 30)},
        {"radiant_evil",
         Node("Radiant Evil", JOB_ADVANCEMENT_HERO_V, 1, V_NODE_KIND_JOB, 30)},
        {"puncture_boost", Node("Puncture Boost", JOB_ADVANCEMENT_HERO_V, 2,
                                V_NODE_KIND_BOOST, 60)},
    };
  }

  // The rendered line containing `needle`, or "" if none does. Rows have colour
  // escapes between their columns, so searching the whole render for two
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

// The one thing a player can't learn from the skills: most skills name no
// weapon, and neither warrior line's 3rd job names one.
TEST_F(JobInspectPanelTest, NamesTheWeaponsTheJobIsBuiltAround) {
  EXPECT_NE(RenderElement(PanelOn(JOB_FIGHTER, 2).Render()).find("Sword / Axe"),
            std::string::npos);
  EXPECT_NE(
      RenderElement(PanelOn(JOB_CRUSADER, 3).Render()).find("Sword / Axe"),
      std::string::npos);
  EXPECT_NE(RenderElement(PanelOn(JOB_PRIEST, 3).Render()).find("Staff"),
            std::string::npos);
}

// This job's own book, not the whole line's: a player choosing between Fighter
// and Page already has the Swordman's skills.
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
  // The tag, name and max level are on one row. Checked per line rather than
  // over the whole render, which has colour escapes between them.
  EXPECT_NE(LineWith(rendered, "Brandish").find("A:"), std::string::npos);
  EXPECT_NE(LineWith(rendered, "Brandish").find("Max 20"), std::string::npos);
  EXPECT_NE(LineWith(rendered, "Weapon Mastery").find("P:"), std::string::npos);
  EXPECT_NE(LineWith(rendered, "Weapon Mastery").find("Max 10"),
            std::string::npos);
}

// Every list in the game marks its selected row with a caret, so this one does
// too. An inverted name would look like a button, and nothing here is pressed.
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

// A name half again as long as its column is cut to fit and scrolls while
// selected, instead of widening the panel or being lost.
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
  panel.MoveCursor(1);  // past the end, back to the top
  EXPECT_EQ(panel.selected_skill()->name(), "Brandish");
  panel.MoveCursor(-1);  // and past the top, back to the end
  EXPECT_EQ(panel.selected_skill()->name(), "Agile Arms");
}

// Opening the panel on another job resets the cursor to the top of its book,
// since the old row number means nothing in a different book.
TEST_F(JobInspectPanelTest, ANewJobStartsTheCursorOver) {
  JobInspectPanel panel = PanelOn(JOB_FIGHTER, 2);
  panel.MoveCursor(1);
  panel.SetJob(JOB_PAGE, 2);
  ASSERT_NE(panel.selected_skill(), nullptr);
  EXPECT_EQ(panel.selected_skill()->name(), "Divine Swing");
}

// The 5th advancement gives a matrix instead of a book, and the matrix includes
// the common nodes as well as the job's own, of every kind, including boosts,
// which go to 60 while the other two stop at 30. Listing only the job's own
// nodes would show one where advancing gives three.
TEST_F(JobInspectPanelTest, AFifthAdvancementListsItsWholeMatrix) {
  JobInspectPanel panel = PanelOn(JOB_HERO, 5);
  std::vector<const Skill*> nodes = panel.Skills();
  ASSERT_EQ(nodes.size(), 3u);
  EXPECT_EQ(nodes[0]->name(), "Radiant Evil") << "the job's own lead";
  EXPECT_EQ(nodes[1]->name(), "Puncture Boost") << "then the boosts";
  EXPECT_EQ(nodes[2]->name(), "Rope Lift") << "and the commons at the foot";

  EXPECT_NE(RenderElement(panel.Render()).find("Max 60"), std::string::npos)
      << "a boost node goes twice as far as the rest";
}

// A 4th job lists its own book.
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

// This is why the tallest card is passed in: the screen is the same height
// whichever skill the cursor is on.
TEST_F(JobInspectPanelTest, TheScreenStandsStillUnderAShortCard) {
  constexpr int kTallest = 20;
  EXPECT_EQ(Rows(JobInspectScreen(Block(9), Block(kTallest), kTallest)),
            kTallest);
  EXPECT_EQ(Rows(JobInspectScreen(Block(9), Block(6), kTallest)), kTallest);
}

TEST_F(JobInspectPanelTest, ABookTallerThanEveryCardIsNotClipped) {
  EXPECT_EQ(Rows(JobInspectScreen(Block(30), Block(6), 20)), 30);
}

// A card that measures its own width has to request its right margin.
TEST_F(JobInspectPanelTest, EveryRowKeepsAColumnClearOfTheRightBorder) {
  std::vector<std::string> touching =
      RowsTouchingTheRightBorder(PanelOn(JOB_FIGHTER, 2).Render());
  EXPECT_TRUE(touching.empty()) << (touching.empty() ? "" : touching[0]);
}

}  // namespace
}  // namespace ms
