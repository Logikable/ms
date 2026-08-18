// Checks the shipped skill catalog rather than any one function: every job's
// book has to cost exactly the SP that job earns, and that only holds if the
// data says so. Arithmetic done by hand in a textproto is the sort of thing
// that rots the moment a skill is added.
#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "src/character/character.h"
#include "src/combat/constants.h"
#include "src/combat/damage.h"
#include "src/frontend/widgets/panel_util.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

// What a job stage's levels pay out, indexed by stage, with the advancement
// itself granting nothing. Levels 11-30 feed stage 1, 31-60 feed stage 2 and
// 61-100 feed stage 3, at 3 SP a level -- so 60, then 90, then 120. The 4th
// job's 101-140 pay 5 a level, so its book is 200.
constexpr int kSpByStage[] = {0, 60, 90, 120, 200};

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
  for (int stage = 1; stage <= 4; ++stage) {
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

// A skill's levers at `level`, on the ladder every reader climbs:
// base + per_level * (L - 1).
SkillEffect EffectAt(const Skill& skill, int level) {
  SkillEffect at = skill.base();
  const google::protobuf::Descriptor* levers = SkillEffect::descriptor();
  const google::protobuf::Reflection* reflection = at.GetReflection();
  for (int i = 0; i < levers->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = levers->field(i);
    double climbed = LeverValue(skill.base(), field) +
                     LeverValue(skill.per_level(), field) * (level - 1);
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
// swing on. A skill that only puts a buff up is cast on its own clock and
// takes no swing, so nothing ever asks how long its animation is.
bool SpendsASwing(const Skill& skill) {
  return skill.kind() == SKILL_KIND_ATTACK ||
         (skill.kind() == SKILL_KIND_ACTIVE && skill.base().heal_pct() > 0.0);
}

std::map<std::string, Skill> LoadSkills() {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&err));
  EXPECT_NE(runfiles, nullptr) << err;
  return LoadTextProtoDir<Skill>(runfiles->Rlocation("ms/data/skills"));
}

TEST(SkillDataTest, EverySkillBelongsToAnAdvancement) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    EXPECT_NE(entry.second.job_advancement(), JOB_ADVANCEMENT_UNSPECIFIED)
        << entry.first << " would be unreachable: no tab shows it and no SP "
        << "pool buys it";
  }
}

// The kind decides what the skill does in a fight and what tag opens its row
// in the book, so an unset one is a skill that neither fights nor says what it
// is.
TEST(SkillDataTest, EverySkillNamesItsKind) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    EXPECT_NE(entry.second.kind(), SKILL_KIND_UNSPECIFIED)
        << entry.first << " would list with no tag and do nothing";
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
    cost_by_advancement[entry.second.job_advancement()] +=
        entry.second.max_level();
  }
  // Every advancement has to turn up, not just sum correctly: the skills sit in
  // a folder per job, and a folder that stopped being read would drop one out
  // of the map entirely and leave the rest to pass on their own.
  for (JobAdvancement advancement :
       EveryValueOf<JobAdvancement>(JobAdvancement_descriptor())) {
    EXPECT_TRUE(cost_by_advancement.count(advancement))
        << "advancement " << advancement << " has no skills at all";
  }
  for (const std::pair<const int, int>& entry : cost_by_advancement) {
    int stage = StageForAdvancement(static_cast<JobAdvancement>(entry.first));
    ASSERT_GT(stage, 0) << "advancement " << entry.first << " has no stage";
    ASSERT_LT(stage,
              static_cast<int>(sizeof(kSpByStage) / sizeof(kSpByStage[0])))
        << "stage " << stage << " has no SP figure to be held to";
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
TEST(SkillDataTest, EveryRequirementNamesASkillTheSameCharacterCanHold) {
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
  std::map<int, std::map<int, std::string>> by_advancement;
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    int order = entry.second.skill_order();
    EXPECT_GT(order, 0) << entry.first << " has no place in its book";
    std::pair<std::map<int, std::string>::iterator, bool> added =
        by_advancement[entry.second.job_advancement()].insert(
            {order, entry.first});
    EXPECT_TRUE(added.second)
        << entry.first << " and " << added.first->second << " both sit at "
        << order << " of advancement " << entry.second.job_advancement();
  }
  for (const std::pair<const int, std::map<int, std::string>>& book :
       by_advancement) {
    int expected = 1;
    for (const std::pair<const int, std::string>& entry : book.second) {
      EXPECT_EQ(entry.first, expected)
          << entry.second << " leaves a gap in advancement " << book.first;
      ++expected;
    }
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
    EXPECT_GT(skill.cooldown_seconds(), 0.0)
        << entry.first << "'s buff would never be waited for";
    EXPECT_GT(skill.cooldown_seconds(), skill.buff().duration_seconds())
        << entry.first << "'s buff is up for longer than it waits, so it is a "
        << "passive rather than a buff";
  }
}

