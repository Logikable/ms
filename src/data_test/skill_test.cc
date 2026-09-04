// Checks the shipped skill catalog rather than any one function: every job's
// book has to cost exactly the SP that job earns, and that only holds if the
// data says so. Arithmetic done by hand in a textproto is the sort of thing
// that rots the moment a skill is added.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/character/character.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/frontend/panel_widths.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/screens/boss_fight_panel.h"
#include "src/frontend/screens/job_inspect_panel.h"
#include "src/frontend/screens/skill_inspect_panel.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/game_names.h"
#include "src/frontend/widgets/text_columns.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "src/testing/data_files.h"

namespace ms {
namespace {

// What a job stage's levels pay out, indexed by stage, with the advancement
// itself granting nothing. Levels 11-30 feed stage 1, 31-60 feed stage 2 and
// 61-100 feed stage 3, at 3 SP a level -- so 60, then 90, then 120. The 4th
// job's 101-140 pay 5 a level, so its book is 200.
constexpr int kSpByStage[] = {0, 60, 90, 120, 200};

// What a job's Hyper page costs, and so how many Hyper Skills it holds: one
// point at level 140 and every fifth level to 195, each buying one skill
// outright. A page short of twelve leaves a point that can never be spent.
constexpr int kHyperSkillsPerJob = 12;
// The rungs those points arrive on, which are the only levels a Hyper Skill
// may open at: unlocking between two of them is a skill that waits for a point
// it could have spent.
constexpr int kFirstHyperLevel = 140;
constexpr int kLastHyperLevel = 195;
constexpr int kHyperLevelStep = 5;

// Every value of an enum bar its UNSPECIFIED zero, taken from the descriptor
// rather than listed: a hardcoded list is one a new job joins only when
// somebody remembers to add it, and a job nobody remembers is a job whose book
// no test below ever looks at.
template <typename Enum>
std::vector<Enum> EveryValueOf(const google::protobuf::EnumDescriptor* desc) {
  std::vector<Enum> all;
  for (int i = 0; i < desc->value_count(); ++i) {
    if (desc->value(i)->number() != 0) {
      all.push_back(static_cast<Enum>(desc->value(i)->number()));
    }
  }
  return all;
}

// The books one job holds: their own and every one they climbed through.
std::set<JobAdvancement> BooksFor(Job job) {
  std::set<JobAdvancement> books;
  // To 5, not 4: a 5th job skill is written and named by the books below it
  // even though no character reaches the stage that buys it.
  for (int stage = 1; stage <= 5; ++stage) {
    JobAdvancement advancement = AdvancementForJobStage(job, stage);
    if (advancement != JOB_ADVANCEMENT_UNSPECIFIED) {
      books.insert(advancement);
    }
  }
  return books;
}

// One lever's value, whatever numeric type it is stored as, so a check can
// walk every lever a SkillEffect has without naming any of them. A field the
// message does not carry reads 0.
double LeverValue(const SkillEffect& effect,
                  const google::protobuf::FieldDescriptor* field) {
  // A Final Attack's percent is per strike, so on its own it says nothing
  // about what one is worth: three of 112% beat one of 147%.
  if (field->name() == "final_attack_pct") {
    return effect.final_attack_pct() * std::max(1, effect.final_attack_lines());
  }
  const google::protobuf::Reflection* reflection = effect.GetReflection();
  switch (field->cpp_type()) {
    case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
      return reflection->GetDouble(effect, field);
    case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
      return reflection->GetInt32(effect, field);
    default:
      return 0.0;
  }
}

// One pair of blocks at `level`, on the ladder every reader climbs:
// base + per_level * (L - 1).
SkillEffect EffectFrom(const SkillEffect& base, const SkillEffect& per,
                       int level) {
  SkillEffect at = base;
  const google::protobuf::Descriptor* levers = SkillEffect::descriptor();
  const google::protobuf::Reflection* reflection = at.GetReflection();
  for (int i = 0; i < levers->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = levers->field(i);
    double climbed =
        LeverValue(base, field) + LeverValue(per, field) * (level - 1);
    if (field->cpp_type() ==
        google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE) {
      reflection->SetDouble(&at, field, climbed);
    } else if (field->cpp_type() ==
               google::protobuf::FieldDescriptor::CPPTYPE_INT32) {
      reflection->SetInt32(&at, field, static_cast<int>(climbed));
    }
  }
  return at;
}

// What the skill is worth to the character holding it, and what it is worth to
// everybody beside them.
SkillEffect EffectAt(const Skill& skill, int level) {
  return EffectFrom(skill.base(), skill.per_level(), level);
}

SkillEffect AllyEffectAt(const Skill& skill, int level) {
  return EffectFrom(skill.ally_base(), skill.ally_per_level(), level);
}

// Whether some one job holds both `skill`'s book and the book of a skill
// called `name`. Every reference one skill makes to another is by display
// name, and a name no character can reach from where the reference is written
// is a grant nothing will ever read.
bool SameCharacterCanHold(const std::map<std::string, Skill>& skills,
                          const Skill& skill, const std::string& name) {
  for (Job job : EveryValueOf<Job>(Job_descriptor())) {
    std::set<JobAdvancement> books = BooksFor(job);
    if (books.count(skill.job_advancement()) == 0) {
      continue;
    }
    for (const std::pair<const std::string, Skill>& other : skills) {
      if (other.second.name() == name &&
          books.count(other.second.job_advancement()) > 0) {
        return true;
      }
    }
  }
  return false;
}

// Whether the character swings this skill: an attack, or a cast they spend a
// swing on. A skill that only puts a buff up is raised on its own clock and
// never takes the swing's place, so it is never the swing being named.
bool SpendsASwing(const Skill& skill) {
  return skill.kind() == SKILL_KIND_ATTACK ||
         (skill.kind() == SKILL_KIND_ACTIVE && skill.base().heal_pct() > 0.0);
}

// Whether the character stands there casting it: everything they press. A
// buff's cast costs them the time even though it takes no swing -- see
// BuffOption::cast_seconds -- so it has to say how long it is.
bool HasACastAnimation(const Skill& skill) {
  // A toggle is a switch rather than a cast: the fight never spends a swing on
  // it, and what it grants stands whether it was just thrown or thrown an hour
  // ago. See Skill.toggle.
  if (skill.toggle()) {
    return false;
  }
  return skill.kind() == SKILL_KIND_ATTACK || skill.kind() == SKILL_KIND_ACTIVE;
}

// What raising a buff costs, whatever its animation would be. GMS paces a
// skill sequence at a flat 120ms a skill, and a player puts their buffs up in
// one -- so the animation only ever plays for the casts a sequence will not
// take: a heal, and the invincibility skills GMS refuses to register.
constexpr int kBuffCastMs = 120;

// Whether the whole of what pressing it does is put a buff up. Told apart from
// a heal by the same test SpendsASwing uses: a heal is cast in place of the
// swing, where a buff goes up on a clock of its own.
bool RaisesOnlyABuff(const Skill& skill) {
  return skill.kind() == SKILL_KIND_ACTIVE && !SpendsASwing(skill);
}

std::map<std::string, Skill> LoadSkills() {
  return LoadTestData<Skill>("skills");
}

// Two fields no skill can leave unset. The advancement is what puts it in a
// tab and names the SP pool that buys it; the kind decides what it does in a
// fight and what tag opens its row in the book.
TEST(SkillDataTest, EverySkillNamesItsAdvancementAndItsKind) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    EXPECT_NE(entry.second.job_advancement(), JOB_ADVANCEMENT_UNSPECIFIED)
        << entry.first << " would be unreachable: no tab shows it and no SP "
        << "pool buys it";
    EXPECT_NE(entry.second.kind(), SKILL_KIND_UNSPECIFIED)
        << entry.first << " would list with no tag and do nothing";
  }
}

// The name of the swing being charged goes in the player's panel in a boss
// fight, which is the narrowest place a skill name is drawn. It wraps over the
// panel's rows; a name that needs one more of them is half a name on screen,
// and the fix is the panel rather than the skill -- so this fails where the
// width is decided.
TEST(SkillDataTest, EverySwingsNameFitsTheBossFightPanel) {
  // The border, and the column of clearance every panel keeps inside it.
  const int kRoom = kBossPanelWidth - 4;
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!SpendsASwing(skill)) {
      continue;
    }
    ++checked;
    std::vector<std::string> lines = WrapBalanced(skill.name(), kRoom);
    EXPECT_LE(static_cast<int>(lines.size()), kPlayerBarRows)
        << entry.first << " takes more rows than the arena has";
    // A word too long for the row is not wrapped at all: it overhangs, which
    // is a name with its head and tail cut off.
    for (const std::string& line : lines) {
      EXPECT_LE(static_cast<int>(line.size()), kRoom)
          << entry.first << " has a word too long for the arena";
    }
  }
  EXPECT_GT(checked, 0);
}

