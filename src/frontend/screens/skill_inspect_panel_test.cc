#include "src/frontend/screens/skill_inspect_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_test_base.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

// Where a card row reads `label` and then `value`, whatever gap the card's
// columns put between them, or npos. A test says what a row says; how wide
// the label column came out is the card's business.
size_t RowIn(const std::string& rendered, const std::string& label,
             const std::string& value) {
  for (size_t at = rendered.find(label); at != std::string::npos;
       at = rendered.find(label, at + 1)) {
    size_t after = at + label.size();
    size_t gap = rendered.find_first_not_of(' ', after);
    if (gap != std::string::npos && gap > after &&
        rendered.compare(gap, value.size(), value) == 0) {
      return at;
    }
  }
  return std::string::npos;
}

class SkillInspectPanelTest : public PanelTest {
 protected:
  std::string RenderAt(const Skill& skill, int level, int bonus = 0) {
    SkillInspectPanel panel;
    panel.SetSkill(&skill, level, bonus);
    return RenderElement(panel.Render());
  }

  std::string RenderPreview(const Skill& skill) {
    SkillInspectPanel panel;
    panel.SetSkill(&skill, 0, 0, SkillInspectPanel::kPreview);
    return RenderElement(panel.Render());
  }

  // A rendered panel split into its rows, so a test can say what sits above
  // what rather than only what is somewhere on screen.
  static std::vector<std::string> Lines(const std::string& rendered) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= rendered.size()) {
      size_t end = rendered.find('\n', start);
      if (end == std::string::npos) {
        end = rendered.size();
      }
      lines.push_back(rendered.substr(start, end - start));
      start = end + 1;
    }
    return lines;
  }
};

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

TEST_F(SkillInspectPanelTest, ShowsTheNameMaxLevelAndDescription) {
  Skill skill = IronBody();
  std::string rendered = RenderAt(skill, 5);
  EXPECT_NE(rendered.find("Iron Body"), std::string::npos);
  EXPECT_NE(rendered.find("Max Level: 20"), std::string::npos);
  EXPECT_NE(rendered.find("Boosts DEF and Max HP."), std::string::npos);
}

// The title is the one place the panel says which kind of skill this is.
TEST_F(SkillInspectPanelTest, TitlesItselfPassiveOrActive) {
  Skill passive = IronBody();
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
  Skill skill = IronBody();
  // A pool granted outright, which reads as a plain number rather than as the
  // "per level" the pair beside it takes.
  skill.mutable_base()->set_max_hp(525);
  skill.mutable_per_level()->set_max_hp(25);
  std::string rendered = RenderAt(skill, 5);
  EXPECT_NE(rendered.find("Level 5"), std::string::npos);
  EXPECT_NE(rendered.find("+50"), std::string::npos);    // DEF
  EXPECT_NE(rendered.find("+5%"), std::string::npos);    // Max HP
  EXPECT_NE(rendered.find("-2.5%"), std::string::npos);  // Damage Taken
  EXPECT_NE(RowIn(rendered, "Max HP", "+625"), std::string::npos) << rendered;
}

// Decent Mystic Door's shape: a whole point every fifth level, written as a
// fifth of one per level. The page must read the rung the character is
// standing on, not the one below it.
TEST_F(SkillInspectPanelTest, AFractionalLadderReadsWhatItGrants) {
  Skill skill = IronBody();
  skill.clear_base();
  skill.clear_per_level();
  skill.mutable_base()->set_str(1);
  skill.mutable_per_level()->set_str(0.2);
  EXPECT_NE(RowIn(RenderAt(skill, 5), "STR", "+1"), std::string::npos);
  EXPECT_NE(RowIn(RenderAt(skill, 6), "STR", "+2"), std::string::npos);
}

