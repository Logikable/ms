#include "src/frontend/screens/skill_inspect_panel.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/skill_placement.h"
#include "src/character/v_matrix.h"
#include "src/frontend/testing/panel_test_base.h"
#include "src/frontend/widgets/colors.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/prototypes.h"

namespace ms {
namespace {

// The position of a card row reading `label` then `value`, whatever gap the
// card's columns put between them, or npos. Tests check what a row says; how
// wide the label column came out is up to the card.
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

  // A rendered panel split into rows, so a test can check what is above what,
  // not only what is on screen.
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

// Lucky Seven: three strikes of 72% + 2% per level, five enemies, claw only.
Skill MakeLuckySeven() {
  Skill skill;
  skill.set_name("Lucky Seven");
  skill.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(skill, JOB_ADVANCEMENT_ROGUE);
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

// The title is the only place the panel says which kind of skill this is.
TEST_F(SkillInspectPanelTest, TitlesItselfPassiveOrActive) {
  Skill passive = IronBody();
  EXPECT_NE(RenderAt(passive, 5).find("Passive"), std::string::npos);
  Skill active = MakeLuckySeven();
  EXPECT_NE(RenderAt(active, 5).find("Active"), std::string::npos);
}

// A skill the player uses that does nothing modelled is still Active.
TEST_F(SkillInspectPanelTest, TitlesACastNonAttackActive) {
  Skill skill = MakeLuckySeven();
  skill.set_kind(SKILL_KIND_ACTIVE);
  EXPECT_NE(RenderAt(skill, 1).find("Active"), std::string::npos);
}

TEST_F(SkillInspectPanelTest, ReadsEveryLeverAtTheLearnedLevel) {
  Skill skill = IronBody();
  // A flat HP bonus, shown as a plain number rather than "per level" like the
  // pair beside it.
  skill.mutable_base()->set_max_hp(525);
  skill.mutable_per_level()->set_max_hp(25);
  std::string rendered = RenderAt(skill, 5);
  EXPECT_NE(rendered.find("Level 5"), std::string::npos);
  EXPECT_NE(rendered.find("+50"), std::string::npos);    // DEF
  EXPECT_NE(rendered.find("+5%"), std::string::npos);    // Max HP
  EXPECT_NE(rendered.find("-2.5%"), std::string::npos);  // Damage Taken
  EXPECT_NE(RowIn(rendered, "Max HP", "+625"), std::string::npos) << rendered;
}

// Shaped like Decent Mystic Door: a whole point every fifth level, stored as a
// fifth of one per level. The page must show the value at the character's
// level, not the step below it.
TEST_F(SkillInspectPanelTest, AFractionalLadderReadsWhatItGrants) {
  Skill skill = IronBody();
  skill.clear_base();
  skill.clear_per_level();
  skill.mutable_base()->set_str(1);
  skill.mutable_per_level()->set_str(0.2);
  EXPECT_NE(RowIn(RenderAt(skill, 5), "STR", "+1"), std::string::npos);
  EXPECT_NE(RowIn(RenderAt(skill, 6), "STR", "+2"), std::string::npos);
}

// Reckless Hunt trades DEF for damage. The cost is half the skill, so the page
// shows it as a loss instead of dropping the row for not being a gain.
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
  EXPECT_NE(rendered.find("+60"), std::string::npos);  // DEF one point later
}

// A node's levels don't cost one point each, so the next level's block shows
// its price, as on the Hyper Stat card. The level already paid for shows no
// price, and SP skills show none anywhere, since a level costs one point.
TEST_F(SkillInspectPanelTest, ANodesNextLevelWearsItsPrice) {
  Skill node = IronBody();
  node.set_v_node(V_NODE_KIND_COMMON);
  node.set_max_level(MaxVNodeLevel(V_NODE_KIND_COMMON));
  EXPECT_NE(RenderAt(node, 0).find("Level 1 - 7 VP"), std::string::npos)
      << "a common node's first level costs seven";
  std::string climbing = RenderAt(node, 5);
  EXPECT_NE(climbing.find("Level 6 - 4 VP"), std::string::npos);
  EXPECT_EQ(climbing.find("Level 5 - "), std::string::npos)
      << "the level already paid for is stated bare";
  // At the maximum there is no next block to price.
  EXPECT_EQ(RenderAt(node, node.max_level()).find(" VP"), std::string::npos);
  // The preview shows no price, since it describes the skill rather than a
  // player's choice.
  EXPECT_EQ(RenderPreview(node).find(" VP"), std::string::npos);
  EXPECT_EQ(RenderAt(IronBody(), 5).find(" VP"), std::string::npos)
      << "an SP skill's level is a point";
}

// Nothing spent yet, so there is no current level to show, only what the first
// point would give.
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

// The card shows what the skill is worth, so bonus levels from the book count
// in both blocks: a 5 with two bonus levels shows as 7, and the next point as
// 8.
TEST_F(SkillInspectPanelTest, LentLevelsAreCountedIntoBothBlocks) {
  Skill skill = IronBody();
  std::string rendered = RenderAt(skill, 5, /*bonus=*/2);
  EXPECT_NE(rendered.find("Level 7"), std::string::npos);
  EXPECT_NE(rendered.find("+70"), std::string::npos);  // DEF at 7, not at 5
  EXPECT_NE(rendered.find("Level 8"), std::string::npos);
  EXPECT_EQ(rendered.find("Level 5"), std::string::npos);
}

// An unopened skill gets no bonus, so there is still no current level to show,
// but the first point comes with the bonus, and its block shows the level it
// would really be.
TEST_F(SkillInspectPanelTest, AnUnlearnedSkillIsLentNothingUntilItIsOpened) {
  Skill skill = IronBody();
  std::string rendered = RenderAt(skill, 0, /*bonus=*/2);
  EXPECT_EQ(rendered.find("Level 0"), std::string::npos);
  EXPECT_EQ(rendered.find("Level 1"), std::string::npos);
  EXPECT_NE(rendered.find("Level 3"), std::string::npos);
  EXPECT_NE(rendered.find("+30"), std::string::npos);  // DEF at 3, not at 1
}

// Two effects of the master level on bonus levels. A skill marked for the 4th
// job can go two levels past it, and both blocks show that; an unmarked skill
// stops there, and a point that adds nothing gets no block.
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

  // The two levels past the master level come only from bonuses, never from
  // points, so a maxed skill has nothing left to spend on however far it can
  // go.
  std::string bought_out = RenderAt(marked, 20, /*bonus=*/0);
  EXPECT_NE(bought_out.find("Level 20"), std::string::npos);
  EXPECT_EQ(bought_out.find("Level 21"), std::string::npos);
}

// A player choosing a job has no points spent and none to spend, so "one more
// point" means nothing. The two ends of the skill are what they compare.
TEST_F(SkillInspectPanelTest, APreviewShowsTheFirstLevelAndTheLast) {
  Skill skill = IronBody();
  std::string rendered = RenderPreview(skill);
  EXPECT_NE(rendered.find("Level 1"), std::string::npos);
  EXPECT_NE(rendered.find("+10"), std::string::npos);  // DEF at level 1
  EXPECT_NE(rendered.find("Level 20"), std::string::npos);
  EXPECT_NE(rendered.find("+200"), std::string::npos);  // DEF at level 20
  EXPECT_EQ(rendered.find("Level 2 "), std::string::npos);
}

// The learned level is ignored in a preview, since the card describes the skill
// and nothing has been spent on it.
TEST_F(SkillInspectPanelTest, APreviewIgnoresWhatIsLearned) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 7, 0, SkillInspectPanel::kPreview);
  std::string rendered = RenderElement(panel.Render());
  EXPECT_EQ(rendered.find("Level 7"), std::string::npos);
  EXPECT_EQ(rendered.find("Level 8"), std::string::npos);
  EXPECT_NE(rendered.find("Level 1"), std::string::npos);
}

// A one-level skill's first and last level are the same, so the block isn't
// drawn twice.
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

// Damage per strike, the number of strikes, and the total; the total is what a
// player compares attack skills by.
TEST_F(SkillInspectPanelTest, SpellsOutAMultiLineSwing) {
  Skill skill = MakeLuckySeven();
  EXPECT_NE(RenderAt(skill, 1).find("72% x3 = 216%"), std::string::npos);
}

// Shuriken Burst's first hit on one enemy is far stronger than the spread that
// follows, and a page showing only the spread would make it look weak.
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
  // Piercing Arrow II's fragment bounces onto two of the enemies the arrow went
  // through, so the row saying which enemy has to count them.
  skill.set_lead_enemies(2);
  EXPECT_NE(RenderAt(skill, 1).find("(2 enemies)"), std::string::npos);
}

// A scattered attack shows its strike count and what hitting twice costs, and
// one whose count grows with the burns already applied shows the range instead
// of a single number the reader would rarely see twice.
TEST_F(SkillInspectPanelTest, SpellsOutAScatteredSwingAndTheBandItWidensTo) {
  Skill skill = MakeLuckySeven();
  Scatter* scatter = skill.mutable_scatter();
  scatter->set_hits(11);
  scatter->set_repeat_final_dmg_pct(-0.55);
  EXPECT_NE(RowIn(RenderAt(skill, 1), "Scattered",
                  "11 strikes, repeats at -55% Final Damage"),
            std::string::npos);

  scatter->set_hits_per_dot(1.0);
  scatter->set_max_hits(25);
  EXPECT_NE(RowIn(RenderAt(skill, 1), "Scattered",
                  "11-25 strikes, +1 per DoT stack, repeats at -55% Final "
                  "Damage"),
            std::string::npos);
}

