/* save_swap_sim: a real save's boss damage under one swap at a time.
 *
 * Measures the Boss preset against one phase of a boss over each window in
 * --windows, starting fresh: every cooldown ready and no buff up, which is
 * what a phase with reset_cooldowns hands the player. Each swap starts from a
 * fresh load, so swaps never stack. The swaps are every soul line at the
 * worn soul's tier, the Continuous Ring in the Cont Ring slot, and the Ring of
 * Restraint in place of each worn ring, at each level a ring box can roll.
 *
 *   bazelisk run //analysis:save_swap_sim -- --save=/path/to/copy/ms.save \
 *       --boss=lucid --difficulty=Hard --windows=1200,45
 */
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "analysis/sim_boss.h"
#include "analysis/sim_world.h"
#include "src/character/character.h"
#include "src/character/stat_preset.h"
#include "src/combat/encounter.h"
#include "src/combat/measure.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/boss.pb.h"
#include "src/protos/equip.pb.h"
#include "src/save.h"

ABSL_FLAG(std::string, save, "", "The save to read. Use a copy.");
ABSL_FLAG(std::string, boss, "lucid", "Boss key, as data/bosses names it.");
ABSL_FLAG(std::string, difficulty, "Hard", "Difficulty name.");
ABSL_FLAG(int, phase, -1,
          "Phase index to measure against; -1 is the boss's objective phase.");
ABSL_FLAG(std::string, windows, "1200,45",
          "Comma-separated fight lengths in seconds, each measured fresh.");

