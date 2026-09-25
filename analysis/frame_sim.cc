/* frame_sim: what one frame of the game costs.
 *
 * The game loop does two things each tick: it steps the fight and draws the
 * screen. This times each part for a max-level character on the map they'd be
 * farming, so the numbers are the worst case rather than a level 1's.
 *
 * Compare the microseconds against the tick the game actually redraws on, the
 * marquee step. The ticker wakes on it, steps the fight and posts a redraw, and
 * nothing in the loop runs faster.
 */
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "analysis/sim_world.h"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/character/character_stats.h"
#include "src/character/job_name.h"
#include "src/character/stat_preset.h"
#include "src/combat/encounter.h"
#include "src/combat/fight.h"
#include "src/frontend/panels/character_panel.h"
#include "src/frontend/panels/combat_panel.h"
#include "src/frontend/panels/equipped_panel.h"
#include "src/frontend/panels/inventory_panel.h"
#include "src/frontend/widgets/marquee.h"
#include "src/game_state.h"

ABSL_FLAG(int, reps, 2000, "how many times to run each measurement");
ABSL_FLAG(int, width, 200, "terminal columns to draw into");
ABSL_FLAG(int, height, 50, "terminal rows to draw into");

namespace ms {
namespace {

// Time budget for one tick, which every number below is compared against.
const double kTickBudgetUs =
    std::chrono::duration<double, std::micro>(kMarqueeStep).count();
const double kTickSeconds = std::chrono::duration<double>(kMarqueeStep).count();

// Average cost of one call of `job`, in microseconds, over `reps` calls.
double Cost(int reps, const std::function<void()>& job) {
  std::chrono::steady_clock::time_point began =
      std::chrono::steady_clock::now();
  for (int i = 0; i < reps; ++i) {
    job();
  }
  return std::chrono::duration<double, std::micro>(
             std::chrono::steady_clock::now() - began)
             .count() /
         reps;
}

// Same as Cost, printed against the tick and returned so the caller can total
// it.
double Time(const char* label, int reps, const std::function<void()>& job) {
  double each = Cost(reps, job);
  std::printf("  %-34s %10.2f us   %6.2f%% of a tick\n", label, each,
              100.0 * each / kTickBudgetUs);
  return each;
}

// Draws `element` into a screen of the flag's size, as the terminal does for
// every panel each frame.
void Draw(const ftxui::Element& element) {
  ftxui::Screen screen(absl::GetFlag(FLAGS_width), absl::GetFlag(FLAGS_height));
  ftxui::Render(screen, element);
}

// The character measured: a max-level Hero on the map
// //analysis:progression_sim leaves them farming.
std::unique_ptr<GameState> MaxState(const Catalogs& catalogs) {
  TestOptions test;
  test.job = JOB_ADVANCEMENT_HERO;
  // Matches every other sim's max character; see NewMaxState.
  test.link_skills = false;
  std::unique_ptr<GameState> state = std::make_unique<GameState>(
      catalogs.equips, catalogs.scrolls, catalogs.items, catalogs.mobs,
      catalogs.maps, catalogs.skills, GameMode::kMax, test, /*seed=*/1,
      catalogs.sets);
  state->bosses = catalogs.bosses;
  // The hardest map the game ships, where a max-level character farms. Both the
  // panels and the fight cost more with more on the map.
  std::vector<std::string> grounds = HuntingGrounds(catalogs);
  if (!grounds.empty()) {
    state->current_map = grounds.back();
  }
  return state;
}

void Run() {
  const int reps = absl::GetFlag(FLAGS_reps);
  Catalogs catalogs = LoadCatalogs();
  std::unique_ptr<GameState> owned = MaxState(catalogs);
  GameState& state = *owned;
  std::printf(
      "Lv%d %s on \"%s\", %d reps a measurement, %dx%d screen.\n"
      "One tick is %.0f us.\n\n",
      state.character.proto().level(),
      JobName(state.character.proto().job()).c_str(), state.current_map.c_str(),
      reps, absl::GetFlag(FLAGS_width), absl::GetFlag(FLAGS_height),
      kTickBudgetUs);

  std::printf("The fight\n");
  Time("DerivedStatsFor", reps, [&state] {
    DerivedStatsFor(state.character, state.skills, {}, {}, Activity::kFarming);
  });
  double params_cost = Time("ComputeCombatParams", reps,
                            [&state] { ComputeCombatParams(state); });
  CombatParams params = ComputeCombatParams(state);
  CombatSim sim;
  double tick = Time("CombatSim::Advance (one tick)", reps,
                     [&sim, &params] { sim.Advance(params, kTickSeconds); });
  // Params are rebuilt every step: the two-argument AdvanceCombat, which the
  // TUI's ticker calls, computes them each time.
  tick += params_cost;

  std::printf("\nThe screen\n");
  int panel_focus = 0;
  CharacterPanel char_panel(state.character, state.account, panel_focus,
                            state.skills);
  EquippedPanel equip_panel(state.character, state.account, panel_focus);
  InventoryPanel bag_panel(state.character, state.account, panel_focus);
  CombatPanel combat_panel(state, sim, panel_focus);
  ftxui::Component equip_component = equip_panel.MakeComponent({});
  ftxui::Component bag_component = bag_panel.MakeComponent({});
  tick += Time("CharacterPanel", reps,
               [&char_panel] { Draw(char_panel.Render()); });
  tick += Time("EquippedPanel", reps,
               [&equip_component] { Draw(equip_component->Render()); });
  tick += Time("InventoryPanel", reps,
               [&bag_component] { Draw(bag_component->Render()); });
  tick += Time("CombatPanel", reps,
               [&combat_panel] { Draw(combat_panel.Render()); });

  // The main view steps the fight once and draws all four panels, so its tick
  // costs the sum of the rows above.
  std::printf("\n  %-34s %10.2f us   %6.2f%% of a tick\n",
              "The whole main-view tick", tick, 100.0 * tick / kTickBudgetUs);
  std::printf("\n");
}

}  // namespace
}  // namespace ms

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  ms::Run();
  return 0;
}
