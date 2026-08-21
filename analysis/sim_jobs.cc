#include "analysis/sim_jobs.h"

#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "src/character/character.h"
#include "src/game_state.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {

std::string BranchName(Job job) {
  switch (job) {
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
      return "I/L Wizard";
    case JOB_FIRE_POISON_WIZARD:
      return "F/P Wizard";
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
      return "I/L Mage";
    case JOB_FIRE_POISON_MAGE:
      return "F/P Mage";
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
      return "I/L Arch Mage";
    case JOB_FIRE_POISON_ARCH_MAGE:
      return "F/P Arch Mage";
    case JOB_NIGHT_LORD:
      return "Night Lord";
    case JOB_SHADOWER:
      return "Shadower";
    case JOB_BISHOP:
      return "Bishop";
    default:
      return "?";
  }
}

std::vector<Job> PathTo(Job branch) {
  std::vector<Job> path;
  for (int stage = 1;; ++stage) {
    JobAdvancement advancement = AdvancementForJobStage(branch, stage);
    if (advancement == JOB_ADVANCEMENT_UNSPECIFIED) {
      return path;
    }
    path.push_back(JobForAdvancement(advancement));
  }
}

int StageOf(Job branch) {
  std::vector<Job> path = PathTo(branch);
  // A job that appears on its own path but not at the end of it is a rung
  // rather than a branch: AdvancementForJobStage answers for the whole line,
  // so a Swordman asked for stage 4 gives whatever that line ends in.
  if (path.empty() || path.back() != branch) {
    return 0;
  }
  return static_cast<int>(path.size());
}

std::vector<Job> EveryBranch() {
  std::vector<Job> branches;
  for (int value = Job_MIN; value <= Job_MAX; ++value) {
    if (!Job_IsValid(value)) {
      continue;
    }
    Job job = static_cast<Job>(value);
    if (StageOf(job) > 0) {
      branches.push_back(job);
    }
  }
  return branches;
}

std::vector<Job> BranchesAt(int level) {
  int deepest = 0;
  for (int stage = 1; NextAdvancementLevel(stage - 1) > 0; ++stage) {
    if (level >= NextAdvancementLevel(stage - 1)) {
      deepest = stage;
    }
  }
  std::vector<Job> branches;
  for (Job job : EveryBranch()) {
    if (StageOf(job) == deepest) {
      branches.push_back(job);
    }
  }
  return branches;
}

Job ParseBranch(const std::string& name, int min_stage) {
  Job job = JOB_UNSPECIFIED;
  if (!Job_Parse("JOB_" + absl::AsciiStrToUpper(name), &job) ||
      StageOf(job) < min_stage) {
    LOG(FATAL) << "Unknown job '" << name << "'";
  }
  return job;
}

void GrowTo(GameState& state, int level, const std::vector<Job>& path,
            bool spend_sp) {
  CharacterInstance& character = state.character;
  int taken = 0;
  while (character.proto().level() < level) {
    character.LevelUp();
    if (character.CanAdvanceJob() && taken < static_cast<int>(path.size())) {
      character.AdvanceJob(path[taken++]);
    }
    while (character.AllocateStat(PrimaryStatField(character.proto().job()))) {
    }
    if (!spend_sp) {
      continue;
    }
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      while (character.LearnSkill(entry.second)) {
      }
    }
  }
}

}  // namespace ms