namespace ms {
namespace {

// The levels a Black Jade Boss Ring Box rolls.
constexpr int kBoxLevels[] = {1, 2, 3, 4};

using Swap = std::function<bool(const Catalogs&, GameState&)>;

StatPreset BossGear(const CharacterInstance& c) {
  return c.SlotFor(PresetKind::kEquip, Activity::kBossing);
}

bool Load(const Catalogs& catalogs, GameState& state) {
  state.bosses = catalogs.bosses;
  const LoadResult load = LoadGameFromFile(state, absl::GetFlag(FLAGS_save));
  if (load.status != LoadStatus::kLoaded) {
    LOG(ERROR) << load.message;
    return false;
  }
  return true;
}

// Puts `name` from the catalog in `slot` of the Boss preset.
bool Wear(const Catalogs& catalogs, GameState& state, const std::string& name,
          EquipSlot slot) {
  CharacterInstance& c = state.character;
  const EquipPrototype* proto = nullptr;
  for (const auto& [key, entry] : catalogs.equips) {
    if (entry.name() == name) {
      proto = &entry;
    }
  }
  if (proto == nullptr) {
    return false;
  }
  c.PickUp(std::make_unique<EquipInstance>(*proto));
  return c.Equip(c.inventory().size() - 1, BossGear(c), slot);
}

const Soul* WornSoul(const CharacterInstance& c, EquipSlot* slot) {
  for (const auto& [at, item] : c.equipped(BossGear(c))) {
    if (item->equip_state().has_soul()) {
      *slot = at;
      return &item->equip_state().soul();
    }
  }
  return nullptr;
}

std::vector<double> Measure(const Catalogs& catalogs, const Swap& swap,
                            const std::vector<double>& windows,
                            std::string* note) {
  GameState state = NewState(catalogs, 1);
  if (!Load(catalogs, state) || !swap(catalogs, state)) {
    return {};
  }
  const Boss& boss = state.bosses.at(absl::GetFlag(FLAGS_boss));
  const int difficulty =
      BossDifficultyIndex(boss, absl::GetFlag(FLAGS_difficulty));
  const BossDifficulty& chosen = boss.difficulties(difficulty);
  int phase = absl::GetFlag(FLAGS_phase);
  if (phase < 0) {
    phase = BossObjectivePhase(state.mobs, chosen);
  }
  CombatParams params =
      ComputeBossParams(state, absl::GetFlag(FLAGS_boss), chosen, phase);
  std::vector<double> dps;
  for (double window : windows) {
    Sequence run = MeasureFight(params, window);
    dps.push_back(run.damage / run.seconds);
    for (size_t i = 0; i < params.buffs.size() && note != nullptr; ++i) {
      if (params.buffs[i].name == "Ring of Restraint") {
        absl::StrAppend(note, " RoR up ",
                        static_cast<int>(100 * run.buff_uptime[i] + 0.5),
                        "% of ", static_cast<int>(window), "s;");
      }
    }
  }
  return dps;
}

void Row(const std::string& label, const std::vector<double>& dps,
         const std::vector<double>& base, const std::string& note) {
  std::printf("  %-40s", label.c_str());
  if (dps.empty()) {
    std::printf(" (swap failed)\n");
    return;
  }
  for (size_t i = 0; i < dps.size(); ++i) {
    std::printf(" %+7.2f%%", 100.0 * (dps[i] / base[i] - 1.0));
  }
  std::printf("%s\n", note.c_str());
}

void Run() {
  const Catalogs catalogs = LoadCatalogs();
  std::vector<double> windows;
  for (absl::string_view w :
       absl::StrSplit(absl::GetFlag(FLAGS_windows), ',')) {
    windows.push_back(std::stod(std::string(w)));
  }
  const std::vector<double> base = Measure(
      catalogs, [](const Catalogs&, GameState&) { return true; }, windows,
      nullptr);
  if (base.empty()) {
    return;
  }
  std::printf("%s %s, objective phase; DPS by window:",
              absl::GetFlag(FLAGS_boss).c_str(),
              absl::GetFlag(FLAGS_difficulty).c_str());
  for (size_t i = 0; i < windows.size(); ++i) {
    std::printf("  %.0fs %.4gB/s", windows[i], base[i] / 1e9);
  }
  std::printf("\n");

  GameState probe = NewState(catalogs, 1);
  Load(catalogs, probe);
  EquipSlot soul_slot = EQUIP_SLOT_UNSPECIFIED;
  const Soul* worn = WornSoul(probe.character, &soul_slot);
  if (worn != nullptr) {
    std::printf("\nSoul lines at the worn soul's tier (worn: %s)\n",
                SoulLine_Name(worn->line()).c_str());
    for (int line = SOUL_LINE_ATTACK; line <= SoulLine_MAX; ++line) {
      Soul soul = *worn;
      soul.set_line(static_cast<SoulLine>(line));
      Row(SoulLine_Name(soul.line()),
          Measure(
              catalogs,
              [&](const Catalogs&, GameState& s) {
                return s.character.TakeSoul(soul_slot, soul,
                                            BossGear(s.character)) ||
                       s.character.TakeSoul(soul_slot, soul);
              },
              windows, nullptr),
          base, "");
    }
  }

  std::printf("\nContinuous Ring in the Cont Ring slot\n");
  for (int level : kBoxLevels) {
    const std::string name = absl::StrCat("Continuous Ring Lv. ", level);
    Row(name,
        Measure(
            catalogs,
            [&](const Catalogs& c, GameState& s) {
              return Wear(c, s, name, EQUIP_SLOT_CONT_RING);
            },
            windows, nullptr),
        base, "");
  }

  std::printf("\nRing of Restraint in place of a worn ring\n");
  for (const auto& [slot, item] :
       probe.character.equipped(BossGear(probe.character))) {
    if (slot != EQUIP_SLOT_RING && slot != EQUIP_SLOT_RING_2 &&
        slot != EQUIP_SLOT_RING_3 && slot != EQUIP_SLOT_RING_4) {
      continue;
    }
    const EquipSlot ring_slot = slot;
    for (int level : kBoxLevels) {
      const std::string name = absl::StrCat("Ring of Restraint Lv. ", level);
      std::string note;
      Row(absl::StrCat(name, " for ", item->prototype().name()),
          Measure(
              catalogs,
              [&](const Catalogs& c, GameState& s) {
                return Wear(c, s, name, ring_slot);
              },
              windows, &note),
          base, note);
    }
  }
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  ms::Run();
  return 0;
}