// The character panel's widest column is chosen for the longest name the game
// ships, so a longer one arriving has to move that number rather than sit cut
// on every terminal -- this is where it says so.
//
// A Hyper Skill is the exception. GMS names one for the skill it strengthens
// and then for what it does -- "Advanced Final Attack - Opportunity" -- and
// there is no shortening of that which is still GMS's name, so the row slides
// the name under the cursor instead. What it may not do is sit cut down to
// something another skill also cuts down to.
TEST(SkillDataTest, EverySkillNameFitsTheWidestCharacterPanel) {
  std::map<std::string, Skill> skills = LoadSkills();
  // The level column is measured over a whole book, so the widest level in
  // the catalog is what any name might be sitting beside. Combat Orders lends
  // at most two levels.
  int level_width = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    std::string text = std::to_string(entry.second.max_level()) + " (+2)";
    level_width = std::max(level_width, 1 + static_cast<int>(text.size()) + 1);
  }
  // A book long enough to scroll gives a column to the scroll bar, which is
  // the case a long name has to fit.
  int name_width =
      CharacterPanel::SkillNameWidth(level_width, kLeftColumnMax - 2 - 1);
  // Keyed by the cut name and holding the whole one, so the branches that
  // share a display name -- one Physical Training, one Advanced Final Attack
  // -- are one row rather than a collision.
  std::map<std::string, std::string> cut_to;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const std::string& name = entry.second.name();
    if (!entry.second.hyper()) {
      EXPECT_LE(TextColumns(name), name_width)
          << entry.first << " is wider than the panel ever gets";
    }
    std::string cut = name.substr(0, name_width);
    std::pair<std::map<std::string, std::string>::iterator, bool> seen =
        cut_to.emplace(cut, name);
    EXPECT_TRUE(seen.second || seen.first->second == name)
        << entry.first << " and \"" << seen.first->second
        << "\" both cut down to \"" << cut << "\"";
  }
}

// A tag is read by rules outside the skill, so an unset one is a skill quietly
// left out of a group it was meant to be in.
TEST(SkillDataTest, EveryTagNamesAGroup) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    for (int tag : entry.second.tags()) {
      EXPECT_NE(tag, SKILL_TAG_UNSPECIFIED)
          << entry.first << " carries a tag that names no group";
    }
  }
}

// The inspect screen has nothing else to say about a skill: its levers are
// numbers, and only this tells the player what the numbers are for.
TEST(SkillDataTest, EverySkillDescribesItself) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    EXPECT_FALSE(entry.second.description().empty())
        << entry.first << " would inspect to a blank panel";
  }
}

// Every book costs exactly what its own levels pay out, so reaching the end of
// a stage means having bought the whole of it -- neither short nor with points
// left over.
TEST(SkillDataTest, EveryBookCostsExactlyWhatItsLevelsPayOut) {
  std::map<int, int> cost_by_advancement;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    // A Hyper Skill is bought from a pool of its own -- see the page's own
    // test below. A Vengeance form is bought by buying the skill it stands in
    // for, so it is that skill's ladder either way.
    if (entry.second.hyper() || !entry.second.replaces_skill_name().empty()) {
      continue;
    }
    cost_by_advancement[entry.second.job_advancement()] +=
        entry.second.max_level();
  }
  // Every advancement below the 5th has to turn up, not just sum correctly:
  // the skills sit in a folder per job, and a folder that stopped being read
  // would drop one out of the map entirely and leave the rest to pass on their
  // own. The 5th jobs are written one at a time, so an empty one is expected
  // rather than a folder gone missing.
  for (JobAdvancement advancement :
       EveryValueOf<JobAdvancement>(JobAdvancement_descriptor())) {
    if (StageForAdvancement(advancement) >= 5) {
      continue;
    }
    EXPECT_TRUE(cost_by_advancement.count(advancement))
        << "advancement " << advancement << " has no skills at all";
  }
  for (const std::pair<const int, int>& entry : cost_by_advancement) {
    int stage = StageForAdvancement(static_cast<JobAdvancement>(entry.first));
    ASSERT_GT(stage, 0) << "advancement " << entry.first << " has no stage";
    // A stage past the SP table is one whose levels pay nothing yet: the 5th
    // job's band is undecided, so its book is not held to a total. Everything
    // below it is.
    if (stage >= static_cast<int>(sizeof(kSpByStage) / sizeof(kSpByStage[0]))) {
      continue;
    }
    EXPECT_EQ(entry.second, kSpByStage[stage])
        << "advancement " << entry.first << " costs " << entry.second
        << " against the " << kSpByStage[stage] << " its levels pay out";
  }
}

// A requirement may name a skill from a book below it -- Evil Eye Shock II
// waits on the Spearman's Evil Eye Shock -- because learned levels are keyed
// by display name and a character keeps every book they climbed through. What
// it may not do is name a book the same character could never hold, which is
// what would leave a skill permanently unbuyable.
TEST(SkillDataTest, EveryRequirementNamesAHoldableSkill) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::vector<Job> jobs = EveryValueOf<Job>(Job_descriptor());
  for (const std::pair<const std::string, Skill>& entry : skills) {
    if (!entry.second.has_required_skill()) {
      continue;
    }
    const SkillRequirement& required = entry.second.required_skill();
    // Some job that holds the requiring skill's book has to also hold a book
    // the required name is in, at a level it can be raised to.
    bool satisfiable = false;
    for (Job job : jobs) {
      std::set<JobAdvancement> books = BooksFor(job);
      if (books.count(entry.second.job_advancement()) == 0) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        if (other.second.name() != required.skill_name() ||
            books.count(other.second.job_advancement()) == 0) {
          continue;
        }
        EXPECT_LE(required.level(), other.second.max_level())
            << entry.first << " waits on a level of " << required.skill_name()
            << " that cannot be reached";
        satisfiable = true;
      }
    }
    EXPECT_GT(required.level(), 0) << entry.first;
    EXPECT_TRUE(satisfiable)
        << entry.first << " waits on \"" << required.skill_name()
        << "\", which no character holding it can learn";
  }
}

// skill_order is the whole of the list order now, so a book that skips a
// number or repeats one has two skills the player cannot tell apart the
// position of -- and a book that leaves it unset piles up at the top.
TEST(SkillDataTest, EveryBookIsNumberedOneThroughItsSize) {
  // Keyed by the PAIR: a Hyper page and the book it upgrades name the same
  // advancement but are two lists, so each is numbered from one.
  std::map<std::pair<int, bool>, std::map<int, std::string>> by_advancement;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    int order = entry.second.skill_order();
    EXPECT_GT(order, 0) << entry.first << " has no place in its book";
    // A Vengeance form takes the row of the skill it stands in for rather than
    // one of its own -- the test below holds it to that number.
    if (!entry.second.replaces_skill_name().empty()) {
      continue;
    }
    std::pair<std::map<int, std::string>::iterator, bool> added =
        by_advancement[{entry.second.job_advancement(), entry.second.hyper()}]
            .insert({order, entry.first});
    EXPECT_TRUE(added.second)
        << entry.first << " and " << added.first->second << " both sit at "
        << order << " of advancement " << entry.second.job_advancement();
  }
  for (const std::pair<const std::pair<int, bool>, std::map<int, std::string>>&
           book : by_advancement) {
    int expected = 1;
    for (const std::pair<const int, std::string>& entry : book.second) {
      EXPECT_EQ(entry.first, expected)
          << entry.second << " leaves a gap in advancement "
          << book.first.first;
      ++expected;
    }
  }
}

// A Vengeance form and the skill it stands in for are one row of one book:
// the same page, the same place on it, and the same ladder. Anything else and
// the swap would move the row, or leave the form at a level nobody bought.
TEST(SkillDataTest, EveryFormStandsInAnotherSkillsRow) {
  std::map<std::string, Skill> skills = LoadSkills();
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    if (skill.replaces_skill_name().empty()) {
      EXPECT_TRUE(skill.toggle_skill_name().empty())
          << entry.first << " names a switch and no skill to swap";
      continue;
    }
    ++checked;
    const Skill* replaced = nullptr;
    const Skill* toggle = nullptr;
    for (const std::pair<const std::string, Skill>& other : skills) {
      if (other.second.name() == skill.replaces_skill_name()) {
        replaced = &other.second;
      }
      if (other.second.name() == skill.toggle_skill_name()) {
        toggle = &other.second;
      }
    }
    ASSERT_NE(replaced, nullptr)
        << entry.first << " stands in for \"" << skill.replaces_skill_name()
        << "\", which no book holds";
    ASSERT_NE(toggle, nullptr)
        << entry.first << " waits on \"" << skill.toggle_skill_name()
        << "\", which no book holds";
    EXPECT_TRUE(toggle->toggle())
        << entry.first << " waits on " << skill.toggle_skill_name()
        << ", which is not a switch";
    EXPECT_EQ(skill.job_advancement(), replaced->job_advancement())
        << entry.first << " is listed on a page it never replaces a row on";
    EXPECT_EQ(skill.skill_order(), replaced->skill_order()) << entry.first;
    EXPECT_EQ(skill.max_level(), replaced->max_level())
        << entry.first << " climbs a ladder the skill it replaces does not";
    EXPECT_FALSE(skill.hyper())
        << entry.first << " is bought by buying another skill, not with a "
        << "Hyper point";
  }
  EXPECT_EQ(checked, 4) << "the Bishop's four, and nothing else so far";
}

// A switch is worth pressing only if something answers it.
TEST(SkillDataTest, EverySwitchRaisesSomething) {
  std::map<std::string, Skill> skills = LoadSkills();
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    if (!entry.second.toggle()) {
      continue;
    }
    ++checked;
    int raised = 0;
    for (const std::pair<const std::string, Skill>& other : skills) {
      if (other.second.toggle_skill_name() == entry.second.name()) {
        ++raised;
      }
    }
    EXPECT_GT(raised, 0) << entry.first << " switches nothing on";
  }
  EXPECT_EQ(checked, 1) << "Righteously Indignant, and nothing else so far";
}

