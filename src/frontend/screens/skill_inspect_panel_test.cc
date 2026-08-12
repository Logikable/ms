#include "src/frontend/screens/skill_inspect_panel.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

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

// Reckless Hunt sells DEF for damage. The price is half the skill, so the page
// prints it as a loss rather than dropping the row for not being a gain.
TEST_F(SkillInspectPanelTest, ShowsALeverTheSkillTakesAway) {
  Skill skill = MakeIronBody();
  skill.mutable_base()->set_def_pct(-0.07);
  skill.mutable_per_level()->set_def_pct(-0.02);
  EXPECT_NE(RenderAt(skill, 3).find("Defense           -11%"),
            std::string::npos);
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
}

// How fast a skill swings, how often its turret fires and what one hit of it
// is worth to a counter are all bookkeeping the player cannot act on, and the
// pacing band means the seconds would not even be the seconds they see.
TEST_F(SkillInspectPanelTest, KeepsASkillsTimingOffThePage) {
  Skill skill = MakeLuckySeven();
  skill.set_base_delay_ms(120);
  skill.set_fixed_delay(true);
  skill.set_hits_per_attack_count(7);
  skill.mutable_auto_mode()->set_cast_interval_seconds(0.21);
  skill.mutable_auto_mode()->set_max_enemies(4);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_EQ(rendered.find("0.12"), std::string::npos);
  EXPECT_EQ(rendered.find("0.21"), std::string::npos);
  EXPECT_EQ(rendered.find("Counts As"), std::string::npos);
  EXPECT_EQ(rendered.find("Turret Enemies"), std::string::npos);
}

// The one clock the player sets themselves, and the only one the page states.
TEST_F(SkillInspectPanelTest, SaysHowManyAttacksSetASkillOff) {
  Skill mirage = MakeLuckySeven();
  mirage.set_kind(SKILL_KIND_AUTO_ATTACK);
  mirage.set_attacks_per_cast(4);
  EXPECT_NE(RenderAt(mirage, 1).find("Fires Every       4 Attacks"),
            std::string::npos);
}

// A passive that reaches across to one other skill says which, since nothing
// on that skill's own page could tell the player where the damage came from.
TEST_F(SkillInspectPanelTest, ABoostNamesTheSkillItReachesAcrossTo) {
  Skill mirage = MakeIronBody();
  mirage.set_boosts_skill_name("Wind Arrow");
  mirage.mutable_base()->set_boosted_skill_pct(0.70);
  EXPECT_NE(RenderAt(mirage, 1).find("Boosts            Wind Arrow +70%"),
            std::string::npos);
  // Half a bargain writes no row: a name with no damage behind it. Matched on
  // the padded label, since this skill's own description opens with "Boosts".
  Skill bare = MakeIronBody();
  bare.set_boosts_skill_name("Wind Arrow");
  EXPECT_EQ(RenderAt(bare, 1).find("Boosts            "), std::string::npos);
}

// One skill with two ways of hurting things: the swing the player holds the
// key for, and the turret it leaves behind. Both halves belong on the one page,
// or the player buys twenty levels of a skill and sees half of what they got.
TEST_F(SkillInspectPanelTest, AnAutoModeStatesItsOwnHalfOfTheSkill) {
  Skill blaster = MakeLuckySeven();
  blaster.set_max_enemies(4);
  blaster.set_lines(1);
  blaster.mutable_base()->set_skill_pct(1.24);
  AutoMode* turret = blaster.mutable_auto_mode();
  turret->set_cast_interval_seconds(0.21);
  turret->set_max_enemies(4);
  turret->mutable_base()->set_skill_pct(0.66);

  std::string rendered = RenderAt(blaster, 1);
  EXPECT_NE(rendered.find("Damage            124%"), std::string::npos);
  EXPECT_NE(rendered.find("Turret Damage     66%"), std::string::npos);
  // A skill without one says nothing about a turret.
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Turret"), std::string::npos);
}

