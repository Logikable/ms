#include "src/character/job_branch.h"

#include "src/protos/character.pb.h"

namespace ms {

JobBranch BranchOf(Job job) {
  // The one roster in the game. The static_assert is the tripwire -- Clang
  // cannot check the switch itself, because -Wswitch over a proto enum demands
  // the two DO_NOT_USE sentinels as well.
  static_assert(Job_ARRAYSIZE == 36, "a new job needs its branch here");
  switch (job) {
    case JOB_BEGINNER:
      return JobBranch::kBeginner;
    case JOB_SWORDMAN:
    case JOB_FIGHTER:
    case JOB_PAGE:
    case JOB_SPEARMAN:
    case JOB_BERSERKER:
    case JOB_CRUSADER:
    case JOB_WHITE_KNIGHT:
    case JOB_DARK_KNIGHT:
    case JOB_PALADIN:
    case JOB_HERO:
      return JobBranch::kWarrior;
    case JOB_MAGICIAN:
    case JOB_ICE_LIGHTNING_WIZARD:
    case JOB_FIRE_POISON_WIZARD:
    case JOB_CLERIC:
    case JOB_ICE_LIGHTNING_MAGE:
    case JOB_FIRE_POISON_MAGE:
    case JOB_PRIEST:
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
    case JOB_FIRE_POISON_ARCH_MAGE:
    case JOB_BISHOP:
      return JobBranch::kMagician;
    case JOB_ARCHER:
    case JOB_HUNTER:
    case JOB_CROSSBOWMAN:
    case JOB_RANGER:
    case JOB_SNIPER:
    case JOB_BOW_MASTER:
    case JOB_MARKSMAN:
      return JobBranch::kArcher;
    case JOB_ROGUE:
    case JOB_ASSASSIN:
    case JOB_BANDIT:
    case JOB_HERMIT:
    case JOB_CHIEF_BANDIT:
    case JOB_NIGHT_LORD:
    case JOB_SHADOWER:
      return JobBranch::kRogue;
    default:
      return JobBranch::kNone;
  }
}

Job LineOf(Job job) {
  static_assert(Job_ARRAYSIZE == 36, "a new job needs its line here");
  switch (job) {
    case JOB_FIGHTER:
    case JOB_CRUSADER:
    case JOB_HERO:
      return JOB_FIGHTER;
    case JOB_PAGE:
    case JOB_WHITE_KNIGHT:
    case JOB_PALADIN:
      return JOB_PAGE;
    case JOB_SPEARMAN:
    case JOB_BERSERKER:
    case JOB_DARK_KNIGHT:
      return JOB_SPEARMAN;
    case JOB_ICE_LIGHTNING_WIZARD:
    case JOB_ICE_LIGHTNING_MAGE:
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return JOB_ICE_LIGHTNING_WIZARD;
    case JOB_FIRE_POISON_WIZARD:
    case JOB_FIRE_POISON_MAGE:
    case JOB_FIRE_POISON_ARCH_MAGE:
      return JOB_FIRE_POISON_WIZARD;
    case JOB_CLERIC:
    case JOB_PRIEST:
    case JOB_BISHOP:
      return JOB_CLERIC;
    case JOB_HUNTER:
    case JOB_RANGER:
    case JOB_BOW_MASTER:
      return JOB_HUNTER;
    case JOB_CROSSBOWMAN:
    case JOB_SNIPER:
    case JOB_MARKSMAN:
      return JOB_CROSSBOWMAN;
    case JOB_ASSASSIN:
    case JOB_HERMIT:
    case JOB_NIGHT_LORD:
      return JOB_ASSASSIN;
    case JOB_BANDIT:
    case JOB_CHIEF_BANDIT:
    case JOB_SHADOWER:
      return JOB_BANDIT;
    default:
      return JOB_UNSPECIFIED;
  }
}

}  // namespace ms