// The Hyper page is a whole page or no page: a job with some of its twelve
// written would leave a point with nothing to buy, and a hyper hanging off an
// advancement that is not the 4th is one no page ever draws.
TEST(SkillDataTest, EveryHyperPageIsWholeAndOpensOnARung) {
  std::map<int, int> hypers_by_advancement;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!skill.hyper()) {
      EXPECT_EQ(skill.required_level(), 0)
          << entry.first << " gates on a level nothing but a hyper carries";
      continue;
    }
    ++hypers_by_advancement[skill.job_advancement()];
    EXPECT_EQ(skill.max_level(), 1)
        << entry.first << " is bought with one point and must cost one";
    EXPECT_EQ(StageForAdvancement(skill.job_advancement()), 4)
        << entry.first << " hangs off a book with no Hyper page";
    EXPECT_GE(skill.required_level(), kFirstHyperLevel) << entry.first;
    EXPECT_LE(skill.required_level(), kLastHyperLevel) << entry.first;
    EXPECT_EQ((skill.required_level() - kFirstHyperLevel) % kHyperLevelStep, 0)
        << entry.first << " opens at " << skill.required_level()
        << ", between two of the levels that pay a point";
  }
  for (const std::pair<const int, int>& entry : hypers_by_advancement) {
    EXPECT_EQ(entry.second, kHyperSkillsPerJob)
        << "advancement " << entry.first << " has " << entry.second
        << " Hyper Skills against the " << kHyperSkillsPerJob
        << " its points buy";
  }
}

// Only an attack splits what it grants, so a kept half on anything else is a
// half nobody reads -- every other kind keeps the whole of `base` already.
TEST(SkillDataTest, OnlyAnAttackStatesAKeptHalf) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.kind() == SKILL_KIND_ATTACK) {
      continue;
    }
    EXPECT_FALSE(skill.has_passive())
        << entry.first << " states a kept half nothing will read";
    EXPECT_FALSE(skill.has_passive_per_level())
        << entry.first << " states a kept half nothing will read";
  }
}

// An auto-attack naming no clock never fires, so a skill that means to be one
// and forgets to say when is a skill that silently does nothing. There are two
// clocks it can name -- seconds passed, or swings landed -- and it needs one.
TEST(SkillDataTest, EveryAutoAttackSaysWhenItFires) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    bool by_seconds = skill.cast_interval_seconds() > 0.0;
    bool by_swings = skill.attacks_per_cast() > 0;
    if (skill.kind() != SKILL_KIND_AUTO_ATTACK) {
      EXPECT_FALSE(by_seconds)
          << entry.first << " sets an interval it will never be asked for";
      EXPECT_FALSE(by_swings)
          << entry.first << " sets a swing count it will never be asked for";
      continue;
    }
    EXPECT_TRUE(by_seconds || by_swings) << entry.first << " would never fire";
    EXPECT_FALSE(by_seconds && by_swings)
        << entry.first << " runs on two clocks at once";
    EXPECT_GT(skill.base().skill_pct(), 0.0)
        << entry.first << " would fire for nothing";
  }
}

// A skill's own-clock half is a second attack out of one skill, so it needs
// both halves of what makes an attack: something to fire, and when. And a
// name, since the page has to tell one from another.
TEST(SkillDataTest, EveryAutoModeSaysWhenItFiresAndForHowMuch) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    for (const AutoMode& mode : entry.second.auto_mode()) {
      EXPECT_GT(mode.cast_interval_seconds(), 0.0)
          << entry.first << "'s own-clock half would never fire";
      EXPECT_GT(mode.base().skill_pct(), 0.0)
          << entry.first << "'s own-clock half would fire for nothing";
      EXPECT_FALSE(mode.label().empty())
          << entry.first << "'s own-clock half has no row to sit on";
    }
  }
}

// The strike a swing sets off beside itself needs all three of the things that
// make it one: damage, a wait, and a row to sit on. With no wait it would go
// out on every swing, which is what extra_hit already is.
TEST(SkillDataTest, EverySideStrikeSaysWhenItFiresAndForHowMuch) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (!entry.second.has_side_strike()) {
      continue;
    }
    const SideStrike& side = entry.second.side_strike();
    EXPECT_GT(side.cooldown_seconds(), 0.0)
        << entry.first << "'s side strike would go out on every swing";
    EXPECT_GT(side.base().skill_pct(), 0.0)
        << entry.first << "'s side strike would go out for nothing";
    EXPECT_FALSE(side.label().empty())
        << entry.first << "'s side strike has no row to sit on";
  }
}

// A held swing needs everything the hold is made of: a rate to pulse at, a
// count to stop at, a floor to be let go after, and a strike to end on. Its
// extra hits have to be that strike alone, since the fight reads everything
// past the first block of lines as what the hold ended with.
TEST(SkillDataTest, EveryHeldSwingSaysHowItPulsesAndWhatItEndsOn) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!skill.has_channel()) {
      continue;
    }
    const Channel& channel = skill.channel();
    EXPECT_EQ(skill.kind(), SKILL_KIND_ATTACK)
        << entry.first << " is held but is not a swing";
    EXPECT_GT(channel.pulse_interval_ms(), 0)
        << entry.first << " would pulse on no clock at all";
    EXPECT_GT(channel.max_pulses(), 0)
        << entry.first << "'s hold is worth no pulses";
    EXPECT_GT(skill.base().skill_pct(), 0.0)
        << entry.first << " would pulse for nothing";
    EXPECT_GT(skill.base_delay_ms(), channel.finish_delay_ms())
        << entry.first << " has no room to pulse inside its shortest hold";
    EXPECT_GT(channel.finish().base().skill_pct(), 0.0)
        << entry.first << "'s hold ends on nothing";
    EXPECT_FALSE(channel.finish().label().empty())
        << entry.first << "'s finish has no row to sit on";
    EXPECT_EQ(skill.extra_hit_size(), 0)
        << entry.first << " lands extra hits the fight would read as its "
        << "finish";
  }
}

// A scattered swing has to be a swing, throw strikes, and reach no further than
// it has strikes to throw -- an enemy no strike lands on is one the swing was
// never going to touch, so a reach past the count is the file misstating what
// the skill does.
TEST(SkillDataTest, EveryScatteredSwingReachesNoFurtherThanItsStrikes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!skill.has_scatter()) {
      continue;
    }
    EXPECT_EQ(skill.kind(), SKILL_KIND_ATTACK)
        << entry.first << " scatters strikes but is not a swing";
    EXPECT_GT(skill.scatter().hits(), 1)
        << entry.first << " scatters a single strike, which is every swing";
    EXPECT_LE(std::max(1, skill.max_enemies()), skill.scatter().hits())
        << entry.first << " reaches further than it has strikes to throw";
    // A cut of the whole would make a repeat worth nothing, and more than the
    // whole would have it healing the monster.
    EXPECT_GT(skill.scatter().repeat_final_dmg_pct(), -1.0)
        << entry.first << " takes the whole of a repeat strike away";
    EXPECT_LE(skill.scatter().repeat_final_dmg_pct(), 0.0)
        << entry.first << " pays a repeat strike more than the first";
  }
}

// A timed buff is worth nothing without the two halves that make it one: a
// stretch it stands for, and a wait for the next one. A buff with no wait
// would simply be a passive written the hard way.
TEST(SkillDataTest, EveryBuffStandsForAWhileAndWaitsForTheNextOne) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!skill.has_buff()) {
      continue;
    }
    EXPECT_GT(skill.buff().duration_seconds(), 0.0)
        << entry.first << "'s buff would never stand";
    // A buff its own swing lays waits for that swing rather than for a clock,
    // and the swing costs the fight a turn either way. Only the ones raised
    // for free need a wait to keep them from being permanent.
    if (skill.kind() == SKILL_KIND_ATTACK) {
      continue;
    }
    // The other way of waiting: a count of landed hits rather than seconds.
    // Nothing counts while it stands, so it cannot be permanent either.
    if (skill.buff().charge_lines() > 0) {
      EXPECT_EQ(skill.cooldown_seconds(), 0.0)
          << entry.first << "'s buff waits on hits and on a clock at once";
      continue;
    }
    EXPECT_GT(skill.cooldown_seconds(), 0.0)
        << entry.first << "'s buff would never be waited for";
    // A SHELL is ended by the blows it swallows rather than by its clock, so
    // its stated duration is a ceiling it rarely reaches. What keeps it from
    // being permanent is the count -- Divine Shield stands for ninety seconds
    // and ten blows, and the blows are gone long before the clock is.
    if (skill.buff().shield().hits() > 0.0 ||
        skill.buff().shield().hits_per_level() > 0.0) {
      continue;
    }
    EXPECT_GT(skill.cooldown_seconds(), skill.buff().duration_seconds())
        << entry.first << "'s buff is up for longer than it waits, so it is a "
        << "passive rather than a buff";
  }
}

// An empowered form has to say what it takes the place of, so it is nothing
// without a name to aim it -- and a form with no period or no damage is a swing
// that never lands or lands for nothing.
TEST(SkillDataTest, EveryEmpoweredFormSaysHowOftenAndForHowMuch) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    for (const EmpoweredForm& form : skill.empowered_form()) {
      // A form either names the skill it upgrades or upgrades the attack this
      // skill already is -- and a passive has no attack of its own, so it must
      // name one.
      EXPECT_TRUE(!form.skill_name().empty() ||
                  skill.kind() != SKILL_KIND_PASSIVE)
          << entry.first << "'s empowered form takes the place of nothing";
      EXPECT_GT(form.casts_per_trigger(), 0)
          << entry.first << "'s empowered form would never be swung";
      EXPECT_GT(form.base().skill_pct(), 0.0)
          << entry.first << "'s empowered form would be swung for nothing";
      // Marking enemies, the form goes exactly as far as the ones that came
      // due, so a reach beside it is a number nothing reads.
      EXPECT_FALSE(form.brands_each_enemy() && form.max_enemies() > 0)
          << entry.first << "'s empowered form states a reach it does not use";
    }
    // Two forms off one ladder have to say which is which, or the second would
    // land in the first one's place.
    if (skill.empowered_form_size() > 1) {
      std::set<std::string> targets;
      for (const EmpoweredForm& form : skill.empowered_form()) {
        EXPECT_FALSE(form.skill_name().empty())
            << entry.first << " carries several forms and one names no skill";
        EXPECT_TRUE(targets.insert(form.skill_name()).second)
            << entry.first << " upgrades \"" << form.skill_name() << "\" twice";
      }
    }
  }
}

