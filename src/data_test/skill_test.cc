// Checks the shipped skill catalog as a whole: every job's book must cost
// exactly the SP that job earns, which only holds if the data is right.
// Arithmetic done by hand in a textproto breaks as soon as a skill is added.
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

#include "absl/strings/str_join.h"
#include "src/character/character.h"
#include "src/character/job_branch.h"
#include "src/character/v_matrix.h"
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

// The SP each job stage's levels pay; the advancement itself grants none. 3 SP
// a level over 11-30, 31-60 and 61-100, then 5 a level over 101-140.
constexpr int kSpByStage[] = {0, 60, 90, 120, 200};

// What a job's Hyper page costs, and so how many Hyper Skills it has: one point
// at level 140 and every fifth level up to 195, each buying one skill outright.
// A page with fewer than twelve leaves a point that can never be spent.
constexpr int kHyperSkillsPerJob = 12;
// The levels those points arrive at, which are the only levels a Hyper Skill
// may unlock at. Unlocking between two of them would make a point wait for a
// skill it could have bought.
constexpr int kFirstHyperLevel = 140;
constexpr int kLastHyperLevel = 195;
constexpr int kHyperLevelStep = 5;

// Every value of an enum except UNSPECIFIED, read from the descriptor so a new
// job's book is checked without anyone remembering to list it.
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

// The books a job has: its own and every one before it.
std::set<JobAdvancement> BooksFor(Job job) {
  std::set<JobAdvancement> books;
  // Up to 5, not 4, so the 5th job's book is included.
  for (int stage = 1; stage <= 5; ++stage) {
    JobAdvancement advancement = AdvancementForJobStage(job, stage);
    if (advancement != JOB_ADVANCEMENT_UNSPECIFIED) {
      books.insert(advancement);
    }
  }
  return books;
}

// One lever's value, whatever numeric type it is stored as, so a check can go
// through every lever of a SkillEffect without naming them. A field the message
// doesn't have reads as 0.
double LeverValue(const SkillEffect& effect,
                  const google::protobuf::FieldDescriptor* field) {
  // A Final Attack's percent is per strike, so on its own it doesn't show what
  // the attack is worth: three at 112% beat one at 147%.
  if (field->name() == "final_attack_pct") {
    return effect.final_attack_pct() *
           std::max(1, WholeValue(effect.final_attack_lines()));
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

// The effect at `level` from a base and per-level pair, on the usual ladder:
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

// What the skill is worth to the character who has it, and to everyone else in
// the party.
SkillEffect EffectAt(const Skill& skill, int level) {
  return EffectFrom(skill.base(), skill.per_level(), level);
}

SkillEffect AllyEffectAt(const Skill& skill, int level) {
  return EffectFrom(skill.ally_base(), skill.ally_per_level(), level);
}

// Whether any of `books` lists `skill`. A shared skill is in several books, so
// one of the job's books is enough.
bool ReachedBy(const std::set<JobAdvancement>& books, const Skill& skill) {
  for (const SkillPlacement& placement : skill.placement()) {
    if (books.count(placement.job_advancement()) > 0) {
      return true;
    }
  }
  return false;
}

// The skill's position in each book, keyed by book.
std::map<int, int> OrdersOf(const Skill& skill) {
  std::map<int, int> orders;
  for (const SkillPlacement& placement : skill.placement()) {
    orders[placement.job_advancement()] = placement.skill_order();
  }
  return orders;
}

// Whether some job has both `skill`'s book and the book of a skill named
// `name`. Skills refer to each other by display name, and a name no character
// can reach from where it is referenced is a grant nothing will ever read.
bool SameCharacterCanHold(const std::map<std::string, Skill>& skills,
                          const Skill& skill, const std::string& name) {
  for (Job job : EveryValueOf<Job>(Job_descriptor())) {
    std::set<JobAdvancement> books = BooksFor(job);
    if (!ReachedBy(books, skill)) {
      continue;
    }
    for (const std::pair<const std::string, Skill>& other : skills) {
      if (other.second.name() == name && ReachedBy(books, other.second)) {
        return true;
      }
    }
  }
  return false;
}

// Whether the character swings this skill: an attack, or a cast that uses a
// swing. A skill that only casts a buff does so on its own timer and never
// replaces the swing, so it is never the named swing.
bool SpendsASwing(const Skill& skill) {
  return skill.kind() == SKILL_KIND_ATTACK ||
         (skill.kind() == SKILL_KIND_ACTIVE && skill.base().heal_pct() > 0.0);
}

// Whether the character stops to cast it, which covers everything they press. A
// buff's cast takes time even though it doesn't use a swing (see
// BuffOption::cast_seconds), so it has to state how long it takes.
bool HasACastAnimation(const Skill& skill) {
  // A toggle is a switch, not a cast: the fight never spends a swing on it, and
  // what it grants applies whether it was switched on a second or an hour ago.
  // See Skill.toggle.
  if (skill.toggle()) {
    return false;
  }
  return skill.kind() == SKILL_KIND_ATTACK || skill.kind() == SKILL_KIND_ACTIVE;
}

// Whether pressing it does nothing but cast a buff. Distinguished from a heal
// by the same test SpendsASwing uses: a heal is cast instead of a swing, while
// a buff is cast on its own timer.
bool RaisesOnlyABuff(const Skill& skill) {
  // A buff that loads a swing is pressed to start a burst, not cast from the
  // buff sequence, so it costs its own animation like any attack.
  return skill.kind() == SKILL_KIND_ACTIVE && !SpendsASwing(skill) &&
         skill.buff().magazine().charges() <= 0;
}

const std::map<std::string, Skill>& LoadSkills() {
  return TestData<Skill>("skills");
}

// Two fields every skill must set. The advancement puts it in a tab and picks
// the SP pool that pays for it; the kind decides what it does in a fight and
// which label starts its row in the book.
TEST(SkillDataTest, EverySkillNamesItsAdvancementAndItsKind) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    EXPECT_FALSE(entry.second.placement().empty())
        << entry.first << " would be unreachable: no tab shows it and no SP "
        << "pool buys it";
    EXPECT_NE(entry.second.kind(), SKILL_KIND_UNSPECIFIED)
        << entry.first << " would list with no tag and do nothing";
  }
}

// A load fires as its own button, on the press of a skill that spends it, or on
// every swing. The last two ride on a press they didn't pay for, so neither
// states an animation.
TEST(SkillDataTest, EveryLoadIsFiredByAButtonOrByASkillThatSpendsIt) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::set<std::string> names;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    names.insert(entry.second.name());
  }
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Magazine& magazine = entry.second.buff().magazine();
    if (magazine.charges() <= 0) {
      continue;
    }
    if (!magazine.spent_by_skill_name().empty()) {
      EXPECT_FALSE(magazine.spent_by_every_swing())
          << entry.first << " names the skill that spends it AND takes every "
          << "swing, which cannot both be how it is fired";
      EXPECT_GT(names.count(magazine.spent_by_skill_name()), 0u)
          << entry.first << " is spent by \"" << magazine.spent_by_skill_name()
          << "\", which no skill answers to";
      EXPECT_NE(magazine.spent_by_skill_name(), entry.second.name())
          << entry.first << " is spent by its own press";
    }
    if (magazine.spent_by_skill_name().empty() &&
        !magazine.spent_by_every_swing()) {
      EXPECT_GT(magazine.base_delay_ms(), 0)
          << entry.first << " loads a swing of its own with no animation";
      continue;
    }
    EXPECT_EQ(magazine.base_delay_ms(), 0)
        << entry.first << " rides a press it did not pay for and states an "
        << "animation of its own";
  }
}

// A self-filling load states both its clock and its cap, and what one press
// uses must fit within the cap.
TEST(SkillDataTest, EverySelfFillingBankStatesItsClockAndItsCap) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Magazine& magazine = entry.second.buff().magazine();
    if (magazine.charges() <= 0) {
      continue;
    }
    EXPECT_EQ(magazine.recharge_seconds() > 0.0, magazine.recharge_max() > 0)
        << entry.first << " states one half of a bank that fills itself";
    EXPECT_LE(magazine.charges_per_swing(), magazine.charges())
        << entry.first << " spends more charges on a press than a raising of "
        << "the buff hands over";
  }
}

// The boss fight panel is the narrowest place a skill name appears. A name
// needing one more row is cut in half; the fix belongs in the panel, so this
// fails where the width is set.
TEST(SkillDataTest, EverySwingsNameFitsTheBossFightPanel) {
  // The border, plus the one column of margin every panel keeps inside it.
  const int kRoom = kBossPanelWidth - 4;
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    // The magazine's own label, not the buff that loads it: the panel names the
    // swing being fired.
    std::vector<std::string> names;
    if (SpendsASwing(skill)) {
      names.push_back(skill.name());
    }
    if (skill.buff().magazine().charges() > 0) {
      names.push_back(skill.buff().magazine().label());
    }
    for (const std::string& name : names) {
      ++checked;
      std::vector<std::string> lines = WrapBalanced(name, kRoom);
      EXPECT_LE(static_cast<int>(lines.size()), kPlayerBarRows)
          << entry.first << " takes more rows than the arena has";
      // A word too long for the row isn't wrapped at all: it overflows, cutting
      // off both ends of the name.
      for (const std::string& line : lines) {
        EXPECT_LE(static_cast<int>(line.size()), kRoom)
            << entry.first << " has a word too long for the arena";
      }
    }
  }
  EXPECT_GT(checked, 0);
}

