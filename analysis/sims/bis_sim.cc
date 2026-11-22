/* bis_sim: what --mode=bis wears and rolls, and what it is worth over --max.
 *
 * Prints, per branch, every worn piece's stars, potential and flame lines,
 * then the soul, familiars, Inner Ability and HEXA Stats the search chose,
 * and the measured DPS of the bis and max characters against the fight the
 * search ranks by.
 *
 *   bazelisk run -c opt //analysis/sims:bis_sim -- --branch=HERO
 *   bazelisk run -c opt //analysis/sims:bis_sim -- --level=280
 */
#include <cstdio>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "analysis/parallel.h"
#include "analysis/sim_jobs.h"
#include "analysis/sim_world.h"
#include "src/character/character.h"
#include "src/character/max_character.h"
#include "src/character/stat_preset.h"
#include "src/combat/bis_rate.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/upgrade/potential.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/familiar.pb.h"

ABSL_FLAG(int, level, 260, "Level of the characters searched.");
ABSL_FLAG(std::string, branch, "",
          "One branch, as its Job enum name without the JOB_ prefix. Empty "
          "searches every branch with a 4th job.");

namespace ms {
namespace {

constexpr unsigned int kSeed = 1;

std::string Short(const std::string& name, const std::string& prefix) {
  return name.rfind(prefix, 0) == 0 ? name.substr(prefix.size()) : name;
}

std::string Lines(const Potential& potential) {
  std::string out;
  for (const PotentialLine& line : potential.lines()) {
    out += (out.empty() ? "" : " ") +
           Short(PotentialLineType_Name(line.type()), "POTENTIAL_LINE_TYPE_");
  }
  return out;
}

std::string Report(const Catalogs& catalogs, Job branch, int level) {
  const JobAdvancement advancement =
      AdvancementForJobStage(branch, StageOf(branch));
  GameState bis = NewBisState(catalogs, advancement, level, kSeed);
  GameState max = NewMaxState(catalogs, advancement, level, kSeed);
  const double bis_rate = BisFightRate(bis);
  const double max_rate = BisFightRate(max);
  const CharacterInstance& c = bis.character;
  const Mob* target = MaxBossTarget(bis.bosses, bis.mobs, level);

  std::string out;
  char line[512];
  std::snprintf(
      line, sizeof(line), "%s %d vs %s: bis %.3gT/s, max %.3gT/s, %.2fx\n",
      BranchName(branch).c_str(), level,
      target != nullptr ? target->name().c_str() : "-", bis_rate / 1e12,
      max_rate / 1e12, max_rate > 0.0 ? bis_rate / max_rate : 0.0);
  out += line;
  for (const auto& [slot, worn] : c.equipped()) {
    if (worn->equip_state().symbol_level() > 0) {
      continue;
    }
    std::string flame;
    for (const FlameLine& f : worn->equip_state().flame()) {
      flame += (flame.empty() ? "" : " ") +
               Short(FlameStat_Name(f.stat()), "FLAME_STAT_");
    }
    std::snprintf(line, sizeof(line), "  %-18s %-34s %2d* | %s | %s | %s\n",
                  Short(EquipSlot_Name(slot), "EQUIP_SLOT_").c_str(),
                  worn->prototype().name().c_str(), worn->equip_state().stars(),
                  Lines(worn->equip_state().main_potential()).c_str(),
                  Lines(worn->equip_state().bonus_potential()).c_str(),
                  flame.c_str());
    out += line;
    if (worn->equip_state().has_soul()) {
      out += "    soul " +
             Short(SoulLine_Name(worn->equip_state().soul().line()),
                   "SOUL_LINE_") +
             "\n";
    }
  }
  out += "  familiars:";
  for (const std::string& name :
       c.summoned_familiars(AutoswapSlotFor(Activity::kBossing))) {
    for (const Familiar& familiar : c.familiars().familiars()) {
      if (familiar.name() != name) {
        continue;
      }
      for (const FamiliarLine& l : familiar.lines()) {
        out +=
            " " + Short(FamiliarLineType_Name(l.type()), "FAMILIAR_LINE_TYPE_");
      }
      out += " /";
    }
  }
  out += "\n  ability:";
  for (const AbilityLine& l :
       c.ability(AutoswapSlotFor(Activity::kBossing)).lines()) {
    out += " " + Short(AbilityLineType_Name(l.type()), "ABILITY_LINE_TYPE_");
  }
  out += "\n";
  for (const HexaStatNode& node : c.hexa_stats().nodes()) {
    std::snprintf(line, sizeof(line), "  hexa: %s %d, %s %d, %s %d\n",
                  Short(HexaStat_Name(node.primary()), "HEXA_STAT_").c_str(),
                  node.primary_level(),
                  Short(HexaStat_Name(node.first()), "HEXA_STAT_").c_str(),
                  node.first_level(),
                  Short(HexaStat_Name(node.second()), "HEXA_STAT_").c_str(),
                  node.second_level());
    out += line;
  }
  return out + "\n";
}

void Run() {
  const int level = absl::GetFlag(FLAGS_level);
  std::vector<Job> branches;
  if (absl::GetFlag(FLAGS_branch).empty()) {
    for (Job job : EveryBranch()) {
      if (StageOf(job) == 4) {
        branches.push_back(job);
      }
    }
  } else {
    branches.push_back(ParseBranch(absl::GetFlag(FLAGS_branch)));
  }
  const Catalogs catalogs = LoadCatalogs();
  std::vector<std::string> reports(branches.size());
  ParallelFor(static_cast<int>(branches.size()), [&](int i) {
    reports[i] = Report(catalogs, branches[i], level);
  });
  for (const std::string& report : reports) {
    std::fputs(report.c_str(), stdout);
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