// The two clocks answer different questions -- how long until this comes back,
// against how often this goes off by itself -- and a skill wanting both is a
// skill whose author meant one of them.
TEST(SkillDataTest, NoSkillNamesBothClocks) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.cooldown_seconds() <= 0.0) {
      continue;
    }
    EXPECT_EQ(entry.second.cast_interval_seconds(), 0.0)
        << entry.first << " both recharges and fires on its own clock";
    // A passive that raises a buff is the one exception: Divine Shield goes up
    // when the character is struck rather than when they press anything, so
    // the wait is the buff's and there is nothing to press.
    EXPECT_TRUE(entry.second.kind() != SKILL_KIND_PASSIVE ||
                entry.second.has_buff())
        << entry.first << " is never used, so it never recharges";
  }
}

// Anything the character presses takes as long as its own animation, so it has
// to say how long that is -- the attacks, and the casts, buff and heal alike.
// Nothing else does: the delay of a skill on its own clock is its cast
// interval, and a passive is never cast at all.
TEST(SkillDataTest, EverySwingSaysHowLongItTakes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (!HasACastAnimation(entry.second)) {
      EXPECT_EQ(entry.second.base_delay_ms(), 0)
          << entry.first << " sets a swing delay it will never be asked for";
      continue;
    }
    // A buff is raised from a sequence, which paces every skill in it alike --
    // so its own animation is never what it costs. See kBuffCastMs.
    if (RaisesOnlyABuff(entry.second)) {
      EXPECT_EQ(entry.second.base_delay_ms(), kBuffCastMs)
          << entry.first << " charges its animation for a buff a sequence "
          << "raises in " << kBuffCastMs << "ms";
      continue;
    }
    EXPECT_GT(entry.second.base_delay_ms(), 0)
        << entry.first << " would swing at the bare poke's speed";
    EXPECT_LE(entry.second.base_delay_ms(), 2000) << entry.first;
    // Loose bounds either side of every animation GMS has for a 1st or 2nd job
    // attack, to catch a figure entered in seconds or in frames. A key-down
    // skill is not an animation and is not held to them: GMS paces those in
    // the low hundreds of milliseconds, and Arrow Blaster is 120.
    if (entry.second.fixed_delay()) {
      EXPECT_GE(entry.second.base_delay_ms(), kTickMs) << entry.first;
      continue;
    }
    EXPECT_GE(entry.second.base_delay_ms(), 300) << entry.first;
  }
}

// The opening hit is a pair: a multiplier and how many times it strikes. One
// without the other is a figure nothing will read.
TEST(SkillDataTest, AnOpeningHitStatesBothOfItsHalves) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.lead_lines() <= 0 && skill.base().lead_pct() <= 0.0) {
      continue;
    }
    EXPECT_EQ(skill.kind(), SKILL_KIND_ATTACK)
        << entry.first << " opens with a hit it never swings";
    EXPECT_GT(skill.base().lead_pct(), 0.0) << entry.first;
    EXPECT_GT(skill.lead_lines(), 0) << entry.first;
  }
}

// A cast either takes the swing an attack would have had and heals with it, or
// puts a buff up on a clock of its own. One that does neither would be a row in
// the book that does nothing at all -- the encounter declines to offer such a
// skill, and this says none is written.
TEST(SkillDataTest, EveryCastDoesSomethingWithTheSwingItTakes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    // A toggle spends no swing at all -- what it does is stand switched on, so
    // its levers are read where a passive's are.
    if (entry.second.kind() != SKILL_KIND_ACTIVE || entry.second.toggle()) {
      continue;
    }
    EXPECT_TRUE(entry.second.base().heal_pct() > 0.0 ||
                entry.second.buff().duration_seconds() > 0.0)
        << entry.first << " spends a swing and does nothing with it";
  }
}

// Every weapon list a skill carries: the one gating the whole skill, then one
// per weapon bonus. The rules below hold of each list on its own.
std::vector<std::set<EquipType>> WeaponLists(const Skill& skill) {
  std::vector<std::set<EquipType>> lists(1);
  for (int i = 0; i < skill.required_equip_type_size(); ++i) {
    lists.back().insert(static_cast<EquipType>(skill.required_equip_type(i)));
  }
  for (const WeaponBonus& bonus : skill.weapon_bonus()) {
    lists.push_back({});
    for (int i = 0; i < bonus.required_equip_type_size(); ++i) {
      lists.back().insert(static_cast<EquipType>(bonus.required_equip_type(i)));
    }
  }
  return lists;
}

// A skill demanding a weapon says so on the inspect screen, and an unnamed
// weapon type leaves that line saying "Requires" and nothing else -- or, with
// only the one demand, drops it entirely. The type may well have no item yet;
// it still has to have a name.
TEST(SkillDataTest, EveryWeaponASkillDemandsHasAName) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    for (const std::set<EquipType>& list : WeaponLists(entry.second)) {
      for (EquipType type : list) {
        EXPECT_FALSE(FormatEquipType(type).empty())
            << entry.first << " demands a weapon with no name to print";
      }
    }
  }
}

// A bonus for a weapon the skill itself will not work with can never be read:
// the skill lapses whole before the bonus is ever reached.
// Every attack names the weapons it is swung with. The starter Sword and Long
// Sword are holdable by every class, so an ungated attack is a magician
// casting Energy Bolt with a longsword -- and, within a class, a Fighter
// swinging Brandish off a spear.
TEST(SkillDataTest, EveryAttackNamesTheWeaponsItNeeds) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.kind() != SKILL_KIND_ATTACK) {
      continue;
    }
    EXPECT_GT(entry.second.required_equip_type_size(), 0)
        << entry.first << " can be swung with anything the class can hold";
  }
}

// What each book's attacks are swung with. Written out rather than derived:
// which weapons a line masters is a decision, and a book added without one
// fails here until somebody makes it. The rogue's first book is the one that
// holds two sets -- Double Stab is a dagger and Lucky Seven a claw, which is
// the whole reason the branch exists.
struct BookWeapons {
  JobAdvancement book;
  std::set<EquipType> weapons;
};

const std::set<EquipType> kSwordAxe = {
    EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD,
    EQUIP_TYPE_ONE_HANDED_AXE, EQUIP_TYPE_TWO_HANDED_AXE};
const std::set<EquipType> kSwordBlunt = {
    EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD,
    EQUIP_TYPE_ONE_HANDED_BLUNT, EQUIP_TYPE_TWO_HANDED_BLUNT};
const std::set<EquipType> kSpears = {EQUIP_TYPE_SPEAR, EQUIP_TYPE_POLEARM};

std::vector<BookWeapons> ExpectedBookWeapons() {
  std::set<EquipType> every_warrior = kSwordAxe;
  every_warrior.insert(kSwordBlunt.begin(), kSwordBlunt.end());
  every_warrior.insert(kSpears.begin(), kSpears.end());
  return {
      {JOB_ADVANCEMENT_SWORDMAN, every_warrior},
      {JOB_ADVANCEMENT_FIGHTER, kSwordAxe},
      {JOB_ADVANCEMENT_CRUSADER, kSwordAxe},
      {JOB_ADVANCEMENT_HERO, kSwordAxe},
      {JOB_ADVANCEMENT_PAGE, kSwordBlunt},
      {JOB_ADVANCEMENT_WHITE_KNIGHT, kSwordBlunt},
      {JOB_ADVANCEMENT_PALADIN, kSwordBlunt},
      {JOB_ADVANCEMENT_SPEARMAN, kSpears},
      {JOB_ADVANCEMENT_BERSERKER, kSpears},
      {JOB_ADVANCEMENT_DARK_KNIGHT, kSpears},
      {JOB_ADVANCEMENT_ARCHER, {EQUIP_TYPE_BOW, EQUIP_TYPE_CROSSBOW}},
      {JOB_ADVANCEMENT_HUNTER, {EQUIP_TYPE_BOW}},
      {JOB_ADVANCEMENT_RANGER, {EQUIP_TYPE_BOW}},
      {JOB_ADVANCEMENT_BOW_MASTER, {EQUIP_TYPE_BOW}},
      {JOB_ADVANCEMENT_CROSSBOWMAN, {EQUIP_TYPE_CROSSBOW}},
      {JOB_ADVANCEMENT_SNIPER, {EQUIP_TYPE_CROSSBOW}},
      {JOB_ADVANCEMENT_MARKSMAN, {EQUIP_TYPE_CROSSBOW}},
      {JOB_ADVANCEMENT_ROGUE, {EQUIP_TYPE_DAGGER, EQUIP_TYPE_CLAW}},
      {JOB_ADVANCEMENT_ASSASSIN, {EQUIP_TYPE_CLAW}},
      {JOB_ADVANCEMENT_HERMIT, {EQUIP_TYPE_CLAW}},
      {JOB_ADVANCEMENT_NIGHT_LORD, {EQUIP_TYPE_CLAW}},
      {JOB_ADVANCEMENT_BANDIT, {EQUIP_TYPE_DAGGER}},
      {JOB_ADVANCEMENT_CHIEF_BANDIT, {EQUIP_TYPE_DAGGER}},
      {JOB_ADVANCEMENT_SHADOWER, {EQUIP_TYPE_DAGGER}},
      {JOB_ADVANCEMENT_MAGICIAN, {EQUIP_TYPE_STAFF}},
      {JOB_ADVANCEMENT_ICE_LIGHTNING_WIZARD, {EQUIP_TYPE_STAFF}},
      {JOB_ADVANCEMENT_ICE_LIGHTNING_MAGE, {EQUIP_TYPE_STAFF}},
      {JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE, {EQUIP_TYPE_STAFF}},
      {JOB_ADVANCEMENT_FIRE_POISON_WIZARD, {EQUIP_TYPE_STAFF}},
      {JOB_ADVANCEMENT_FIRE_POISON_MAGE, {EQUIP_TYPE_STAFF}},
      {JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE, {EQUIP_TYPE_STAFF}},
      {JOB_ADVANCEMENT_CLERIC, {EQUIP_TYPE_STAFF}},
      {JOB_ADVANCEMENT_PRIEST, {EQUIP_TYPE_STAFF}},
      {JOB_ADVANCEMENT_BISHOP, {EQUIP_TYPE_STAFF}},
  };
}