// Empowered Arrows strengthens Piercing Arrow twice over: a permanent bonus on
// every shot, and a bigger shot every fourth. Both halves belong on the page,
// and the upgraded swing's reach with them -- it is wider than the one it
// stands in for, which no other row could tell the player.
TEST_F(SkillInspectPanelTest, StatesBothHalvesOfAnEmpoweredSwing) {
  Skill arrows = MakeIronBody();
  arrows.set_boosts_skill_name("Piercing Arrow");
  arrows.mutable_base()->set_boosted_skill_pct(1.02);
  EmpoweredForm* form = arrows.mutable_empowered_form();
  form->set_casts_per_trigger(4);
  form->set_max_enemies(8);
  form->set_lines(6);
  form->mutable_base()->set_skill_pct(2.03);

  std::string rendered = RenderAt(arrows, 1);
  EXPECT_NE(rendered.find("Every 4th"), std::string::npos);
  EXPECT_NE(rendered.find("Piercing Arrow"), std::string::npos);
  EXPECT_NE(rendered.find("Empowered Enemies 8"), std::string::npos);
  EXPECT_NE(rendered.find("Empowered Damage  203% x6 = 1218%"),
            std::string::npos);
  EXPECT_NE(rendered.find("Piercing Arrow +102%"), std::string::npos);
  // A skill that upgrades nothing says nothing about upgrading.
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Empower"), std::string::npos);
}

// Holy Fountain: the pulse and the wait between pulses both move with the
// level, so a page showing one of them says nothing about what a point bought.
TEST_F(SkillInspectPanelTest, StatesBothHalvesOfAFountain) {
  Skill fountain = MakeIronBody();
  fountain.mutable_base()->set_regen_pct(0.13);
  fountain.mutable_per_level()->set_regen_pct(0.03);
  fountain.mutable_base()->set_regen_interval_seconds(7.5);
  fountain.mutable_per_level()->set_regen_interval_seconds(-0.5);

  std::string rendered = RenderAt(fountain, 1);
  EXPECT_NE(rendered.find("HP Recovered      13%"), std::string::npos);
  EXPECT_NE(rendered.find("Heals Every       7.5s"), std::string::npos);

  std::string maxed = RenderAt(fountain, 10);
  EXPECT_NE(maxed.find("HP Recovered      40%"), std::string::npos);
  EXPECT_NE(maxed.find("Heals Every       3s"), std::string::npos);
  // A skill with no fountain says nothing about one.
  EXPECT_EQ(RenderAt(MakeLuckySeven(), 1).find("Heals Every"),
            std::string::npos);
}

// Creeping Toxin upgrades its own attack, so there is no name to print -- and
// its form carries a normal-monster reading of its own beside the damage.
TEST_F(SkillInspectPanelTest, StatesAFormThatUpgradesItsOwnSkill) {
  Skill toxin = MakeIronBody();
  EmpoweredForm* form = toxin.mutable_empowered_form();
  form->set_casts_per_trigger(4);
  form->set_lines(4);
  form->mutable_base()->set_skill_pct(2.00);
  form->mutable_base()->set_normal_skill_pct(0.50);

  toxin.set_max_enemies(10);
  form->set_max_enemies(10);

  std::string rendered = RenderAt(toxin, 1);
  EXPECT_NE(rendered.find("Every 4th attack"), std::string::npos);
  // The form reaches exactly as far as the attack it stands in for, so a row
  // saying so twice is noise.
  EXPECT_EQ(rendered.find("Empowered Enemies"), std::string::npos);
  EXPECT_NE(rendered.find("Empowered Damage  200% x4 = 800%"),
            std::string::npos);
  EXPECT_NE(rendered.find("Empowered Normal  250% x4 = 1000%"),
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
  Skill skill = MakeIronBody();
  skill.mutable_base()->set_hp_recover_pct(0.001);
  skill.mutable_base()->set_heal_pct(0.23);
  std::string rendered = RenderAt(skill, 1);
  EXPECT_NE(rendered.find("Heal per Attack   +0.1% HP"), std::string::npos);
  EXPECT_NE(rendered.find("Heal              +23% HP"), std::string::npos);
}

// Every other skill in the game has none, so the row must not appear at all.
TEST_F(SkillInspectPanelTest, NoOpeningHitRowWithoutOne) {
  Skill skill = MakeLuckySeven();
  EXPECT_EQ(RenderAt(skill, 1).find("Opening"), std::string::npos);
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

// Two weapons with names as long as "One-Handed Sword" run past the value
// column, and a requirement cut off mid-weapon says the wrong thing.
TEST_F(SkillInspectPanelTest, ALongWeaponListWrapsRatherThanClipping) {
  Skill skill = MakeIronBody();
  skill.add_required_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_AXE);
  std::string rendered = RenderAt(skill, 5);
  EXPECT_NE(rendered.find("One-Handed Sword"), std::string::npos);
  EXPECT_NE(rendered.find("Two-Handed Axe"), std::string::npos);
  // Wrapped, so the whole list survives across two rows rather than one.
  for (const std::string& line : Lines(rendered)) {
    EXPECT_EQ(line.find("One-Handed Sword / Two-Handed Axe"),
              std::string::npos);
  }
}

// A skill taking either hand's sword names the sword, not the two of them --
// four spelled-out weapons would run the requirement down four lines.
TEST_F(SkillInspectPanelTest, BothHandsOfAWeaponReadAsTheWeapon) {
  Skill skill = MakeIronBody();
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
  Skill skill = MakeIronBody();
  skill.add_required_equip_type(EQUIP_TYPE_ONE_HANDED_BLUNT);
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_BLUNT);
  EXPECT_NE(RenderAt(skill, 5).find("Blunt"), std::string::npos);
  EXPECT_EQ(RenderAt(skill, 5).find("Handed"), std::string::npos);
}

