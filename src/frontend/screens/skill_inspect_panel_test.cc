#include "src/frontend/screens/skill_inspect_panel.h"

#include <gtest/gtest.h>

#include <string>

#include "src/frontend/widgets/panel_test_base.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

class SkillInspectPanelTest : public PanelTest {
 protected:
  std::string RenderAt(const Skill& skill, int level) {
    SkillInspectPanel panel;
    panel.SetSkill(&skill, level);
    return RenderElement(panel.Render());
  }
};

// Iron Body: DEF +10/level, Max HP +1%/level, damage taken -0.5%/level.
Skill MakeIronBody() {
  Skill skill;
  skill.set_name("Iron Body");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SWORDMAN);
  skill.set_max_level(20);
  skill.set_description("Boosts DEF and Max HP.");
  skill.mutable_base()->set_def(10);
  skill.mutable_base()->set_max_hp_pct(0.01);
  skill.mutable_base()->set_damage_taken_pct(0.005);
  skill.mutable_per_level()->set_def(10);
  skill.mutable_per_level()->set_max_hp_pct(0.01);
  skill.mutable_per_level()->set_damage_taken_pct(0.005);
  return skill;
}

// Lucky Seven: three strikes of 72%+2%/level, five enemies, claw only.
Skill MakeLuckySeven() {
  Skill skill;
  skill.set_name("Lucky Seven");
  skill.set_kind(SKILL_KIND_ATTACK);
  skill.set_job_advancement(JOB_ADVANCEMENT_ROGUE);
  skill.set_max_level(20);
  skill.set_description("Throw 7 lucky throwing stars.");
  skill.set_lines(3);
  skill.set_max_enemies(5);
  skill.add_required_equip_type(EQUIP_TYPE_CLAW);
  skill.mutable_base()->set_skill_pct(0.72);
  skill.mutable_per_level()->set_skill_pct(0.02);
  return skill;
}