// A line masters what it masters: every attack in a book is swung with the
// book's own weapons, and nothing else in the class will swing it. A book
// whose attacks disagree with the table above is a mapping mistake.
TEST(SkillDataTest, EveryAttackIsSwungWithItsBooksWeapons) {
  std::map<JobAdvancement, std::set<EquipType>> expected;
  for (const BookWeapons& book : ExpectedBookWeapons()) {
    expected[book.book] = book.weapons;
  }
  std::set<JobAdvancement> seen;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.kind() != SKILL_KIND_ATTACK) {
      continue;
    }
    seen.insert(skill.job_advancement());
    std::map<JobAdvancement, std::set<EquipType>>::const_iterator it =
        expected.find(skill.job_advancement());
    ASSERT_NE(it, expected.end())
        << entry.first << "'s book names no weapons in this test";
    std::set<EquipType> weapons = WeaponLists(skill).front();
    // The rogue's book splits, so each of its attacks takes one of its two.
    if (skill.job_advancement() == JOB_ADVANCEMENT_ROGUE) {
      EXPECT_EQ(weapons.size(), 1u) << entry.first;
      for (EquipType type : weapons) {
        EXPECT_GT(it->second.count(type), 0u) << entry.first;
      }
      continue;
    }
    EXPECT_EQ(weapons, it->second)
        << entry.first << " is not swung with its book's weapons";
  }
  // Every book named above must still hold attacks, or the table is carrying
  // a line that no longer exists.
  for (const BookWeapons& book : ExpectedBookWeapons()) {
    EXPECT_GT(seen.count(book.book), 0u)
        << JobAdvancement_Name(book.book) << " has no attacks left";
  }
}

TEST(SkillDataTest, EveryWeaponBonusIsForAWeaponTheSkillAccepts) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    std::set<EquipType> accepted;
    for (int i = 0; i < skill.required_equip_type_size(); ++i) {
      accepted.insert(static_cast<EquipType>(skill.required_equip_type(i)));
    }
    for (const WeaponBonus& bonus : skill.weapon_bonus()) {
      EXPECT_GT(bonus.required_equip_type_size(), 0)
          << entry.first << " has a bonus for no weapon at all";
      // Nothing set is nothing granted, and the skill page prints a row per
      // lever -- so an empty bonus is an empty promise nobody can even read.
      EXPECT_GT(bonus.effect().ByteSizeLong(), 0u)
          << entry.first << " has a bonus that grants nothing";
      for (int i = 0; i < bonus.required_equip_type_size(); ++i) {
        EquipType type = static_cast<EquipType>(bonus.required_equip_type(i));
        EXPECT_TRUE(accepted.empty() || accepted.count(type) > 0)
            << entry.first << " bonuses a " << FormatEquipType(type)
            << " it will not work with";
      }
    }
  }
}

// A skill that wants a sword wants a sword in either hand, and the same goes
// for an axe -- naming only the half with items today is a skill that quietly
// stops working the day the other half gets one.
//
// The DEMAND only. A weapon bonus is the opposite thing: it exists to pay one
// weapon and not another, which is why High Paladin's ignored defence is on
// the blunt weapon alone.
// The job inspect screen sets a skill card beside the 35-wide book, and the
// card is now as wide as its widest label and value. No shipped skill may
// widen it past what the narrowest terminal the game lays out at leaves.
TEST(SkillDataTest, NoShippedCardOutgrowsTheJobInspectScreen) {
  std::map<std::string, Skill> skills = LoadSkills();
  const int kRoom = kLeftColumnMin + kRightColumnMin - kJobInspectBookWidth;
  std::string widest_name;
  int widest = 0;
  for (const auto& [stem, skill] : skills) {
    SkillInspectPanel panel;
    panel.SetSkill(&skill, 0, 0, SkillInspectPanel::kPreview);
    ftxui::Element card = panel.Render();
    card->ComputeRequirement();
    if (card->requirement().min_x > widest) {
      widest = card->requirement().min_x;
      widest_name = skill.name();
    }
  }
  EXPECT_LE(widest, kRoom) << widest_name << " asks for " << widest
                           << " columns beside the book";
}

TEST(SkillDataTest, AWeaponDemandCoversBothHands) {
  const std::pair<EquipType, EquipType> kPairs[] = {
      {EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD},
      {EQUIP_TYPE_ONE_HANDED_AXE, EQUIP_TYPE_TWO_HANDED_AXE},
      {EQUIP_TYPE_ONE_HANDED_BLUNT, EQUIP_TYPE_TWO_HANDED_BLUNT},
  };
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    std::set<EquipType> demanded = WeaponLists(entry.second).front();
    for (const std::pair<EquipType, EquipType>& pair : kPairs) {
      EXPECT_EQ(demanded.count(pair.first), demanded.count(pair.second))
          << entry.first << " takes one hand's " << FormatEquipType(pair.second)
          << " and not the other's";
    }
  }
}

// What the pile is worth grants nothing without a pile to hold, an ice swing
// to build it and a lightning swing to spend it. The pile need not be on the
// same skill -- Frost Clutch betters Freezing Crush's stack -- but all four
// must reach the same character.
TEST(SkillDataTest, FreezeStacksHaveBothHalvesAndBothElements) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::vector<Job> jobs = EveryValueOf<Job>(Job_descriptor());
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    if (skill.base().crit_dmg_per_freeze_stack() <= 0.0 &&
        skill.base().final_dmg_pct_per_freeze_stack() <= 0.0 &&
        skill.base().ied_pct_per_freeze_stack() <= 0.0) {
      continue;
    }
    bool pile = false;
    bool ice = false;
    bool lightning = false;
    for (Job job : jobs) {
      std::set<JobAdvancement> books = BooksFor(job);
      if (books.count(skill.job_advancement()) == 0) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        if (books.count(other.second.job_advancement()) == 0) {
          continue;
        }
        pile = pile || other.second.freeze_stack_cap() > 0;
        for (int i = 0; i < other.second.tags_size(); ++i) {
          ice = ice || other.second.tags(i) == SKILL_TAG_ICE;
          lightning = lightning || other.second.tags(i) == SKILL_TAG_LIGHTNING;
        }
      }
    }
    EXPECT_TRUE(pile) << entry.first << " is worth a pile that cannot be held";
    EXPECT_TRUE(ice) << entry.first << " has no ice swing to build the pile";
    EXPECT_TRUE(lightning) << entry.first
                           << " has no lightning swing to spend the pile";
  }
}

// Whether the enemy's condition is worth anything to a book depends on that
// book being able to put them in one. Two statuses count -- the ice a swing
// leaves and a burn -- and either will do, so Storm Magic asks its book for
// the freeze and Burning Magic asks its own for the burn.
TEST(SkillDataTest, AConditionIsBothInflictedAndRead) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::vector<Job> jobs = EveryValueOf<Job>(Job_descriptor());
  auto afflicts = [](const Skill& skill) {
    return skill.freeze_seconds() > 0.0 || skill.has_dot();
  };
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    bool counts = skill.base().final_dmg_pct_per_dot() > 0.0;
    if (skill.base().final_dmg_pct_when_afflicted() <= 0.0 && !counts) {
      continue;
    }
    bool inflicted = false;
    bool counted = false;
    for (Job job : jobs) {
      std::set<JobAdvancement> books = BooksFor(job);
      if (books.count(skill.job_advancement()) == 0) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        if (books.count(other.second.job_advancement()) == 0) {
          continue;
        }
        inflicted = inflicted || afflicts(other.second);
        counted = counted || other.second.dot_count_cap() > 0;
      }
    }
    EXPECT_TRUE(inflicted) << entry.first
                           << " reads a condition its book cannot inflict";
    if (counts) {
      EXPECT_TRUE(counted) << entry.first << " pays per burn with no count";
    }
  }
}

// Damage off the character's pool is a SWING's, added line by line. A passive
// has no lines to add it to and would grant nothing at all.
TEST(SkillDataTest, OnlyASwingPaysOutOfThePool) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.base().max_hp_damage_pct() <= 0.0) {
      continue;
    }
    EXPECT_EQ(entry.second.kind(), SKILL_KIND_ATTACK)
        << entry.first << " pays out of the pool without being swung";
  }
}