// The page can't show a timer attack speed shortens: an ordinary attack's delay
// depends on the speed stage of the weapon held, so one number would be wrong
// for half the weapons that can use it. What one hit counts toward a counter is
// bookkeeping either way.
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
  // The turret's own rate is shown, since nothing the player holds changes it.
  EXPECT_NE(RowIn(rendered, "Turret", "4 enemies every 0.21s"),
            std::string::npos);
}

// The two timers the player controls (how many attacks they make and how many
// monsters they kill) are the only ones the page shows.
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

// A passive that affects one other skill names it, since nothing on that
// skill's own page would tell the player where the damage came from.
TEST_F(SkillInspectPanelTest, ABoostNamesTheSkillItReachesAcrossTo) {
  Skill mirage = IronBody();
  SkillBoost* boost = mirage.add_boost();
  boost->set_skill_name("Wind Arrow");
  boost->mutable_effect()->set_skill_pct(0.70);
  EXPECT_NE(
      RowIn(RenderAt(mirage, 1), "Boosts Wind Arrow", "+70% Damage per Strike"),
      std::string::npos);
  // Every field the boost grants is named, since one row holds them all. The
  // two kinds of damage are told apart: points on the skill's multiplier read
  // per strike, while a share of the character's damage reads plain.
  boost->mutable_effect()->set_ied_pct(0.20);
  boost->mutable_effect()->set_damage_pct(0.20);
  EXPECT_NE(RenderAt(mirage, 1).find(
                "+70% Damage per Strike, +20% Damage, +20% Ignore DEF"),
            std::string::npos);
  // Half a bonus writes no row, since it would be a name with nothing behind
  // it.
  Skill bare = IronBody();
  bare.add_boost()->set_skill_name("Wind Arrow");
  EXPECT_EQ(RenderAt(bare, 1).find("Boosts Wind Arrow"), std::string::npos);
}

// A buff that adds strikes to another skill says what the extra strikes deal
// and how many enemies they hit, since neither is on that skill's own page,
// which shows what it does without the buff.
TEST_F(SkillInspectPanelTest, ABoostSaysWhatTheHitsItHandsOverLandFor) {
  Skill barrage = IronBody();
  barrage.set_kind(SKILL_KIND_ACTIVE);
  barrage.mutable_buff()->set_duration_seconds(30.0);
  SkillBoost* spread = barrage.mutable_buff()->add_boost();
  spread->set_skill_name("Quad Star");
  SwingHit* stars = spread->add_extra_hit();
  stars->set_label("Spread");
  stars->set_casts(3);
  stars->set_lines(4);
  stars->set_max_enemies(4);
  stars->mutable_base()->set_skill_pct(3.79);
  EXPECT_NE(RowIn(RenderAt(barrage, 1), "Boosts Quad Star",
                  "Spread 379% x4 x3 = 4548% on 4 enemies"),
            std::string::npos);
}

// One skill with two ways of dealing damage: the attack the player presses, and
// the turret it leaves behind. Both belong on the one page, or the player buys
// twenty levels and sees only half of what they got.
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
  // Its damage is below the attack's, named as in its reach row above.
  EXPECT_NE(RowIn(rendered, "Turret", "66%"), std::string::npos);
  // A skill without a turret says nothing about one.
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Turret"), std::string::npos);
}