// The character panel's widest column is sized for the longest name, so a
// longer name should change that number. Hyper Skills scroll instead, since no
// shorter "Advanced Final Attack - Opportunity" is GMS's name.
TEST(SkillDataTest, EverySkillNameFitsTheWidestCharacterPanel) {
  std::map<std::string, Skill> skills = LoadSkills();
  // The level column is sized over a whole book, so any name might sit beside
  // the widest level in the catalog. Combat Orders adds at most two levels.
  int level_width = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    std::string text = std::to_string(SkillMaxLevel(entry.second)) + " (+2)";
    level_width = std::max(level_width, 1 + static_cast<int>(text.size()) + 1);
  }
  // A book long enough to scroll gives a column to the scroll bar, which is the
  // case a long name must fit.
  int name_width =
      CharacterPanel::SkillNameWidth(level_width, kLeftColumnMax - 2 - 1);
  // Keyed by the shortened name and holding the full one, so branches sharing a
  // display name (one Physical Training, one Advanced Final Attack) are one
  // row, not a collision.
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

// Rules outside the skill read tags, so an unset tag silently leaves the skill
// out of a group it was meant to be in.
TEST(SkillDataTest, EveryTagNamesAGroup) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    for (int tag : entry.second.tags()) {
      EXPECT_NE(tag, SKILL_TAG_UNSPECIFIED)
          << entry.first << " carries a tag that names no group";
    }
  }
}

// The inspect screen has nothing else to say about a skill: its levers are
// numbers, and only the description explains what they are for.
TEST(SkillDataTest, EverySkillDescribesItself) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    EXPECT_FALSE(entry.second.description().empty())
        << entry.first << " would inspect to a blank panel";
  }
}

// Every book costs exactly what its levels pay, so finishing a stage means
// having bought all of it, with no shortfall and no points left over.
TEST(SkillDataTest, EveryBookCostsExactlyWhatItsLevelsPayOut) {
  std::map<int, int> cost_by_advancement;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    // A Hyper Skill is bought from its own pool, a V node with V Points, and a
    // link skill costs nothing. A Vengeance form uses the ladder of the skill
    // it replaces.
    if (entry.second.hyper() ||
        entry.second.v_node() != V_NODE_KIND_UNSPECIFIED ||
        entry.second.account_levels_per_level() > 0 ||
        entry.second.link_line() != JOB_UNSPECIFIED ||
        !entry.second.replaces_skill_name().empty()) {
      continue;
    }
    // Charged to every book that lists it: a shared skill is one file, but each
    // job that has it pays for its levels from their own pool.
    for (const SkillPlacement& placement : entry.second.placement()) {
      cost_by_advancement[placement.job_advancement()] +=
          entry.second.max_level();
    }
  }
  // Every advancement below the 5th must appear, not just add up correctly.
  // Skills are in a folder per job, and a folder that stopped being read would
  // disappear from the map entirely and let the rest pass.
  for (JobAdvancement advancement :
       EveryValueOf<JobAdvancement>(JobAdvancement_descriptor())) {
    // None of these four is a book this test counts: 5th job books hold only V
    // nodes, bought with V Points; the common nodes are bought with V Points
    // too; and the beginner book and link skills aren't bought at all.
    if (StageForAdvancement(advancement) >= 5 ||
        advancement == JOB_ADVANCEMENT_COMMON ||
        advancement == JOB_ADVANCEMENT_BEGINNER ||
        advancement == JOB_ADVANCEMENT_LINK) {
      continue;
    }
    EXPECT_TRUE(cost_by_advancement.count(advancement))
        << "advancement " << advancement << " has no skills at all";
  }
  for (const std::pair<const int, int>& entry : cost_by_advancement) {
    int stage = StageForAdvancement(static_cast<JobAdvancement>(entry.first));
    ASSERT_GT(stage, 0) << "advancement " << entry.first << " has no stage";
    // A stage past the SP table has no SP to be held to. That is the 5th job,
    // whose book holds only V nodes. Every stage below it is checked.
    if (stage >= static_cast<int>(sizeof(kSpByStage) / sizeof(kSpByStage[0]))) {
      continue;
    }
    EXPECT_EQ(entry.second, kSpByStage[stage])
        << "advancement " << entry.first << " costs " << entry.second
        << " against the " << kSpByStage[stage] << " its levels pay out";
  }
}

// Skills are only shared between jobs at the same stage, so every book charges
// a shared skill to the same pool.
TEST(SkillDataTest, EveryBookListingASkillChargesItToTheSamePool) {
  int shared = 0;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.placement_size() < 2) {
      continue;
    }
    ++shared;
    int stage = StageForAdvancement(BookOf(skill));
    for (const SkillPlacement& placement : skill.placement()) {
      EXPECT_EQ(StageForAdvancement(placement.job_advancement()), stage)
          << entry.first << " is listed in books at two different stages";
    }
  }
  EXPECT_GT(shared, 0) << "the shared skill folders stopped being read";
}

// The Job Inspect screen is read before an advancement, so one it can open with
// nothing to show is unfinished. JOB_ADVANCEMENT_COMMON is nobody's, and a 5th
// job shows its matrix.
TEST(SkillDataTest, EveryAdvancementAPlayerIsOfferedHasSomethingToRead) {
  std::map<std::string, Skill> skills = LoadSkills();
  JobInspectPanel panel(skills);
  int offered = 0;
  for (int stage = 1; stage <= kMaxJobStage; ++stage) {
    for (int j = Job_MIN; j <= Job_MAX; ++j) {
      if (!Job_IsValid(j)) {
        continue;
      }
      for (Job to : JobChoicesForStage(static_cast<Job>(j), stage)) {
        ++offered;
        panel.SetJob(to, stage);
        EXPECT_FALSE(panel.Skills().empty())
            << JobAdvancement_Name(AdvancementForJobStage(to, stage))
            << " inspects empty";
      }
    }
  }
  EXPECT_GT(offered, 0) << "nothing is offered at all";
}

// The fight builds one damage table per combination of timed buffs, so a
// character past kMaxBuffWindows silently loses the rest. Counted from the
// books, so it fails while the buff is being written.
TEST(SkillDataTest, NoBookHandsOutMoreBuffsThanTheFightModels) {
  std::map<std::string, Skill> skills = LoadSkills();
  for (Job job : EveryValueOf<Job>(Job_descriptor())) {
    std::set<JobAdvancement> books = BooksFor(job);
    // The V Matrix's common nodes belong to no job stage, but every character
    // who reaches the 5th job has them.
    books.insert(JOB_ADVANCEMENT_COMMON);
    std::vector<std::string> raised;
    for (const std::pair<const std::string, Skill>& entry : skills) {
      if (entry.second.buff().duration_seconds() > 0.0 &&
          ReachedBy(books, entry.second)) {
        // A buff with stages or stacks uses one window for each, so it counts
        // that many times against the limit.
        raised.insert(raised.end(), BuffWindowsFor(entry.second.buff()),
                      entry.second.name());
      }
    }
    EXPECT_LE(static_cast<int>(raised.size()), kMaxBuffWindows)
        << Job_Name(job) << " raises " << absl::StrJoin(raised, ", ");
  }
}

// A pulse is matched to its buff by name, and every stage of a staged buff
// shares the name, so a staged buff with a pulse would attach it to whichever
// stage was read last.
TEST(SkillDataTest, NoSheddingBuffAlsoBleeds) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Buff& buff = entry.second.buff();
    if (buff.stages() <= 1) {
      continue;
    }
    EXPECT_GT(buff.stage_interval_seconds(), 0.0)
        << entry.first << " sheds stages on no clock";
    EXPECT_LE(buff.pulse().cast_interval_seconds(), 0.0)
        << entry.first << " both sheds stages and bleeds";
  }
}

// A duty cycle needs both numbers: an interval with no active time would
// silence the buff, and active time with no interval would never repeat.
TEST(SkillDataTest, ADutyCycleStatesBothOfItsNumbers) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Buff& buff = entry.second.buff();
    if (buff.duty_seconds() <= 0.0 && buff.duty_interval_seconds() <= 0.0) {
      continue;
    }
    EXPECT_GT(buff.duty_seconds(), 0.0)
        << entry.first << " cycles its grant on no grant";
    EXPECT_LT(buff.duty_seconds(), buff.duty_interval_seconds())
        << entry.first << " grants for its whole cycle, which is no cycle";
    EXPECT_LE(buff.duty_interval_seconds(), buff.duration_seconds())
        << entry.first << " cycles slower than it stands";
  }
}

// Whether any stance of the buff has a pulse with a base value.
bool AnyStancePulseHasBase(const Buff& buff) {
  for (const Stance& stance : buff.stance()) {
    if (stance.pulse().has_base()) {
      return true;
    }
  }
  return false;
}

// Whether any boost the skill gives applies at level 1. That is how a boost
// node, whose whole effect is in what it boosts, makes its first level worth
// buying.
bool GrantsAtFirstLevel(const Skill& skill) {
  // A buff whose whole effect is the swing it loads states its ladder there.
  if (skill.buff().magazine().has_base()) {
    return true;
  }
  for (const SkillBoost& boost : skill.boost()) {
    if (boost.min_level() <= 1) {
      return true;
    }
  }
  // A buff whose whole effect is what it adds while active, like Throwing Star
  // Barrage, which hits nothing itself and gives Quad Star three more
  // directions from the first level.
  for (const SkillBoost& boost : skill.buff().boost()) {
    if (boost.min_level() <= 1) {
      return true;
    }
  }
  return false;
}