// A scar is worth nothing to a book that leaves none, and a scar left with no
// clock on it is never carried at all. Both halves have to reach one
// character, and they need not sit on one skill: Chance Attack reads the scar
// Scarring Sword leaves.
TEST(SkillDataTest, AScarIsBothLeftAndRead) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::vector<Job> jobs = EveryValueOf<Job>(Job_descriptor());
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const SkillEffect& base = entry.second.base();
    if (base.scar_chance() > 0.0) {
      EXPECT_GT(base.scar_seconds(), 0.0)
          << entry.first << " scars for no time at all";
    }
    if (base.final_dmg_pct_when_scarred() <= 0.0 &&
        base.enemy_attack_pct_when_scarred() <= 0.0) {
      continue;
    }
    bool scars = false;
    for (Job job : jobs) {
      std::set<JobAdvancement> books = BooksFor(job);
      if (books.count(entry.second.job_advancement()) == 0) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        scars = scars || (books.count(other.second.job_advancement()) > 0 &&
                          other.second.base().scar_chance() > 0.0);
      }
    }
    EXPECT_TRUE(scars) << entry.first << " reads a scar nothing leaves";
  }
}

// A freeze lands on what a swing reached, so a passive that touches nobody
// cannot leave one. Being ICE is NOT required: the tag marks a swing that
// feeds the I/L's Freeze Stacks, and Frostprey freezes as a bird instead.
TEST(SkillDataTest, OnlyASwingFreezes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.freeze_seconds() <= 0.0) {
      continue;
    }
    EXPECT_TRUE(DealsDamage(skill.kind()))
        << entry.first << " freezes what it never attacks";
  }
}

// An element is a mark on a SWING: it says what that swing does to the pile of
// Freeze Stacks, and a passive does nothing to it either way.
TEST(SkillDataTest, OnlyASwingCarriesAnElement) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    for (int i = 0; i < skill.tags_size(); ++i) {
      if (skill.tags(i) != SKILL_TAG_ICE &&
          skill.tags(i) != SKILL_TAG_LIGHTNING) {
        continue;
      }
      EXPECT_TRUE(DealsDamage(skill.kind()))
          << entry.first << " is marked with an element but never attacks";
    }
  }
}

// A pulse is its clock and its damage together: everything else written on one
// -- its reach, its strikes, the count it runs out at -- is read only where
// there is a clock to read it on.
TEST(SkillDataTest, EveryBuffPulseStatesTheClockItTicksOn) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const BuffPulse& pulse = entry.second.buff().pulse();
    if (pulse.cast_interval_seconds() > 0.0) {
      EXPECT_GT(pulse.base().skill_pct(), 0.0)
          << entry.first << " ticks and deals nothing";
      EXPECT_FALSE(pulse.label().empty())
          << entry.first << " bleeds under no name";
      continue;
    }
    EXPECT_EQ(pulse.lines(), 0) << entry.first << " strikes on no clock";
    EXPECT_EQ(pulse.casts(), 0) << entry.first << " strikes on no clock";
    EXPECT_EQ(pulse.max_enemies(), 0) << entry.first << " reaches on no clock";
    EXPECT_EQ(pulse.max_pulses(), 0) << entry.first << " runs out of no clock";
  }
}

// A per-orb bargain is worth the orbs times the bargain, so one without the
// other is a skill that says something and grants nothing. The two halves need
// not be the same skill -- Combo Synergy prices the orbs Combo Attack hands
// out -- but they do have to reach the same character.
TEST(SkillDataTest, EveryPerOrbBargainHasOrbsToBePaidAgainst) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::vector<Job> jobs = EveryValueOf<Job>(Job_descriptor());
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    bool prices_orbs = skill.base().attack_per_combo_orb() > 0 ||
                       skill.base().final_dmg_pct_per_combo_orb() > 0.0 ||
                       skill.base().boss_pct_per_combo_orb() > 0.0 ||
                       skill.base().def_per_combo_orb() > 0;
    if (!prices_orbs) {
      EXPECT_EQ(skill.combo_orbs(), 0)
          << entry.first << " hands out orbs nothing it grants is worth";
      continue;
    }
    bool paid = false;
    for (Job job : jobs) {
      std::set<JobAdvancement> books = BooksFor(job);
      if (books.count(skill.job_advancement()) == 0) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        paid = paid || (other.second.combo_orbs() > 0 &&
                        books.count(other.second.job_advancement()) > 0);
      }
    }
    EXPECT_TRUE(paid) << entry.first
                      << " prices Combo Orbs no character holding it carries";
  }
}

// A ladder counted in whole levels has to land on one. Its per-level step is a
// fraction that cannot be written exactly, so the top of the ladder sits a hair
// under the level it climbs to and is only carried over by the epsilon the
// floor adds -- shorten the literal in the data and the last level buys
// nothing. This is the test that says so.
TEST(SkillDataTest, ABonusLevelLadderEndsOnAWholeLevel) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    double step = skill.base().skill_level_bonus();
    if (step <= 0.0) {
      continue;
    }
    double top =
        step + skill.per_level().skill_level_bonus() * (skill.max_level() - 1);
    EXPECT_NEAR(top, std::round(top), 1e-9)
        << entry.first << " ends its ladder between two levels";
    EXPECT_GT(top, 1.0) << entry.first << " never climbs at all";
  }
}

// GMS lets granted levels carry a skill past its master level only in the 4th
// job, and only where that master level is 10 or more. Below it the skill
// simply stops, however many levels are on offer.
constexpr int kSmallestMasterLevelPastIt = 10;

// The 4th job skills Combat Orders does NOT carry, by file stem. It is GMS's
// mechanic and GMS names the skills it reaches, so a book holding one of these
// is not a mistake -- and naming them here keeps the check strict for the rest
// rather than weakening it to a direction that catches nothing.
//
// Infinity is ours rather than GMS's: it is the largest single lever any book
// grants, and two free levels of it were not worth handing over. Blood Money
// is GMS's own -- it says so on the skill.
//
// The last three are GMS master-level-1 passives, stretched to ten rungs here
// so the book has something to climb. GMS states one number for each and does
// not mark them, so there is no level above the top for a grant to reach.
const char* const kHeldToTheirMasterLevel[] = {
    "enchanted_quiver", "infinity",    "fire_poison_infinity",
    "bishop_infinity",  "blood_money", "blessed_harmony",
    "fervent_drain",    "frost_clutch"};

bool GmsHoldsItToTheMasterLevel(const std::string& stem) {
  for (const char* held : kHeldToTheirMasterLevel) {
    if (stem == held) {
      return true;
    }
  }
  return false;
}

// Which skills those are is a property of the catalog, so the catalog is what
// has to say it. Neither mistake announces itself: a 4th job skill missing the
// mark quietly stops two levels short of where its book goes, and one below
// the 4th job carrying it quietly grants two levels nobody wrote.
TEST(SkillDataTest, OnlyA4thJobSkillPassesItsMasterLevel) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    bool eligible = StageForAdvancement(skill.job_advancement()) == 4 &&
                    skill.max_level() >= kSmallestMasterLevelPastIt &&
                    !GmsHoldsItToTheMasterLevel(entry.first);
    if (eligible) {
      EXPECT_TRUE(skill.exceeds_master_level())
          << entry.first << " stops at " << skill.max_level()
          << " where Combat Orders carries a 4th job skill two past it";
      continue;
    }
    EXPECT_FALSE(skill.exceeds_master_level())
        << entry.first << " climbs past a master level GMS holds it to";
  }
}

// A strike is whole, so a rate too small to buy one before the master level
// is a lever that reads as a climb and never climbs.
TEST(SkillDataTest, ALineLadderBuysAStrikeBeforeTheMasterLevel) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.lines_per_level() <= 0.0) {
      continue;
    }
    EXPECT_GT(skill.lines(), 0)
        << entry.first << " climbs a strike count it never states";
    EXPECT_GT(SkillLinesAt(skill, skill.max_level()), skill.lines())
        << entry.first << " never buys a whole strike";
  }
}

// Damage belongs to the things that swing. The reverse no longer holds -- an
// active can carry a permanent grant, which is how GMS writes Phoenix and how
// LearnedPassives now reads it -- but a passive carrying a swing's damage is
// still data nothing will ever read.
TEST(SkillDataTest, APassiveCarriesNoSwingsDamage) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.kind() == SKILL_KIND_PASSIVE) {
      EXPECT_EQ(skill.base().skill_pct(), 0.0)
          << entry.first << " is a passive carrying a swing's damage";
      EXPECT_EQ(skill.base().normal_skill_pct(), 0.0)
          << entry.first << " is a passive carrying a swing's damage";
    }
  }
}

// The name an empowered form aims at has to be one the same character can
// learn: a form upgrading a skill its own holder can never hold upgrades
// nothing.
TEST(SkillDataTest, EveryEmpoweredTargetIsHoldable) {
  std::map<std::string, Skill> skills = LoadSkills();
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    for (const EmpoweredForm& form : skill.empowered_form()) {
      if (form.skill_name().empty()) {
        continue;
      }
      EXPECT_TRUE(SameCharacterCanHold(skills, skill, form.skill_name()))
          << entry.first << " empowers \"" << form.skill_name()
          << "\", which no character holding it can learn";
    }
  }
}

// The levers a SkillBoost::effect is read for. Anything else written there is
// data nothing will ever apply -- see SkillBoost::effect.
bool BoostEffectIsSupported(const SkillEffect& effect, std::string& unread) {
  static const std::set<std::string> kRead = {
      "skill_pct", "damage_pct",    "boss_pct",           "ied_pct",
      "crit_rate", "final_dmg_pct", "final_attack_chance"};
  std::vector<const google::protobuf::FieldDescriptor*> set;
  effect.GetReflection()->ListFields(effect, &set);
  for (const google::protobuf::FieldDescriptor* field : set) {
    std::string name(field->name());
    if (kRead.find(name) == kRead.end()) {
      unread = name;
      return false;
    }
  }
  return true;
}

