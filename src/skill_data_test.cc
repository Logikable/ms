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
#include "src/frontend/widgets/panel_util.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/skill.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

// What a job stage's levels pay out, indexed by stage: 3 SP a level over the
// span that feeds it, with the advancement itself granting nothing. Levels
// 11-30 feed stage 1, 31-60 feed stage 2 and 61-100 feed stage 3 -- so 60,
// then 90, then 120.
constexpr int kSpByStage[] = {0, 60, 90, 120};

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
  for (int stage = 1; stage <= 3; ++stage) {
    JobAdvancement advancement = AdvancementForJobStage(job, stage);
    if (advancement != JOB_ADVANCEMENT_UNSPECIFIED) {
      books.insert(advancement);
    }
  }
  return books;
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

// An auto-attack with no interval never fires, so a skill that means to be one
// and forgets to say how often is a skill that silently does nothing.
TEST(SkillDataTest, EveryAutoAttackSaysHowOftenItFires) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.kind() != SKILL_KIND_AUTO_ATTACK) {
      EXPECT_EQ(entry.second.cast_interval_seconds(), 0.0)
          << entry.first << " sets an interval it will never be asked for";
      continue;
    }
    EXPECT_GT(entry.second.cast_interval_seconds(), 0.0)
        << entry.first << " would never fire";
    EXPECT_GT(entry.second.base().skill_pct(), 0.0)
        << entry.first << " would fire for nothing";
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
// its cast interval, and a passive is never swung at all.
TEST(SkillDataTest, EverySwingSaysHowLongItTakes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.kind() != SKILL_KIND_ATTACK &&
        entry.second.kind() != SKILL_KIND_ACTIVE) {
      EXPECT_EQ(entry.second.base_delay_ms(), 0)
          << entry.first << " sets a swing delay it will never be asked for";
      continue;
    }
    EXPECT_GT(entry.second.base_delay_ms(), 0)
        << entry.first << " would swing at the bare poke's speed";
    // Loose bounds either side of every animation GMS has for a 1st or 2nd job
    // attack, to catch a figure entered in seconds or in frames.
    EXPECT_GE(entry.second.base_delay_ms(), 300) << entry.first;
    EXPECT_LE(entry.second.base_delay_ms(), 2000) << entry.first;
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

// A cast takes the swing an attack would have had, so one with no lever behind
// it would cost the character a swing and give nothing back. The encounter
// declines to offer such a skill at all; this says none is written.
TEST(SkillDataTest, EveryCastDoesSomethingWithTheSwingItTakes) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    if (entry.second.kind() != SKILL_KIND_ACTIVE) {
      continue;
    }
    EXPECT_GT(entry.second.base().heal_pct(), 0.0)
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
TEST(SkillDataTest, AWeaponDemandCoversBothHands) {
  const std::pair<EquipType, EquipType> kPairs[] = {
      {EQUIP_TYPE_ONE_HANDED_SWORD, EQUIP_TYPE_TWO_HANDED_SWORD},
      {EQUIP_TYPE_ONE_HANDED_AXE, EQUIP_TYPE_TWO_HANDED_AXE},
      {EQUIP_TYPE_ONE_HANDED_BLUNT, EQUIP_TYPE_TWO_HANDED_BLUNT},
  };
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    for (const std::set<EquipType>& demanded : WeaponLists(entry.second)) {
      for (const std::pair<EquipType, EquipType>& pair : kPairs) {
        EXPECT_EQ(demanded.count(pair.first), demanded.count(pair.second))
            << entry.first << " takes one hand's "
            << FormatEquipType(pair.second) << " and not the other's";
      }
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

// Damage belongs to the things that swing, and the levers belong to the
// passives. A skill filed on the wrong side of that carries data nothing will
// ever read.
TEST(SkillDataTest, DamageAndPassiveLeversDoNotCross) {
  for (const std::pair<const std::string, Skill>& entry : LoadSkills()) {
    const Skill& skill = entry.second;
    if (skill.kind() == SKILL_KIND_PASSIVE) {
      EXPECT_EQ(skill.base().skill_pct(), 0.0)
          << entry.first << " is a passive carrying a swing's damage";
      EXPECT_EQ(skill.base().normal_skill_pct(), 0.0)
          << entry.first << " is a passive carrying a swing's damage";
    } else {
      EXPECT_EQ(skill.base().final_attack_chance(), 0.0)
          << entry.first << " is not a passive, so its levers go unread";
      EXPECT_EQ(skill.base().mastery(), 0.0) << entry.first;
      EXPECT_EQ(skill.base().str(), 0) << entry.first;
      EXPECT_EQ(skill.base().final_dmg_pct_per_combo_orb(), 0.0) << entry.first;
      EXPECT_EQ(skill.base().def_pct(), 0.0) << entry.first;
    }
  }
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

}  // namespace
}  // namespace ms