// Whether the skill hits anything, by any of three routes: a swing the
// character makes, a turret on its own timer, or a buff's pulse. Spirit of Snow
// uses only the third: the cast summons a spirit, and the spirit attacks.
bool StrikesSomething(const Skill& skill) {
  if (DealsDamage(skill.kind())) {
    return true;
  }
  if (skill.buff().pulse().has_base() || AnyStancePulseHasBase(skill.buff())) {
    return true;
  }
  for (const AutoMode& mode : skill.auto_mode()) {
    if (mode.has_base()) {
      return true;
    }
  }
  return false;
}

// Whether any turret the skill runs has its own base value. Silhouette Mirage
// is a node that is nothing but a turret.
bool AnyAutoModeHasBase(const Skill& skill) {
  for (const AutoMode& mode : skill.auto_mode()) {
    if (mode.has_base()) {
      return true;
    }
  }
  return false;
}

// A node must match its kind: the kind decides its max level, and where it is
// listed follows from which matrix has it.
TEST(SkillDataTest, EveryVNodeMatchesItsKind) {
  int commons = 0;
  int archetypes = 0;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    bool common_home = ListedIn(skill, JOB_ADVANCEMENT_COMMON);
    if (skill.v_node() == V_NODE_KIND_UNSPECIFIED) {
      EXPECT_FALSE(common_home)
          << skill.name() << " is not a node but lives among them";
      continue;
    }
    EXPECT_EQ(skill.max_level(), MaxVNodeLevel(skill.v_node()))
        << skill.name() << " does not go as far as its kind";
    EXPECT_EQ(skill.required_level(), 0)
        << skill.name() << " is gated by the matrix, not by a level";
    EXPECT_FALSE(skill.hyper()) << skill.name() << " cannot be both";
    // A ladder is base + per_level x (L - 1), so a node with no base gains
    // nothing at level 1. A node whose effect is a buff, burn, pulse or turret
    // states its ladder there, and a boost node on the skills it boosts.
    EXPECT_TRUE(skill.has_base() || skill.buff().has_base() ||
                skill.dot().has_base() || skill.buff().pulse().has_base() ||
                AnyStancePulseHasBase(skill.buff()) ||
                AnyAutoModeHasBase(skill) || GrantsAtFirstLevel(skill))
        << skill.name() << " grants nothing at its first level";
    if (skill.v_node() == V_NODE_KIND_COMMON) {
      ++commons;
      EXPECT_TRUE(common_home)
          << skill.name() << " is common and belongs under COMMON";
      continue;
    }
    EXPECT_EQ(StageForAdvancement(BookOf(skill)), 5)
        << skill.name() << " is not everybody's and belongs to a 5th job";
    // An archetype node has no book of its own, so it names every 5th job of
    // its line. It must name more than one, or it is that job's own node.
    if (skill.v_node() == V_NODE_KIND_ARCHETYPE) {
      ++archetypes;
      EXPECT_GT(skill.placement_size(), 1)
          << skill.name() << " is a line's and names one job";
      EXPECT_FALSE(common_home)
          << skill.name() << " is one line's, not everybody's";
    }
  }
  EXPECT_GT(commons, 0) << "the common folder stopped being read";
  EXPECT_GT(archetypes, 0) << "the line's shared folders stopped being read";
}

// An archetype node belongs to a job line, so every 5th job on that line lists
// it, including any added later. Checked in both directions: whatever one job's
// book lists, its siblings' books must list too.
TEST(SkillDataTest, EveryFifthJobListsItsLinesArchetypeNodes) {
  std::map<JobAdvancement, JobBranch> line_of;
  std::map<JobBranch, std::set<JobAdvancement>> books_on;
  for (Job job : EveryValueOf<Job>(Job_descriptor())) {
    JobAdvancement fifth = AdvancementForJobStage(job, 5);
    if (fifth == JOB_ADVANCEMENT_UNSPECIFIED) {
      continue;
    }
    line_of[fifth] = BranchOf(job);
    books_on[BranchOf(job)].insert(fifth);
  }
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.v_node() != V_NODE_KIND_ARCHETYPE) {
      continue;
    }
    std::set<JobAdvancement> named;
    for (const SkillPlacement& placement : skill.placement()) {
      named.insert(placement.job_advancement());
    }
    ASSERT_FALSE(named.empty()) << skill.name() << " names no book";
    for (JobAdvancement book : books_on[line_of[*named.begin()]]) {
      EXPECT_GT(named.count(book), 0u)
          << skill.name() << " is its line's and " << JobAdvancement_Name(book)
          << " does not list it";
    }
  }
}

// A requirement may name a skill from an earlier book, since a character keeps
// every earlier book, but not one the character could never have.
TEST(SkillDataTest, EveryRequirementNamesAHoldableSkill) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::vector<Job> jobs = EveryValueOf<Job>(Job_descriptor());
  for (const std::pair<const std::string, Skill>& entry : skills) {
    if (!entry.second.has_required_skill()) {
      continue;
    }
    const SkillRequirement& required = entry.second.required_skill();
    // Some job with the requiring skill's book must also have a book containing
    // the required skill, at a level it can reach.
    bool satisfiable = false;
    for (Job job : jobs) {
      std::set<JobAdvancement> books = BooksFor(job);
      if (!ReachedBy(books, entry.second)) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        if (other.second.name() != required.skill_name() ||
            !ReachedBy(books, other.second)) {
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

// skill_order is the only thing that sets list order, so a book that skips or
// repeats a number has two skills with ambiguous positions, and a book that
// leaves it unset piles them at the top.
TEST(SkillDataTest, EveryBookIsNumberedOneThroughItsSize) {
  // Keyed by the pair: a Hyper page and the 4th job book name the same
  // advancement but are separate lists, so each is numbered from one.
  std::map<std::pair<int, bool>, std::map<int, std::string>> by_advancement;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    for (const SkillPlacement& placement : entry.second.placement()) {
      int order = placement.skill_order();
      EXPECT_GT(order, 0) << entry.first << " has no place in its book";
      // A Vengeance form takes the row of the skill it replaces instead of its
      // own; the next test checks that number.
      if (!entry.second.replaces_skill_name().empty()) {
        continue;
      }
      std::pair<std::map<int, std::string>::iterator, bool> added =
          by_advancement[{placement.job_advancement(), entry.second.hyper()}]
              .insert({order, entry.first});
      EXPECT_TRUE(added.second)
          << entry.first << " and " << added.first->second << " both sit at "
          << order << " of advancement " << placement.job_advancement();
    }
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

// A Vengeance form and the skill it replaces are one row of one book: the same
// page, the same position and the same ladder. Otherwise the swap would move
// the row, or leave the form at a level nobody bought.
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
    EXPECT_EQ(OrdersOf(skill), OrdersOf(*replaced))
        << entry.first << " is listed on a page it never replaces a row on, "
        << "or at a place the skill it stands in for does not sit";
    EXPECT_EQ(skill.max_level(), replaced->max_level())
        << entry.first << " climbs a ladder the skill it replaces does not";
    EXPECT_FALSE(skill.hyper())
        << entry.first << " is bought by buying another skill, not with a "
        << "Hyper point";
  }
  EXPECT_EQ(checked, 4) << "the Bishop's four, and nothing else so far";
}

// A switch is only worth pressing if something responds to it.
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

// A Hyper page is all or nothing: a job with only some of its twelve would
// leave a point with nothing to buy, and a hyper on an advancement other than
// the 4th is on a page that is never shown.
TEST(SkillDataTest, EveryHyperPageIsWholeAndOpensOnARung) {
  std::map<int, int> hypers_by_advancement;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!skill.hyper()) {
      EXPECT_EQ(skill.required_level(), 0)
          << entry.first << " gates on a level nothing but a hyper carries";
      continue;
    }
    for (const SkillPlacement& placement : skill.placement()) {
      ++hypers_by_advancement[placement.job_advancement()];
    }
    EXPECT_EQ(skill.max_level(), 1)
        << entry.first << " is bought with one point and must cost one";
    EXPECT_EQ(StageForAdvancement(BookOf(skill)), 4)
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

// Only damage-dealing skills split what they grant, so a kept part on any other
// skill is never read; every other kind already keeps all of `base`.
TEST(SkillDataTest, OnlyADamagingSkillStatesAKeptHalf) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (DealsDamage(skill.kind())) {
      continue;
    }
    EXPECT_FALSE(skill.has_passive())
        << entry.first << " states a kept half nothing will read";
    EXPECT_FALSE(skill.has_passive_per_level())
        << entry.first << " states a kept half nothing will read";
  }
}

// An auto-attack with no timer never fires, so a skill that forgets to say when
// silently does nothing. It can use one of three timers (seconds, swings landed
// or enemies defeated) and needs exactly one.
TEST(SkillDataTest, EveryAutoAttackSaysWhenItFires) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    int clocks = (skill.cast_interval_seconds() > 0.0) +
                 (skill.attacks_per_cast() > 0) + (skill.kills_per_cast() > 0);
    if (skill.kind() != SKILL_KIND_AUTO_ATTACK) {
      EXPECT_EQ(clocks, 0) << entry.first
                           << " sets a clock it will never be asked for";
      continue;
    }
    EXPECT_EQ(clocks, 1) << entry.first << " names " << clocks << " clocks";
    EXPECT_GT(skill.base().skill_pct(), 0.0)
        << entry.first << " would fire for nothing";
  }
}