// An empowered form is aimed by boosts_skill_name, so it is nothing without
// one -- and a form with no period or no damage is a swing that never lands or
// lands for nothing.
TEST(SkillDataTest, EveryEmpoweredFormSaysHowOftenAndForHowMuch) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (!skill.has_empowered_form()) {
      continue;
    }
    // A form either names the skill it upgrades or upgrades the one carrying
    // it -- and a passive has no attack of its own to upgrade, so it must name.
    EXPECT_TRUE(!skill.boosts_skill_name().empty() ||
                skill.kind() != SKILL_KIND_PASSIVE)
        << entry.first << "'s empowered form takes the place of nothing";
    EXPECT_GT(skill.empowered_form().casts_per_trigger(), 0)
        << entry.first << "'s empowered form would never be swung";
    EXPECT_GT(skill.empowered_form().base().skill_pct(), 0.0)
        << entry.first << "'s empowered form would be swung for nothing";
    // Marking enemies, the form goes exactly as far as the ones that came due,
    // so a reach beside it is a number nothing reads.
    EXPECT_FALSE(skill.empowered_form().brands_each_enemy() &&
                 skill.empowered_form().max_enemies() > 0)
        << entry.first << "'s empowered form states a reach it does not use";
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
    EXPECT_NE(entry.second.kind(), SKILL_KIND_PASSIVE)
        << entry.first << " is never used, so it never recharges";
  }
}

// Anything spending a swing takes as long as its own animation, so it has to
// say how long that is -- the attacks, and the casts that spend a swing on
// something else. Nothing else does: the delay of a skill on its own clock is
// its cast interval, a passive is never swung at all, and a skill that only
// puts a buff up costs the character no swing either.
TEST(SkillDataTest, EverySwingSaysHowLongItTakes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (!SpendsASwing(entry.second)) {
      EXPECT_EQ(entry.second.base_delay_ms(), 0)
          << entry.first << " sets a swing delay it will never be asked for";
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
    if (entry.second.kind() != SKILL_KIND_ACTIVE) {
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
                       skill.base().final_dmg_pct_per_combo_orb() > 0.0;
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

// The 4th job skills GMS does NOT mark, by file stem. Combat Orders is GMS's
// mechanic and GMS says which skills take it, so a book with one of these in
// it is not a mistake -- and naming them here keeps the check strict for the
// rest rather than weakening it to a direction that catches nothing.
const char* const kHeldToTheirMasterLevel[] = {"enchanted_quiver"};

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

// A skill naming another one has to name one that exists, and the two halves
// of the bargain have to both be there: a name with no damage behind it grants
// nothing, and damage with no name has nowhere to go.
TEST(SkillDataTest, EveryBoostNamesASkillTheSameCharacterCanHold) {
  std::map<std::string, Skill> skills = LoadSkills();
  for (const std::pair<const std::string, Skill>& entry : skills) {
    const Skill& skill = entry.second;
    bool named = !skill.boosts_skill_name().empty();
    // A skill pays a boost either way it can: a percentage on every swing of
    // the named skill, or a bigger swing standing in for every Nth. Divine
    // Judgment pays only the second -- its whole effect is the detonation.
    bool percent = skill.base().boosted_skill_pct() > 0.0;
    bool form = skill.empowered_form().casts_per_trigger() > 0;
    EXPECT_FALSE(percent && !named)
        << entry.first << " pays a boost with nowhere to send it";
    // An empty name with a form is the skill upgrading its own attack, which
    // is what Creeping Toxin does. An empty name with neither is nothing.
    EXPECT_FALSE(named && !percent && !form)
        << entry.first << " names a skill it hands nothing to";
    if (!named) {
      continue;
    }
    EXPECT_TRUE(SameCharacterCanHold(skills, skill, skill.boosts_skill_name()))
        << entry.first << " boosts \"" << skill.boosts_skill_name()
        << "\", which no character holding it can learn";
  }
}

// The same rule for the structural half of a boost: strikes and reach handed
// to a skill nobody holding this one can learn are strikes nobody ever swings.
TEST(SkillDataTest, EverySkillBoostNamesASkillTheSameCharacterCanHold) {
  std::map<std::string, Skill> skills = LoadSkills();
  int checked = 0;
  for (const std::pair<const std::string, Skill>& entry : skills) {
    for (const SkillBoost& boost : entry.second.boost()) {
      EXPECT_FALSE(boost.skill_name().empty())
          << entry.first << " grants strikes to nobody";
      EXPECT_TRUE(boost.lines() > 0 || boost.max_enemies() > 0 ||
                  boost.max_enemies_per_level() > 0.0)
          << entry.first << " names " << boost.skill_name()
          << " and hands it nothing";
      ++checked;
      EXPECT_TRUE(
          SameCharacterCanHold(skills, entry.second, boost.skill_name()))
          << entry.first << " grants strikes to \"" << boost.skill_name()
          << "\", which no character holding it can learn";
    }
  }
  EXPECT_GT(checked, 0) << "no skill in the catalog grants strikes or reach";
}

// Superseding is the bluntest thing one skill can do to another -- the named
// skill stops paying at all -- so it may only name a skill the same character
// can hold, and never itself. A self-reference would leave a book that
// silently teaches nothing.
TEST(SkillDataTest, EverySupersededSkillIsOneTheSameCharacterCanHold) {
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
    if (skill.supersedes_skill_name().empty()) {
      continue;
    }
    for (const std::pair<const std::string, Skill>& other : skills) {
      if (other.second.name() != skill.supersedes_skill_name()) {
        continue;
      }
      ++checked;
      SkillEffect replaced = EffectAt(other.second, other.second.max_level());
      SkillEffect replacing = EffectAt(skill, 1);
      for (int i = 0; i < levers->field_count(); ++i) {
        const google::protobuf::FieldDescriptor* field = levers->field(i);
        double was = LeverValue(replaced, field);
        double now = LeverValue(replacing, field);
        if (was <= 0.0) {
          continue;
        }
        EXPECT_GE(now, was)
            << entry.first << " supersedes " << other.first << " but pays "
            << now << " of " << field->name() << " where it paid " << was;
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

}  // namespace
}  // namespace ms