// Half a pair is still that weapon: a Spearman's sword-only skill would read
// as taking every sword if the collapse did not check both halves.
TEST_F(SkillInspectPanelTest, OneHandOfAPairKeepsItsFullName) {
  Skill skill = MakeIronBody();
  skill.add_required_equip_type(EQUIP_TYPE_TWO_HANDED_SWORD);
  EXPECT_NE(RenderAt(skill, 5).find("Two-Handed Sword"), std::string::npos);
}

// A grant that only lands with some of the skill's weapons says which in
// brackets, or it reads as unconditional beside the rows that are.
TEST_F(SkillInspectPanelTest, AWeaponBonusNamesTheWeaponItNeeds) {
  Skill skill = MakeIronBody();
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
  Skill skill = MakeIronBody();
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
  Skill skill = MakeIronBody();
  std::string rendered = RenderAt(skill, 5);
  EXPECT_EQ(rendered.find("Enemies Hit"), std::string::npos);
  EXPECT_EQ(rendered.find("Required Weapon"), std::string::npos);
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

  EXPECT_NE(RenderAt(skill, 20).find("Final Attack      40% for 160%"),
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
  EXPECT_NE(out.find("STR               +6"), std::string::npos);
  EXPECT_NE(out.find("DEX               +6"), std::string::npos);
  EXPECT_NE(out.find("Mastery           14%"), std::string::npos);
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
  EXPECT_NE(out.find("Critical Damage   +0.5%"), std::string::npos);
  EXPECT_NE(out.find("MATT              +3"), std::string::npos);
}

// Built from the requirement rather than from a sentence typed beside it, so
// the wording and the rule the skills tab enforces cannot drift apart.
TEST_F(SkillInspectPanelTest, SpellsOutWhatMustBeLearnedFirst) {
  Skill skill = MakeIronBody();
  skill.set_name("Hyper Body");
  skill.mutable_required_skill()->set_skill_name("Iron Wall");
  skill.mutable_required_skill()->set_level(3);

  EXPECT_NE(RenderAt(skill, 1).find("Required Skill    Iron Wall Lv. 3+"),
            std::string::npos);
}

// And it is ruled off from the description. What the skill does and what the
// player must do first are two different claims.
TEST_F(SkillInspectPanelTest, RulesTheRequirementOffFromTheDescription) {
  Skill skill = MakeIronBody();
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
  EXPECT_EQ(RenderAt(MakeIronBody(), 1).find("Required Skill"),
            std::string::npos);
}

// A weapon in hand and a skill already learned are the same kind of claim, so
// they read alike: one after the other, labels alike, values in one column.
TEST_F(SkillInspectPanelTest, TheTwoRequirementsReadAlike) {
  Skill skill = MakeIronBody();
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

}  // namespace
}  // namespace ms