// An auto mode needs damage, a name and exactly one kind of timer: both leaves
// the fight to guess, and neither never fires.
TEST(SkillDataTest, EveryAutoModeSaysWhenItFiresAndForHowMuch) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    for (const AutoMode& mode : entry.second.auto_mode()) {
      EXPECT_NE(mode.cast_interval_seconds() > 0.0, mode.attacks_per_cast() > 0)
          << entry.first << "'s own-clock half names no clock, or two";
      EXPECT_GT(mode.base().skill_pct(), 0.0)
          << entry.first << "'s own-clock half would fire for nothing";
      EXPECT_FALSE(mode.label().empty())
          << entry.first << "'s own-clock half has no row to sit on";
    }
  }
}

// A side strike needs all three things that make one: damage, a cooldown, and a
// row on the skill page. With no cooldown it would fire on every swing, which
// is what extra_hit already does.
TEST(SkillDataTest, EverySideStrikeSaysWhenItFiresAndForHowMuch) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (!entry.second.has_side_strike()) {
      continue;
    }
    const SideStrike& side = entry.second.side_strike();
    // Its own cooldown, or the swing's. A strike attached to a skill with no
    // cooldown would fire on every swing.
    EXPECT_TRUE(side.cooldown_seconds() > 0.0 ||
                entry.second.cooldown_seconds() > 0.0)
        << entry.first << "'s side strike would go out on every swing";
    EXPECT_GT(side.base().skill_pct(), 0.0)
        << entry.first << "'s side strike would go out for nothing";
    EXPECT_FALSE(side.label().empty())
        << entry.first << "'s side strike has no row to sit on";
  }
}

// A held swing needs a pulse rate, a pulse count and a minimum hold. A written
// final attack needs a row, and its extra hits must be its own, since the fight
// treats everything after the first block as the finish.
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
    if (channel.has_finish()) {
      EXPECT_GT(channel.finish().base().skill_pct(), 0.0)
          << entry.first << "'s hold ends on nothing";
      EXPECT_FALSE(channel.finish().label().empty())
          << entry.first << "'s finish has no row to sit on";
    }
    // A hold that grows needs both what it grows into and when. Either alone is
    // a hold that stays the same throughout while claiming otherwise.
    EXPECT_EQ(channel.has_grown(), channel.small_pulses() > 0)
        << entry.first << " states half of a growth";
    if (channel.has_grown()) {
      EXPECT_GT(channel.grown().base().skill_pct(), 0.0)
          << entry.first << " grows into nothing";
      EXPECT_FALSE(channel.grown().label().empty())
          << entry.first << "'s grown pulse has no row to sit on";
      EXPECT_LT(channel.small_pulses(), channel.max_pulses())
          << entry.first << " never reaches the pulse it grows into";
    }
    EXPECT_EQ(skill.extra_hit_size(), 0)
        << entry.first << " lands extra hits the fight would read as its "
        << "finish";
    // A hold is limited by charges or by a cooldown, never half of each: all
    // three charge numbers or none, and charges must not refill faster than
    // they are used, or the hold would have no limit.
    bool banked = channel.charge_seconds() > 0.0;
    EXPECT_EQ(banked, channel.max_charges() > 0)
        << entry.first << " states half of a charge bank";
    EXPECT_EQ(banked, channel.pulses_per_charge() > 0)
        << entry.first << " states half of a charge bank";
    if (banked) {
      EXPECT_LE(channel.max_charges() * channel.pulses_per_charge(),
                channel.max_pulses() + channel.pulses_per_charge())
          << entry.first << "'s bank buys more hold than it has pulses";
      EXPECT_GT(skill.cooldown_seconds(), 0.0)
          << entry.first << " banks charges with nothing between presses";
      EXPECT_LT(skill.cooldown_seconds(), channel.charge_seconds())
          << entry.first << " waits longer than its charges take to fill, so "
          << "the bank is not what paces it";
    }
  }
}

// A repeated strike belongs only to a swing, and a buff started at the cast
// belongs only to a swing that applies one. Neither means anything elsewhere,
// so a file setting one elsewhere states something nothing reads.
TEST(SkillDataTest, RepeatedStrikesAndCastRaisedBuffsBelongToSwings) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.casts() > 0) {
      EXPECT_TRUE(DealsDamage(skill.kind()))
          << entry.first << " repeats a strike it never lands";
      EXPECT_GT(skill.base().skill_pct(), 0.0)
          << entry.first << " repeats a strike worth nothing";
    }
    for (const SwingHit& hit : skill.extra_hit()) {
      EXPECT_GE(hit.casts(), 0) << entry.first << " lands a negative count";
    }
    if (skill.buff().raised_on_cast()) {
      EXPECT_EQ(skill.kind(), SKILL_KIND_ATTACK)
          << entry.first << " raises a buff at a cast it never makes";
    }
  }
}

// A scattered cast fires strikes, reaches no more enemies than it has strikes,
// and a repeat is worth something but never more than the first.
void CheckScatter(const std::string& what, const Scatter& scatter,
                  int max_enemies) {
  EXPECT_GT(scatter.hits(), 1)
      << what << " scatters a single strike, which is every swing";
  // A count that grows with active burns is checked at its maximum, since that
  // is the only reach all of its strikes could ever fill.
  int widest = std::max(scatter.hits(), scatter.max_hits());
  EXPECT_LE(std::max(1, max_enemies), widest)
      << what << " reaches further than it has strikes to throw";
  if (scatter.hits_per_dot() > 0.0) {
    EXPECT_GT(scatter.max_hits(), scatter.hits())
        << what << " widens with the burns but no further than it already "
        << "threw";
  } else {
    EXPECT_EQ(scatter.max_hits(), 0)
        << what << " caps a count that never grows";
  }
  EXPECT_GT(scatter.repeat_final_dmg_pct(), -1.0)
      << what << " takes the whole of a repeat strike away";
  EXPECT_LE(scatter.repeat_final_dmg_pct(), 0.0)
      << what << " pays a repeat strike more than the first";
  // A cap of one means the cast never hits an enemy twice, which is just a
  // normal swing written the hard way, and a cap above the count it can fire
  // limits nothing.
  if (scatter.max_hits_per_enemy() > 0) {
    EXPECT_GT(scatter.max_hits_per_enemy(), 1)
        << what << " caps the pile at one strike, which is an ordinary swing";
    EXPECT_LT(scatter.max_hits_per_enemy(), widest)
        << what << " caps the pile no lower than the strikes it throws";
  }
}

TEST(SkillDataTest, EveryScatteredSwingReachesNoFurtherThanItsStrikes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.has_scatter()) {
      EXPECT_EQ(skill.kind(), SKILL_KIND_ATTACK)
          << entry.first << " scatters strikes but is not a swing";
      CheckScatter(entry.first, skill.scatter(), skill.max_enemies());
    }
    if (skill.side_strike().has_scatter()) {
      const SideStrike& side = skill.side_strike();
      CheckScatter(
          entry.first + "'s side strike", side.scatter(),
          side.max_enemies() > 0 ? side.max_enemies() : skill.max_enemies());
    }
    if (skill.buff().magazine().has_scatter()) {
      const Magazine& magazine = skill.buff().magazine();
      CheckScatter(entry.first + "'s " + magazine.label(), magazine.scatter(),
                   magazine.max_enemies());
    }
    if (skill.buff().pulse().has_fixed_strikes()) {
      const BuffPulse& pulse = skill.buff().pulse();
      CheckScatter(entry.first + "'s " + pulse.label(), pulse.fixed_strikes(),
                   pulse.max_enemies());
    }
  }
}

// A timed buff needs both parts that make it one: a duration and a cooldown. A
// buff with no cooldown is just a passive written the hard way.
TEST(SkillDataTest, EveryBuffStandsForAWhileAndWaitsForTheNextOne) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!skill.has_buff()) {
      continue;
    }
    EXPECT_GT(LongestBuffDuration(skill.buff()), 0.0)
        << entry.first << "'s buff would never stand";
    // A buff applied by its own swing waits for that swing, not a timer, and
    // the swing costs the fight a turn either way. Only buffs cast for free
    // need a cooldown to stop them being permanent.
    if (skill.kind() == SKILL_KIND_ATTACK) {
      continue;
    }
    // The second way to wait: a count of landed hits instead of seconds.
    // Nothing is counted while the buff is active, so it can't be permanent
    // either.
    if (skill.buff().charge_lines() > 0) {
      EXPECT_EQ(skill.cooldown_seconds(), 0.0)
          << entry.first << "'s buff waits on hits and on a clock at once";
      continue;
    }
    // The third way: a roll on every landed swing. A roll that can fail is its
    // own kind of wait (a lapsed stack has to be earned again), so such a buff
    // needs no timer. One that always succeeds does.
    if (skill.buff().raise_chance() > 0.0) {
      continue;
    }
    EXPECT_GT(skill.cooldown_seconds(), 0.0)
        << entry.first << "'s buff would never be waited for";
    // A shield ends when it has blocked its hits, so its duration is only a
    // ceiling; the hit count stops it being permanent.
    if (skill.buff().shield().hits() > 0.0 ||
        skill.buff().shield().hits_per_level() > 0.0) {
      continue;
    }
    // The longest it can last, including extra time from burns: a window the
    // burns lengthen is still a window only if the cooldown is longer.
    double longest =
        skill.buff().duration_seconds() +
        skill.buff().duration_seconds_per_dot() * skill.buff().dot_count_cap();
    EXPECT_GT(skill.cooldown_seconds(), longest)
        << entry.first << "'s buff is up for longer than it waits, so it is a "
        << "passive rather than a buff";
  }
}

