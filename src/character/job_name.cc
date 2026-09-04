#include "src/character/job_name.h"

#include <string>

#include "src/protos/character.pb.h"

namespace ms {

std::string JobName(Job job) {
  switch (job) {
    case JOB_BEGINNER:
      return "Beginner";
    case JOB_SWORDMAN:
      return "Swordman";
    case JOB_ARCHER:
      return "Archer";
    case JOB_MAGICIAN:
      return "Magician";
    case JOB_ROGUE:
      return "Rogue";
    case JOB_FIGHTER:
      return "Fighter";
    case JOB_PAGE:
      return "Page";
    case JOB_SPEARMAN:
      return "Spearman";
    case JOB_HUNTER:
      return "Hunter";
    case JOB_CROSSBOWMAN:
      return "Crossbowman";
    case JOB_ICE_LIGHTNING_WIZARD:
      return "Ice/Lightning Wizard";
    case JOB_FIRE_POISON_WIZARD:
      return "Fire/Poison Wizard";
    case JOB_CLERIC:
      return "Cleric";
    case JOB_ASSASSIN:
      return "Assassin";
    case JOB_BANDIT:
      return "Bandit";
    case JOB_BERSERKER:
      return "Berserker";
    case JOB_CRUSADER:
      return "Crusader";
    case JOB_WHITE_KNIGHT:
      return "White Knight";
    case JOB_RANGER:
      return "Ranger";
    case JOB_SNIPER:
      return "Sniper";
    case JOB_ICE_LIGHTNING_MAGE:
      return "Ice/Lightning Mage";
    case JOB_FIRE_POISON_MAGE:
      return "Fire/Poison Mage";
    case JOB_PRIEST:
      return "Priest";
    case JOB_HERMIT:
      return "Hermit";
    case JOB_CHIEF_BANDIT:
      return "Chief Bandit";
    case JOB_DARK_KNIGHT:
      return "Dark Knight";
    case JOB_PALADIN:
      return "Paladin";
    case JOB_HERO:
      return "Hero";
    case JOB_BOW_MASTER:
      return "Bow Master";
    case JOB_MARKSMAN:
      return "Marksman";
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return "Ice/Lightning Arch Mage";
    case JOB_FIRE_POISON_ARCH_MAGE:
      return "Fire/Poison Arch Mage";
    case JOB_BISHOP:
      return "Bishop";
    case JOB_NIGHT_LORD:
      return "Night Lord";
    case JOB_SHADOWER:
      return "Shadower";
    default:
      return "Unknown";
  }
}

std::string ShortJobName(Job job) {
  switch (job) {
    case JOB_ICE_LIGHTNING_WIZARD:
      return "I/L Wizard";
    case JOB_FIRE_POISON_WIZARD:
      return "F/P Wizard";
    case JOB_ICE_LIGHTNING_MAGE:
      return "I/L Mage";
    case JOB_ICE_LIGHTNING_ARCH_MAGE:
      return "I/L Arch Mage";
    case JOB_FIRE_POISON_ARCH_MAGE:
      return "F/P Arch Mage";
    case JOB_FIRE_POISON_MAGE:
      return "F/P Mage";
    default:
      return JobName(job);
  }
}

namespace {

// The stage whose advancement leaves the job's name where it was.
constexpr int kFifthJobStage = 5;

}  // namespace

std::string AdvancementName(Job job, int stage) {
  return JobName(job) + (stage == kFifthJobStage ? " V" : "");
}

std::string ShortAdvancementName(Job job, int stage) {
  return ShortJobName(job) + (stage == kFifthJobStage ? " V" : "");
}

}  // namespace ms