// Whether any skill in the catalog upgrades `name` with an empowered form --
// the form may be granted by a skill of its own, so the target's own page
// never says so.
bool HasEmpoweredForm(const std::map<std::string, Skill>& skills,
                      const std::string& name) {
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    for (const EmpoweredForm& form : skill.empowered_form()) {
      const std::string& target =
          form.skill_name().empty() ? skill.name() : form.skill_name();
      if (target == name) {
        return true;
      }
    }
  }
  return false;
}

// Whether `name` lands anything beside its own lines: an opening hit, an extra
// hit, or either of them in an empowered form of it.
bool LandsASecondHit(const std::map<std::string, Skill>& skills,
                     const std::string& name) {
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    if (skill.name() == name &&
        (skill.base().lead_pct() > 0.0 || skill.extra_hit_size() > 0)) {
      return true;
    }
    for (const EmpoweredForm& form : skill.empowered_form()) {
      const std::string& target =
          form.skill_name().empty() ? skill.name() : form.skill_name();
      if (target == name &&
          (form.base().lead_pct() > 0.0 || form.extra_hit_size() > 0)) {
        return true;
      }
    }
  }
  return false;
}

// A boost has to name a skill the same character can hold, and hand it
// something: strikes, reach, a clock, or a lever that skill alone carries.
TEST(SkillDataTest, EverySkillBoostNamesAHoldableSkill) {
  std::map<std::string, Skill> skills = LoadSkills();
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    for (const SkillBoost& boost : entry.second.boost()) {
      EXPECT_FALSE(boost.skill_name().empty())
          << entry.first << " grants strikes to nobody";
      EXPECT_TRUE(
          boost.lines() > 0 || boost.extra_hit_lines() > 0 ||
          boost.max_enemies() > 0 || boost.max_enemies_per_level() > 0.0 ||
          boost.attacks_per_cast() > 0 || boost.cooldown_pct() > 0.0 ||
          boost.dot_skill_pct() != 0.0 || boost.dot_duration_seconds() != 0.0 ||
          boost.buff_duration_seconds() != 0.0 || boost.shield_hits() != 0.0 ||
          boost.shield_boss_damage_taken_pct() != 0.0 || boost.has_effect())
          << entry.first << " names " << boost.skill_name()
          << " and hands it nothing";
      // A share of a wait, so a whole one would leave the skill with no wait
      // at all and a figure above one would run it backwards.
      EXPECT_LT(boost.cooldown_pct(), 1.0)
          << entry.first << " takes the whole of " << boost.skill_name()
          << "'s wait away";
      // A per-level step with no level-1 value behind it is half a lever.
      EXPECT_FALSE(boost.has_effect_per_level() && !boost.has_effect())
          << entry.first << " climbs a lever it never grants";
      EXPECT_FALSE(boost.dot_skill_pct_per_level() != 0.0 &&
                   boost.dot_skill_pct() == 0.0)
          << entry.first << " climbs a burn it never lifts";
      EXPECT_FALSE(boost.dot_duration_seconds_per_level() != 0.0 &&
                   boost.dot_duration_seconds() == 0.0)
          << entry.first << " climbs a burn's clock it never lengthens";
      std::string unread;
      EXPECT_TRUE(BoostEffectIsSupported(boost.effect(), unread))
          << entry.first << " boosts " << boost.skill_name() << " with "
          << unread << ", which no swing reads";
      EXPECT_TRUE(BoostEffectIsSupported(boost.effect_per_level(), unread))
          << entry.first << " climbs " << boost.skill_name() << "'s " << unread
          << ", which no swing reads";
      ++checked;
      EXPECT_TRUE(
          SameCharacterCanHold(skills, entry.second, boost.skill_name()))
          << entry.first << " grants strikes to \"" << boost.skill_name()
          << "\", which no character holding it can learn";
      // A strike for hits the named skill does not land is a strike nobody
      // takes. Its form's count, since a form lands its parent's second hits
      // as often as its own -- Snipe's mark is the empowered shot's.
      if (boost.extra_hit_lines() > 0) {
        EXPECT_TRUE(LandsASecondHit(skills, boost.skill_name()))
            << entry.first << " adds a strike to " << boost.skill_name()
            << "'s second hits, which it does not land";
      }
      // Following a skill into a form it does not have reaches nothing.
      if (boost.reaches_empowered_form()) {
        EXPECT_TRUE(HasEmpoweredForm(skills, boost.skill_name()))
            << entry.first << " follows " << boost.skill_name()
            << " into an empowered form, which nothing gives it";
      }
      // A clock handed to a skill that is not on one is a clock nothing reads,
      // and so is a share off a wait the named skill does not have.
      for (const std::pair<const std::string, Skill>& target : skills) {
        if (target.second.name() != boost.skill_name()) {
          continue;
        }
        if (boost.attacks_per_cast() > 0) {
          EXPECT_GT(target.second.attacks_per_cast(), 0)
              << entry.first << " reclocks " << target.first
              << ", which is not clocked by swings landed";
        }
        if (boost.cooldown_pct() > 0.0) {
          EXPECT_GT(target.second.cooldown_seconds(), 0.0)
              << entry.first << " shortens " << target.first
              << "'s wait, which it does not have";
        }
        // Points on a tick nothing burns for are points nobody takes, and so
        // are seconds added to a burn that never lights.
        if (boost.dot_skill_pct() != 0.0 ||
            boost.dot_duration_seconds() != 0.0) {
          EXPECT_GT(target.second.dot().interval_seconds(), 0.0)
              << entry.first << " lifts " << target.first
              << "'s burn, which it does not leave";
        }
      }
    }
  }
  EXPECT_GT(checked, 0) << "no skill in the catalog grants strikes or reach";
}

// Superseding is the bluntest thing one skill can do to another -- the named
// skill stops paying at all -- so it may only name a skill the same character
// can hold, and never itself. A self-reference would leave a book that
// silently teaches nothing.
TEST(SkillDataTest, EverySupersededSkillIsHoldable) {
  std::map<std::string, Skill> skills = LoadSkills();
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    if (skill.supersedes_skill_name().empty()) {
      continue;
    }
    ++checked;
    EXPECT_NE(skill.supersedes_skill_name(), skill.name())
        << entry.first << " supersedes itself";
    EXPECT_TRUE(
        SameCharacterCanHold(skills, skill, skill.supersedes_skill_name()))
        << entry.first << " supersedes \"" << skill.supersedes_skill_name()
        << "\", which no character holding it can learn";
  }
  EXPECT_GT(checked, 0) << "no skill in the catalog supersedes another";
}

// The one book where GMS itself spends points going backwards, by file stem.
// Evil Eye Shock II opens at 139% against a maxed Evil Eye Shock's 150%, so
// its first three points are a downgrade. That is GMS's own ladder and the
// skill is meant to be maxed; the value is recorded rather than corrected.
const std::set<std::string>& SupersedesBelowWhatItReplaces() {
  static const std::set<std::string>* kStems =
      new std::set<std::string>{"evil_eye_shock_ii"};
  return *kStems;
}

// A superseding skill states the whole of what it replaces, so its FIRST level
// has to clear the replaced skill's LAST one on every lever they share. The
// point that buys it is otherwise a point spent going backwards, and nothing
// in the game would say so -- the replaced skill keeps its page and its level
// and quietly stops paying. GMS starts Advanced Final Attack at 41% against a
// Final Attack that reaches 40% for exactly this reason.
TEST(SkillDataTest, ASupersedingSkillIsNeverWorseAtLevelOne) {
  std::map<std::string, Skill> skills = LoadSkills();
  const google::protobuf::Descriptor* levers = SkillEffect::descriptor();
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    if (skill.supersedes_skill_name().empty() ||
        SupersedesBelowWhatItReplaces().count(entry.first) > 0) {
      continue;
    }
    for (const std::pair<const std::string, Skill>& other : skills) {
      if (other.second.name() != skill.supersedes_skill_name()) {
        continue;
      }
      ++checked;
      // Both halves, and the party's for the same reason as the character's:
      // a Bishop's first point in Blessed Harmony must not cost their party
      // the EXP the Ensemble under it was handing out.
      const SkillEffect replaced_pair[] = {
          EffectAt(other.second, other.second.max_level()),
          AllyEffectAt(other.second, other.second.max_level())};
      const SkillEffect replacing_pair[] = {EffectAt(skill, 1),
                                            AllyEffectAt(skill, 1)};
      const char* const kHalf[] = {"", "the party's "};
      for (int half = 0; half < 2; ++half) {
        for (int i = 0; i < levers->field_count(); ++i) {
          const google::protobuf::FieldDescriptor* field = levers->field(i);
          double was = LeverValue(replaced_pair[half], field);
          double now = LeverValue(replacing_pair[half], field);
          // The half that takes over may be an own-clock mode rather than the
          // skill's own block: Revenge of the Evil Eye's `base` is its
          // counterattack, and the Evil Eye Shock III replacing the shout is
          // one of its modes. An ally never reads a mode, so only this half.
          if (half == 0) {
            for (const AutoMode& mode : skill.auto_mode()) {
              now = std::max(now, LeverValue(mode.base(), field));
            }
          }
          if (was <= 0.0) {
            continue;
          }
          EXPECT_GE(now, was) << entry.first << " supersedes " << other.first
                              << " but pays " << now << " of " << kHalf[half]
                              << field->name() << " where it paid " << was;
        }
      }
    }
  }
  EXPECT_GT(checked, 0) << "no skill in the catalog supersedes another";
}