TEST_F(SkillInspectPanelTest, ShowsTheNameAndMaxLevel) {
  Skill skill = MakeIronBody();
  std::string rendered = RenderAt(skill, 5);
  EXPECT_NE(rendered.find("Iron Body"), std::string::npos);
  EXPECT_NE(rendered.find("Max Level: 20"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, ShowsTheDescription) {
  Skill skill = MakeIronBody();
  EXPECT_NE(RenderAt(skill, 5).find("Boosts DEF and Max HP."),
            std::string::npos);
}

// The title is the one place the panel says which kind of skill this is.
TEST_F(SkillInspectPanelTest, TitlesItselfPassiveOrActive) {
  Skill passive = MakeIronBody();
  EXPECT_NE(RenderAt(passive, 5).find("Passive"), std::string::npos);
  Skill active = MakeLuckySeven();
  EXPECT_NE(RenderAt(active, 5).find("Active"), std::string::npos);
}

// A skill the player casts but that does nothing modelled is still Active.
TEST_F(SkillInspectPanelTest, TitlesACastNonAttackActive) {
  Skill skill = MakeLuckySeven();
  skill.set_kind(SKILL_KIND_ACTIVE);
  EXPECT_NE(RenderAt(skill, 1).find("Active"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, ReadsEveryLeverAtTheLearnedLevel) {
  Skill skill = MakeIronBody();
  std::string rendered = RenderAt(skill, 5);
  EXPECT_NE(rendered.find("Level 5"), std::string::npos);
  EXPECT_NE(rendered.find("+50"), std::string::npos);    // DEF
  EXPECT_NE(rendered.find("+5%"), std::string::npos);    // Max HP
  EXPECT_NE(rendered.find("-2.5%"), std::string::npos);  // Damage Taken
}

TEST_F(SkillInspectPanelTest, ShowsWhatTheNextPointBuys) {
  Skill skill = MakeIronBody();
  std::string rendered = RenderAt(skill, 5);
  EXPECT_NE(rendered.find("Level 6"), std::string::npos);
  EXPECT_NE(rendered.find("+60"), std::string::npos);  // DEF one point on
}

// Nothing has been spent yet, so there is no current level to show -- only
// what the first point would buy.
TEST_F(SkillInspectPanelTest, AnUnlearnedSkillShowsOnlyTheNextLevel) {
  Skill skill = MakeIronBody();
  std::string rendered = RenderAt(skill, 0);
  EXPECT_EQ(rendered.find("Level 0"), std::string::npos);
  EXPECT_NE(rendered.find("Level 1"), std::string::npos);
  EXPECT_NE(rendered.find("+10"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, AMaxedSkillShowsNoNextLevel) {
  Skill skill = MakeIronBody();
  std::string rendered = RenderAt(skill, 20);
  EXPECT_NE(rendered.find("Level 20"), std::string::npos);
  EXPECT_EQ(rendered.find("Level 21"), std::string::npos);
}

// Damage is per strike, how many strikes, and what the two come to -- the
// total is what a player compares one attack skill against another with.
TEST_F(SkillInspectPanelTest, SpellsOutAMultiLineSwing) {
  Skill skill = MakeLuckySeven();
  EXPECT_NE(RenderAt(skill, 1).find("72% x3 = 216%"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, ASingleLineSwingIsJustItsPercentage) {
  Skill skill = MakeLuckySeven();
  skill.clear_lines();
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(rendered.find("72%"), std::string::npos);
  EXPECT_EQ(rendered.find("x1"), std::string::npos);
}

// Reach and the weapon it demands do not move with the level, so they sit
// above the level blocks rather than being repeated in both.
TEST_F(SkillInspectPanelTest, ShowsTheFactsThatHoldAtEveryLevel) {
  Skill skill = MakeLuckySeven();
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(rendered.find("Enemies Hit"), std::string::npos);
  EXPECT_NE(rendered.find("Requires"), std::string::npos);
  EXPECT_NE(rendered.find("Claw"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, SaysNothingAboutReachForASingleTargetSkill) {
  Skill skill = MakeIronBody();
  std::string rendered = RenderAt(skill, 5);
  EXPECT_EQ(rendered.find("Enemies Hit"), std::string::npos);
  EXPECT_EQ(rendered.find("Requires"), std::string::npos);
}

// base + per_level * (L - 1) lands a hair under the round figure at some
// levels: Iron Body's seven steps of +1% come to 6.999999999999999, and its
// damage reduction to 3.4999999999999996. Truncating would show "6.9%" and
// "-3.4%" for a skill whose data plainly says 7 and 3.5.
TEST_F(SkillInspectPanelTest, PercentagesRoundRatherThanTruncate) {
  Skill skill = MakeIronBody();
  std::string rendered = RenderAt(skill, 7);
  EXPECT_NE(rendered.find("+7%"), std::string::npos);
  EXPECT_NE(rendered.find("-3.5%"), std::string::npos);
  EXPECT_EQ(rendered.find("6.9"), std::string::npos);
  EXPECT_EQ(rendered.find("3.4"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, AttackSpeedCountsItsStages) {
  Skill skill = MakeIronBody();
  skill.mutable_base()->set_attack_speed(1);
  EXPECT_NE(RenderAt(skill, 1).find("+1 stage"), std::string::npos);
  EXPECT_EQ(RenderAt(skill, 1).find("+1 stages"), std::string::npos);
}

// Magic Guard's only lever is one nothing reads yet. It still has to say what
// the skill does, or its levels stand over an empty block.
TEST_F(SkillInspectPanelTest, ShowsLeversCombatDoesNotReadYet) {
  Skill skill;
  skill.set_name("Magic Guard");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_max_level(10);
  skill.mutable_base()->set_damage_to_mp_pct(0.22);
  skill.mutable_per_level()->set_damage_to_mp_pct(0.07);
  std::string rendered = RenderAt(skill, 2);
  EXPECT_NE(rendered.find("Damage to MP"), std::string::npos);
  EXPECT_NE(rendered.find("29%"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, SaysSoWhenALevelBuysNothingModelled) {
  Skill skill;
  skill.set_name("Double Jump");
  skill.set_kind(SKILL_KIND_ACTIVE);
  skill.set_max_level(10);
  EXPECT_NE(RenderAt(skill, 1).find("(no effect)"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, WrapsALongDescriptionOntoItsOwnLines) {
  Skill skill = MakeIronBody();
  skill.set_description(
      "Boosts DEF and Max HP by a set percentage, and decreases damage taken "
      "when hit by enemies.");
  std::string rendered = RenderAt(skill, 5);
  // Every word survives the wrap, and none of them run past the border.
  EXPECT_NE(rendered.find("Boosts DEF and Max HP by a"), std::string::npos);
  EXPECT_NE(rendered.find("enemies."), std::string::npos);
  EXPECT_EQ(rendered.find("percentage, and decreases damage taken when"),
            std::string::npos);
}

TEST_F(SkillInspectPanelTest, RendersAPlaceholderWithNoSkill) {
  SkillInspectPanel panel;
  panel.SetSkill(nullptr, 0);
  EXPECT_NE(RenderElement(panel.Render()).find("(no skill)"),
            std::string::npos);
}

}  // namespace
}  // namespace ms