// A window that burns lengthen must state both the seconds each burn adds and
// the count it stops at. Either alone is a rule with no limit or a limit on
// nothing, and both need a window to lengthen.
TEST(SkillDataTest, EveryBuffLengthenedByBurnsSaysWhatStopsIt) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Buff& buff = entry.second.buff();
    if (buff.duration_seconds_per_dot() <= 0.0 && buff.dot_count_cap() <= 0) {
      continue;
    }
    EXPECT_GT(buff.duration_seconds_per_dot(), 0.0)
        << entry.first << " counts burns its window never grows with";
    EXPECT_GT(buff.dot_count_cap(), 0)
        << entry.first << " grows with every burn alight and stops at none";
    EXPECT_GT(buff.duration_seconds(), 0.0)
        << entry.first << " lengthens a window it never opens";
  }
}

// An empowered form must name what it replaces, and a form with no period or no
// damage never lands or lands for nothing.
TEST(SkillDataTest, EveryEmpoweredFormSaysHowOftenAndForHowMuch) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    for (const EmpoweredForm& form : skill.empowered_form()) {
      // A form either names the skill it upgrades or upgrades this skill's own
      // attack. A passive has no attack of its own, so it must name one.
      EXPECT_TRUE(!form.skill_name().empty() ||
                  skill.kind() != SKILL_KIND_PASSIVE)
          << entry.first << "'s empowered form takes the place of nothing";
      EXPECT_GT(form.casts_per_trigger(), 0)
          << entry.first << "'s empowered form would never be swung";
      EXPECT_GT(form.base().skill_pct(), 0.0)
          << entry.first << "'s empowered form would be swung for nothing";
      // A form that marks enemies hits exactly the enemies whose count came
      // due, so a reach set beside it is never read.
      EXPECT_FALSE(form.brands_each_enemy() && form.max_enemies() > 0)
          << entry.first << "'s empowered form states a reach it does not use";
    }
    // Two forms on one ladder must say which is which, or the second would
    // replace the first.
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

// The two timers answer different questions (how long until it can be used
// again, and how often it fires by itself), so a skill setting both means one
// of them was a mistake.
TEST(SkillDataTest, NoSkillNamesBothClocks) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.cooldown_seconds() <= 0.0) {
      continue;
    }
    EXPECT_EQ(entry.second.cast_interval_seconds(), 0.0)
        << entry.first << " both recharges and fires on its own clock";
    EXPECT_EQ(entry.second.kills_per_cast(), 0)
        << entry.first << " both recharges and fires on its own clock";
    // A passive that casts a buff is the one exception: Divine Shield activates
    // when the character is hit, not when they press anything, so the cooldown
    // is the buff's and there's nothing to press.
    EXPECT_TRUE(entry.second.kind() != SKILL_KIND_PASSIVE ||
                entry.second.has_buff())
        << entry.first << " is never used, so it never recharges";
  }
}

// Anything the character presses states its animation length. A skill on its
// own timer uses its cast interval, and a passive is never cast.
TEST(SkillDataTest, EverySwingSaysHowLongItTakes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (!HasACastAnimation(entry.second)) {
      EXPECT_EQ(entry.second.base_delay_ms(), 0)
          << entry.first << " sets a swing delay it will never be asked for";
      continue;
    }
    // A buff is cast from a sequence that paces every skill in it equally, so
    // its own animation never determines its cost. See kWaivedCastMs.
    if (RaisesOnlyABuff(entry.second)) {
      EXPECT_EQ(entry.second.base_delay_ms(), kWaivedCastMs)
          << entry.first << " charges its animation for a buff a sequence "
          << "raises in " << kWaivedCastMs << "ms";
      continue;
    }
    EXPECT_GT(entry.second.base_delay_ms(), 0)
        << entry.first << " would swing at the bare poke's speed";
    // Loose bounds to catch a value entered in seconds or frames. A fixed-delay
    // skill gets its own: GMS paces key-down skills in the low hundreds of ms
    // (Arrow Blaster is 120), while Perfect Shot's 2.12 seconds is an aiming
    // window.
    if (entry.second.fixed_delay()) {
      EXPECT_GE(entry.second.base_delay_ms(), kTickMs) << entry.first;
      EXPECT_LE(entry.second.base_delay_ms(), 2500) << entry.first;
      continue;
    }
    EXPECT_GE(entry.second.base_delay_ms(), 300) << entry.first;
    EXPECT_LE(entry.second.base_delay_ms(), 2000) << entry.first;
  }
}

// A waived cast still records the animation GMS would play: the flag says the
// animation isn't charged, and dropping the value too would state the waiver
// twice and lose the client's number.
TEST(SkillDataTest, AWaivedCastStillRecordsTheAnimationItSkips) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!skill.waives_cast()) {
      continue;
    }
    EXPECT_EQ(skill.kind(), SKILL_KIND_ATTACK)
        << entry.first << " waives a cast it never makes";
    EXPECT_GE(skill.base_delay_ms(), 300)
        << entry.first << " waives an animation it does not state";
    EXPECT_FALSE(skill.fixed_delay())
        << entry.first << " is a hold, which plays its animation";
  }
}

// A buff with its own press is charged for that press, so the value must be an
// animation, not a sequence's flat pace. Only a buff on an attack needs it;
// everything else is already cast on its own cooldown.
TEST(SkillDataTest, ABuffWithItsOwnPressStatesTheAnimation) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.buff().own_cast_delay_ms() <= 0) {
      continue;
    }
    EXPECT_EQ(skill.kind(), SKILL_KIND_ATTACK)
        << entry.first << " is raised on its own wait already, so a press of "
        << "its own says nothing";
    EXPECT_GE(skill.buff().own_cast_delay_ms(), 300)
        << entry.first << " presses faster than any animation GMS plays";
    EXPECT_LE(skill.buff().own_cast_delay_ms(), 2000) << entry.first;
    EXPECT_GT(skill.buff().duration_seconds(), 0.0)
        << entry.first << " pays for a press that puts up nothing that lapses";
  }
}

// The opening hit is a pair: a multiplier and a strike count. One without the
// other is a value nothing reads.
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

// A cast either heals with the swing it takes or casts a buff on its own timer.
// One that does neither would be a book row that does nothing.
TEST(SkillDataTest, EveryCastDoesSomethingWithTheSwingItTakes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    // A toggle uses no swing: it stays switched on, so its levers are read the
    // way a passive's are.
    if (entry.second.kind() != SKILL_KIND_ACTIVE || entry.second.toggle()) {
      continue;
    }
    EXPECT_TRUE(entry.second.base().heal_pct() > 0.0 ||
                LongestBuffDuration(entry.second.buff()) > 0.0)
        << entry.first << " spends a swing and does nothing with it";
  }
}

// Every weapon list a skill has: the one gating the whole skill, then one per
// weapon bonus. The rules below apply to each list separately.
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

// An unnamed weapon type leaves the inspect screen's requirement reading just
// "Requires", or drops it.
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

// Every attack names the weapons it needs. Every class can hold the starter
// Sword and Long Sword, so an attack with no requirement would let a magician
// cast Energy Bolt with a longsword.
TEST(SkillDataTest, EveryAttackNamesTheWeaponsItNeeds) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.kind() != SKILL_KIND_ATTACK) {
      continue;
    }
    EXPECT_GT(entry.second.required_equip_type_size(), 0)
        << entry.first << " can be swung with anything the class can hold";
  }
}