// The catalog keys on file stem but learned levels key on DISPLAY name, so two
// skills one character can reach under one name share a level: buying either
// buys both. Exclusive branches are the only thing preventing it -- each
// 2nd-job warrior has their own Weapon Mastery and no character sees two. The
// trap this guards is a later stage repeating an earlier stage's name, where
// both books do belong to one character.
TEST(SkillDataTest, OneSkillPerNamePerCharacter) {
  std::map<std::string, Skill> skills = LoadSkills();
  for (Job job : EveryValueOf<Job>(Job_descriptor())) {
    std::set<JobAdvancement> books = BooksFor(job);
    std::map<std::string, std::string> stem_by_name;
    for (const std::pair<const std::string, Skill>& entry : skills) {
      if (books.count(entry.second.job_advancement()) == 0) {
        continue;
      }
      std::pair<std::map<std::string, std::string>::iterator, bool> added =
          stem_by_name.insert({entry.second.name(), entry.first});
      EXPECT_TRUE(added.second)
          << Job_Name(job) << " reaches both " << entry.first << " and "
          << added.first->second << ", which are both called \""
          << entry.second.name() << "\" and so share one learned level";
    }
  }
}

// A 4th job's mastery skill is what its branch holds a weapon by, so all of
// them climb one ladder: 51% at level 1 to 70% at 20. Written to its own
// arithmetic, one branch would end up better at holding a weapon than the
// next for no reason a player could read.
TEST(SkillDataTest, EveryFourthJobMasteryClimbsTheSameLadder) {
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (StageForAdvancement(skill.job_advancement()) != 4 ||
        skill.base().mastery() <= 0.0) {
      continue;
    }
    ++checked;
    EXPECT_EQ(skill.max_level(), 20) << entry.first;
    EXPECT_NEAR(skill.base().mastery(), 0.51, 1e-9) << entry.first;
    EXPECT_NEAR(skill.base().mastery() +
                    skill.per_level().mastery() * (skill.max_level() - 1),
                0.70, 1e-9)
        << entry.first;
  }
  EXPECT_GT(checked, 0) << "no 4th job mastery skill in the catalog";
}

// The skills GMS gives to a party, by display name, with the two Meditations
// and the two Sharp Eyes counted once. Written out rather than derived,
// because what this is checking is the audit itself: a skill dropped from the
// data keeps its ally half only if somebody notices, and the wiki is the only
// thing that says it ever had one.
//
// The two GMS party skills NOT here are casts -- Heal is raised on somebody,
// and Angel Ray's healing rides each hit -- and nothing carries a caster's
// actions to an ally's fight yet. Dispel is out for a duller reason: what it
// cures is a display-only lever, so an ally half of it would grant a row and
// nothing else.
const char* const kPartySkills[] = {
    "Absolute Zero Aura",
    "Advanced Blessing",
    // Three of the Bishop's hypers, which reach the party the way the skill
    // they are named for does: GMS strengthens the buff rather than the
    // caster, so what it adds reaches whoever the buff reaches.
    "Advanced Blessing - Boss Rush",
    "Advanced Blessing - Extra Point",
    "Advanced Blessing - Ferocity",
    "Angel Ray",
    "Bless",
    "Blessed Ensemble",
    "Blessed Harmony",
    "Combat Orders",
    "Hex of the Evil Eye",
    "Holy Fountain",
    "Holy Magic Shell",
    "Holy Symbol",
    // The same again, and the fourth Holy Symbol hyper -- Experience -- is
    // absent because GMS pays that one to the caster alone.
    "Holy Symbol - Item Drop",
    "Holy Water",
    "Meditation",
    "Parashock Guard",
    "Puncture",
    "Sharp Eyes",
    // The two Hyper Skills that reach the party through the buff they name.
    // GMS stores their values on Sharp Eyes itself rather than as a modifier
    // aimed at the caster's own damage, so what rides the buff reaches whoever
    // the buff reaches.
    "Sharp Eyes - Critical Chance",
    "Sharp Eyes - Guardbreak",
    "Smokescreen",
    "Spirit Blade",
};

TEST(SkillDataTest, EveryPartySkillReachesTheParty) {
  std::set<std::string> want(std::begin(kPartySkills), std::end(kPartySkills));
  std::set<std::string> found;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!skill.has_ally_base() && !skill.has_ally_per_level() &&
        !skill.buff().has_ally_base() && !skill.buff().has_ally_per_level()) {
      continue;
    }
    EXPECT_GT(want.count(skill.name()), 0u)
        << entry.first << " grants a party half no audit knows about";
    found.insert(skill.name());
  }
  EXPECT_EQ(found, want);
}

// A description has to match the grant: a skill the party feels says so, and
// one that says so grants it. Four ways of reaching them count -- a half held
// for allies, the same again inside a buff, a demand for company, and a buff
// that stands over the whole party however many raise it. Two skills are
// excused, and GMS excuses both. Puncture's party clause lives in its readout
// rather than its flavour text. Blessed Harmony hands out nothing of its own --
// it restates the Ensemble it replaces, and names it, and the Ensemble's own
// page says what that is worth. Either way the card's Your Party row still
// carries the number.
TEST(SkillDataTest, ADescriptionSaysWhetherThePartyIsReached) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.name() == "Puncture" || skill.name() == "Blessed Harmony") {
      continue;
    }
    bool reaches = skill.has_ally_base() || skill.has_ally_per_level() ||
                   skill.buff().has_ally_base() ||
                   skill.buff().has_ally_per_level() ||
                   skill.requires_party() || skill.buff().party_shared();
    bool says = skill.description().find("party") != std::string::npos;
    EXPECT_EQ(reaches, says)
        << entry.first << ": \"" << skill.description() << "\"";
  }
}

// Parashock Guard is the only skill whose own half waits on a party, and its
// ally half has to be there too -- a skill that needs company and gives it
// nothing is a lever nobody would ever raise.
TEST(SkillDataTest, ASkillNeedingAPartyAlsoGivesItSomething) {
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!skill.requires_party()) {
      continue;
    }
    ++checked;
    EXPECT_TRUE(skill.has_ally_base()) << entry.first;
  }
  EXPECT_EQ(checked, 1) << "Parashock Guard, and nothing else so far";
}

// Two allies holding one buff are one buff. What stacks instead pays for the
// company kept, and only the Cleric's line does: Blessed Ensemble, and the
// Blessed Harmony that states the whole of it.
TEST(SkillDataTest, OnlyTheClericsLineStacksAcrossAParty) {
  std::set<std::string> stacking;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.ally_effect_stacks()) {
      stacking.insert(entry.second.name());
    }
  }
  EXPECT_EQ(stacking,
            (std::set<std::string>{"Blessed Ensemble", "Blessed Harmony"}));
}

// The Evil Eye shouts once, however far the book is taught. GMS gives Evil Eye
// Shock II no attack of its own -- it restates the base skill's whole readout
// and raises its damage -- and Revenge of the Evil Eye does the same again as
// Evil Eye Shock III, shortening the clock from 12 seconds to 10. Three
// stacking volleys is the reading this data was first written under, and this
// pins the correction.
TEST(SkillDataTest, TheEvilEyeShoutsOnce) {
  std::map<std::string, Skill> skills = LoadSkills();
  EXPECT_EQ(skills.at("evil_eye_shock_ii").supersedes_skill_name(),
            "Evil Eye Shock");
  EXPECT_EQ(skills.at("revenge_of_the_evil_eye").supersedes_skill_name(),
            "Evil Eye Shock II");
  // The base keeps its own clock; the top of the chain takes two seconds off.
  EXPECT_EQ(skills.at("evil_eye_shock").cast_interval_seconds(), 12.0);
  EXPECT_EQ(skills.at("evil_eye_shock_ii").cast_interval_seconds(), 12.0);
  const Skill& revenge = skills.at("revenge_of_the_evil_eye");
  ASSERT_EQ(revenge.auto_mode_size(), 2);
  EXPECT_EQ(revenge.auto_mode(0).label(), "Shock III");
  EXPECT_EQ(revenge.auto_mode(0).cast_interval_seconds(), 10.0);
}

// The save this reconcile was written for. A Berserker who had maxed the old
// book carried Lord of Darkness at 20; the book now stops it at 10 and spends
// the ten it gives up on the Evil Eye of Domination that took its place. Every
// other skill in the book is already at its own max, so there is exactly one
// taker and the draw has nothing to choose between.
TEST(SkillDataTest, AMaxedBerserkerBookRebalancesOntoDomination) {
  std::map<std::string, Skill> skills = LoadSkills();
  Character proto;
  proto.set_job(JOB_BERSERKER);
  proto.set_job_stage(3);
  proto.set_level(100);
  // The old book, as a save from before Domination existed holds it.
  for (const char* name :
       {"La Mancha Spear", "Cross Surge", "Evil Eye Shock II",
        "Hex of the Evil Eye", "Lord of Darkness", "Endure"}) {
    (*proto.mutable_skill_levels())[name] = 20;
  }
  std::mt19937 rng(0);
  CharacterInstance berserker(rng, std::move(proto));

  EXPECT_EQ(berserker.ReconcileSkills(skills), 10);
  EXPECT_EQ(berserker.skill_level(skills.at("lord_of_darkness")), 10);
  EXPECT_EQ(berserker.skill_level(skills.at("evil_eye_of_domination")), 10);
}

}  // namespace
}  // namespace ms