// Reckless Hunt sells DEF for damage. The price is half the skill, so the page
// prints it as a loss rather than dropping the row for not being a gain.
TEST_F(SkillInspectPanelTest, ShowsALeverTheSkillTakesAway) {
  Skill skill = IronBody();
  skill.mutable_base()->set_def_pct(-0.07);
  skill.mutable_per_level()->set_def_pct(-0.02);
  EXPECT_NE(RowIn(RenderAt(skill, 3), "Defense", "-11%"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, ShowsWhatTheNextPointBuys) {
  Skill skill = IronBody();
  std::string rendered = RenderAt(skill, 5);
  EXPECT_NE(rendered.find("Level 6"), std::string::npos);
  EXPECT_NE(rendered.find("+60"), std::string::npos);  // DEF one point on
}

// Nothing has been spent yet, so there is no current level to show -- only
// what the first point would buy.
TEST_F(SkillInspectPanelTest, AnUnlearnedSkillShowsOnlyTheNextLevel) {
  Skill skill = IronBody();
  std::string rendered = RenderAt(skill, 0);
  EXPECT_EQ(rendered.find("Level 0"), std::string::npos);
  EXPECT_NE(rendered.find("Level 1"), std::string::npos);
  EXPECT_NE(rendered.find("+10"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, AMaxedSkillShowsNoNextLevel) {
  Skill skill = IronBody();
  std::string rendered = RenderAt(skill, 20);
  EXPECT_NE(rendered.find("Level 20"), std::string::npos);
  EXPECT_EQ(rendered.find("Level 21"), std::string::npos);
}

// The card is about what the skill is worth, so the levels a book lends are
// counted into both blocks: a 5 with two lent reads as a 7, and the point
// after it as an 8.
TEST_F(SkillInspectPanelTest, LentLevelsAreCountedIntoBothBlocks) {
  Skill skill = IronBody();
  std::string rendered = RenderAt(skill, 5, /*bonus=*/2);
  EXPECT_NE(rendered.find("Level 7"), std::string::npos);
  EXPECT_NE(rendered.find("+70"), std::string::npos);  // DEF at 7, not at 5
  EXPECT_NE(rendered.find("Level 8"), std::string::npos);
  EXPECT_EQ(rendered.find("Level 5"), std::string::npos);
}

// A skill nobody has opened is lent nothing, so there is still no current
// level to show -- but the first point in it arrives lent, and the block for
// what that point buys says the level it would really be worth.
TEST_F(SkillInspectPanelTest, AnUnlearnedSkillIsLentNothingUntilItIsOpened) {
  Skill skill = IronBody();
  std::string rendered = RenderAt(skill, 0, /*bonus=*/2);
  EXPECT_EQ(rendered.find("Level 0"), std::string::npos);
  EXPECT_EQ(rendered.find("Level 1"), std::string::npos);
  EXPECT_NE(rendered.find("Level 3"), std::string::npos);
  EXPECT_NE(rendered.find("+30"), std::string::npos);  // DEF at 3, not at 1
}

// Two things the master level does to a lent one. A skill marked for the 4th
// job takes two levels past it, and both blocks say so; one not marked stops
// there, and the point that buys nothing new gets no block of its own.
TEST_F(SkillInspectPanelTest, LentLevelsStopWhereTheSkillDoes) {
  Skill plain = IronBody();
  std::string held = RenderAt(plain, 19, /*bonus=*/2);
  EXPECT_NE(held.find("Level 20"), std::string::npos);
  EXPECT_EQ(held.find("Level 21"), std::string::npos)
      << "the next point buys a level the lent ones already reached";

  Skill marked = IronBody();
  marked.set_exceeds_master_level(true);
  std::string past = RenderAt(marked, 19, /*bonus=*/2);
  EXPECT_NE(past.find("Level 21"), std::string::npos);
  EXPECT_NE(past.find("Level 22"), std::string::npos);
  EXPECT_NE(past.find("Max Level: 20"), std::string::npos)
      << "and the maximum it is past is still the maximum";

  // The two levels past the master level are lent, never bought, so a maxed
  // skill has nothing left to spend on however far it is allowed to reach.
  std::string bought_out = RenderAt(marked, 20, /*bonus=*/0);
  EXPECT_NE(bought_out.find("Level 20"), std::string::npos);
  EXPECT_EQ(bought_out.find("Level 21"), std::string::npos);
}

// A player choosing a job has no points spent and none to spend, so "one more
// point" says nothing. The two ends of the skill are what there is to compare.
TEST_F(SkillInspectPanelTest, APreviewShowsTheFirstLevelAndTheLast) {
  Skill skill = IronBody();
  std::string rendered = RenderPreview(skill);
  EXPECT_NE(rendered.find("Level 1"), std::string::npos);
  EXPECT_NE(rendered.find("+10"), std::string::npos);  // DEF at level 1
  EXPECT_NE(rendered.find("Level 20"), std::string::npos);
  EXPECT_NE(rendered.find("+200"), std::string::npos);  // DEF at level 20
  EXPECT_EQ(rendered.find("Level 2 "), std::string::npos);
}

// The learned level is not read at all under a preview: the card is about the
// skill, and nothing has been spent on it.
TEST_F(SkillInspectPanelTest, APreviewIgnoresWhatIsLearned) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 7, 0, SkillInspectPanel::kPreview);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_EQ(rendered.find("Level 7"), std::string::npos);
  EXPECT_EQ(rendered.find("Level 8"), std::string::npos);
  EXPECT_NE(rendered.find("Level 1"), std::string::npos);
}

// One level is both ends of it, so the block is not drawn twice.
TEST_F(SkillInspectPanelTest, APreviewOfAOneLevelSkillShowsOneBlock) {
  Skill skill = IronBody();
  skill.set_max_level(1);
  std::vector<std::string> lines = Lines(RenderPreview(skill));
  int blocks = 0;
  for (const std::string& line : lines) {
    if (line.find("Level 1") != std::string::npos) {
      ++blocks;
    }
  }
  EXPECT_EQ(blocks, 1);
}

// Damage is per strike, how many strikes, and what the two come to -- the
// total is what a player compares one attack skill against another with.
TEST_F(SkillInspectPanelTest, SpellsOutAMultiLineSwing) {
  Skill skill = MakeLuckySeven();
  EXPECT_NE(RenderAt(skill, 1).find("72% x3 = 216%"), std::string::npos);
}

// Shuriken Burst opens on one enemy for far more than the spread that
// follows, and a page that showed only the spread would read as a weak skill.
TEST_F(SkillInspectPanelTest, SpellsOutTheOpeningHit) {
  Skill skill = MakeLuckySeven();
  skill.mutable_base()->set_lead_pct(4.08);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(rendered.find("Opening Hit"), std::string::npos);
  EXPECT_NE(rendered.find("408%"), std::string::npos);
  EXPECT_NE(rendered.find("one enemy"), std::string::npos)
      << "or it reads as an alternative to the swing's own damage";
}

TEST_F(SkillInspectPanelTest, AMultiStrikeOpeningHitTotalsItself) {
  Skill skill = MakeLuckySeven();
  skill.mutable_base()->set_lead_pct(1.0);
  skill.set_lead_lines(3);
  EXPECT_NE(RenderAt(skill, 1).find("100% x3 = 300%"), std::string::npos);
  // Piercing Arrow II's fragment bounces onto two of the enemies the arrow
  // went through, so the row that says which enemy has to count them.
  skill.set_lead_enemies(2);
  EXPECT_NE(RenderAt(skill, 1).find("(2 enemies)"), std::string::npos);
}

// A clock the weapon can hurry is not one the page can state: an ordinary
// swing's delay is scaled by the speed stage of whatever is in hand, so a
// figure here would be wrong for half the weapons that can swing it. What one
// hit is worth to a counter is bookkeeping either way.
TEST_F(SkillInspectPanelTest, KeepsWhatTheWeaponMovesOffThePage) {
  Skill skill = MakeLuckySeven();
  skill.set_base_delay_ms(660);
  skill.set_hits_per_attack_count(7);
  AutoMode* mode = skill.add_auto_mode();
  mode->set_label("Turret");
  mode->set_cast_interval_seconds(0.21);
  mode->set_max_enemies(4);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_EQ(rendered.find("0.66"), std::string::npos);
  EXPECT_EQ(rendered.find("Counts As"), std::string::npos);
  // The turret's own rate is stated: nothing the player holds moves it.
  EXPECT_NE(RowIn(rendered, "Turret", "4 enemies every 0.21s"),
            std::string::npos);
}

// The two clocks the player sets themselves -- what they swing and what they
// leave dead -- and the only ones the page states.
TEST_F(SkillInspectPanelTest, SaysWhatCountSetsASkillOff) {
  Skill mirage = MakeLuckySeven();
  mirage.set_kind(SKILL_KIND_AUTO_ATTACK);
  mirage.set_attacks_per_cast(4);
  EXPECT_NE(RowIn(RenderAt(mirage, 1), "Fires Every", "4 Attacks"),
            std::string::npos);

  Skill fountain = MakeLuckySeven();
  fountain.set_kind(SKILL_KIND_AUTO_ATTACK);
  fountain.set_kills_per_cast(12);
  EXPECT_NE(RowIn(RenderAt(fountain, 1), "Fires Every", "12 Defeats"),
            std::string::npos);
}

// A passive that reaches across to one other skill says which, since nothing
// on that skill's own page could tell the player where the damage came from.
TEST_F(SkillInspectPanelTest, ABoostNamesTheSkillItReachesAcrossTo) {
  Skill mirage = IronBody();
  SkillBoost* boost = mirage.add_boost();
  boost->set_skill_name("Wind Arrow");
  boost->mutable_effect()->set_skill_pct(0.70);
  EXPECT_NE(
      RowIn(RenderAt(mirage, 1), "Boosts Wind Arrow", "+70% Damage per Strike"),
      std::string::npos);
  // Every lever the boost carries is named, since one row holds them all --
  // and the two damages are told apart, points on the skill's multiplier
  // reading per strike where a share of the character's damage reads plain.
  boost->mutable_effect()->set_ied_pct(0.20);
  boost->mutable_effect()->set_damage_pct(0.20);
  EXPECT_NE(RenderAt(mirage, 1).find(
                "+70% Damage per Strike, +20% Damage, +20% Ignore DEF"),
            std::string::npos);
  // Half a bargain writes no row: a name with nothing behind it.
  Skill bare = IronBody();
  bare.add_boost()->set_skill_name("Wind Arrow");
  EXPECT_EQ(RenderAt(bare, 1).find("Boosts Wind Arrow"), std::string::npos);
}

// One skill with two ways of hurting things: the swing the player holds the
// key for, and the turret it leaves behind. Both halves belong on the one page,
// or the player buys twenty levels of a skill and sees half of what they got.
TEST_F(SkillInspectPanelTest, AnAutoModeStatesItsOwnHalfOfTheSkill) {
  Skill blaster = MakeLuckySeven();
  blaster.set_max_enemies(4);
  blaster.set_lines(1);
  blaster.mutable_base()->set_skill_pct(1.24);
  AutoMode* turret = blaster.add_auto_mode();
  turret->set_label("Turret");
  turret->set_cast_interval_seconds(0.21);
  turret->set_max_enemies(4);
  turret->mutable_base()->set_skill_pct(0.66);

  std::string rendered = RenderAt(blaster, 1);
  EXPECT_NE(RowIn(rendered, "Damage", "124%"), std::string::npos);
  // Its damage sits under the swing's, named as its reach row above names it.
  EXPECT_NE(RowIn(rendered, "Turret", "66%"), std::string::npos);
  // A skill without one says nothing about a turret.
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Turret"), std::string::npos);
}

// A summon's whole worth is how hard it hits and how often, and the second of
// those used to be nowhere on the page. It rides the row that already states
// the skill's reach, so it costs no row of its own.
TEST_F(SkillInspectPanelTest, ASkillOnItsOwnClockStatesItWithItsReach) {
  Skill phoenix = MakeLuckySeven();
  phoenix.set_kind(SKILL_KIND_AUTO_ATTACK);
  phoenix.set_max_enemies(4);
  phoenix.set_cast_interval_seconds(1.71);
  std::string rendered = RenderAt(phoenix, 1);
  EXPECT_NE(RowIn(rendered, "Attacks", "4 enemies every 1.71s"),
            std::string::npos);
  EXPECT_EQ(rendered.find("Enemies Hit"), std::string::npos);

  // A key-down skill fires at a rate the weapon cannot hurry, so that rate is
  // knowable here too. Arrow Blaster is the only one.
  Skill blaster = MakeLuckySeven();
  blaster.set_max_enemies(4);
  blaster.set_base_delay_ms(120);
  blaster.set_fixed_delay(true);
  EXPECT_NE(RowIn(RenderAt(blaster, 1), "Attacks", "4 enemies every 0.12s"),
            std::string::npos);
  // An ordinary swing's delay moves with the weapon, so the page states none.
  Skill swing = MakeLuckySeven();
  swing.set_base_delay_ms(660);
  EXPECT_EQ(RenderAt(swing, 1).find("every 0.66s"), std::string::npos);

  // One enemy is one enemy, and a skill that swings when the player does keeps
  // the plain reach row.
  Skill sphere = phoenix;
  sphere.set_max_enemies(1);
  sphere.set_cast_interval_seconds(2.0);
  EXPECT_NE(RowIn(RenderAt(sphere, 1), "Attacks", "1 enemy every 2s"),
            std::string::npos);
  EXPECT_NE(RowIn(RenderAt(MakeLuckySeven(), 1), "Enemies Hit", "5"),
            std::string::npos);
}

// Divine Mark lands two hits at once and GMS prices them differently, so the
// page prices them differently too: a row each, and the bonus against ordinary
// monsters under the half that carries it rather than over both.
TEST_F(SkillInspectPanelTest, StatesEachHitOfASwingThatLandsTwo) {
  Skill mark = MakeLuckySeven();
  mark.set_lines(7);
  mark.mutable_base()->set_skill_pct(4.20);
  mark.clear_per_level();
  SwingHit* blast = mark.add_extra_hit();
  blast->set_label("Explosion");
  blast->set_lines(5);
  blast->mutable_base()->set_skill_pct(2.90);
  blast->mutable_base()->set_normal_skill_pct(0.87);

  std::string rendered = RenderAt(mark, 1);
  EXPECT_NE(RowIn(rendered, "Damage", "420% x7 = 2940%"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Explosion", "290% x5 = 1450%"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Explosion Normal", "377% x5 = 1885%"),
            std::string::npos);
  // The hammer carries no such bonus, so it gets no such row.
  EXPECT_EQ(rendered.find("Normal Monsters"), std::string::npos);

  // A hit certain to crit says so on the same row, since the two halves of
  // Raging Blow are otherwise the same number printed twice.
  mark.mutable_extra_hit(0)->mutable_base()->set_crit_rate(1.00);
  EXPECT_NE(RowIn(RenderAt(mark, 1), "Explosion", "290% x5 = 1450% (crit)"),
            std::string::npos);
  // A rate short of certainty prints the rate beside the damage rather than
  // instead of it.
  mark.mutable_extra_hit(0)->mutable_base()->set_crit_rate(0.20);
  EXPECT_NE(RowIn(RenderAt(mark, 1), "Explosion", "290% x5 = 1450% (20% crit)"),
            std::string::npos);
}

// What a skill hands another that is not damage reads as the same sentence the
// damage boost does, one row per skill named -- two skills granted different
// things cannot share a row.
TEST_F(SkillInspectPanelTest, StatesTheStrikesAndReachItHandsAnotherSkill) {
  Skill vessel = IronBody();
  vessel.set_max_level(10);
  SkillBoost* charge = vessel.add_boost();
  charge->set_skill_name("Divine Charge");
  charge->set_lines(1);
  charge->set_max_enemies(1);
  charge->set_max_enemies_per_level(0.2);
  SkillBoost* blast = vessel.add_boost();
  blast->set_skill_name("Blast");
  blast->set_lines(1);

  std::string rendered = RenderAt(vessel, 10);
  EXPECT_NE(RowIn(rendered, "Boosts Divine Charge", "+1 Strike, +2 Enemies"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Boosts Blast", "+1 Strike"), std::string::npos);
  // The reach climbs with the level; the strike does not.
  EXPECT_NE(RenderAt(vessel, 1).find("+1 Strike, +1 Enemy"), std::string::npos);
  // A skill granting neither writes no row.
  EXPECT_EQ(RenderAt(IronBody(), 1).find("Boosts Divine Charge"),
            std::string::npos);

  // A clock handed over reads as the clock it becomes, not as a change to one.
  Skill second = IronBody();
  second.set_max_level(20);
  SkillBoost* mirage = second.add_boost();
  mirage->set_skill_name("Speed Mirage");
  mirage->set_lines(12);
  mirage->set_attacks_per_cast(7);
  std::string clocked = RenderAt(second, 1);
  EXPECT_NE(
      RowIn(clocked, "Boosts Speed Mirage", "+12 Strikes, every 7 attacks"),
      std::string::npos);

  // A share off a wait reads as the share, the seconds it comes to being on
  // the named skill's own page.
  Skill cutter = IronBody();
  SkillBoost* hammer = cutter.add_boost();
  hammer->set_skill_name("Heaven's Hammer");
  hammer->set_cooldown_pct(0.30);
  EXPECT_NE(
      RowIn(RenderAt(cutter, 1), "Boosts Heaven's Hammer", "-30% Cooldown"),
      std::string::npos);

  // Points on the mark a skill leaves read as the tick they lift, so the row
  // is never mistaken for the strike that lays it.
  Skill eruption = IronBody();
  eruption.set_max_level(20);
  SkillBoost* fog = eruption.add_boost();
  fog->set_skill_name("Poison Mist");
  fog->set_dot_skill_pct(0.315);
  fog->set_dot_skill_pct_per_level(0.015);
  EXPECT_NE(RowIn(RenderAt(eruption, 20), "Boosts Poison Mist",
                  "+60% DoT Damage per Tick"),
            std::string::npos);

  // The burn's clock reads in seconds, which is the only way it could: GMS
  // states them, and the ladder they land on is the named skill's own.
  Skill aftermath = IronBody();
  SkillBoost* longer = aftermath.add_boost();
  longer->set_skill_name("Poison Mist");
  longer->set_dot_duration_seconds(6.0);
  EXPECT_NE(
      RowIn(RenderAt(aftermath, 1), "Boosts Poison Mist", "+6s DoT Duration"),
      std::string::npos);

  // A lever handed over backwards keeps its sign. Hurricane - Split Attack
  // buys a second arrow with a quarter off what each one lands, and a row
  // that dropped the cut would sell it as a free strike.
  Skill split = IronBody();
  SkillBoost* hurricane = split.add_boost();
  hurricane->set_skill_name("Hurricane");
  hurricane->set_lines(1);
  hurricane->mutable_effect()->set_final_dmg_pct(-0.25);
  EXPECT_NE(RowIn(RenderAt(split, 1), "Boosts Hurricane",
                  "+1 Strike, -25% Final Damage"),
            std::string::npos);

  // A strike for the hits a swing lands beside itself is a different strike
  // from one for its lines, so the row says which it bought.
  Skill fragment = IronBody();
  SkillBoost* arrow = fragment.add_boost();
  arrow->set_skill_name("Piercing Arrow II");
  arrow->set_lines(1);
  arrow->set_extra_hit_lines(1);
  EXPECT_NE(RowIn(RenderAt(fragment, 1), "Boosts Piercing Arrow II",
                  "+1 Strike, +1 Strike to its extra hits"),
            std::string::npos);
}

// A wait that never moves is stated once above the divider; one that shortens
// as the skill is taught is part of what a point buys, so it reads at the
// level with everything else a point buys.
TEST_F(SkillInspectPanelTest, AShorteningWaitIsReadAtTheLevel) {
  Skill hammer = MakeLuckySeven();
  hammer.set_max_level(30);
  hammer.set_cooldown_seconds(29.5);
  hammer.set_cooldown_seconds_per_level(-0.5);
  EXPECT_NE(RowIn(RenderAt(hammer, 1), "Cooldown", "29.5s"), std::string::npos);
  EXPECT_NE(RowIn(RenderAt(hammer, 30), "Cooldown", "15s"), std::string::npos);

  // Above the divider it would have to state one wait for all thirty levels,
  // so the row up there belongs to the skills whose wait really is invariant.
  std::string rendered = RenderAt(hammer, 30);
  EXPECT_LT(rendered.find("Level 30"), rendered.find("Cooldown"));

  Skill flat = MakeLuckySeven();
  flat.set_cooldown_seconds(7.0);
  std::string plain = RenderAt(flat, 5);
  EXPECT_LT(plain.find("Cooldown"), plain.find("Level 5"));
}

// A growing ring of Combo Orbs takes the same split as the shortening wait
// above, and for the same reason: one number cannot stand for twenty levels.
TEST_F(SkillInspectPanelTest, AGrowingRingOfOrbsIsReadAtTheLevel) {
  Skill advanced = IronBody();
  advanced.set_combo_orbs(5);
  advanced.set_combo_orbs_per_level(0.26316);
  EXPECT_NE(RowIn(RenderAt(advanced, 1), "Combo Orbs", "5"), std::string::npos);
  std::string rendered = RenderAt(advanced, 20);
  EXPECT_NE(RowIn(rendered, "Combo Orbs", "10"), std::string::npos);
  EXPECT_LT(rendered.find("Level 20"), rendered.find("Combo Orbs"));

  Skill flat = IronBody();
  flat.set_combo_orbs(5);
  std::string plain = RenderAt(flat, 5);
  EXPECT_NE(RowIn(plain, "Combo Orbs", "5"), std::string::npos);
  EXPECT_LT(plain.find("Combo Orbs"), plain.find("Level 5"));
}

// Revenge of the Evil Eye: three attacks out of one row in the book, and the
// auras land twenty strikes on three enemies where the volley beside them
// reaches ten. Without a reach row for each half the biggest number on the page
// is the one that lands on the fewest enemies, and nothing says so.
TEST_F(SkillInspectPanelTest, EachHalfStatesTheReachItHasRatherThanTheSkills) {
  Skill revenge = MakeLuckySeven();
  revenge.set_kind(SKILL_KIND_AUTO_ATTACK);
  revenge.set_max_enemies(10);
  revenge.set_cast_interval_seconds(5.0);
  AutoMode* volley = revenge.add_auto_mode();
  volley->set_label("Shock III");
  volley->set_cast_interval_seconds(10.0);
  volley->set_max_enemies(10);
  volley->set_lines(7);
  volley->mutable_base()->set_skill_pct(3.40);
  AutoMode* auras = revenge.add_auto_mode();
  auras->set_label("Dark Auras");
  auras->set_cast_interval_seconds(10.0);
  auras->set_max_enemies(3);
  auras->set_lines(20);
  auras->mutable_base()->set_skill_pct(2.20);

  std::string rendered = RenderAt(revenge, 1);
  EXPECT_NE(RowIn(rendered, "Attacks", "10 enemies every 5s"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Shock III", "10 enemies every 10s"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Dark Auras", "3 enemies every 10s"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Dark Auras", "220% x20 = 4400%"),
            std::string::npos);
}

// An arrow that gains as it travels states the gain beside the reach: the
// reach is how far it compounds, so the two are one fact.
TEST_F(SkillInspectPanelTest, APiercingSwingStatesItsGainBesideItsReach) {
  Skill arrow;
  arrow.set_name("Piercing Arrow");
  arrow.set_kind(SKILL_KIND_ATTACK);
  arrow.set_job_advancement(JOB_ADVANCEMENT_CROSSBOWMAN);
  arrow.set_max_level(20);
  arrow.set_max_enemies(6);
  arrow.set_lines(4);
  arrow.set_pierce_gain_pct(0.15);
  arrow.mutable_base()->set_skill_pct(0.92);
  EXPECT_NE(RowIn(RenderAt(arrow, 1), "Enemies Hit", "6, +15% each"),
            std::string::npos);

  // A swing that hits everything it reaches alike says only how many.
  arrow.clear_pierce_gain_pct();
  std::string plain = RenderAt(arrow, 1);
  EXPECT_NE(RowIn(plain, "Enemies Hit", "6"), std::string::npos);
  EXPECT_EQ(plain.find("each"), std::string::npos);
}

// Empowered Arrows strengthens Piercing Arrow twice over: a permanent bonus on
// every shot, and a bigger shot every fourth. Both halves belong on the page,
// and the upgraded swing's reach with them -- it is wider than the one it
// stands in for, which no other row could tell the player.
TEST_F(SkillInspectPanelTest, StatesBothHalvesOfAnEmpoweredSwing) {
  Skill arrows = IronBody();
  SkillBoost* boost = arrows.add_boost();
  boost->set_skill_name("Piercing Arrow");
  boost->mutable_effect()->set_skill_pct(1.02);
  EmpoweredForm* form = arrows.add_empowered_form();
  form->set_skill_name("Piercing Arrow");
  form->set_casts_per_trigger(4);
  form->set_max_enemies(8);
  form->set_lines(6);
  form->mutable_base()->set_skill_pct(2.03);

  std::string rendered = RenderAt(arrows, 1);
  EXPECT_NE(rendered.find("Every 4th"), std::string::npos);
  EXPECT_NE(rendered.find("Piercing Arrow"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Empowered Enemies", "8"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Empowered Damage", "203% x6 = 1218%"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Boosts Piercing Arrow", "+102% Damage per Strike"),
            std::string::npos);
  // Counted on the swing, which is the ordinary reading and needs no row.
  EXPECT_EQ(rendered.find("Marks"), std::string::npos);
  // A skill that upgrades nothing says nothing about upgrading.
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Empower"), std::string::npos);
}

// Two forms off one ladder cannot both read "Empowered Damage", so each takes
// the name of the swing it upgrades -- and each states what it lands beside
// itself under its own row.
TEST_F(SkillInspectPanelTest, NamesEachOfTwoEmpoweredSwings) {
  Skill greater = IronBody();
  EmpoweredForm* arrow = greater.add_empowered_form();
  arrow->set_skill_name("Piercing Arrow II");
  arrow->set_casts_per_trigger(4);
  arrow->set_max_enemies(10);
  arrow->set_lines(6);
  arrow->mutable_base()->set_skill_pct(4.27);
  SwingHit* blast = arrow->add_extra_hit();
  blast->set_label("Fragment");
  blast->set_lines(10);
  blast->mutable_base()->set_skill_pct(2.80);
  EmpoweredForm* shot = greater.add_empowered_form();
  shot->set_skill_name("Snipe");
  shot->set_casts_per_trigger(4);
  shot->set_max_enemies(1);
  shot->set_lines(10);
  shot->mutable_base()->set_skill_pct(4.94);

  std::string rendered = RenderAt(greater, 1);
  // One "Empowers" row per form, each naming its own swing.
  EXPECT_NE(rendered.find("Every 4th Piercing"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Every 4th Snipe"), std::string::npos) << rendered;
  // Named by what each upgrades, not by the one label they would share.
  EXPECT_NE(RowIn(rendered, "Piercing Arrow II", "427% x6"), std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Snipe", "494% x10"), std::string::npos)
      << rendered;
  EXPECT_EQ(rendered.find("Empowered Damage"), std::string::npos) << rendered;
  // The form's own second hit reads between the two swings: under the one it
  // belongs to, and above the one it does not.
  EXPECT_LT(RowIn(rendered, "Piercing Arrow II", "427%"),
            RowIn(rendered, "Fragment", "280%"))
      << rendered;
  EXPECT_LT(RowIn(rendered, "Fragment", "280%"),
            RowIn(rendered, "Snipe", "494%"))
      << rendered;
}

// Divine Judgment counts the marks one enemy has taken rather than the swings
// landed, and its reach is whichever of them came due -- so the page says the
// first and drops the second.
TEST_F(SkillInspectPanelTest, StatesAFormThatMarksEachEnemy) {
  Skill judgment = IronBody();
  EmpoweredForm* form = judgment.add_empowered_form();
  form->set_skill_name("Blast");
  form->set_casts_per_trigger(5);
  form->set_brands_each_enemy(true);
  form->set_max_enemies(8);
  form->set_lines(10);
  form->mutable_base()->set_skill_pct(4.34);

  std::string rendered = RenderAt(judgment, 1);
  EXPECT_NE(rendered.find("Every 5th"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Marks", "Each Enemy Hit"), std::string::npos);
  EXPECT_EQ(rendered.find("Empowered Enemies"), std::string::npos)
      << "a form that marks enemies carries no reach of its own";
}

// Holy Fountain: the pulse and the wait between pulses both move with the
// level, so a page showing one of them says nothing about what a point bought.
TEST_F(SkillInspectPanelTest, StatesBothHalvesOfAFountain) {
  Skill fountain = IronBody();
  fountain.mutable_base()->set_regen_pct(0.13);
  fountain.mutable_per_level()->set_regen_pct(0.03);
  fountain.mutable_base()->set_regen_interval_seconds(7.5);
  fountain.mutable_per_level()->set_regen_interval_seconds(-0.5);

  // Both halves on one row: the pulse grows and the wait shortens together,
  // so either alone understates every point after the first.
  EXPECT_NE(RowIn(RenderAt(fountain, 1), "HP Recovered", "13% every 7.5s"),
            std::string::npos);
  EXPECT_NE(RowIn(RenderAt(fountain, 10), "HP Recovered", "40% every 3s"),
            std::string::npos);
  // A skill with no fountain says nothing about one.
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("HP Recovered"),
            std::string::npos);

  // Holy Water pours one more helping per step of INT, which is most of what
  // the points buy -- so the row says so, wrapping rather than dropping it.
  Skill water = IronBody();
  water.mutable_base()->set_regen_pct(0.005);
  water.mutable_per_level()->set_regen_pct(0.005);
  water.mutable_base()->set_regen_interval_seconds(10.0);
  water.mutable_base()->set_regen_int_step(2500);
  std::string rendered = RenderAt(water, 10);
  EXPECT_NE(RowIn(rendered, "HP Recovered", "5% every 10s, +5%"),
            std::string::npos)
      << rendered;
  EXPECT_NE(rendered.find("per 2500 INT"), std::string::npos) << rendered;
}

// Holy Symbol's whole effect. It buys no part of a fight, so it would read as
// a skill granting nothing at all if the page had no row for it.
TEST_F(SkillInspectPanelTest, StatesTheExpASkillAdds) {
  Skill symbol = IronBody();
  symbol.clear_base();
  symbol.clear_per_level();
  symbol.mutable_base()->set_exp_pct(0.215);
  symbol.mutable_per_level()->set_exp_pct(0.015);

  EXPECT_NE(RowIn(RenderAt(symbol, 1), "Additional EXP", "+21.5%"),
            std::string::npos);
  EXPECT_NE(RowIn(RenderAt(symbol, 20), "Additional EXP", "+50%"),
            std::string::npos);
  EXPECT_EQ(RenderAt(symbol, 1).find("no effect"), std::string::npos);
}

// Shadow Partner's whole effect. A share of each hit rather than a flat
// percentage, and the row has to say so -- 70% behind a 210% line is 147%.
TEST_F(SkillInspectPanelTest, StatesWhatAShadowLineIsWorth) {
  Skill partner = IronBody();
  partner.clear_base();
  partner.clear_per_level();
  partner.mutable_base()->set_mirror_line_pct(0.51);
  partner.mutable_per_level()->set_mirror_line_pct(0.01);

  EXPECT_NE(RowIn(RenderAt(partner, 1), "Shadow Damage", "51% of each hit"),
            std::string::npos);
  EXPECT_NE(RowIn(RenderAt(partner, 20), "Shadow Damage", "70% of each hit"),
            std::string::npos);
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Shadow Damage"),
            std::string::npos);
}

// A thrown meso reads like every other swing on the page: what one line does,
// times how many a meso is worth. Pick Pocket's chance is its own row, because
// it is a chance rather than a gain.
TEST_F(SkillInspectPanelTest, StatesWhatAMesoIsWorthAndHowOftenOneFalls) {
  Skill explosion = IronBody();
  explosion.clear_base();
  explosion.clear_per_level();
  explosion.set_lines(2);
  explosion.mutable_base()->set_meso_hit_pct(0.43);
  explosion.mutable_per_level()->set_meso_hit_pct(0.03);

  EXPECT_NE(RowIn(RenderAt(explosion, 1), "Damage per Meso", "43% x2 = 86%"),
            std::string::npos);
  EXPECT_NE(RowIn(RenderAt(explosion, 20), "Damage per Meso", "100% x2 = 200%"),
            std::string::npos);

  Skill pocket = IronBody();
  pocket.clear_base();
  pocket.clear_per_level();
  pocket.mutable_base()->set_meso_drop_chance(0.12);
  pocket.mutable_per_level()->set_meso_drop_chance(0.02);
  EXPECT_NE(RowIn(RenderAt(pocket, 10), "Meso Drop Chance", "30%"),
            std::string::npos);
  EXPECT_EQ(RenderAt(pocket, 10).find("Damage per Meso"), std::string::npos);
}

// Dispel keeps a real promise that nothing in the game can call on yet. The
// page says what it does rather than calling it empty, and says the same thing
// at every level, because that is what a point buys: nothing more.
TEST_F(SkillInspectPanelTest, StatesWhatDispelCures) {
  Skill dispel = IronBody();
  dispel.clear_base();
  dispel.clear_per_level();
  dispel.mutable_base()->set_cures_conditions(true);

  std::string rendered = RenderAt(dispel, 1);
  EXPECT_NE(RowIn(rendered, "Cures", "All Conditions"), std::string::npos);
  EXPECT_EQ(rendered.find("no effect"), std::string::npos);
  EXPECT_NE(RowIn(RenderAt(dispel, 10), "Cures", "All Conditions"),
            std::string::npos);
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Cures"), std::string::npos);
}

// The barrier's two halves: a share off the monster's attack that climbs with
// the level, and the switch a second skill throws to reach bosses with it.
TEST_F(SkillInspectPanelTest, StatesTheBarrierAndWhoWalksIntoIt) {
  Skill curse = IronBody();
  curse.clear_base();
  curse.clear_per_level();
  curse.mutable_base()->set_enemy_attack_pct(0.11);
  curse.mutable_per_level()->set_enemy_attack_pct(0.01);

  EXPECT_NE(RowIn(RenderAt(curse, 1), "Enemy ATT", "-11%"), std::string::npos);
  EXPECT_NE(RowIn(RenderAt(curse, 20), "Enemy ATT", "-30%"), std::string::npos);

  Skill rush = IronBody();
  rush.clear_base();
  rush.clear_per_level();
  rush.mutable_base()->set_enemy_attack_reaches_boss(true);
  std::string rendered = RenderAt(rush, 1);
  EXPECT_NE(RowIn(rendered, "Enemy ATT", "Also Reduced on Bosses"),
            std::string::npos);
  EXPECT_EQ(rendered.find("no effect"), std::string::npos);
}

// Creeping Toxin upgrades its own attack, so there is no name to print -- and
// its form carries a normal-monster reading of its own beside the damage.
TEST_F(SkillInspectPanelTest, StatesAFormThatUpgradesItsOwnSkill) {
  Skill toxin = IronBody();
  EmpoweredForm* form = toxin.add_empowered_form();
  form->set_casts_per_trigger(4);
  form->set_lines(4);
  form->mutable_base()->set_skill_pct(2.00);
  form->mutable_base()->set_normal_skill_pct(0.50);

  toxin.set_max_enemies(10);

  std::string rendered = RenderAt(toxin, 1);
  EXPECT_NE(rendered.find("Every 4th attack"), std::string::npos);
  // A form that states no reach of its own goes as far as the attack it stands
  // in for, so a row saying so twice is noise.
  EXPECT_EQ(rendered.find("Empowered Enemies"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Empowered Damage", "200% x4 = 800%"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Empowered Normal", "250% x4 = 1000%"),
            std::string::npos);
}

// Beam Blade's bonus against normal monsters adds to the swing per LINE, so
// the row states the whole swing. Stated as the bonus alone it would read as
// 216 + 72 against a swing that actually lands 432.
TEST_F(SkillInspectPanelTest, TheNormalMonsterRowStatesTheWholeSwing) {
  Skill skill = MakeLuckySeven();
  skill.mutable_base()->set_normal_skill_pct(0.72);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(rendered.find("Normal Monsters"), std::string::npos);
  EXPECT_NE(rendered.find("144% x3 = 432%"), std::string::npos);
  EXPECT_EQ(rendered.find("+72%"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, NoNormalMonsterRowWithoutTheBonus) {
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Normal Monsters"),
            std::string::npos);
}

// Both healing levers are a share of the HP pool, not of anything the row
// sits beside, and both say so.
TEST_F(SkillInspectPanelTest, TheHealingRowsNameWhatTheyAreAShareOf) {
  Skill skill = IronBody();
  skill.mutable_base()->set_hp_recover_pct(0.001);
  skill.mutable_base()->set_heal_pct(0.23);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(RowIn(rendered, "Heal per Attack", "+0.1% HP"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Heal", "+23% HP"), std::string::npos);
}

// A skill that states the whole of an earlier one says which, or the two read
// as though they stack.
TEST_F(SkillInspectPanelTest, ASupersedingSkillNamesWhatItReplaces) {
  Skill skill = IronBody();
  EXPECT_EQ(RenderAt(skill, 1).find("Replaces"), std::string::npos);
  skill.set_supersedes_skill_name("Vessel of Light");
  EXPECT_NE(RowIn(RenderAt(skill, 1), "Replaces", "Vessel of Light"),
            std::string::npos);
}

// A lever below a tenth of a percent gets a second decimal rather than a row
// reading "0%": Mortal Blow puts back a hundredth of a percent a swing at its
// first level, and the point still bought something.
TEST_F(SkillInspectPanelTest, ATinyLeverKeepsASecondDecimal) {
  Skill skill = IronBody();
  skill.mutable_base()->set_hp_recover_pct(0.0001);
  EXPECT_NE(RowIn(RenderAt(skill, 1), "Heal per Attack", "+0.01% HP"),
            std::string::npos);
}

// Every other skill in the game has none, so the row must not appear at all.
TEST_F(SkillInspectPanelTest, NoOpeningHitRowWithoutOne) {
  Skill skill = MakeLuckySeven();
  EXPECT_EQ(RenderAt(skill, 1).find("Opening"), std::string::npos);
}

// A skill whose strike count climbs has to say so where the damage is read,
// or the page states a swing the fight does not land.
TEST_F(SkillInspectPanelTest, TheStrikeCountClimbsWithTheLevel) {
  Skill skill = MakeLuckySeven();
  skill.set_lines_per_level(0.5);
  EXPECT_NE(RenderAt(skill, 1).find("72% x3 = 216%"), std::string::npos);
  EXPECT_NE(RenderAt(skill, 3).find("76% x4 = 304%"), std::string::npos);
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
  EXPECT_NE(rendered.find("Required Weapon"), std::string::npos);
  EXPECT_NE(rendered.find("Claw"), std::string::npos);
}

// The scroll bar's column is part of the card, so a section rule crosses it.
// Stopping at the text's edge left a notch a column short of the border.
TEST_F(SkillInspectPanelTest, ASectionRuleReachesTheBorder) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  ftxui::Element card = panel.Render();
  // Drawn at the card's own width rather than the test screen's: a window
  // stretched to fill the screen stretches its rules with it, which hides
  // exactly the gap this is looking for.
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  // Read off the pixel grid rather than ToString, which carries the card's
  // color escapes and makes every line a different length in bytes.
  int rules = 0;
  for (int y = 0; y < screen.dimy(); ++y) {
    if (screen.PixelAt(0, y).character != "\u251c") {
      continue;
    }
    ++rules;
    EXPECT_EQ(screen.PixelAt(screen.dimx() - 2, y).character, "\u2500")
        << "row " << y << " stops short of the border";
    EXPECT_EQ(screen.PixelAt(screen.dimx() - 1, y).character, "\u2524");
  }
  EXPECT_GT(rules, 0) << "the card rules off its sections";
}

// The columns the card lays out in, borders included.
int CardColumns(const Skill& skill, int level) {
  SkillInspectPanel panel;
  panel.SetSkill(&skill, level, 0);
  ftxui::Element card = panel.Render();
  card->ComputeRequirement();
  return card->requirement().min_x;
}

// The card is as wide as what the skill has to say and no wider. A skill with
// little to say still gets the width GMS's sentences read at, and a value too
// long for that widens the card rather than wrapping inside it.
TEST_F(SkillInspectPanelTest, TheCardIsAsWideAsTheSkillNeeds) {
  Skill skill = IronBody();
  int floor = CardColumns(skill, 5);
  EXPECT_EQ(floor, 61) << "the floor a description reads at, plus the border "
                          "and the scroll bar's column";

  skill.add_required_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_AXE);
  skill.add_required_equip_type(EQUIP_TYPE_SPEAR);
  EXPECT_GT(CardColumns(skill, 5), floor);
  EXPECT_NE(RowIn(RenderAt(skill, 5), "Required Weapon",
                  "One-Handed Sword / Two-Handed Axe / Spear"),
            std::string::npos);
}

// Two weapons with names as long as "One-Handed Sword" make a value longer
// than most. The card widens to seat the pair where it has the room, and
// where it does not it breaks the list between entries -- a requirement cut
// off mid-weapon says the wrong weapon.
TEST_F(SkillInspectPanelTest, ALongWeaponListIsSeatedOrWrappedWhole) {
  Skill skill = IronBody();
  skill.add_required_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_AXE);
  EXPECT_NE(RowIn(RenderAt(skill, 5), "Required Weapon",
                  "One-Handed Sword / Two-Handed Axe"),
            std::string::npos);

  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  panel.SetWidthBounds(0, 41);
  std::string narrow = RenderElement(panel.Render());
  EXPECT_NE(narrow.find("One-Handed Sword"), std::string::npos);
  EXPECT_NE(narrow.find("Two-Handed Axe"), std::string::npos);
  for (const std::string& line : Lines(narrow)) {
    EXPECT_EQ(line.find("One-Handed Sword / Two-Handed Axe"),
              std::string::npos);
  }
}

// A skill taking either hand's sword names the sword, not the two of them --
// four spelled-out weapons would run the requirement down four lines.
TEST_F(SkillInspectPanelTest, BothHandsOfAWeaponReadAsTheWeapon) {
  Skill skill = IronBody();
  skill.add_required_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_SWORD);
  skill.add_required_equip_type(EQUIP_TYPE_ONE_HANDED_AXE);
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_AXE);
  std::string rendered = RenderAt(skill, 5);
  EXPECT_NE(rendered.find("Sword / Axe"), std::string::npos);
  EXPECT_EQ(rendered.find("One-Handed"), std::string::npos);
  EXPECT_EQ(rendered.find("Two-Handed"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, BothHandsOfABluntReadAsABlunt) {
  Skill skill = IronBody();
  skill.add_required_equip_type(EQUIP_TYPE_ONE_HANDED_BLUNT);
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_BLUNT);
  EXPECT_NE(RenderAt(skill, 5).find("Blunt"), std::string::npos);
  EXPECT_EQ(RenderAt(skill, 5).find("Handed"), std::string::npos);
}

// Half a pair is still that weapon: a Spearman's sword-only skill would read
// as taking every sword if the collapse did not check both halves.
TEST_F(SkillInspectPanelTest, OneHandOfAPairKeepsItsFullName) {
  Skill skill = IronBody();
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_SWORD);
  EXPECT_NE(RenderAt(skill, 5).find("Two-Handed Sword"), std::string::npos);
}

// A grant that only lands with some of the skill's weapons says which in
// brackets, or it reads as unconditional beside the rows that are.
TEST_F(SkillInspectPanelTest, AWeaponBonusNamesTheWeaponItNeeds) {
  Skill skill = IronBody();
  skill.add_required_equip_type(EQUIP_TYPE_ONE_HANDED_AXE);
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_AXE);
  skill.add_required_equip_type(EQUIP_TYPE_SPEAR);
  WeaponBonus* bonus = skill.add_weapon_bonus();
  bonus->add_required_equip_type(EQUIP_TYPE_ONE_HANDED_AXE);
  bonus->add_required_equip_type(EQUIP_TYPE_TWO_HANDED_AXE);
  bonus->mutable_effect()->set_damage_pct(0.05);
  std::string rendered = RenderAt(skill, 5);
  EXPECT_NE(rendered.find("+5% (Axe)"), std::string::npos);
}

// The bonus is flat, so it reads the same in the level-5 block as in the
// level-6 one below it -- unlike every other row there.
TEST_F(SkillInspectPanelTest, AWeaponBonusReadsTheSameAtEveryLevel) {
  Skill skill = IronBody();
  skill.add_required_equip_type(EQUIP_TYPE_SPEAR);
  WeaponBonus* bonus = skill.add_weapon_bonus();
  bonus->add_required_equip_type(EQUIP_TYPE_SPEAR);
  bonus->mutable_effect()->set_damage_pct(0.05);
  int matches = 0;
  for (const std::string& line : Lines(RenderAt(skill, 5))) {
    matches += line.find("+5% (Spear)") != std::string::npos ? 1 : 0;
  }
  EXPECT_EQ(matches, 2);
}

TEST_F(SkillInspectPanelTest, NoReachRowForASingleTargetSkill) {
  Skill skill = IronBody();
  std::string rendered = RenderAt(skill, 5);
  EXPECT_EQ(rendered.find("Enemies Hit"), std::string::npos);
  EXPECT_EQ(rendered.find("Required Weapon"), std::string::npos);
}

// base + per_level * (L - 1) lands a hair under the round figure at some
// levels: Iron Body's seven steps of +1% come to 6.999999999999999, and its
// damage reduction to 3.4999999999999996. Truncating would show "6.9%" and
// "-3.4%" for a skill whose data plainly says 7 and 3.5.
TEST_F(SkillInspectPanelTest, PercentagesRoundRatherThanTruncate) {
  Skill skill = IronBody();
  std::string rendered = RenderAt(skill, 7);
  EXPECT_NE(rendered.find("+7%"), std::string::npos);
  EXPECT_NE(rendered.find("-3.5%"), std::string::npos);
  EXPECT_EQ(rendered.find("6.9"), std::string::npos);
  EXPECT_EQ(rendered.find("3.4"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, AttackSpeedCountsItsStages) {
  Skill skill = IronBody();
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
  Skill skill = IronBody();
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
  panel.SetSkill(nullptr, 0, 0);
  EXPECT_NE(RenderElement(panel.Render()).find("(no skill)"),
            std::string::npos);
}

// Evil Eye Shock: fights on its own clock every 12 seconds.
Skill MakeEvilEyeShock() {
  Skill skill;
  skill.set_name("Evil Eye Shock");
  skill.set_kind(SKILL_KIND_AUTO_ATTACK);
  skill.set_job_advancement(JOB_ADVANCEMENT_SPEARMAN);
  skill.set_max_level(10);
  skill.set_max_enemies(10);
  skill.set_lines(6);
  skill.set_cast_interval_seconds(12.0);
  skill.set_description("Your Evil Eye shouts.");
  skill.mutable_base()->set_skill_pct(1.23);
  skill.mutable_per_level()->set_skill_pct(0.03);
  return skill;
}

// A skill that fights on its own is something the character casts, not a
// lever bolted to their stat line, and the panel has to say so.
TEST_F(SkillInspectPanelTest, TitlesASkillOnItsOwnClockActive) {
  EXPECT_NE(RenderAt(MakeEvilEyeShock(), 1).find("Active"), std::string::npos);
}

// It is a swing like any other, whatever sets it off, so its damage reads the
// same way -- not as a skill with no effect worth naming.
TEST_F(SkillInspectPanelTest, ShowsTheDamageOfASkillOnItsOwnClock) {
  std::string out = RenderAt(MakeEvilEyeShock(), 1);
  EXPECT_NE(out.find("123% x6 = 738%"), std::string::npos);
  EXPECT_EQ(out.find("no effect"), std::string::npos);
}

// How often it goes off is not the player's to change, and the seconds the
// data holds are not the seconds the pacing band plays them at.
TEST_F(SkillInspectPanelTest, SaysNothingAboutHowOftenASkillFires) {
  EXPECT_EQ(RenderAt(MakeEvilEyeShock(), 1).find("Fires Every"),
            std::string::npos);
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Fires Every"),
            std::string::npos);
}

// A form that takes the place of every cast is permanent. "Empowers" says
// that on its own -- a rate is what marks the other shape out from it.
TEST_F(SkillInspectPanelTest, ReadsAFormOnEveryCastAsUnconditional) {
  Skill skill;
  skill.set_name("Mist Eruption");
  skill.set_kind(SKILL_KIND_ATTACK);
  skill.set_job_advancement(JOB_ADVANCEMENT_FIRE_POISON_MAGE);
  skill.set_max_level(20);
  skill.set_description("Sets off the poison hanging around you.");
  skill.set_max_enemies(12);
  skill.set_lines(10);
  skill.mutable_base()->set_skill_pct(2.71);
  EmpoweredForm* form = skill.add_empowered_form();
  form->set_skill_name("Poison Mist");
  form->set_casts_per_trigger(1);
  form->set_lines(1);
  form->mutable_base()->set_skill_pct(2.71);

  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(RowIn(rendered, "Empowers", "Poison Mist"), std::string::npos);
  EXPECT_EQ(rendered.find("Every"), std::string::npos) << rendered;
}

// A DoT is one row: what a tick is worth, how often it comes and how long it
// lasts. None of the three says anything without the others.
TEST_F(SkillInspectPanelTest, ReadsADotAsOneRow) {
  Skill skill;
  skill.set_name("Flame Sweep");
  skill.set_kind(SKILL_KIND_ATTACK);
  skill.set_job_advancement(JOB_ADVANCEMENT_FIRE_POISON_MAGE);
  skill.set_max_level(30);
  skill.set_description("Sweeps the room with flame.");
  skill.set_max_enemies(8);
  skill.set_lines(7);
  skill.mutable_base()->set_skill_pct(1.42);
  skill.mutable_per_level()->set_skill_pct(0.02);
  Dot* burn = skill.mutable_dot();
  burn->set_interval_seconds(1.0);
  burn->set_duration_seconds(5.0);
  burn->set_lines(1);
  burn->mutable_base()->set_skill_pct(1.24);
  burn->mutable_per_level()->set_skill_pct(0.04);

  EXPECT_NE(RowIn(RenderAt(skill, 30), "DoT", "240% every 1s for 5s"),
            std::string::npos);

  // A poison says the two things a burn a swing simply leaves has nothing to
  // say about: what it takes to land, and how deep it piles.
  burn->set_chance(0.32);
  burn->set_chance_per_level(0.02);
  burn->set_max_stacks(2.1666667);
  burn->set_max_stacks_per_level(0.1666667);
  std::string poison = RenderAt(skill, 10);
  EXPECT_NE(RowIn(poison, "DoT", "50% chance of 160% every 1s for"),
            std::string::npos);
  EXPECT_NE(poison.find("5s, stacks 3 times"), std::string::npos);
}

// The strike a swing sets off reads as a swing of its own: what one strike is
// worth, times its count, and how often it goes out. Its bargain against an
// ordinary monster follows, exactly as the swing's own does.
TEST_F(SkillInspectPanelTest, ReadsASideStrikeWithItsWait) {
  Skill skill;
  skill.set_name("Showdown");
  skill.set_kind(SKILL_KIND_ATTACK);
  skill.set_job_advancement(JOB_ADVANCEMENT_HERMIT);
  skill.set_max_level(30);
  skill.set_description("Provoke the enemies around you.");
  skill.set_max_enemies(6);
  skill.set_lines(2);
  skill.mutable_base()->set_skill_pct(3.73);
  skill.mutable_per_level()->set_skill_pct(0.08);
  SideStrike* side = skill.mutable_side_strike();
  side->set_label("Shuriken");
  side->set_lines(6);
  side->set_cooldown_seconds(5.0);
  side->mutable_base()->set_skill_pct(0.09);
  side->mutable_per_level()->set_skill_pct(0.0051724);
  side->mutable_base()->set_normal_skill_pct(2.00);

  std::string card = RenderAt(skill, 30);
  EXPECT_NE(RowIn(card, "Shuriken", "24% x6 = 144%"), std::string::npos);
  EXPECT_NE(card.find("every 5s"), std::string::npos);
  EXPECT_NE(RowIn(card, "Shuriken Normal", "224% x6 = 1344%"),
            std::string::npos);
}

// Neither half of Final Attack says anything alone, so they share a line.
TEST_F(SkillInspectPanelTest, ReadsFinalAttackAsOneFact) {
  Skill skill;
  skill.set_name("Final Attack");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SPEARMAN);
  skill.set_max_level(20);
  skill.set_description("A chance at a second blow.");
  skill.mutable_base()->set_final_attack_chance(0.02);
  skill.mutable_base()->set_final_attack_pct(1.22);
  skill.mutable_per_level()->set_final_attack_chance(0.02);
  skill.mutable_per_level()->set_final_attack_pct(0.02);

  EXPECT_NE(RowIn(RenderAt(skill, 20), "Final Attack", "40% for 160%"),
            std::string::npos);

  // A mark that throws several stars reads them the way the swing above reads
  // its own lines: what one is worth, times how many, and the total.
  skill.mutable_base()->set_final_attack_lines(3);
  EXPECT_NE(RenderAt(skill, 20).find("40% for 160% x3 = 480%"),
            std::string::npos);
  skill.mutable_base()->clear_final_attack_lines();

  // Blizzard's own ladder, which is the widest this row goes: it names the one
  // enemy it falls on, whole and on the one line. The note is the only thing
  // here long enough to be cut off.
  skill.set_final_attack_single_enemy(true);
  skill.set_max_level(30);
  skill.mutable_base()->set_final_attack_pct(1.04);
  skill.mutable_per_level()->set_final_attack_pct(0.04);
  EXPECT_NE(RenderAt(skill, 30).find("60% for 220%, one enemy"),
            std::string::npos);
}

TEST_F(SkillInspectPanelTest, ReadsTheNewStatLevers) {
  Skill skill;
  skill.set_name("Physical Training");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_SPEARMAN);
  skill.set_max_level(5);
  skill.set_description("Raises STR and DEX.");
  skill.mutable_base()->set_str(6);
  skill.mutable_base()->set_dex(6);
  skill.mutable_base()->set_mastery(0.14);

  std::string out = RenderAt(skill, 1);
  EXPECT_NE(RowIn(out, "STR", "+6"), std::string::npos);
  EXPECT_NE(RowIn(out, "DEX", "+6"), std::string::npos);
  EXPECT_NE(RowIn(out, "Mastery", "14%"), std::string::npos);
}

// The wizard's pair: magic attack reads beside ATT, and critical damage
// beside critical rate.
TEST_F(SkillInspectPanelTest, ReadsTheWizardsLevers) {
  Skill skill;
  skill.set_name("Freezing Crush");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_job_advancement(JOB_ADVANCEMENT_MAGICIAN);
  skill.set_max_level(10);
  skill.set_description("Sharpens what a critical hit is worth.");
  skill.mutable_base()->set_crit_dmg(0.005);
  skill.mutable_base()->set_magic_attack(3);

  std::string out = RenderAt(skill, 1);
  EXPECT_NE(RowIn(out, "Critical Damage", "+0.5%"), std::string::npos);
  EXPECT_NE(RowIn(out, "MATT", "+3"), std::string::npos);
}

// Built from the requirement rather than from a sentence typed beside it, so
// the wording and the rule the skills tab enforces cannot drift apart.
TEST_F(SkillInspectPanelTest, SpellsOutWhatMustBeLearnedFirst) {
  Skill skill = IronBody();
  skill.set_name("Hyper Body");
  skill.mutable_required_skill()->set_skill_name("Iron Wall");
  skill.mutable_required_skill()->set_level(3);

  EXPECT_NE(RowIn(RenderAt(skill, 1), "Required Skill", "Iron Wall Lv. 3+"),
            std::string::npos);
}

// And it is ruled off from the description. What the skill does and what the
// player must do first are two different claims.
TEST_F(SkillInspectPanelTest, RulesTheRequirementOffFromTheDescription) {
  Skill skill = IronBody();
  skill.set_name("Hyper Body");
  skill.mutable_required_skill()->set_skill_name("Iron Wall");
  skill.mutable_required_skill()->set_level(3);

  std::vector<std::string> lines = Lines(RenderAt(skill, 1));
  int row = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (lines[i].find("Required Skill") != std::string::npos) {
      row = i;
    }
  }
  ASSERT_GT(row, 0) << "the requirement is not on screen at all";
  // A rule is drawn as a run of box-drawing horizontals, and the description
  // above it is not.
  EXPECT_NE(lines[row - 1].find("──"), std::string::npos)
      << "no rule above: [" << lines[row - 1] << "]";
  EXPECT_NE(lines[row - 2].find("Boosts DEF"), std::string::npos)
      << "the description does not close where it should";
}

TEST_F(SkillInspectPanelTest, NoRequirementRowWhenThereIsNone) {
  EXPECT_EQ(RenderAt(IronBody(), 1).find("Required Skill"), std::string::npos);
  EXPECT_EQ(RenderAt(IronBody(), 1).find("Required Level"), std::string::npos);
}

// A label too wide for its column takes a row of its own rather than being cut
// into the value beside it -- what it names is a skill, and half a skill's
// name is not one.
TEST_F(SkillInspectPanelTest, ALongBoostLabelKeepsItsWholeName) {
  Skill skill = IronBody();
  SkillBoost* boost = skill.add_boost();
  boost->set_skill_name("Gungnir's Descent");
  boost->mutable_effect()->set_skill_pct(0.20);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(rendered.find("Boosts Gungnir's Descent"), std::string::npos);
  EXPECT_EQ(rendered.find("Descent+20%"), std::string::npos)
      << "the value ran into the name";
}

// A Hyper Skill's gate is a level rather than a skill below it. The title says
// what the skill is and nothing else: where the point came from is the book's
// business, not the card's.
TEST_F(SkillInspectPanelTest, AHyperSkillNamesItsLevel) {
  Skill skill = IronBody();
  skill.set_hyper(true);
  skill.set_required_level(165);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(RowIn(rendered, "Required Level", "165"), std::string::npos);
  EXPECT_NE(rendered.find("Passive"), std::string::npos);
  EXPECT_EQ(rendered.find("Hyper"), std::string::npos);
}

// A weapon in hand and a skill already learned are the same kind of claim, so
// they read alike: one after the other, labels alike, values in one column.
TEST_F(SkillInspectPanelTest, TheTwoRequirementsReadAlike) {
  Skill skill = IronBody();
  skill.add_required_equip_type(EQUIP_TYPE_SPEAR);
  skill.mutable_required_skill()->set_skill_name("Iron Wall");
  skill.mutable_required_skill()->set_level(3);

  std::vector<std::string> lines = Lines(RenderAt(skill, 1));
  int weapon = -1;
  int prereq = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (lines[i].find("Required Weapon") != std::string::npos) {
      weapon = i;
    }
    if (lines[i].find("Required Skill") != std::string::npos) {
      prereq = i;
    }
  }
  ASSERT_GE(weapon, 0);
  ASSERT_GE(prereq, 0);
  EXPECT_EQ(prereq, weapon + 1) << "the two requirements are not together";
  // Both rows carry the same border prefix, so an equal byte offset is an
  // equal column.
  EXPECT_EQ(lines[weapon].find("Spear"), lines[prereq].find("Iron Wall"))
      << "the values do not share a column:\n[" << lines[weapon] << "]\n["
      << lines[prereq] << "]";
}

// The screen is sized from the whole book rather than from the card the
// cursor happens to be on, so it stands still as the cursor walks it: no card
// is larger than the size, one of them is exactly it, and the order they are
// measured in cannot change the answer.
TEST_F(SkillInspectPanelTest, TheLargestCardSizesThemAll) {
  Skill iron_body = IronBody();
  Skill lucky_seven = MakeLuckySeven();
  PreviewCardSize size = LargestPreviewCard({&iron_body, &lucky_seven}, 0);
  PreviewCardSize reversed = LargestPreviewCard({&lucky_seven, &iron_body}, 0);
  EXPECT_EQ(reversed.rows, size.rows);
  EXPECT_EQ(reversed.columns, size.columns);

  SkillInspectPanel panel;
  panel.SetWidthBounds(size.columns, size.columns);
  int tallest = 0;
  for (const Skill* skill : {&iron_body, &lucky_seven}) {
    panel.SetSkill(skill, 0, 0, SkillInspectPanel::kPreview);
    ftxui::Element card = panel.Render();
    card->ComputeRequirement();
    EXPECT_EQ(card->requirement().min_x, size.columns);
    EXPECT_LE(card->requirement().min_y, size.rows);
    tallest = std::max(tallest, card->requirement().min_y);
  }
  EXPECT_EQ(tallest, size.rows);

  EXPECT_EQ(LargestPreviewCard({}, 0).rows, 0);
  EXPECT_EQ(LargestPreviewCard({}, 0).columns, 0);
}

// The two Dark Knight levers a plain row could state wrongly: one is charged
// against what the player spent rather than what they carry, and the other is
// a wait that shortens, so it takes no plus sign.
TEST_F(SkillInspectPanelTest, StatesTheShareOfApAndTheWaitToRevive) {
  Skill skill = IronBody();
  skill.clear_base();
  skill.clear_per_level();
  skill.mutable_base()->set_ap_stat_pct(0.01);
  skill.mutable_per_level()->set_ap_stat_pct(0.00483);
  skill.mutable_base()->set_revive_cooldown_seconds(1103);
  skill.mutable_per_level()->set_revive_cooldown_seconds(-7);

  std::string rendered = RenderAt(skill, 20);
  EXPECT_NE(rendered.find("Stats from AP"), std::string::npos);
  EXPECT_NE(rendered.find("+10.2%"), std::string::npos);
  EXPECT_NE(rendered.find("Revives Every"), std::string::npos);
  EXPECT_NE(rendered.find("970s"), std::string::npos);
}

// A timed buff is half of what the skill does, and the half a player has to
// plan around -- so the page leads with it, heads both halves, and keeps the
// wait for the next cast on one row with what a landed hit takes off it.
TEST_F(SkillInspectPanelTest, StatesBothHalvesOfATimedBuff) {
  Skill resonance = IronBody();
  resonance.clear_base();
  resonance.clear_per_level();
  resonance.set_kind(SKILL_KIND_ACTIVE);
  resonance.set_cooldown_seconds(70.0);
  resonance.mutable_base()->set_ied_pct(0.01);
  resonance.mutable_per_level()->set_ied_pct(0.01);
  Buff* buff = resonance.mutable_buff();
  buff->set_duration_seconds(15.0);
  buff->set_duration_seconds_per_level(0.5);
  buff->set_cooldown_reduction_seconds(0.35);
  buff->mutable_base()->set_heal_pct(0.13);
  buff->mutable_per_level()->set_heal_pct(0.03);
  buff->mutable_base()->set_final_dmg_pct(0.02);

  std::string rendered = RenderAt(resonance, 11);
  EXPECT_NE(RowIn(rendered, "Cooldown", "70s, -0.35s per hit"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Heal on Cast", "+43% HP"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Final Damage", "+2%"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Ignore DEF", "+11%"), std::string::npos);
  // The heading says "while up" once, so no row repeats it.
  EXPECT_EQ(rendered.find("while up"), std::string::npos);
  EXPECT_EQ(rendered.find("Buff Duration"), std::string::npos);

  // What lapses is read first, and what keeps is under its own heading.
  std::vector<std::string> lines = Lines(rendered);
  int active = -1;
  int passive = -1;
  int keeps = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (lines[i].find("Active for 20s") != std::string::npos) {
      active = i;
    }
    // Unambiguous here because this skill's window is titled "Active".
    if (lines[i].find("Passive") != std::string::npos) {
      passive = i;
    }
    if (lines[i].find("Ignore DEF") != std::string::npos) {
      keeps = i;
    }
  }
  EXPECT_GT(active, 0);
  EXPECT_GT(passive, active);
  EXPECT_GT(keeps, passive);
}

// A wound bleeds only while it stands, so it reads under the heading that
// says how long that is -- not above it, where a permanent aura reads.
TEST_F(SkillInspectPanelTest, AWoundReadsUnderTheWindowItBleedsIn) {
  Skill puncture = IronBody();
  puncture.set_kind(SKILL_KIND_ATTACK);
  puncture.set_max_enemies(8);
  Buff* wound = puncture.mutable_buff();
  wound->set_duration_seconds(45.0);
  wound->set_duration_seconds_per_level(0.5);
  wound->mutable_base()->set_damage_pct(0.11);
  BuffPulse* pulse = wound->mutable_pulse();
  pulse->set_label("Wound");
  pulse->set_cast_interval_seconds(2.0);
  pulse->set_lines(1);
  pulse->mutable_base()->set_skill_pct(0.33);
  pulse->mutable_per_level()->set_skill_pct(0.05);

  std::string rendered = RenderAt(puncture, 11);
  // Its damage and its clock share a row, and its reach is the swing's own.
  EXPECT_NE(RowIn(rendered, "Wound", "83% every 2s"), std::string::npos)
      << rendered;
  std::vector<std::string> lines = Lines(rendered);
  int active = -1;
  int wounded = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (lines[i].find("Active for 50s") != std::string::npos) {
      active = i;
    }
    if (lines[i].find("Wound") != std::string::npos) {
      wounded = i;
    }
  }
  EXPECT_GT(active, 0);
  EXPECT_GT(wounded, active);
}

// A pulse reaching enemies of its own says so where every other own-clock half
// does, and its damage row is then the damage alone -- so much, so many times,
// so many strikes, and how many ticks the window is worth.
TEST_F(SkillInspectPanelTest, APulseWithItsOwnReachStatesItBesideItsClock) {
  Skill valhalla = IronBody();
  valhalla.set_kind(SKILL_KIND_ACTIVE);
  Buff* buff = valhalla.mutable_buff();
  buff->set_duration_seconds(30.0);
  buff->mutable_base()->set_attack(50);
  BuffPulse* pulse = buff->mutable_pulse();
  pulse->set_label("Sword Strikes");
  pulse->set_cast_interval_seconds(2.0);
  pulse->set_lines(2);
  pulse->set_casts(3);
  pulse->set_max_enemies(6);
  pulse->set_max_pulses(12);
  pulse->mutable_base()->set_skill_pct(5.20);

  std::string rendered = RenderAt(valhalla, 1);
  EXPECT_NE(RowIn(rendered, "Sword Strikes", "520% x2 x3 = 3120%, 12 times"),
            std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Attacks", "6 enemies every 2s"), std::string::npos)
      << rendered;
}

// The skill list tells an active from a passive by colour; a skill that is
// both has to tell its own halves apart the same way, or the colours mean one
// thing in the book and another on the page.
TEST_F(SkillInspectPanelTest, HeadsTheTwoHalvesInTheSkillListsColors) {
  Skill resonance = IronBody();
  resonance.set_kind(SKILL_KIND_ACTIVE);
  resonance.mutable_buff()->set_duration_seconds(15.0);
  resonance.mutable_buff()->mutable_base()->set_final_dmg_pct(0.02);
  SkillInspectPanel panel;
  panel.SetSkill(&resonance, 1, 0);
  EXPECT_EQ(LabelColor(panel.Render(), "Active for 15s"), kGold);
  EXPECT_EQ(LabelColor(panel.Render(), "Passive"), kGreen);
}

// A shared buff grants the party nothing of its own -- everyone raises the
// same one in turn -- so the heading is where the page has to say so.
TEST_F(SkillInspectPanelTest, ASharedBuffSaysSoInItsHeading) {
  Skill epic = IronBody();
  epic.set_kind(SKILL_KIND_ACTIVE);
  epic.mutable_buff()->set_duration_seconds(60.0);
  epic.mutable_buff()->mutable_base()->set_damage_pct(0.10);
  EXPECT_EQ(RenderAt(epic, 1).find("shared with your party"),
            std::string::npos);
  epic.mutable_buff()->set_party_shared(true);
  EXPECT_NE(RenderAt(epic, 1).find("Active for 60s, shared with your party"),
            std::string::npos);
}

// An attack's ignored defence is true only while that swing is in the air; the
// ATT it grants is the character's for good. Unheaded the two rows read alike.
// The same levers on a passive ARE the character's, so nothing heads them.
TEST_F(SkillInspectPanelTest, HeadsWhatRidesTheSwingApartFromWhatIsKept) {
  Skill mist = MakeLuckySeven();
  mist.mutable_base()->set_ied_pct(0.40);
  mist.mutable_base()->set_attack(20);

  std::vector<std::string> lines = Lines(RenderAt(mist, 1));
  int swing = -1;
  int ied = -1;
  int passive = -1;
  int att = -1;
  // The first of each: the card carries a second level block saying the same
  // things again, and the claim here is about the order inside one block.
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (swing < 0 && lines[i].find("This Attack Only") != std::string::npos) {
      swing = i;
    }
    if (ied < 0 && lines[i].find("Ignore DEF") != std::string::npos) {
      ied = i;
    }
    if (passive < 0 && lines[i].find("Passive") != std::string::npos) {
      passive = i;
    }
    if (att < 0 && lines[i].find("ATT ") != std::string::npos) {
      att = i;
    }
  }
  EXPECT_GT(swing, 0);
  EXPECT_GT(ied, swing);
  EXPECT_GT(passive, ied);
  EXPECT_GT(att, passive);

  SkillInspectPanel panel;
  panel.SetSkill(&mist, 1, 0);
  EXPECT_EQ(LabelColor(panel.Render(), "This Attack Only"), kGold);

  Skill aim = IronBody();
  aim.mutable_base()->set_ied_pct(0.02);
  std::string rendered = RenderAt(aim, 1);
  EXPECT_NE(RowIn(rendered, "Ignore DEF", "+2%"), std::string::npos);
  EXPECT_EQ(rendered.find("This Attack Only"), std::string::npos) << rendered;
}

// Every other skill has one half and nothing to disambiguate, so a heading
// over it would be a row spent saying what the window title already says.
TEST_F(SkillInspectPanelTest, ASkillWithOneHalfIsNotHeadedAtAll) {
  std::vector<std::string> lines = Lines(RenderAt(IronBody(), 5));
  for (int i = 1; i < static_cast<int>(lines.size()); ++i) {
    // Row 0 is the window's own title, which is "Passive" for this skill.
    // What must not appear is a heading row saying it a second time.
    EXPECT_EQ(lines[i].find("Passive"), std::string::npos) << lines[i];
    EXPECT_EQ(lines[i].find("Active for"), std::string::npos) << lines[i];
  }
}

// What a skill holds over the party is its own section: those are not the
// reader's numbers, and a page that mixed them into the passive half would be
// claiming the reader gets both.
TEST_F(SkillInspectPanelTest, WhatThePartyGetsIsHeadedApart) {
  Skill bless = IronBody();
  bless.set_name("Bless");
  bless.mutable_ally_base()->set_attack(6);
  bless.mutable_ally_per_level()->set_attack(1);

  std::vector<std::string> lines = Lines(RenderAt(bless, 10));
  int party = -1;
  int attack = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (lines[i].find("Your Party") != std::string::npos) {
      party = i;
    }
    if (lines[i].find("ATT") != std::string::npos) {
      attack = i;
    }
  }
  EXPECT_GT(party, 0);
  EXPECT_GT(attack, party) << "the party's attack sits under its heading";
  // And a skill with nothing to give raises no heading at all.
  EXPECT_EQ(RenderAt(IronBody(), 10).find("Your Party"), std::string::npos);
}

// Holy Magic Shell stands over the caster and the party as one shell, so both
// halves of the card state what it blocks -- and the boss row is named for
// what it covers, since a player reading it wants to know which hits the
// shell cannot swallow.
TEST_F(SkillInspectPanelTest, AShellStatesWhatItBlocksForBothHalves) {
  Skill shell = IronBody();
  shell.set_name("Holy Magic Shell");
  shell.clear_base();
  shell.clear_per_level();
  shell.set_kind(SKILL_KIND_ACTIVE);
  Buff* buff = shell.mutable_buff();
  buff->set_duration_seconds(10.25);
  buff->set_duration_seconds_per_level(0.25);
  buff->mutable_base()->set_heal_pct(0.31);
  buff->mutable_per_level()->set_heal_pct(0.01);
  buff->mutable_ally_base()->set_heal_pct(0.31);
  buff->mutable_ally_per_level()->set_heal_pct(0.01);
  buff->mutable_shield()->set_party(true);
  buff->mutable_shield()->set_hits(5.5);
  buff->mutable_shield()->set_hits_per_level(0.5);
  buff->mutable_shield()->set_boss_damage_taken_pct(0.10);

  std::string rendered = RenderAt(shell, 20);
  EXPECT_NE(RowIn(rendered, "Blocks", "15 attacks"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Damage Taken (Boss)", "-10%"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Heal on Cast", "+50% HP"), std::string::npos);

  // Both halves say it, and the party's is under the buff's own heading.
  std::vector<std::string> lines = Lines(rendered);
  int party = -1;
  std::vector<int> blocks;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (party < 0 && lines[i].find("Your Party") != std::string::npos) {
      party = i;
    }
    if (lines[i].find("Blocks") != std::string::npos) {
      blocks.push_back(i);
    }
  }
  ASSERT_GT(party, 0);
  ASSERT_GE(blocks.size(), 2u);
  EXPECT_LT(blocks.front(), party);
  EXPECT_GT(blocks.back(), party);
}

// Smokescreen's party half lapses with the buff, so it is read under the
// buff's own heading rather than at the foot of the card, where it would look
// like something the party keeps.
TEST_F(SkillInspectPanelTest, ABuffsPartyHalfSitsUnderTheBuff) {
  Skill smoke = IronBody();
  smoke.set_name("Smokescreen");
  Buff* buff = smoke.mutable_buff();
  buff->set_duration_seconds(30.0);
  buff->mutable_base()->set_crit_dmg(0.02);
  buff->mutable_ally_base()->set_damage_taken_pct(0.01);
  buff->mutable_ally_per_level()->set_damage_taken_pct(0.01);

  // The first of each: the card states every level the reader can see, so the
  // rows below all repeat further down it.
  std::vector<std::string> lines = Lines(RenderAt(smoke, 10));
  int active = -1;
  int party = -1;
  int taken = -1;
  for (int i = static_cast<int>(lines.size()) - 1; i >= 0; --i) {
    if (lines[i].find("Active for") != std::string::npos) {
      active = i;
    }
    if (lines[i].find("Your Party") != std::string::npos) {
      party = i;
    }
    if (lines[i].find("Damage Taken") != std::string::npos) {
      taken = i;
    }
  }
  EXPECT_GT(active, 0);
  EXPECT_GT(party, active) << "the party's share belongs to the buff";
  EXPECT_GT(taken, party) << "and sits under its own heading";
}

// Parashock Guard pays its caster only for shielding somebody. A player
// maxing it alone sees nothing move, so the heading over its own half has to
// say why -- and it says so even on a page with no other section on it.
TEST_F(SkillInspectPanelTest, ASkillNeedingAPartySaysSoOverItsOwnHalf) {
  Skill guard = IronBody();
  guard.set_name("Parashock Guard");
  guard.set_requires_party(true);

  EXPECT_NE(RenderAt(guard, 10).find("Passive, in a Party"), std::string::npos);
  EXPECT_EQ(RenderAt(IronBody(), 10).find("in a Party"), std::string::npos);
}

// --- Scrolling a card too tall for the terminal ---

// A rendered row as the plain text it says. The colour escapes, the window
// border and the scroll bar's own glyph are all either escapes or multibyte,
// so stripping to ASCII leaves the row's words and nothing about how it was
// drawn -- which is what a claim about WHICH row is on screen is about.
std::string RowText(const std::string& line) {
  std::string out;
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '\x1B') {
      while (i < line.size() && line[i] != 'm') {
        ++i;
      }
      continue;
    }
    unsigned char c = static_cast<unsigned char>(line[i]);
    if (c >= 0x20 && c < 0x7F) {
      out += line[i];
    }
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

// The rows a card takes with no budget on it, borders included. Every scroll
// test measures the card rather than assuming a height: the card grows
// whenever a skill gains a lever, and a hardcoded number would go quietly
// wrong rather than loudly.
int CardRows(SkillInspectPanel& panel) {
  ftxui::Element card = panel.Render();
  card->ComputeRequirement();
  return card->requirement().min_y;
}

// The bar is drawn with ftxui's half-height glyphs; any of the three means a
// thumb is on screen.
bool HasScrollBar(const std::string& rendered) {
  return rendered.find("\u2503") != std::string::npos ||
         rendered.find("\u2579") != std::string::npos ||
         rendered.find("\u257B") != std::string::npos;
}

// A budget the card cannot fit into cuts it and says so, rather than drawing
// rows off the bottom of the terminal where nobody can see them.
TEST_F(SkillInspectPanelTest, ACardTooTallForItsBudgetIsCutAndSaysSo) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  int tall = CardRows(panel);
  ASSERT_GT(tall, 8) << "the card has to have something to lose";

  std::vector<std::string> full = Lines(RenderElement(panel.Render()));
  panel.SetMaxRows(tall - 3);
  std::string cut = RenderElement(panel.Render());
  std::vector<std::string> lines = Lines(cut);

  EXPECT_TRUE(HasScrollBar(cut));
  EXPECT_EQ(RowText(lines[1]), RowText(full[1])) << "cut at the foot";
  // Three rows shorter, so the last row it still shows is three from the end.
  EXPECT_EQ(RowText(lines[tall - 5]), RowText(full[tall - 5]));
  EXPECT_EQ(cut.find(RowText(full[tall - 2])), std::string::npos)
      << "and the last row of the card is off the bottom of it";
}