// The weapons each book's attacks use. Written out because which weapons a line
// masters is a design decision, so a new book fails here until someone makes
// it. Two lists mean two kinds of attack.
struct BookWeapons {
  JobAdvancement book;
  std::vector<std::set<EquipType>> lists;
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
      {JOB_ADVANCEMENT_SWORDMAN, {every_warrior}},
      // The Hero's V book has both: Worldreaver is the Hero's own, and Blitz
      // Shield is used by every warrior whatever weapon their line masters.
      {JOB_ADVANCEMENT_HERO_V, {kSwordAxe, every_warrior}},
      {JOB_ADVANCEMENT_FIGHTER, {kSwordAxe}},
      {JOB_ADVANCEMENT_CRUSADER, {kSwordAxe}},
      {JOB_ADVANCEMENT_HERO, {kSwordAxe}},
      {JOB_ADVANCEMENT_PAGE, {kSwordBlunt}},
      {JOB_ADVANCEMENT_WHITE_KNIGHT, {kSwordBlunt}},
      {JOB_ADVANCEMENT_PALADIN, {kSwordBlunt}},
      // The Paladin's too: Grand Guardian is the line's own.
      {JOB_ADVANCEMENT_PALADIN_V, {kSwordBlunt, every_warrior}},
      {JOB_ADVANCEMENT_SPEARMAN, {kSpears}},
      {JOB_ADVANCEMENT_BERSERKER, {kSpears}},
      {JOB_ADVANCEMENT_DARK_KNIGHT, {kSpears}},
      // The Dark Knight's V book also has both, like the Hero's: Spear of
      // Darkness is the line's own, Blitz Shield every warrior's.
      {JOB_ADVANCEMENT_DARK_KNIGHT_V, {kSpears, every_warrior}},
      {JOB_ADVANCEMENT_ARCHER, {{EQUIP_TYPE_BOW, EQUIP_TYPE_CROSSBOW}}},
      {JOB_ADVANCEMENT_HUNTER, {{EQUIP_TYPE_BOW}}},
      {JOB_ADVANCEMENT_RANGER, {{EQUIP_TYPE_BOW}}},
      {JOB_ADVANCEMENT_BOW_MASTER, {{EQUIP_TYPE_BOW}}},
      {JOB_ADVANCEMENT_CROSSBOWMAN, {{EQUIP_TYPE_CROSSBOW}}},
      {JOB_ADVANCEMENT_SNIPER, {{EQUIP_TYPE_CROSSBOW}}},
      {JOB_ADVANCEMENT_MARKSMAN, {{EQUIP_TYPE_CROSSBOW}}},
      // Perfect Shot is a crossbow shot like the rest of the line's, and the
      // Marksman's V book has no other attack.
      {JOB_ADVANCEMENT_MARKSMAN_V, {{EQUIP_TYPE_CROSSBOW}}},
      {JOB_ADVANCEMENT_ROGUE, {{EQUIP_TYPE_DAGGER}, {EQUIP_TYPE_CLAW}}},
      {JOB_ADVANCEMENT_ASSASSIN, {{EQUIP_TYPE_CLAW}}},
      {JOB_ADVANCEMENT_HERMIT, {{EQUIP_TYPE_CLAW}}},
      {JOB_ADVANCEMENT_NIGHT_LORD, {{EQUIP_TYPE_CLAW}}},
      // Shurrikane is a thrown star like the rest of the line's, and the Night
      // Lord's V book has no other attack.
      {JOB_ADVANCEMENT_NIGHT_LORD_V, {{EQUIP_TYPE_CLAW}}},
      {JOB_ADVANCEMENT_BANDIT, {{EQUIP_TYPE_DAGGER}}},
      {JOB_ADVANCEMENT_CHIEF_BANDIT, {{EQUIP_TYPE_DAGGER}}},
      {JOB_ADVANCEMENT_SHADOWER, {{EQUIP_TYPE_DAGGER}}},
      {JOB_ADVANCEMENT_SHADOWER_V, {{EQUIP_TYPE_DAGGER}}},
      {JOB_ADVANCEMENT_MAGICIAN, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_ICE_LIGHTNING_WIZARD, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_ICE_LIGHTNING_MAGE, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_ICE_LIGHTNING_ARCH_MAGE_V, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_FIRE_POISON_WIZARD, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_FIRE_POISON_MAGE, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_FIRE_POISON_ARCH_MAGE_V, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_CLERIC, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_PRIEST, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_BISHOP, {{EQUIP_TYPE_STAFF}}},
      {JOB_ADVANCEMENT_BISHOP_V, {{EQUIP_TYPE_STAFF}}},
  };
}

// Every attack in a book uses the book's own weapons, and nothing else in the
// class can use it. A book whose attacks disagree with the table above has a
// mapping mistake.
TEST(SkillDataTest, EveryAttackIsSwungWithItsBooksWeapons) {
  std::map<JobAdvancement, std::vector<std::set<EquipType>>> expected;
  for (const BookWeapons& book : ExpectedBookWeapons()) {
    expected[book.book] = book.lists;
  }
  std::set<JobAdvancement> seen;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.kind() != SKILL_KIND_ATTACK) {
      continue;
    }
    JobAdvancement book = BookOf(skill);
    seen.insert(book);
    std::map<JobAdvancement, std::vector<std::set<EquipType>>>::const_iterator
        it = expected.find(book);
    ASSERT_NE(it, expected.end())
        << entry.first << "'s book names no weapons in this test";
    std::set<EquipType> weapons = WeaponLists(skill).front();
    EXPECT_GT(std::count(it->second.begin(), it->second.end(), weapons), 0)
        << entry.first << " is not swung with its book's weapons";
  }
  // Every book in the table must still have attacks, or the table has an entry
  // for a line that no longer exists.
  for (const BookWeapons& book : ExpectedBookWeapons()) {
    EXPECT_GT(seen.count(book.book), 0u)
        << JobAdvancement_Name(book.book) << " has no attacks left";
  }
}

// A skill that grants or reduces a Final Attack uses GMS's name for it, unless
// the skill is itself called Final Attack and the heading already says so.
TEST(SkillDataTest, EveryFinalAttackIsNamedUnlessItsSkillAlreadyIs) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    bool states = skill.base().final_attack_chance() > 0.0 ||
                  skill.base().final_attack_chance_cut() > 0.0 ||
                  skill.buff().base().final_attack_chance() > 0.0;
    if (!states) {
      continue;
    }
    bool named_for_it = skill.name().find("Final Attack") != std::string::npos;
    EXPECT_EQ(skill.final_attack_label().empty(), named_for_it)
        << entry.first << " must name its Final Attack, or be named for it";
  }
}

// A bonus for a weapon the skill doesn't work with can never apply: the whole
// skill stops working before the bonus is reached.
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
      // Nothing set means nothing granted, and the skill page shows a row per
      // lever, so an empty bonus is a promise nobody can even see.
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

// The job inspect screen puts a skill card beside the 35-wide book, and the
// card is as wide as its widest label and value. No shipped skill may make it
// wider than the narrowest terminal allows.
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

// A sword requirement must name both hands, or the skill breaks when the other
// half gets items. A weapon bonus may reward one hand only, as High Paladin's
// ignored defence does blunt weapons.
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

// Held freeze stacks need a cap, an ice swing and a lightning swing available
// to the same character, not on one skill: Frost Clutch improves Freezing
// Crush's stacks.
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
      if (!ReachedBy(books, skill)) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        if (!ReachedBy(books, other.second)) {
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

// A bonus against afflicted enemies needs a book that afflicts them: Storm
// Magic reads freeze, Burning Magic reads burn.
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
      if (!ReachedBy(books, skill)) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        if (!ReachedBy(books, other.second)) {
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

// Damage based on max HP belongs to a swing, added line by line. A passive has
// no lines to add it to and would grant nothing.
TEST(SkillDataTest, OnlyASwingPaysOutOfThePool) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.base().max_hp_damage_pct() <= 0.0) {
      continue;
    }
    EXPECT_EQ(entry.second.kind(), SKILL_KIND_ATTACK)
        << entry.first << " pays out of the pool without being swung";
  }
}

// A scar needs a book that leaves one, with a duration. Chance Attack reads the
// scar Scarring Sword leaves.
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
      if (!ReachedBy(books, entry.second)) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        scars = scars || (ReachedBy(books, other.second) &&
                          other.second.base().scar_chance() > 0.0);
      }
    }
    EXPECT_TRUE(scars) << entry.first << " reads a scar nothing leaves";
  }
}

// A buff that dismisses a summon must name one the same character has, and one
// on its own timer, since a swing nobody is casting can't be dismissed.
TEST(SkillDataTest, ADismissedSummonIsOneTheBookHolds) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::vector<Job> jobs = EveryValueOf<Job>(Job_descriptor());
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const std::string& out = entry.second.buff().silences_skill_name();
    if (out.empty()) {
      continue;
    }
    const Skill* summon = nullptr;
    for (const std::pair<const std::string, Skill>& other : skills) {
      if (other.second.name() == out) {
        summon = &other.second;
      }
    }
    ASSERT_NE(summon, nullptr)
        << entry.first << " puts out \"" << out << "\", which no book holds";
    EXPECT_EQ(summon->kind(), SKILL_KIND_AUTO_ATTACK)
        << entry.first << " puts out " << out << ", which is no summon";
    bool together = false;
    for (Job job : jobs) {
      std::set<JobAdvancement> books = BooksFor(job);
      together = together ||
                 (ReachedBy(books, entry.second) && ReachedBy(books, *summon));
    }
    EXPECT_TRUE(together) << entry.first << " puts out " << out
                          << ", which nobody holds with it";
  }
}

// Whether the skill has `tag`.
bool Carries(const Skill& skill, SkillTag tag) {
  for (int i = 0; i < skill.tags_size(); ++i) {
    if (skill.tags(i) == tag) {
      return true;
    }
  }
  return false;
}

// A mark nobody can use up is a status with no reader, and a mark worth nothing
// to the line that uses it isn't a mark. Both parts must be in the same book,
// as the scar test above requires of its pair.
TEST(SkillDataTest, AMarkIsBothLeftAndSpendable) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::vector<Job> jobs = EveryValueOf<Job>(Job_descriptor());
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Mark& mark = entry.second.mark();
    if (mark.duration_seconds() <= 0.0 && mark.final_dmg_pct() <= 0.0) {
      continue;
    }
    EXPECT_GT(mark.duration_seconds(), 0.0)
        << entry.first << " marks for no time at all";
    EXPECT_GT(mark.final_dmg_pct(), 0.0)
        << entry.first << " leaves a mark worth nothing to spend";
    ASSERT_NE(mark.lifted_tag(), SKILL_TAG_UNSPECIFIED)
        << entry.first << " leaves a mark nothing can spend";
    bool spends = false;
    for (Job job : jobs) {
      std::set<JobAdvancement> books = BooksFor(job);
      if (!ReachedBy(books, entry.second)) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        spends = spends || (ReachedBy(books, other.second) &&
                            Carries(other.second, mark.lifted_tag()));
      }
    }
    EXPECT_TRUE(spends) << entry.first << " marks for a book that cannot spend";
  }
}