// The other timer a half can use: the character's own attacks. Inhuman Speed's
// afterimage shows its count where a turret shows seconds, and its five shots
// where an attack shows its lines.
TEST_F(SkillInspectPanelTest, AnAutoModeCanBeClockedByAttacksInstead) {
  Skill inhuman = MakeLuckySeven();
  AutoMode* afterimage = inhuman.add_auto_mode();
  afterimage->set_label("Afterimage");
  afterimage->set_attacks_per_cast(10);
  afterimage->set_casts(5);
  afterimage->set_max_enemies(1);
  afterimage->set_lines(3);
  afterimage->mutable_base()->set_skill_pct(8.80);

  std::string rendered = RenderAt(inhuman, 1);
  EXPECT_NE(RowIn(rendered, "Afterimage", "1 enemy every 10 attacks"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Afterimage", "880% x3 x5 = 13200%"),
            std::string::npos);
}

// A summon's value is how hard and how often it hits. The frequency goes on the
// row that already shows the skill's reach, so it needs no row of its own.
TEST_F(SkillInspectPanelTest, ASkillOnItsOwnClockStatesItWithItsReach) {
  Skill phoenix = MakeLuckySeven();
  phoenix.set_kind(SKILL_KIND_AUTO_ATTACK);
  phoenix.set_max_enemies(4);
  phoenix.set_cast_interval_seconds(1.71);
  std::string rendered = RenderAt(phoenix, 1);
  EXPECT_NE(RowIn(rendered, "Attacks", "4 enemies every 1.71s"),
            std::string::npos);
  EXPECT_EQ(rendered.find("Enemies Hit"), std::string::npos);

  // A key-down skill such as Arrow Blaster fires at a rate attack speed can't
  // change, so that rate can be shown here too.
  Skill blaster = MakeLuckySeven();
  blaster.set_max_enemies(4);
  blaster.set_base_delay_ms(120);
  blaster.set_fixed_delay(true);
  EXPECT_NE(RowIn(RenderAt(blaster, 1), "Attacks", "4 enemies every 0.12s"),
            std::string::npos);
  // An ordinary attack's delay depends on the weapon, so the page shows none.
  Skill swing = MakeLuckySeven();
  swing.set_base_delay_ms(660);
  EXPECT_EQ(RenderAt(swing, 1).find("every 0.66s"), std::string::npos);

  // One enemy is one enemy, and a skill that attacks when the player does keeps
  // the plain reach row.
  Skill sphere = phoenix;
  sphere.set_max_enemies(1);
  sphere.set_cast_interval_seconds(2.0);
  EXPECT_NE(RowIn(RenderAt(sphere, 1), "Attacks", "1 enemy every 2s"),
            std::string::npos);
  EXPECT_NE(RowIn(RenderAt(MakeLuckySeven(), 1), "Enemies Hit", "5"),
            std::string::npos);
}

// What a stun gives the attacks that consume it gets a row. That the attack
// stuns at all is for the description, so no row says it.
TEST_F(SkillInspectPanelTest, StatesWhatAStunLifts) {
  Skill orb = MakeLuckySeven();
  orb.add_tags(SKILL_TAG_LIGHTNING);
  orb.mutable_stun()->set_duration_seconds(4.0);
  orb.mutable_stun()->set_final_dmg_pct(0.12);
  orb.mutable_stun()->set_lifted_tag(SKILL_TAG_LIGHTNING);

  std::string rendered = RenderAt(orb, 1);
  EXPECT_NE(RowIn(rendered, "Element", "Lightning"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Stunned Enemies",
                  "+12% Final Damage from "
                  "Lightning"),
            std::string::npos);
  EXPECT_EQ(rendered.find("Stuns"), std::string::npos);

  // A bonus nothing consumes means nothing, so the row is dropped.
  orb.mutable_stun()->clear_lifted_tag();
  EXPECT_EQ(RenderAt(orb, 1).find("Stunned"), std::string::npos);
  orb.mutable_stun()->set_lifted_tag(SKILL_TAG_LIGHTNING);
  orb.mutable_stun()->clear_final_dmg_pct();
  EXPECT_EQ(RenderAt(orb, 1).find("Stunned"), std::string::npos);
}

// Freezing works like stunning: the element row names only the element, and a
// summon that freezes without an element gets no row.
TEST_F(SkillInspectPanelTest, TheElementRowNamesOnlyTheElement) {
  Skill beam = MakeLuckySeven();
  beam.add_tags(SKILL_TAG_ICE);
  beam.set_freeze_seconds(8.0);
  std::string rendered = RenderAt(beam, 1);
  EXPECT_NE(RowIn(rendered, "Element", "Ice"), std::string::npos);
  EXPECT_EQ(rendered.find("Freezes"), std::string::npos);

  Skill prey = MakeLuckySeven();
  prey.set_freeze_seconds(3.0);
  EXPECT_EQ(RenderAt(prey, 1).find("Freez"), std::string::npos);
}

// Shaped like Jupiter Thunder: an attack split into thirty strikes also lands
// its extra hits thirty times, so the current's row counts the shocks. An
// attack whose strikes land together doesn't: Sword Illusion's explosions have
// their own count and land once per attack.
TEST_F(SkillInspectPanelTest, ASequencedSwingCountsItsExtraHitPerStrike) {
  Skill orb = MakeLuckySeven();
  orb.set_lines(8);
  orb.set_casts(30);
  orb.set_cast_interval_ms(330);
  orb.mutable_base()->set_skill_pct(8.71);
  orb.clear_per_level();
  SwingHit* current = orb.add_extra_hit();
  current->set_label("Electric Current");
  current->set_lines(4);
  current->mutable_base()->set_skill_pct(5.13);

  std::string rendered = RenderAt(orb, 1);
  EXPECT_NE(RowIn(rendered, "Damage", "871% x8 x30 = 209040%"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Electric Current", "513% x4 x30 = 61560%"),
            std::string::npos);

  // A half with its own target count shows it, since the attack's count says
  // nothing about where this one lands.
  current->set_max_enemies(2);
  EXPECT_NE(
      RowIn(RenderAt(orb, 1), "Electric Current", "513% x4 x30 = 61560%, 2"),
      std::string::npos);

  // The same skill with its strikes landing together applies the current once.
  orb.clear_cast_interval_ms();
  EXPECT_NE(RowIn(RenderAt(orb, 1), "Electric Current", "513% x4 = 2052%"),
            std::string::npos);
}

// Divine Mark lands two hits at once and GMS prices them differently, so the
// page does too: a row each, with the normal-monster bonus under the half that
// has it rather than over both.
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
  // The hammer has no such bonus, so it gets no such row.
  EXPECT_EQ(rendered.find("Normal Monsters"), std::string::npos);

  // A hit certain to crit says so on the same row, since the two halves of
  // Raging Blow would otherwise show the same number twice.
  mark.mutable_extra_hit(0)->mutable_base()->set_crit_rate(1.00);
  EXPECT_NE(RowIn(RenderAt(mark, 1), "Explosion", "290% x5 = 1450% (crit)"),
            std::string::npos);
  // A crit rate below certain is shown next to the damage rather than replacing
  // it.
  mark.mutable_extra_hit(0)->mutable_base()->set_crit_rate(0.20);
  EXPECT_NE(RowIn(RenderAt(mark, 1), "Explosion", "290% x5 = 1450% (20% crit)"),
            std::string::npos);

  // Final damage only this half has goes on the same row too, since the
  // attack's own Final Damage row shows the character's stat. Assassinate shows
  // this for its finishing blow.
  mark.mutable_extra_hit(0)->mutable_base()->clear_crit_rate();
  mark.mutable_extra_hit(0)->mutable_base()->set_final_dmg_pct(0.50);
  EXPECT_NE(
      RowIn(RenderAt(mark, 1), "Explosion", "290% x5 = 1450% (+50% final)"),
      std::string::npos);
}

// An attack that repeats the same strike several times shows all three numbers
// in GMS's order (damage, lines, strikes), because the total alone hides which
// of the three changed.
TEST_F(SkillInspectPanelTest, StatesTheStrikeCountOfASwingThatRepeats) {
  Skill illusion = MakeLuckySeven();
  illusion.set_lines(4);
  illusion.set_casts(12);
  illusion.mutable_base()->set_skill_pct(1.30);
  illusion.clear_per_level();
  SwingHit* burst = illusion.add_extra_hit();
  burst->set_label("Explosion");
  burst->set_lines(5);
  burst->set_casts(5);
  burst->mutable_base()->set_skill_pct(2.60);

  std::string rendered = RenderAt(illusion, 1);
  EXPECT_NE(RowIn(rendered, "Damage", "130% x4 x12 = 6240%"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Explosion", "260% x5 x5 = 6500%"),
            std::string::npos);

  // A single strike doesn't show a count.
  illusion.clear_casts();
  EXPECT_NE(RowIn(RenderAt(illusion, 1), "Damage", "130% x4 = 520%"),
            std::string::npos);

  // Orbs consumed for final damage are shown as the number of orbs, since the
  // percentage depends on the character's own per-orb bonus.
  illusion.mutable_buff()->set_duration_seconds(8.0);
  illusion.mutable_buff()->mutable_base()->set_final_dmg_combo_orbs(6);
  EXPECT_NE(
      RowIn(RenderAt(illusion, 1), "Final Damage", "+6 Combo Orbs' worth"),
      std::string::npos);
}

// What a skill gives another that isn't damage uses the same sentence as the
// damage boost, one row per named skill, since two skills granted different
// things can't share a row. Several named skills get a heading, which saves
// each row the columns "Boosts" would take.
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
  EXPECT_NE(rendered.find("Boosts"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Divine Charge", "+1 Strike, +2 Enemies"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Blast", "+1 Strike"), std::string::npos);
  // One named skill needs no heading and says so on its own row.
  Skill alone = vessel;
  alone.mutable_boost()->DeleteSubrange(1, 1);
  EXPECT_NE(RowIn(RenderAt(alone, 10), "Boosts Divine Charge",
                  "+1 Strike, +2 Enemies"),
            std::string::npos);
  // Except for a boost node, whose card is only boosts and needs the columns
  // the word would take.
  Skill single_node = alone;
  single_node.set_v_node(V_NODE_KIND_BOOST);
  std::string node_alone = RenderAt(single_node, 10);
  EXPECT_EQ(node_alone.find("Boosts Divine Charge"), std::string::npos);
  EXPECT_NE(RowIn(node_alone, "Divine Charge", "+1 Strike, +2 Enemies"),
            std::string::npos);
  // The reach increases with level; the strike doesn't.
  EXPECT_NE(RenderAt(vessel, 1).find("+1 Strike, +1 Enemy"), std::string::npos);
  // A skill granting neither writes no row.
  EXPECT_EQ(RenderAt(IronBody(), 1).find("Boosts Divine Charge"),
            std::string::npos);
  // A grant for the empowered form only gets the form's own row: the Paladin's
  // Blast node improves Blast and Divine Brand separately.
  Skill node = IronBody();
  node.set_max_level(60);
  SkillBoost* swing = node.add_boost();
  swing->set_skill_name("Blast");
  swing->set_max_enemies(1);
  SkillBoost* brand = node.add_boost();
  brand->set_skill_name("Blast");
  brand->set_reach(BOOST_REACH_EMPOWERED);
  brand->mutable_effect()->set_crit_rate(0.05);
  std::string split = RenderAt(node, 60);
  EXPECT_NE(RowIn(split, "Blast", "+1 Enemy"), std::string::npos);
  EXPECT_NE(RowIn(split, "Empowered Blast", "+5% Critical Rate"),
            std::string::npos);
}

// A boost node shows its damage from level 1, one more enemy at 20 and ignored
// defence at 40: three grants for one skill on one row, with nothing shown for
// a tier the node hasn't reached.
TEST_F(SkillInspectPanelTest, AGatedBoostSaysNothingUntilItsLevel) {
  Skill node = IronBody();
  node.set_max_level(60);
  SkillBoost* damage = node.add_boost();
  damage->set_skill_name("Raging Blow");
  damage->mutable_effect()->set_final_dmg_pct(0.02);
  damage->mutable_effect_per_level()->set_final_dmg_pct(0.02);
  SkillBoost* reach = node.add_boost();
  reach->set_skill_name("Raging Blow");
  reach->set_min_level(20);
  reach->set_max_enemies(1);

  // Level 18 rather than 19: the card shows the next level beside the current
  // one, so at 19 the enemy added at 20 already appears.
  std::string under = RenderAt(node, 18);
  EXPECT_NE(RowIn(under, "Boosts Raging Blow", "+36% Final Damage"),
            std::string::npos);
  EXPECT_EQ(under.find("+1 Enemy"), std::string::npos);
  // In the order the tiers unlock, which is the order they are written in.
  EXPECT_NE(RowIn(RenderAt(node, 20), "Boosts Raging Blow",
                  "+40% Final Damage, +1 Enemy"),
            std::string::npos);

  // A timer granted to another skill shows the new timer, not a change to it.
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

  // A cooldown reduction shows the percentage; the seconds it amounts to are on
  // the named skill's own page.
  Skill cutter = IronBody();
  SkillBoost* hammer = cutter.add_boost();
  hammer->set_skill_name("Heaven's Hammer");
  hammer->set_cooldown_pct(0.30);
  EXPECT_NE(
      RowIn(RenderAt(cutter, 1), "Boosts Heaven's Hammer", "-30% Cooldown"),
      std::string::npos);

  // Points on the mark a skill leaves show as the tick damage they add, so the
  // row isn't mistaken for the strike that applies the mark.
  Skill eruption = IronBody();
  eruption.set_max_level(20);
  SkillBoost* fog = eruption.add_boost();
  fog->set_skill_name("Poison Mist");
  fog->set_dot_skill_pct(0.315);
  fog->set_dot_skill_pct_per_level(0.015);
  EXPECT_NE(RowIn(RenderAt(eruption, 20), "Boosts Poison Mist",
                  "+60% DoT Damage per Tick"),
            std::string::npos);

  // The burn's duration is shown in seconds, the only way it can be: GMS states
  // them, and the ladder they follow is on the named skill's own page.
  Skill aftermath = IronBody();
  SkillBoost* longer = aftermath.add_boost();
  longer->set_skill_name("Poison Mist");
  longer->set_dot_duration_seconds(6.0);
  EXPECT_NE(
      RowIn(RenderAt(aftermath, 1), "Boosts Poison Mist", "+6s DoT Duration"),
      std::string::npos);

  // A negative grant keeps its sign. Hurricane - Split Attack adds a second
  // arrow with a quarter off each one's damage, and a row that dropped the
  // reduction would make it look like a free strike.
  Skill split = IronBody();
  SkillBoost* hurricane = split.add_boost();
  hurricane->set_skill_name("Hurricane");
  hurricane->set_lines(1);
  hurricane->mutable_effect()->set_final_dmg_pct(-0.25);
  EXPECT_NE(RowIn(RenderAt(split, 1), "Boosts Hurricane",
                  "+1 Strike, -25% Final Damage"),
            std::string::npos);

  // A strike added to the hits an attack lands alongside itself is different
  // from one added to its lines, so the row says which.
  Skill fragment = IronBody();
  SkillBoost* arrow = fragment.add_boost();
  arrow->set_skill_name("Piercing Arrow II");
  arrow->set_lines(1);
  arrow->set_extra_hit_lines(1);
  EXPECT_NE(RowIn(RenderAt(fragment, 1), "Boosts Piercing Arrow II",
                  "+1 Strike, +1 Strike to its extra hits"),
            std::string::npos);
}

// A cooldown that never changes is shown once above the divider; one that
// shortens as the skill levels is part of what a point buys, so it goes in the
// level block with everything else a point buys.
TEST_F(SkillInspectPanelTest, AShorteningWaitIsReadAtTheLevel) {
  Skill hammer = MakeLuckySeven();
  hammer.set_max_level(30);
  hammer.set_cooldown_seconds(29.5);
  hammer.set_cooldown_seconds_per_level(-0.5);
  EXPECT_NE(RowIn(RenderAt(hammer, 1), "Cooldown", "29.5s"), std::string::npos);
  EXPECT_NE(RowIn(RenderAt(hammer, 30), "Cooldown", "15s"), std::string::npos);

  // Above the divider it would have to show one cooldown for all thirty levels,
  // so that row is only for skills whose cooldown really doesn't change.
  std::string rendered = RenderAt(hammer, 30);
  EXPECT_LT(rendered.find("Level 30"), rendered.find("Cooldown"));

  Skill flat = MakeLuckySeven();
  flat.set_cooldown_seconds(7.0);
  std::string plain = RenderAt(flat, 5);
  EXPECT_LT(plain.find("Cooldown"), plain.find("Level 5"));
}

// A growing Combo Orb count is split the same way as the shortening cooldown
// above, for the same reason: one number can't stand for twenty levels.
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

// Revenge of the Evil Eye: three attacks from one skill, where the auras land
// twenty strikes on three enemies while the volley beside them hits ten.
// Without a reach row for each half, the biggest number on the page is the one
// that hits the fewest enemies, and nothing says so.
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

// An arrow that gains damage as it travels shows the gain beside its reach,
// since the reach is how far it compounds, making them one fact.
TEST_F(SkillInspectPanelTest, APiercingSwingStatesItsGainBesideItsReach) {
  Skill arrow;
  arrow.set_name("Piercing Arrow");
  arrow.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(arrow, JOB_ADVANCEMENT_CROSSBOWMAN);
  arrow.set_max_level(20);
  arrow.set_max_enemies(6);
  arrow.set_lines(4);
  arrow.set_pierce_gain_pct(0.15);
  arrow.mutable_base()->set_skill_pct(0.92);
  EXPECT_NE(RowIn(RenderAt(arrow, 1), "Enemies Hit", "6, +15% each"),
            std::string::npos);

  // An attack that hits everything it reaches equally shows only how many.
  arrow.clear_pierce_gain_pct();
  std::string plain = RenderAt(arrow, 1);
  EXPECT_NE(RowIn(plain, "Enemies Hit", "6"), std::string::npos);
  EXPECT_EQ(plain.find("each"), std::string::npos);
}

// Empowered Arrows strengthens Piercing Arrow in two ways: a permanent bonus on
// every shot, and a bigger shot every fourth one. Both belong on the page, with
// the upgraded attack's reach, which is wider than the attack it replaces and
// which no other row would show.
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
  // Counted on the attack, the normal case, which needs no row.
  EXPECT_EQ(rendered.find("Marks"), std::string::npos);
  // A skill that upgrades nothing says nothing about upgrading.
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Empower"), std::string::npos);
}

