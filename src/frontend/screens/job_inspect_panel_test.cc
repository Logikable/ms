#include "src/frontend/screens/job_inspect_panel.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

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

  JobInspectPanel PanelOn(Job job) {
    JobInspectPanel panel(Catalog());
    panel.SetJob(job);
    return panel;
  }
};

TEST_F(JobInspectPanelTest, TitlesItselfWithTheJobsFullName) {
  EXPECT_NE(RenderElement(PanelOn(JOB_FIGHTER).Render()).find("Fighter"),
            std::string::npos);
}

// The one thing a player cannot read off the skills: most of a book names no
// weapon at all, and neither warrior line's 3rd job names one.
TEST_F(JobInspectPanelTest, NamesTheWeaponsTheJobIsBuiltAround) {
  EXPECT_NE(RenderElement(PanelOn(JOB_FIGHTER).Render()).find("Sword / Axe"),
            std::string::npos);
  EXPECT_NE(RenderElement(PanelOn(JOB_CRUSADER).Render()).find("Sword / Axe"),
            std::string::npos);
  EXPECT_NE(RenderElement(PanelOn(JOB_PRIEST).Render()).find("Staff"),
            std::string::npos);
}

// This job's own book, not the line's: a player choosing between a Fighter and
// a Page already holds the Swordman's skills.
TEST_F(JobInspectPanelTest, ListsThisJobsBookAndNoOther) {
  std::string rendered = RenderElement(PanelOn(JOB_FIGHTER).Render());
  EXPECT_NE(rendered.find("Brandish"), std::string::npos);
  EXPECT_NE(rendered.find("Weapon Mastery"), std::string::npos);
  EXPECT_EQ(rendered.find("Divine Swing"), std::string::npos);
  EXPECT_EQ(rendered.find("Slash Blast"), std::string::npos);
}

TEST_F(JobInspectPanelTest, SkillsAreInBookOrderWithTheirTagAndMaxLevel) {
  JobInspectPanel panel = PanelOn(JOB_FIGHTER);
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

TEST_F(JobInspectPanelTest, TheCursorStartsAtTheTopAndWrapsBothWays) {
  JobInspectPanel panel = PanelOn(JOB_FIGHTER);
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
  JobInspectPanel panel = PanelOn(JOB_FIGHTER);
  panel.MoveCursor(1);
  panel.SetJob(JOB_PAGE);
  ASSERT_NE(panel.selected_skill(), nullptr);
  EXPECT_EQ(panel.selected_skill()->name(), "Divine Swing");
}

TEST_F(JobInspectPanelTest, AJobWithNoBookSaysSoRatherThanCrashing) {
  JobInspectPanel panel = PanelOn(JOB_HERMIT);  // no Hermit skills in Catalog()
  EXPECT_EQ(panel.selected_skill(), nullptr);
  panel.MoveCursor(1);
  EXPECT_EQ(panel.selected_skill(), nullptr);
  EXPECT_NE(RenderElement(panel.Render()).find("(empty)"), std::string::npos);
}

}  // namespace
}  // namespace ms