// A freeze applies to what a strike hit, so a passive that hits nobody can't
// leave one. It doesn't have to be tagged ice: that tag marks a strike that
// feeds the I/L's Freeze Stacks, and Frostprey freezes as a bird instead.
TEST(SkillDataTest, OnlyAStrikeFreezes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.freeze_seconds() <= 0.0) {
      continue;
    }
    EXPECT_TRUE(StrikesSomething(skill))
        << entry.first << " freezes what it never attacks";
  }
}

// An element says what a strike does to the Freeze Stack pile, so a passive
// carries none. A summon's blizzard is ice too, so a pulse may.
TEST(SkillDataTest, OnlyAStrikeCarriesAnElement) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    for (int i = 0; i < skill.tags_size(); ++i) {
      if (skill.tags(i) != SKILL_TAG_ICE &&
          skill.tags(i) != SKILL_TAG_LIGHTNING) {
        continue;
      }
      EXPECT_TRUE(StrikesSomething(skill))
          << entry.first << " is marked with an element but never attacks";
    }
  }
}

// A pulse is its timer and its damage together; its reach and strikes are only
// read with a timer. A swing-driven pulse may not also have its own timer, or
// follow a skill nobody swings.
TEST(SkillDataTest, EveryBuffPulseStatesTheClockItTicksOn) {
  std::map<std::string, Skill> skills = LoadSkills();
  std::set<std::string> names;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    names.insert(entry.second.name());
  }
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const BuffPulse& pulse = entry.second.buff().pulse();
    if (Pulses(pulse)) {
      EXPECT_GT(pulse.base().skill_pct(), 0.0)
          << entry.first << " ticks and deals nothing";
      EXPECT_FALSE(pulse.label().empty())
          << entry.first << " bleeds under no name";
      if (!pulse.paced_by_skill_name().empty()) {
        EXPECT_EQ(pulse.cast_interval_seconds(), 0.0)
            << entry.first << " rides a swing and keeps a clock of its own";
        EXPECT_GT(names.count(pulse.paced_by_skill_name()), 0u)
            << entry.first << " rides \"" << pulse.paced_by_skill_name()
            << "\", which no skill answers to";
      }
      // A ramp needs a step and a cap: either alone means the skill claims
      // something it never pays.
      EXPECT_EQ(pulse.skill_pct_per_repeat() > 0.0, pulse.max_repeats() > 0)
          << entry.first << " ramps by half a rule";
      // The extra strike happens at the top of the ramp when the count runs
      // out, so it needs both to land at all.
      if (pulse.final_repeat_strike()) {
        EXPECT_GT(pulse.max_pulses(), 0)
            << entry.first << " goes out on a count it never keeps";
        EXPECT_GT(pulse.max_repeats(), 0)
            << entry.first << " goes out at the top of no ramp";
      }
      // The final strike lands when the count runs out, so it needs a count,
      // and it states its label and damage like the pulse does.
      if (pulse.has_final_strike()) {
        EXPECT_GT(pulse.max_pulses(), 0)
            << entry.first << " bursts on a count it never keeps";
        EXPECT_GT(pulse.final_strike().base().skill_pct(), 0.0)
            << entry.first << " bursts for nothing";
        EXPECT_FALSE(pulse.final_strike().label().empty())
            << entry.first << " bursts under no name";
      }
      continue;
    }
    EXPECT_EQ(pulse.lines(), 0) << entry.first << " strikes on no clock";
    EXPECT_EQ(pulse.casts(), 0) << entry.first << " strikes on no clock";
    EXPECT_EQ(pulse.max_enemies(), 0) << entry.first << " reaches on no clock";
    EXPECT_EQ(pulse.max_pulses(), 0) << entry.first << " runs out of no clock";
    EXPECT_EQ(pulse.max_repeats(), 0) << entry.first << " ramps on no clock";
    EXPECT_EQ(pulse.fixed_strikes().hits(), 0)
        << entry.first << " scatters on no clock";
    EXPECT_FALSE(pulse.has_final_strike())
        << entry.first << " goes out of no clock";
  }
  // Only a buff's own pulse may be driven by a swing. A stance's pulse starts
  // and stops with the stance, and nothing reads a swing timer from one.
  for (const std::pair<const std::string, Skill>& entry : skills) {
    for (const Stance& stance : entry.second.buff().stance()) {
      EXPECT_TRUE(stance.pulse().paced_by_skill_name().empty())
          << entry.first << "'s form rides a swing";
    }
  }
}

// A per-orb bonus needs orbs available to the same character, though not on one
// skill: Combo Synergy prices the orbs Combo Attack gives.
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
      if (!ReachedBy(books, skill)) {
        continue;
      }
      for (const std::pair<const std::string, Skill>& other : skills) {
        paid = paid || (other.second.combo_orbs() > 0 &&
                        ReachedBy(books, other.second));
      }
    }
    EXPECT_TRUE(paid) << entry.first
                      << " prices Combo Orbs no character holding it carries";
  }
}

// A ladder in whole levels must end on a whole number. Its fractional step
// leaves the top just under it, reached only through the floor's epsilon, so
// shortening the literal makes the last level worth nothing.
TEST(SkillDataTest, ABonusLevelLadderEndsOnAWholeLevel) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    double step = skill.per_level().skill_level_bonus();
    if (skill.base().skill_level_bonus() <= 0.0 || step <= 0.0) {
      continue;
    }
    double top =
        skill.base().skill_level_bonus() + step * (skill.max_level() - 1);
    EXPECT_NEAR(top, std::round(top), 1e-9)
        << entry.first << " ends its ladder between two levels";
    EXPECT_GT(top, 1.0) << entry.first << " never climbs at all";
  }
}

// GMS lets granted levels take a skill past its master level only in the 4th
// job, and only when the master level is 10 or more. Otherwise the skill stops
// at its master level, however many levels are granted.
constexpr int kSmallestMasterLevelPastIt = 10;

// The 4th job skills Combat Orders doesn't raise, by file stem. Infinity is our
// choice, as the largest single lever in any book. The last three are GMS
// passives with master level 1 stretched to ten, with no level above to reach.
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

// Neither mistake shows: a 4th job skill missing the flag stops two levels
// short, and a lower one with it gets two levels nobody wrote.
TEST(SkillDataTest, OnlyA4thJobSkillPassesItsMasterLevel) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    // A hyper skill is in a 4th job book but gets no granted levels at all, so
    // the flag would do nothing on it. See TakesGrantedLevels.
    bool eligible = StageForAdvancement(BookOf(skill)) == 4 && !skill.hyper() &&
                    skill.v_node() == V_NODE_KIND_UNSPECIFIED &&
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

// A line is whole, so a rate too small to add one before the master level is a
// lever that looks like it grows but never does.
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

// A passive with a swing's damage is data nothing reads. Meso Explosion is the
// exception and says why.
TEST(SkillDataTest, APassiveCarriesNoSwingsDamage) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.kind() != SKILL_KIND_PASSIVE) {
      continue;
    }
    EXPECT_EQ(skill.base().skill_pct(), 0.0)
        << entry.first << " is a passive carrying a swing's damage";
    if (skill.base().meso_hit_pct() > 0.0) {
      continue;
    }
    EXPECT_EQ(skill.base().normal_skill_pct(), 0.0)
        << entry.first << " is a passive carrying a swing's damage";
  }
}

// The skill an empowered form targets must be one the same character can learn;
// otherwise the form upgrades nothing.
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