// Down walks the card under the window. There is no cursor to follow, so the
// page itself is what moves.
TEST_F(SkillInspectPanelTest, ScrollingWalksTheCardUnderTheWindow) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  int tall = CardRows(panel);
  std::vector<std::string> full = Lines(RenderElement(panel.Render()));
  panel.SetMaxRows(tall - 3);

  panel.ScrollBy(1);
  std::vector<std::string> lines = Lines(RenderElement(panel.Render()));
  EXPECT_EQ(RowText(lines[1]), RowText(full[2])) << "one row down the card";

  panel.ScrollBy(2);
  lines = Lines(RenderElement(panel.Render()));
  EXPECT_EQ(RowText(lines[1]), RowText(full[4]));
}

// Held at both ends rather than wrapped: with nothing selected to follow,
// falling out of the foot of a card at its head reads as a glitch.
TEST_F(SkillInspectPanelTest, ScrollingStopsAtBothEndsInsteadOfWrapping) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  int tall = CardRows(panel);
  std::vector<std::string> full = Lines(RenderElement(panel.Render()));
  panel.SetMaxRows(tall - 3);

  for (int i = 0; i < 50; ++i) {
    panel.ScrollBy(1);
  }
  std::vector<std::string> foot = Lines(RenderElement(panel.Render()));
  // Three rows are off the top, and the last row of the card is now the last
  // row of the window.
  EXPECT_EQ(RowText(foot[1]), RowText(full[4]));
  EXPECT_EQ(RowText(foot[tall - 5]), RowText(full[tall - 2]));

  panel.ScrollBy(1);
  EXPECT_EQ(RowText(Lines(RenderElement(panel.Render()))[1]), RowText(foot[1]));

  for (int i = 0; i < 50; ++i) {
    panel.ScrollBy(-1);
  }
  EXPECT_EQ(RowText(Lines(RenderElement(panel.Render()))[1]), RowText(full[1]))
      << "back at the top";
}