// Two forms of one skill can't both be "Empowered Damage", so each uses the
// name of the attack it upgrades, and each shows what it lands alongside itself
// under its own row.
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
  // One "Empowers" row per form, each naming its own attack.
  EXPECT_NE(rendered.find("Every 4th Piercing"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Every 4th Snipe"), std::string::npos) << rendered;
  // Named for what each upgrades, not a shared label.
  EXPECT_NE(RowIn(rendered, "Piercing Arrow II", "427% x6"), std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Snipe", "494% x10"), std::string::npos)
      << rendered;
  EXPECT_EQ(rendered.find("Empowered Damage"), std::string::npos) << rendered;
  // The form's own second hit sits between the two attacks: under the one it
  // belongs to and above the one it doesn't.
  EXPECT_LT(RowIn(rendered, "Piercing Arrow II", "427%"),
            RowIn(rendered, "Fragment", "280%"))
      << rendered;
  EXPECT_LT(RowIn(rendered, "Fragment", "280%"),
            RowIn(rendered, "Snipe", "494%"))
      << rendered;
}

// Divine Judgment counts the marks each enemy has taken rather than the attacks
// landed, and its reach is whichever marks are complete, so the page shows the
// first and leaves out the second.
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

// Holy Fountain: both the heal and the interval change with level, so a page
// showing only one would say nothing about what a point bought.
TEST_F(SkillInspectPanelTest, StatesBothHalvesOfAFountain) {
  Skill fountain = IronBody();
  fountain.mutable_base()->set_regen_pct(0.13);
  fountain.mutable_per_level()->set_regen_pct(0.03);
  fountain.mutable_base()->set_regen_interval_seconds(7.5);
  fountain.mutable_per_level()->set_regen_interval_seconds(-0.5);

  // Both on one row: the heal grows and the interval shortens together, so
  // either alone understates every point after the first.
  EXPECT_NE(RowIn(RenderAt(fountain, 1), "HP Recovered", "13% every 7.5s"),
            std::string::npos);
  EXPECT_NE(RowIn(RenderAt(fountain, 10), "HP Recovered", "40% every 3s"),
            std::string::npos);
  // A skill with no fountain says nothing about one.
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("HP Recovered"),
            std::string::npos);

  // Holy Water heals one more time per step of INT, which is most of what its
  // points buy, so the row shows that, wrapping instead of dropping it.
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

// Holy Symbol's whole effect. It doesn't help in a fight, so without this row
// the skill would look like it grants nothing.
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

// Shadow Partner's whole effect. It is a share of each hit rather than a flat
// percentage, and the row has to say so: 70% behind a 210% line is 147%.
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

// A thrown meso is shown like every other attack on the page: what one line
// does, times how many lines a meso is worth. Pick Pocket's chance is its own
// row, because it is a chance rather than a gain.
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
  // No points set, so no second reading of the row above.
  EXPECT_EQ(RenderAt(explosion, 20).find("Normal Monsters"), std::string::npos);

  // GMS's points against a normal monster apply per line of the coin, so the
  // row shows the whole throw, like the attack's own pair.
  explosion.mutable_base()->set_normal_skill_pct(0.012);
  explosion.mutable_per_level()->set_normal_skill_pct(0.002);
  EXPECT_NE(RowIn(RenderAt(explosion, 20), "Normal Monsters", "105% x2 = 210%"),
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

// Dispel does something nothing in the game uses yet. The page says what it
// does instead of showing it as empty, and says the same at every level, since
// more points add nothing.
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

// The barrier's two parts: a reduction of the monster's attack that grows with
// level, and the switch a second skill turns on to make it work on bosses.
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

// Creeping Toxin upgrades its own attack, so there is no name to show, and its
// form has its own normal-monster value beside the damage.
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
  // A form with no reach of its own reaches as far as the attack it replaces,
  // so a row repeating that is noise.
  EXPECT_EQ(rendered.find("Empowered Enemies"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Empowered Damage", "200% x4 = 800%"),
            std::string::npos);
  EXPECT_NE(RowIn(rendered, "Empowered Normal", "250% x4 = 1000%"),
            std::string::npos);
}

// Beam Blade's bonus against normal monsters is added per line, so the row
// shows the whole attack. Shown as the bonus alone, it would read as 216 + 72
// against an attack that actually deals 432.
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

// Both healing fields are a share of the HP pool, not of anything beside them,
// and both say so.
TEST_F(SkillInspectPanelTest, TheHealingRowsNameWhatTheyAreAShareOf) {
  Skill skill = IronBody();
  skill.mutable_base()->set_hp_recover_pct(0.001);
  skill.mutable_base()->set_heal_pct(0.23);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(RowIn(rendered, "Heal per Attack", "+0.1% HP"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Heal", "+23% HP"), std::string::npos);
}

// The final strike of a hold heals per line of every strike, so its row reads
// like the damage row above: the total matters, since a share of the pool that
// large is what makes the cast a full heal.
TEST_F(SkillInspectPanelTest, AFinishThatHealsStatesTheWholeOfIt) {
  Skill skill = MakeLuckySeven();
  Channel* channel = skill.mutable_channel();
  channel->set_pulse_interval_ms(140);
  channel->set_max_pulses(26);
  channel->mutable_finish()->set_label("Shockwave");
  channel->mutable_finish()->set_lines(15);
  channel->mutable_finish()->set_casts(8);
  channel->mutable_finish()->mutable_base()->set_skill_pct(5.87);
  channel->mutable_finish()->mutable_base()->set_hp_recover_pct(0.10);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(RowIn(rendered, "Shockwave Heal", "+10% x15 x8 = 1200% HP"),
            std::string::npos);
}

// The pulse count is a maximum, not a guarantee, since a hold is released once
// its target is dead. Where charges limit it, the row also says what one charge
// buys.
TEST_F(SkillInspectPanelTest, ThePulseCountReadsAsACeiling) {
  Skill skill = MakeLuckySeven();
  Channel* channel = skill.mutable_channel();
  channel->set_pulse_interval_ms(140);
  channel->set_max_pulses(26);
  EXPECT_NE(RowIn(RenderAt(skill, 1), "Pulses", "Up to 26"), std::string::npos);

  channel->set_charge_seconds(11.0);
  channel->set_pulses_per_charge(4);
  EXPECT_NE(RowIn(RenderAt(skill, 1), "Pulses", "Up to 26, 4 per Charge"),
            std::string::npos);
}

// A skill that includes all of an earlier skill names it, or the two would look
// like they stack.
TEST_F(SkillInspectPanelTest, ASupersedingSkillNamesWhatItReplaces) {
  Skill skill = IronBody();
  EXPECT_EQ(RenderAt(skill, 1).find("Replaces"), std::string::npos);
  skill.set_supersedes_skill_name("Vessel of Light");
  EXPECT_NE(RowIn(RenderAt(skill, 1), "Replaces", "Vessel of Light"),
            std::string::npos);
}

// A value below a tenth of a percent gets a second decimal instead of a row
// reading "0%": Mortal Blow restores a hundredth of a percent per attack at
// level 1, and the point still bought something.
TEST_F(SkillInspectPanelTest, ATinyLeverKeepsASecondDecimal) {
  Skill skill = IronBody();
  skill.mutable_base()->set_hp_recover_pct(0.0001);
  EXPECT_NE(RowIn(RenderAt(skill, 1), "Heal per Attack", "+0.01% HP"),
            std::string::npos);
}

// A skill without an opening hit must not show that row at all.
TEST_F(SkillInspectPanelTest, NoOpeningHitRowWithoutOne) {
  Skill skill = MakeLuckySeven();
  EXPECT_EQ(RenderAt(skill, 1).find("Opening"), std::string::npos);
}

// A skill whose strike count grows has to say so where the damage is shown, or
// the page would show an attack the fight doesn't land.
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

// Reach and the required weapon don't change with level, so they sit above the
// level blocks instead of being repeated in both.
TEST_F(SkillInspectPanelTest, ShowsTheFactsThatHoldAtEveryLevel) {
  Skill skill = MakeLuckySeven();
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(rendered.find("Enemies Hit"), std::string::npos);
  EXPECT_NE(rendered.find("Required Weapon"), std::string::npos);
  EXPECT_NE(rendered.find("Claw"), std::string::npos);
}

// The scroll bar's column is part of the card, so a section rule crosses it.
// Stopping at the text's edge would leave a notch a column short of the border.
TEST_F(SkillInspectPanelTest, ASectionRuleReachesTheBorder) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  ftxui::Element card = panel.Render();
  // Drawn at the card's own width rather than the test screen's, since a window
  // stretched to fill the screen stretches its rules too, hiding the gap this
  // test looks for.
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  // Read from the pixel grid rather than ToString, which includes the card's
  // colour escapes and makes every line a different length in bytes.
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

// The width the card lays out in, borders included.
int CardColumns(const Skill& skill, int level) {
  SkillInspectPanel panel;
  panel.SetSkill(&skill, level, 0);
  ftxui::Element card = panel.Render();
  card->ComputeRequirement();
  return card->requirement().min_x;
}

// The card is as wide as the skill needs and no wider. A skill with little to
// say still gets the width GMS's sentences need, and a value too long for that
// widens the card instead of wrapping inside it.
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

// Two weapons with names as long as "One-Handed Sword" make an unusually long
// value. The card widens to fit the pair where it has room, and otherwise
// breaks the list between entries, since a requirement cut mid-weapon names the
// wrong weapon.
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

// A skill taking either hand's sword names the sword, not both versions; four
// spelled-out weapons would take four lines.
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

// Half a pair is still that specific weapon: a Spearman's sword-only skill
// would look like it takes every sword if the collapse didn't check both
// halves.
TEST_F(SkillInspectPanelTest, OneHandOfAPairKeepsItsFullName) {
  Skill skill = IronBody();
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_SWORD);
  EXPECT_NE(RenderAt(skill, 5).find("Two-Handed Sword"), std::string::npos);
}

// A grant that only applies with some of the skill's weapons names them in
// brackets, or it would look unconditional beside the rows that are.
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
// level-6 block below it, unlike every other row there.
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

// base + per_level * (L - 1) lands slightly under the round figure at some
// levels: Iron Body's seven steps of +1% come to 6.999999999999999, and its
// damage reduction to 3.4999999999999996. Truncating would show "6.9%" and
// "-3.4%" for a skill whose data clearly says 7 and 3.5.
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

// Magic Guard has a single field. The card still has to show what the skill
// does, or its levels sit over an empty block.
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
  // Every word survives the wrap, and none run past the border.
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

// Evil Eye Shock: attacks on its own timer every 12 seconds.
Skill MakeEvilEyeShock() {
  Skill skill;
  skill.set_name("Evil Eye Shock");
  skill.set_kind(SKILL_KIND_AUTO_ATTACK);
  PlaceIn(skill, JOB_ADVANCEMENT_SPEARMAN);
  skill.set_max_level(10);
  skill.set_max_enemies(10);
  skill.set_lines(6);
  skill.set_cast_interval_seconds(12.0);
  skill.set_description("Your Evil Eye shouts.");
  skill.mutable_base()->set_skill_pct(1.23);
  skill.mutable_per_level()->set_skill_pct(0.03);
  return skill;
}

// A skill that attacks on its own is something the character uses, not a bonus
// to their stats, and the panel has to say so.
TEST_F(SkillInspectPanelTest, TitlesASkillOnItsOwnClockActive) {
  EXPECT_NE(RenderAt(MakeEvilEyeShock(), 1).find("Active"), std::string::npos);
}

// It is an attack like any other, whatever triggers it, so its damage reads the
// same way, not as a skill with no effect.
TEST_F(SkillInspectPanelTest, ShowsTheDamageOfASkillOnItsOwnClock) {
  std::string out = RenderAt(MakeEvilEyeShock(), 1);
  EXPECT_NE(out.find("123% x6 = 738%"), std::string::npos);
  EXPECT_EQ(out.find("no effect"), std::string::npos);
}

// How often it triggers isn't the player's to change, and the seconds in the
// data aren't the seconds the game's speed scaling plays them at.
TEST_F(SkillInspectPanelTest, SaysNothingAboutHowOftenASkillFires) {
  EXPECT_EQ(RenderAt(MakeEvilEyeShock(), 1).find("Fires Every"),
            std::string::npos);
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Fires Every"),
            std::string::npos);
}

// A form that replaces every use is permanent. "Empowers" says that alone; a
// rate is what distinguishes the other case.
TEST_F(SkillInspectPanelTest, ReadsAFormOnEveryCastAsUnconditional) {
  Skill skill;
  skill.set_name("Mist Eruption");
  skill.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(skill, JOB_ADVANCEMENT_FIRE_POISON_MAGE);
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

// A skill with two forms has to say so on its card: what causes the wound, how
// long it lasts, and that the stronger attack hits one enemy with a longer
// cooldown.
TEST_F(SkillInspectPanelTest, ReadsBothFormsOfAWoundedSkill) {
  Skill skill;
  skill.set_name("Trickblade");
  skill.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(skill, JOB_ADVANCEMENT_SHADOWER);
  skill.set_max_level(30);
  skill.set_description("Summon your hidden friends.");
  skill.set_max_enemies(10);
  skill.set_lines(5);
  skill.set_cooldown_seconds(14.0);
  skill.mutable_base()->set_skill_pct(7.02);
  Wound* wound = skill.mutable_wound();
  wound->set_duration_seconds(10.0);
  wound->set_max_stacks(3);
  Wound::Source* source = wound->add_source();
  source->set_skill_name("Sonic Blow");
  source->set_stacks(3);
  WoundForm* form = wound->mutable_form();
  form->set_label("Trickblade: Finish");
  form->set_max_enemies(1);
  form->set_lines(7);
  form->set_casts(5);
  form->set_cooldown_seconds(20.0);
  form->mutable_base()->set_skill_pct(8.58);
  form->mutable_base()->set_crit_rate(1.0);

  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(rendered.find("Wound, 3 deep on one enemy"), std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Left By", "Sonic Blow 3"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Lasts", "10s"), std::string::npos);
  EXPECT_NE(rendered.find("Against a Full Wound"), std::string::npos)
      << rendered;
  // 858% seven times, five slashes, with its own reach and cooldown beside it.
  EXPECT_NE(RowIn(rendered, "Damage", "858% x7 x5 = 30030%"), std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Attacks", "1 enemy"), std::string::npos);
  EXPECT_NE(RowIn(rendered, "Cooldown", "20s"), std::string::npos) << rendered;
  EXPECT_NE(RowIn(rendered, "Critical Rate", "+100%"), std::string::npos);
}

// A damage-over-time effect is one row: the tick damage, how often it ticks and
// how long it lasts, since none of the three means anything alone.
TEST_F(SkillInspectPanelTest, ReadsADotAsOneRow) {
  Skill skill;
  skill.set_name("Flame Sweep");
  skill.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(skill, JOB_ADVANCEMENT_FIRE_POISON_MAGE);
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

  // A poison shows two things an attack's burn doesn't have: the chance to
  // apply it, and how high it stacks.
  burn->set_chance(0.32);
  burn->set_chance_per_level(0.02);
  burn->set_max_stacks(2.1666667);
  burn->set_max_stacks_per_level(0.1666667);
  std::string poison = RenderAt(skill, 10);
  EXPECT_NE(RowIn(poison, "DoT", "50% chance of 160% every 1s for"),
            std::string::npos);
  EXPECT_NE(poison.find("5s, stacks 3 times"), std::string::npos);
}

// The side strike an attack triggers reads like an attack of its own: damage
// per strike, times the count, and how often it triggers. Its normal-monster
// value follows, like the attack's.
TEST_F(SkillInspectPanelTest, ReadsASideStrikeWithItsWait) {
  Skill skill;
  skill.set_name("Showdown");
  skill.set_kind(SKILL_KIND_ATTACK);
  PlaceIn(skill, JOB_ADVANCEMENT_HERMIT);
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

// Neither half of Final Attack means anything alone, so they share a line.
TEST_F(SkillInspectPanelTest, ReadsFinalAttackAsOneFact) {
  Skill skill;
  skill.set_name("Final Attack");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_SPEARMAN);
  skill.set_max_level(20);
  skill.set_description("A chance at a second blow.");
  skill.mutable_base()->set_final_attack_chance(0.02);
  skill.mutable_base()->set_final_attack_pct(1.22);
  skill.mutable_per_level()->set_final_attack_chance(0.02);
  skill.mutable_per_level()->set_final_attack_pct(0.02);

  EXPECT_NE(RowIn(RenderAt(skill, 20), "Final Attack", "40% for 160%"),
            std::string::npos);

  // A mark that throws several stars shows them the way the attack above shows
  // its lines: damage per star, times the count, and the total.
  skill.mutable_base()->set_final_attack_lines(3);
  EXPECT_NE(RenderAt(skill, 20).find("40% for 160% x3 = 480%"),
            std::string::npos);
  skill.mutable_base()->clear_final_attack_lines();

  // Blizzard's own values, the widest this row gets: it names the one enemy it
  // hits, in full and on one line. The note is the only thing here long enough
  // to be cut off.
  skill.set_final_attack_max_enemies(1);
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
  PlaceIn(skill, JOB_ADVANCEMENT_SPEARMAN);
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

// The wizard's pair: magic attack beside ATT, and critical damage beside
// critical rate.
TEST_F(SkillInspectPanelTest, ReadsTheWizardsLevers) {
  Skill skill;
  skill.set_name("Freezing Crush");
  skill.set_kind(SKILL_KIND_PASSIVE);
  PlaceIn(skill, JOB_ADVANCEMENT_MAGICIAN);
  skill.set_max_level(10);
  skill.set_description("Sharpens what a critical hit is worth.");
  skill.mutable_base()->set_crit_dmg(0.005);
  skill.mutable_base()->set_magic_attack(3);

  std::string out = RenderAt(skill, 1);
  EXPECT_NE(RowIn(out, "Critical Damage", "+0.5%"), std::string::npos);
  EXPECT_NE(RowIn(out, "MATT", "+3"), std::string::npos);
}

// Built from the requirement rather than from separately typed text, so the
// wording and the rule the skills tab enforces can't drift apart.
TEST_F(SkillInspectPanelTest, SpellsOutWhatMustBeLearnedFirst) {
  Skill skill = IronBody();
  skill.set_name("Hyper Body");
  skill.mutable_required_skill()->set_skill_name("Iron Wall");
  skill.mutable_required_skill()->set_level(3);

  EXPECT_NE(RowIn(RenderAt(skill, 1), "Required Skill", "Iron Wall Lv. 3+"),
            std::string::npos);
}

// It is separated from the description by a rule, since what the skill does and
// what the player must do first are two different things.
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
  // A rule is drawn as a run of box-drawing horizontal lines, and the
  // description above it isn't.
  EXPECT_NE(lines[row - 1].find("──"), std::string::npos)
      << "no rule above: [" << lines[row - 1] << "]";
  EXPECT_NE(lines[row - 2].find("Boosts DEF"), std::string::npos)
      << "the description does not close where it should";
}

TEST_F(SkillInspectPanelTest, NoRequirementRowWhenThereIsNone) {
  EXPECT_EQ(RenderAt(IronBody(), 1).find("Required Skill"), std::string::npos);
  EXPECT_EQ(RenderAt(IronBody(), 1).find("Required Level"), std::string::npos);
}

// A label too wide for its column gets its own row instead of being cut into
// the value beside it, since it names a skill and half a skill name isn't one.
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

// A Hyper Skill's requirement is a level rather than a prerequisite skill. The
// title says what the skill is and nothing else; where the point came from is
// the book's concern, not the card's.
TEST_F(SkillInspectPanelTest, AHyperSkillNamesItsLevel) {
  Skill skill = IronBody();
  skill.set_hyper(true);
  skill.set_required_level(165);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(RowIn(rendered, "Required Level", "165"), std::string::npos);
  EXPECT_NE(rendered.find("Passive"), std::string::npos);
  EXPECT_EQ(rendered.find("Hyper"), std::string::npos);
}

// A weapon in hand and a skill already learned are the same kind of
// requirement, so they look alike: one after the other, with matching labels
// and values in one column.
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
  // Both rows have the same border prefix, so an equal byte offset is an equal
  // column.
  EXPECT_EQ(lines[weapon].find("Spear"), lines[prereq].find("Iron Wall"))
      << "the values do not share a column:\n[" << lines[weapon] << "]\n["
      << lines[prereq] << "]";
}

// The screen is sized from the whole book rather than the card under the
// cursor, so it stays still as the cursor moves: no card is larger than the
// size, one card is exactly that size, and the measuring order doesn't change
// the result.
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

// The two Dark Knight fields a plain row could show wrongly: one is based on
// what the player spent rather than what they have, and the other is a cooldown
// that shortens, so it has no plus sign.
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

// The multiplier on that share is a share of another skill, so its row names
// the skill rather than the character.
TEST_F(SkillInspectPanelTest, NamesTheSkillTheApShareIsMultipliedFrom) {
  Skill skill = IronBody();
  skill.clear_base();
  skill.clear_per_level();
  skill.mutable_base()->set_ap_stat_bonus_pct(0.10);
  skill.mutable_per_level()->set_ap_stat_bonus_pct(0.10);

  std::string rendered = RenderAt(skill, 20);
  EXPECT_NE(rendered.find("Maple Warrior"), std::string::npos);
  EXPECT_NE(rendered.find("+200%"), std::string::npos);
}

// A timed buff is half of what the skill does, and the half a player has to
// plan around, so the page shows it first, puts headings on both halves, and
// keeps the cooldown on one row with what a landed hit reduces it by.
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

  // What ends is shown first, and what is permanent under its own heading.
  std::vector<std::string> lines = Lines(rendered);
  int active = -1;
  int passive = -1;
  int keeps = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (lines[i].find("Active for 20s") != std::string::npos) {
      active = i;
    }
    // Unambiguous here because this skill's window title is "Active".
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

// A wound deals damage only while it lasts, so it is shown under the heading
// that says how long that is, not above it where a permanent aura would go.
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
  // Its damage and interval share a row, and its reach is the attack's own.
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

// A pulse with its own targets states them where every other own-clock half
// does, and its damage row then shows only the damage: amount, repeats,
// strikes, and how many ticks the duration gives.
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

// Storm of Arrows' two rows: a rain whose strikes grow with the number of
// enemies says so below its reach, cap included, and what the storm gives
// another skill is shown under the buff's own heading rather than a Boosts one.
TEST_F(SkillInspectPanelTest, ARainStatesWhatACrowdAndAnAlliedSkillAreWorth) {
  Skill storm = IronBody();
  storm.set_kind(SKILL_KIND_ACTIVE);
  Buff* buff = storm.mutable_buff();
  buff->set_duration_seconds(70.0);
  SkillBoost* boost = buff->add_boost();
  boost->set_skill_name("Enchanted Quiver");
  boost->set_final_attack_chance_mult(2.0);
  BuffPulse* rain = buff->mutable_pulse();
  rain->set_label("Arrow Rain");
  rain->set_cast_interval_seconds(5.0);
  rain->set_max_enemies(10);
  rain->set_lines(7);
  rain->set_casts(4);
  rain->set_lines_per_extra_enemy(2);
  rain->set_max_extra_lines(8);
  rain->mutable_base()->set_skill_pct(16.50);

  std::string rendered = RenderAt(storm, 1);
  EXPECT_NE(RowIn(rendered, "Arrow Rain", "1650% x7 x4 = 46200%"),
            std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Attacks", "10 enemies every 5s"),
            std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Per Extra Enemy", "+2 Strikes, up to +8"),
            std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Boosts Enchanted Quiver", "x2 Final Attack Rate"),
            std::string::npos)
      << rendered;
}

// Poison Chain's two values: the count mentions the extra explosion, and the
// ramp is shown as the damage it builds up to rather than the step alone, since
// the top is what a boss takes for most of a cast.
TEST_F(SkillInspectPanelTest, ARampedPulseStatesTheTopAndTheOneItGoesOutOn) {
  Skill chain = IronBody();
  chain.set_kind(SKILL_KIND_ACTIVE);
  Buff* buff = chain.mutable_buff();
  buff->set_duration_seconds(20.0);
  BuffPulse* blast = buff->mutable_pulse();
  blast->set_label("Poison Explosion");
  blast->set_cast_interval_seconds(2.0);
  blast->set_lines(5);
  blast->set_max_pulses(9);
  blast->set_max_repeats(5);
  blast->set_skill_pct_per_repeat(0.60);
  blast->set_final_repeat_strike(true);
  blast->mutable_base()->set_skill_pct(3.30);

  std::string rendered = RenderAt(chain, 1);
  EXPECT_NE(RowIn(rendered, "Poison Explosion",
                  "330% x5 = 1650%, 9 times, then once more every 2s"),
            std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Per Stack", "+60%, up to 630%"), std::string::npos)
      << rendered;
}

// Dark Lord's Omen's two values: the stars not tied to enemy count are shown as
// a total, since that is what a lone boss takes, and the final burst shows its
// own damage and reach.
TEST_F(SkillInspectPanelTest, APulseStatesItsFixedStrikesAndTheBurstItEndsOn) {
  Skill omen = IronBody();
  omen.set_kind(SKILL_KIND_ACTIVE);
  Buff* buff = omen.mutable_buff();
  buff->set_duration_seconds(12.0);
  BuffPulse* stars = buff->mutable_pulse();
  stars->set_label("Throwing Stars");
  stars->set_cast_interval_seconds(0.99);
  stars->set_lines(6);
  stars->set_max_enemies(7);
  stars->set_max_pulses(12);
  stars->mutable_base()->set_skill_pct(15.10);
  stars->mutable_base()->set_boss_pct(0.30);
  stars->mutable_fixed_strikes()->set_hits(7);
  SwingHit* burst = stars->mutable_final_strike();
  burst->set_label("Explosion");
  burst->set_lines(12);
  burst->set_max_enemies(12);
  burst->mutable_base()->set_skill_pct(34.30);

  std::string rendered = RenderAt(omen, 1);
  EXPECT_NE(RowIn(rendered, "Throwing Stars", "1510% x6 = 9060%, 12 times"),
            std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Attacks", "7 enemies every 0.99s"),
            std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Plus", "1510% x7 = 10570%, spread"),
            std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Explosion", "3430% x12 = 41160% on 12 enemies"),
            std::string::npos)
      << rendered;
  // The turret's fields are shown once, below everything they apply to, rather
  // than again beside the burst that also has them.
  EXPECT_NE(RowIn(rendered, "Boss Damage", "+30%"), std::string::npos)
      << rendered;
  EXPECT_EQ(rendered.find("Boss Damage", rendered.find("Boss Damage") + 1),
            std::string::npos)
      << rendered;
}

// A pulse tied to an attack has no timer to show, so the page names the attack
// instead: the player knows how often that is from the skill they are already
// using.
TEST_F(SkillInspectPanelTest, APulseRidingASwingNamesIt) {
  Skill instinct = IronBody();
  instinct.set_kind(SKILL_KIND_ACTIVE);
  Buff* buff = instinct.mutable_buff();
  buff->set_duration_seconds(20.0);
  BuffPulse* pulse = buff->mutable_pulse();
  pulse->set_label("Tear in Space");
  pulse->set_paced_by_skill_name("Raging Blow");
  pulse->set_lines(6);
  pulse->set_casts(3);
  pulse->set_max_enemies(6);
  pulse->mutable_base()->set_skill_pct(4.00);

  std::string rendered = RenderAt(instinct, 1);
  EXPECT_NE(RowIn(rendered, "Tear in Space", "400% x6 x3 = 7200%"),
            std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Attacks", "6 enemies with every Raging Blow"),
            std::string::npos)
      << rendered;
}

// The skill list tells active from passive by colour, so a skill that is both
// has to label its halves the same way, or the colours would mean one thing in
// the book and another on the page.
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

// A shared buff gives the party nothing of its own (everyone activates the same
// one in turn), so the heading is where the page has to say so.
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

// Throw Blasting's card: the charm has its own block with the count one
// activation provides, and shows both the count a press uses and the count the
// magazine refills to on its own, the passive half, which applies whether or
// not the buff is ever activated.
TEST_F(SkillInspectPanelTest, ASelfFillingLoadStatesWhatItSpendsAndPrepares) {
  Skill blasting = IronBody();
  blasting.set_kind(SKILL_KIND_ACTIVE);
  blasting.set_max_level(30);
  blasting.mutable_buff()->set_duration_seconds(60.0);
  Magazine* charm = blasting.mutable_buff()->mutable_magazine();
  charm->set_label("Explosive Charm");
  charm->set_charges(46);
  charm->set_charges_per_swing(3);
  charm->set_spent_by_every_swing(true);
  charm->set_recharge_seconds(10.0);
  charm->set_recharge_max(1);
  charm->set_max_enemies(6);
  charm->set_lines(5);
  charm->mutable_base()->set_skill_pct(4.94);
  charm->mutable_per_level()->set_skill_pct(0.19);

  std::string rendered = RenderAt(blasting, 30);
  EXPECT_NE(rendered.find("Explosive Charm, 46 charges"), std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Damage", "1045% x5"), std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Spends", "3 per attack"), std::string::npos)
      << rendered;
  EXPECT_NE(RowIn(rendered, "Prepared", "1 every 10s, passively"),
            std::string::npos)
      << rendered;
}

// A buff with forms has no single duration, and the player never chooses
// between them, so the page shows both, each heading its own damage.
TEST_F(SkillInspectPanelTest, ABuffWithFormsHeadsEachOfThem) {
  Skill sword = IronBody();
  sword.set_kind(SKILL_KIND_ACTIVE);
  Stance* mobile = sword.mutable_buff()->add_stance();
  mobile->set_label("Mobile");
  mobile->set_duration_seconds(20.0);
  mobile->mutable_pulse()->set_label("Mobile Sword");
  mobile->mutable_pulse()->set_cast_interval_seconds(0.81);
  mobile->mutable_pulse()->set_lines(12);
  mobile->mutable_pulse()->set_max_enemies(8);
  mobile->mutable_pulse()->mutable_base()->set_skill_pct(4.70);
  Stance* stationary = sword.mutable_buff()->add_stance();
  stationary->set_label("Stationary");
  stationary->set_duration_seconds(120.0);
  stationary->mutable_pulse()->set_label("Stationary Sword");
  stationary->mutable_pulse()->set_cast_interval_seconds(1.0);
  stationary->mutable_pulse()->set_lines(6);
  stationary->mutable_pulse()->set_max_enemies(8);
  stationary->mutable_pulse()->mutable_base()->set_skill_pct(2.52);

  std::string rendered = RenderAt(sword, 1);
  EXPECT_NE(rendered.find("Mobile for 20s"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Stationary for 120s"), std::string::npos)
      << rendered;
  EXPECT_NE(rendered.find("Mobile Sword"), std::string::npos) << rendered;
  EXPECT_NE(rendered.find("Stationary Sword"), std::string::npos) << rendered;
  // No heading of the buff's own, since one duration for two forms would be
  // wrong for one of them.
  EXPECT_EQ(rendered.find("Active for"), std::string::npos) << rendered;
  SkillInspectPanel panel;
  panel.SetSkill(&sword, 1, 0);
  EXPECT_EQ(LabelColor(panel.Render(), "Mobile for 20s"), kGold);
}

// An attack's ignored defence applies only during that attack, while the ATT it
// grants is permanent. Without headings the two rows would look alike. The same
// fields on a passive are permanent, so nothing heads them.
TEST_F(SkillInspectPanelTest, HeadsWhatRidesTheSwingApartFromWhatIsKept) {
  Skill mist = MakeLuckySeven();
  mist.mutable_base()->set_ied_pct(0.40);
  mist.mutable_base()->set_attack(20);

  std::vector<std::string> lines = Lines(RenderAt(mist, 1));
  int swing = -1;
  int ied = -1;
  int passive = -1;
  int att = -1;
  // The first of each: the card has a second level block repeating the same
  // things, and this test is about the order within one block.
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

// Every other skill has only one half and nothing to tell apart, so a heading
// would be a row repeating the window title.
TEST_F(SkillInspectPanelTest, ASkillWithOneHalfIsNotHeadedAtAll) {
  std::vector<std::string> lines = Lines(RenderAt(IronBody(), 5));
  for (int i = 1; i < static_cast<int>(lines.size()); ++i) {
    // Row 0 is the window's own title, which is "Passive" for this skill. What
    // must not appear is a heading row repeating it.
    EXPECT_EQ(lines[i].find("Passive"), std::string::npos) << lines[i];
    EXPECT_EQ(lines[i].find("Active for"), std::string::npos) << lines[i];
  }
}

// What a skill gives the party is its own section: those aren't the reader's
// numbers, and a page that mixed them into the passive half would suggest the
// reader gets both.
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
  // A skill with nothing to give the party gets no heading at all.
  EXPECT_EQ(RenderAt(IronBody(), 10).find("Your Party"), std::string::npos);
}

// Holy Magic Shell covers the caster and the party as one shield, so both
// halves of the card show what it blocks, and the boss row is named for what it
// covers, since a player wants to know which hits the shield can't absorb.
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

  // Both halves show it, and the party's is under the buff's own heading.
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

// Smokescreen's party half ends with the buff, so it is shown under the buff's
// own heading rather than at the bottom of the card, where it would look
// permanent.
TEST_F(SkillInspectPanelTest, ABuffsPartyHalfSitsUnderTheBuff) {
  Skill smoke = IronBody();
  smoke.set_name("Smokescreen");
  Buff* buff = smoke.mutable_buff();
  buff->set_duration_seconds(30.0);
  buff->mutable_base()->set_crit_dmg(0.02);
  buff->mutable_ally_base()->set_damage_taken_pct(0.01);
  buff->mutable_ally_per_level()->set_damage_taken_pct(0.01);

  // The first of each: the card shows every visible level, so the rows below
  // all repeat further down.
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

// Benediction's party rows: what the reader needs is the rate their INT gives
// the party and where it caps, not a total that depends on stats the card can't
// see.
TEST_F(SkillInspectPanelTest, APartyHalfStatesWhatTheCastersIntBuys) {
  Skill blessing = IronBody();
  blessing.set_name("Benediction");
  blessing.set_max_level(30);
  Buff* buff = blessing.mutable_buff();
  buff->set_duration_seconds(30.0);
  buff->mutable_base()->set_final_dmg_pct(0.33);
  buff->mutable_ally_base()->set_final_dmg_pct(0.06);
  AllyIntLever* split = buff->add_ally_int_lever();
  split->set_int_step(3000);
  split->mutable_effect()->set_final_dmg_pct(0.01);
  split->set_cap_is_party_share(true);
  AllyIntLever* capped = buff->add_ally_int_lever();
  capped->set_int_step(2000);
  buff->mutable_ally_base()->set_regen_pct(0.01);
  buff->mutable_ally_base()->set_regen_interval_seconds(2.0);
  capped->mutable_effect()->set_regen_pct(0.01);
  capped->mutable_cap()->set_regen_pct(0.10);

  std::string page = RenderAt(blessing, 30);
  EXPECT_NE(
      page.find(
          "+1% per 3,000 INT, up to your own 33% split between the party"),
      std::string::npos)
      << page;
  EXPECT_NE(page.find("1% every 2s, +1% per 2,000 INT up to 10%"),
            std::string::npos)
      << page;
}

// Parashock Guard pays its caster only for shielding someone. A player maxing
// it alone sees nothing change, so the heading on its own half has to say why,
// even on a page with no other section.
TEST_F(SkillInspectPanelTest, ASkillNeedingAPartySaysSoOverItsOwnHalf) {
  Skill guard = IronBody();
  guard.set_name("Parashock Guard");
  guard.set_requires_party(true);

  EXPECT_NE(RenderAt(guard, 10).find("Passive, in a Party"), std::string::npos);
  EXPECT_EQ(RenderAt(IronBody(), 10).find("in a Party"), std::string::npos);
}

// --- Scrolling a card too tall for the terminal ---

// A rendered row as plain text. The colour escapes, the window border and the
// scroll bar glyph are all escapes or multibyte, so stripping to ASCII leaves
// only the row's words, which is what a check of which row is on screen needs.
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

// The rows a card takes with no budget, borders included. Every scroll test
// measures the card rather than assuming a height, since the card grows
// whenever a skill gains a field, and a hardcoded number would fail quietly.
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

// A card too tall for its budget is cut and shows a scroll bar, instead of
// drawing rows off the bottom of the terminal.
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

// The row where the level blocks start, which is where the scrolling part of
// the card begins; everything above it stays on screen.
int FirstLevelRow(const std::vector<std::string>& lines) {
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    if (RowText(lines[i]).rfind(" Level ", 0) == 0) {
      return i;
    }
  }
  return -1;
}

// Down scrolls the level blocks, and the name, description and every-level
// facts above them stay still. There is no cursor, so the page itself moves.
TEST_F(SkillInspectPanelTest, ScrollingWalksTheLevelsUnderTheHead) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  int tall = CardRows(panel);
  std::vector<std::string> full = Lines(RenderElement(panel.Render()));
  int body = FirstLevelRow(full);
  ASSERT_GT(body, 1);
  panel.SetMaxRows(tall - 3);

  panel.ScrollBy(1);
  std::vector<std::string> lines = Lines(RenderElement(panel.Render()));
  EXPECT_EQ(RowText(lines[1]), RowText(full[1])) << "the head holds";
  EXPECT_EQ(RowText(lines[body - 1]), RowText(full[body - 1]));
  EXPECT_EQ(RowText(lines[body]), RowText(full[body + 1])) << "one row down";

  panel.ScrollBy(2);
  lines = Lines(RenderElement(panel.Render()));
  EXPECT_EQ(RowText(lines[body]), RowText(full[body + 3]));
}

// Stops at both ends instead of wrapping: with nothing selected, jumping from
// the bottom of a card to the top would look like a glitch.
TEST_F(SkillInspectPanelTest, ScrollingStopsAtBothEndsInsteadOfWrapping) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  int tall = CardRows(panel);
  std::vector<std::string> full = Lines(RenderElement(panel.Render()));
  int body = FirstLevelRow(full);
  ASSERT_GT(body, 1);
  panel.SetMaxRows(tall - 3);

  for (int i = 0; i < 50; ++i) {
    panel.ScrollBy(1);
  }
  std::vector<std::string> foot = Lines(RenderElement(panel.Render()));
  // Three rows of the levels are scrolled off the top, and the card's last row
  // is now the window's last row.
  EXPECT_EQ(RowText(foot[body]), RowText(full[body + 3]));
  EXPECT_EQ(RowText(foot[tall - 5]), RowText(full[tall - 2]));

  panel.ScrollBy(1);
  EXPECT_EQ(RowText(Lines(RenderElement(panel.Render()))[body]),
            RowText(foot[body]));

  for (int i = 0; i < 50; ++i) {
    panel.ScrollBy(-1);
  }
  EXPECT_EQ(RowText(Lines(RenderElement(panel.Render()))[body]),
            RowText(full[body]))
      << "back at the top";
}

// The rule between the two level blocks scrolls with them, so the bar crosses
// it instead of being cut in two; the rules in the top part reach the border.
TEST_F(SkillInspectPanelTest, TheBarCrossesTheRuleBetweenTheLevels) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  int tall = CardRows(panel);
  panel.SetMaxRows(tall - 1);
  ftxui::Element card = panel.Render();
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fit(card));
  ftxui::Render(screen, card);
  int right = screen.dimx() - 1;
  std::vector<int> rules;
  for (int y = 1; y + 1 < screen.dimy(); ++y) {
    if (screen.PixelAt(0, y).character == "\u251c") {
      rules.push_back(y);
    }
  }
  ASSERT_GE(rules.size(), 2u) << "a head rule and the one between the levels";
  EXPECT_EQ(screen.PixelAt(right, rules.front()).character, "\u2524")
      << "a head rule reaches the border";
  EXPECT_EQ(screen.PixelAt(right, rules.back()).character, "\u2502")
      << "the bar's column stands between the last rule and the border";
  EXPECT_NE(screen.PixelAt(right - 1, rules.back()).character, "\u2500")
      << "and the rule gives way to the bar";
}

// Nothing off screen, so no thumb, but the bar's column is kept, so a card
// doesn't widen by a column the moment it outgrows the terminal.
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

// The link skills' three effects that a plain field row can't show: the heal
// that triggers when nearly dead, the charges an attack builds up, and the
// window that only opens on a damaged enemy.
TEST_F(SkillInspectPanelTest, TheEmergencyHealStatesAllFourOfItsNumbers) {
  Skill skill;
  skill.set_name("Invincible Belief");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_max_level(9);
  skill.mutable_base()->set_emergency_heal_pct(0.20);
  skill.mutable_base()->set_emergency_heal_seconds(3.0);
  skill.mutable_base()->set_emergency_heal_hp_threshold(0.15);
  skill.mutable_base()->set_emergency_heal_cooldown_seconds(410.0);
  skill.mutable_per_level()->set_emergency_heal_pct(0.03);
  skill.mutable_per_level()->set_emergency_heal_cooldown_seconds(-40.0);

  std::string card = RenderAt(skill, 1);
  EXPECT_NE(RowIn(card, "Below 15% HP", "20% a second for 3s"),
            std::string::npos)
      << card;
  EXPECT_NE(RowIn(card, "Recovery Cooldown", "410s"), std::string::npos)
      << card;
  // The duration and threshold stay fixed while the heal amount and cooldown
  // grow.
  std::string maxed = RenderAt(skill, 9);
  EXPECT_NE(RowIn(maxed, "Below 15% HP", "44% a second for 3s"),
            std::string::npos)
      << maxed;
  EXPECT_NE(RowIn(maxed, "Recovery Cooldown", "90s"), std::string::npos)
      << maxed;
}

TEST_F(SkillInspectPanelTest, ARolledBuffSaysWhatTheRollIsAndWhatItNeeds) {
  Skill skill;
  skill.set_name("Empirical Knowledge");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_max_level(9);
  Buff& buff = *skill.mutable_buff();
  buff.set_duration_seconds(10.0);
  buff.set_raise_chance(0.15);
  buff.set_raise_chance_per_level(0.02);
  buff.set_stacks(3);
  buff.mutable_base()->set_damage_pct(0.01);
  buff.mutable_per_level()->set_damage_pct(0.005);

  std::string card = RenderAt(skill, 9);
  EXPECT_NE(card.find("31% a hit"), std::string::npos) << card;
  EXPECT_NE(RowIn(card, "Stacks", "3, each on its own clock"),
            std::string::npos)
      << card;
  // Each charge grants the whole amount, so the row has to say so.
  EXPECT_NE(RowIn(card, "Damage", "+5% each"), std::string::npos) << card;
}

TEST_F(SkillInspectPanelTest, ABuffNeedingAnAfflictedEnemySaysSo) {
  Skill skill;
  skill.set_name("Thief's Cunning");
  skill.set_kind(SKILL_KIND_PASSIVE);
  skill.set_max_level(9);
  skill.set_cooldown_seconds(20.0);
  Buff& buff = *skill.mutable_buff();
  buff.set_duration_seconds(10.0);
  buff.set_needs_afflicted_target(true);
  buff.mutable_base()->set_damage_pct(0.03);

  std::string card = RenderAt(skill, 1);
  EXPECT_NE(card.find("on a suffering enemy"), std::string::npos) << card;
  EXPECT_NE(RowIn(card, "Damage", "+3%"), std::string::npos) << card;
}

// A card opens at the top, however far down the last one was scrolled.
TEST_F(SkillInspectPanelTest, ResetScrollReturnsToTheHeadOfTheCard) {
  Skill skill = IronBody();
  SkillInspectPanel panel;
  panel.SetSkill(&skill, 5, 0);
  int tall = CardRows(panel);
  std::vector<std::string> full = Lines(RenderElement(panel.Render()));
  panel.SetMaxRows(tall - 3);

  int body = FirstLevelRow(full);
  ASSERT_GT(body, 1);

  panel.ScrollBy(2);
  ASSERT_NE(RowText(Lines(RenderElement(panel.Render()))[body]),
            RowText(full[body]));
  panel.ResetScroll();
  EXPECT_EQ(RowText(Lines(RenderElement(panel.Render()))[body]),
            RowText(full[body]));
}

// A card that measures its own width has to request its right margin. On this
// card the bar's column is that margin, which is why the chrome is three
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