// The levers read from SkillBoost::effect. Anything else written there is never
// applied; see SkillBoost::effect.
bool BoostEffectIsSupported(const SkillEffect& effect, std::string& unread) {
  static const std::set<std::string> kRead = {
      "skill_pct", "damage_pct", "boss_pct",      "normal_pct",
      "ied_pct",   "crit_rate",  "final_dmg_pct", "final_attack_chance"};
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

// Whether any skill in the catalog upgrades `name` with an empowered form. The
// form may come from a different skill, so the target's own page doesn't show
// it.
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

// Whether `name` lands anything besides its own lines: an opening hit, an extra
// hit, or either of those in an empowered form.
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

// A boost must name a skill the same character can have and give it something:
// a strike, reach, a timer or a lever only that skill gets.
TEST(SkillDataTest, EverySkillBoostNamesAHoldableSkill) {
  std::map<std::string, Skill> skills = LoadSkills();
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    std::vector<const SkillBoost*> granted;
    for (const SkillBoost& boost : entry.second.boost()) {
      granted.push_back(&boost);
    }
    for (const SkillBoost& boost : entry.second.buff().boost()) {
      granted.push_back(&boost);
    }
    for (const SkillBoost* held : granted) {
      const SkillBoost& boost = *held;
      EXPECT_FALSE(boost.skill_name().empty())
          << entry.first << " grants strikes to nobody";
      EXPECT_TRUE(
          boost.lines() > 0 || boost.extra_hit_lines() > 0 ||
          boost.max_enemies() > 0 || boost.max_enemies_per_level() > 0.0 ||
          boost.attacks_per_cast() > 0 || boost.cooldown_pct() > 0.0 ||
          boost.dot_skill_pct() != 0.0 || boost.dot_duration_seconds() != 0.0 ||
          boost.buff_duration_seconds() != 0.0 || boost.shield_hits() != 0.0 ||
          boost.shield_boss_damage_taken_pct() != 0.0 ||
          boost.final_attack_chance_mult() != 0.0 || boost.has_effect() ||
          boost.extra_hit_size() > 0)
          << entry.first << " names " << boost.skill_name()
          << " and hands it nothing";
      // Scaling a rate to zero, or negative, isn't a grant. GMS's only one
      // doubles.
      EXPECT_FALSE(boost.final_attack_chance_mult() != 0.0 &&
                   boost.final_attack_chance_mult() < 1.0)
          << entry.first << " scales " << boost.skill_name()
          << "'s Final Attack rate down";
      // A share of a cooldown, so a full one would remove the cooldown entirely
      // and a value above one would make it negative.
      EXPECT_LT(boost.cooldown_pct(), 1.0)
          << entry.first << " takes the whole of " << boost.skill_name()
          << "'s wait away";
      // A per-level step with no level-1 value is half a lever.
      EXPECT_FALSE(boost.has_effect_per_level() && !boost.has_effect())
          << entry.first << " climbs a lever it never grants";
      // A gate above the granting skill's max level is a bonus nobody ever
      // gets.
      EXPECT_LE(boost.min_level(), entry.second.max_level())
          << entry.first << " gates what it hands " << boost.skill_name()
          << " behind a level it never reaches";
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
      // Lines for hits the named skill doesn't land would never be used. The
      // form's hits count too, since a form lands its parent's second hits as
      // well as its own: Snipe's mark belongs to the empowered shot.
      if (boost.extra_hit_lines() > 0) {
        EXPECT_TRUE(LandsASecondHit(skills, boost.skill_name()))
            << entry.first << " adds a strike to " << boost.skill_name()
            << "'s second hits, which it does not land";
      }
      // Naming a form the skill doesn't have reaches nothing.
      if (boost.reach() != BOOST_REACH_ORDINARY) {
        EXPECT_TRUE(HasEmpoweredForm(skills, boost.skill_name()))
            << entry.first << " aims at " << boost.skill_name()
            << "'s empowered form, which nothing gives it";
      }
      // A timer given to a skill that doesn't use one is never read, and
      // neither is a cooldown reduction for a skill with no cooldown.
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
        // Points on a burn tick that never happens, or seconds added to a burn
        // that is never applied, are never used.
        if (boost.dot_skill_pct() != 0.0 ||
            boost.dot_duration_seconds() != 0.0) {
          EXPECT_GT(target.second.dot().interval_seconds(), 0.0)
              << entry.first << " lifts " << target.first
              << "'s burn, which it does not leave";
        }
        // Doubling a rate the named skill doesn't have doubles nothing.
        if (boost.final_attack_chance_mult() != 0.0) {
          EXPECT_GT(target.second.base().final_attack_chance(), 0.0)
              << entry.first << " doubles " << target.first
              << "'s Final Attack rate, which it does not have";
        }
      }
    }
  }
  EXPECT_GT(checked, 0) << "no skill in the catalog grants strikes or reach";
}

// A group of one is a typo: the label ties skills together, and a skill alone
// in a group competes with nothing while looking like it does. Every member
// writes the same string, so a typo shows up here.
TEST(SkillDataTest, EveryExclusiveGroupHoldsMoreThanOneSkill) {
  std::map<std::string, int> members;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (!entry.second.exclusive_group().empty()) {
      ++members[entry.second.exclusive_group()];
    }
  }
  EXPECT_FALSE(members.empty()) << "no skill in the catalog names a group";
  for (const std::pair<const std::string, int>& group : members) {
    EXPECT_GT(group.second, 1)
        << "\"" << group.first << "\" holds one skill and thins nothing";
  }
}

// Superseding is the bluntest thing one skill can do to another (the named
// skill stops granting anything), so it may only name a skill the same
// character can have, and never itself.
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

// Evil Eye Shock II starts at 139% against a maxed Evil Eye Shock's 150%. That
// is GMS's own ladder and the skill is meant to be maxed, so it is kept.
const std::set<std::string>& SupersedesBelowWhatItReplaces() {
  static const std::set<std::string>* kStems =
      new std::set<std::string>{"evil_eye_shock_ii"};
  return *kStems;
}

// A superseding skill's first level must beat the replaced skill's last on
// every shared lever, or the point is a silent step backwards. GMS starts
// Advanced Final Attack at 41% against Final Attack's 40% for this reason.
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
      // Both the character's part and the party's, for the same reason: a
      // Bishop's first point in Blessed Harmony must not cost the party the EXP
      // Blessed Ensemble was giving.
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
          // The replacing part may be an auto mode: Evil Eye Shock III is one
          // of Revenge of the Evil Eye's modes. Allies never read modes, so
          // only this part.
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

// The catalog is keyed by file stem but learned levels by display name, so a
// later stage reusing an earlier stage's name would share its level.
TEST(SkillDataTest, OneSkillPerNamePerCharacter) {
  std::map<std::string, Skill> skills = LoadSkills();
  for (Job job : EveryValueOf<Job>(Job_descriptor())) {
    std::set<JobAdvancement> books = BooksFor(job);
    std::map<std::string, std::string> stem_by_name;
    for (const std::pair<const std::string, Skill>& entry : skills) {
      if (!ReachedBy(books, entry.second)) {
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

// Every 4th job mastery climbs one ladder, 51% at level 1 to 70% at 20, so no
// branch masters its weapon better for no visible reason.
TEST(SkillDataTest, EveryFourthJobMasteryClimbsTheSameLadder) {
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (StageForAdvancement(BookOf(skill)) != 4 ||
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

// The skills GMS gives to the party, by display name. Written out so a skill
// removed from the data only keeps its ally part if someone notices.
//
// The two casts are left out, since a caster's actions don't reach an ally's
// fight, and so is Dispel, whose cure is display-only.
const char* const kPartySkills[] = {
    "Absolute Zero Aura",
    "Advanced Blessing",
    // Three of the Bishop's hypers, which reach the party the same way as the
    // skill they are named for: GMS boosts the buff, not the caster, so the
    // bonus reaches whoever the buff reaches.
    "Advanced Blessing - Boss Rush",
    "Advanced Blessing - Extra Point",
    "Advanced Blessing - Ferocity",
    "Angel Ray",
    "Angel of Balance",
    // The only party effect that grows with the caster's INT instead of their
    // level, and is then split among everyone in it.
    "Benediction",
    "Bless",
    "Blessed Ensemble",
    "Blessed Harmony",
    "Combat Orders",
    // The only grant that goes to a single member instead of all of them.
    "Divine Echo",
    "Hex of the Evil Eye",
    "Holy Fountain",
    "Holy Magic Shell",
    "Holy Symbol",
    // The same again. The fourth Holy Symbol hyper, Experience, is missing
    // because GMS gives that one to the caster only.
    "Holy Symbol - Item Drop",
    "Holy Water",
    "Meditation",
    "Parashock Guard",
    // The light reaches whoever it touches, including the Bishop, and gives
    // them the same final damage it gives the caster.
    "Peacemaker",
    "Puncture",
    "Sharp Eyes",
    // The two Hyper Skills that reach the party through the buff they name. GMS
    // stores their values on Sharp Eyes itself, not as a bonus to the caster's
    // own damage, so they reach whoever the buff reaches.
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

// A skill that affects the party says so, and one that says so does. The two
// excused skills are ones GMS excuses too.
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

// Parashock Guard is the only skill whose own effect needs a party, and it must
// also have an ally effect: a skill that needs company but gives it nothing is
// one nobody would ever level.
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

// Two allies with the same buff give one buff. The only effects that stack
// instead reward the company kept, and only the Cleric's line has them: Blessed
// Ensemble, and Blessed Harmony, which includes all of it.
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

// The Evil Eye attacks once however far the book is learned: GMS's Shock II and
// Revenge restate the base skill with more damage rather than adding attacks.
TEST(SkillDataTest, TheEvilEyeShoutsOnce) {
  std::map<std::string, Skill> skills = LoadSkills();
  EXPECT_EQ(skills.at("evil_eye_shock_ii").supersedes_skill_name(),
            "Evil Eye Shock");
  EXPECT_EQ(skills.at("revenge_of_the_evil_eye").supersedes_skill_name(),
            "Evil Eye Shock II");
  // The base skill keeps its own timer; the last skill in the chain takes two
  // seconds off it.
  EXPECT_EQ(skills.at("evil_eye_shock").cast_interval_seconds(), 12.0);
  EXPECT_EQ(skills.at("evil_eye_shock_ii").cast_interval_seconds(), 12.0);
  const Skill& revenge = skills.at("revenge_of_the_evil_eye");
  ASSERT_EQ(revenge.auto_mode_size(), 2);
  EXPECT_EQ(revenge.auto_mode(0).label(), "Shock III");
  EXPECT_EQ(revenge.auto_mode(0).cast_interval_seconds(), 10.0);
}

// The save this rebalance was written for: a Berserker with Lord of Darkness at
// 20 under the old book. It now caps at 10, and the ten freed points can only
// go to Evil Eye of Domination.
TEST(SkillDataTest, AMaxedBerserkerBookRebalancesOntoDomination) {
  std::map<std::string, Skill> skills = LoadSkills();
  Character proto;
  proto.set_job(JOB_BERSERKER);
  proto.set_job_stage(3);
  proto.set_level(100);
  // The old book, as a save from before Domination existed has it.
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