// Nothing off screen, nothing to indicate -- but the column the bar would take
// is held open, so a card does not jump a column wider the moment it outgrows
// the terminal.
TEST_F(SkillInspectPanelTest, ACardThatFitsHasNoThumbAndTheSameWidth) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  int tall = CardRows(panel);

  ftxui::Element roomy = panel.Render();
  EXPECT_FALSE(HasScrollBar(RenderElement(panel.Render())));
  panel.SetMaxRows(tall);
  EXPECT_FALSE(HasScrollBar(RenderElement(panel.Render())))
      << "a budget it exactly fits is still no reason for a thumb";

  panel.SetMaxRows(tall - 3);
  ftxui::Element cut = panel.Render();
  EXPECT_EQ(ftxui::Dimension::Fit(roomy).dimx, ftxui::Dimension::Fit(cut).dimx);
}

// A card is opened at its head, however far down the last one was read.
TEST_F(SkillInspectPanelTest, ResetScrollReturnsToTheHeadOfTheCard) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  int tall = CardRows(panel);
  std::vector<std::string> full = Lines(RenderElement(panel.Render()));
  panel.SetMaxRows(tall - 3);

  panel.ScrollBy(2);
  ASSERT_NE(RowText(Lines(RenderElement(panel.Render()))[1]), RowText(full[1]));
  panel.ResetScroll();
  EXPECT_EQ(RowText(Lines(RenderElement(panel.Render()))[1]), RowText(full[1]));
}

// A card that measures its own width has to ask for its right margin. On
// this one the bar's column is that margin, which is why the chrome is three
// columns rather than two.
TEST_F(SkillInspectPanelTest, EveryRowKeepsAColumnClearOfTheRightBorder) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 20, 0);
  std::vector<std::string> touching =
      RowsTouchingTheRightBorder(panel.Render());
  EXPECT_TRUE(touching.empty()) << (touching.empty() ? "" : touching[0]);
}

}  // namespace
}  // namespace ms
