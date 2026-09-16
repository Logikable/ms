/* fight_view.h holds what a fight publishes for whoever is reading it: what
 * the last step did, and what the fight looks like now.
 *
 * Kept apart from the fight because nothing here is read back by it. The step
 * writes the whole of this at its end, so a panel drawing a fight or a reward
 * layer paying one out needs no CombatParams of its own -- and cannot reach
 * into the simulation to ask a question the fight has not answered.
 */
#ifndef MS_SRC_COMBAT_FIGHT_VIEW_H_
#define MS_SRC_COMBAT_FIGHT_VIEW_H_

#include <cstdint>
#include <string>
#include <vector>

namespace ms {

// One mob still standing, for a caller drawing them one bar apiece rather than
// merged. `id` is handed out on arrival and never reused, so a slot keeps its
// monster while the ones beside it die.
struct MobStatus {
  int id = 0;
  int type = 0;  // index into CombatParams::types
  std::string name;
  double hp_fraction = 0.0;
};

// One HP bar for the combat panel: a mob type in the engaged window, its
// members merged into an average HP fraction and a count.
struct EngagedGroup {
  std::string name;
  int level = 0;
  int count = 0;
  double hp_fraction = 0.0;
};

// Everything one step of a fight publishes. The lines that landed stay in the
// ledger -- see CombatSim::damage_lines_this_step().
struct FightView {
  // What the step did, cleared as the step opens. Kills are indexed to match
  // the params.types passed in.
  std::vector<int64_t> kills_this_step;
  // Damage dealt, overkill included: what the player watched fly off the
  // monsters, not what the monsters had left to give.
  double damage_this_step = 0.0;
  // True on the step a hit took the player to 0. Reported rather than acted
  // on, as kills are: what dying costs is the reward layer's business.
  bool died_this_step = false;
  // True on the step a respawn beat came round, whether or not it had anything
  // to put on the map. The one clock in the fight a watcher can align to.
  bool respawned_this_step = false;

  // What the fight looks like now; ClearPicture empties all of it. The
  // target's name and level are empty and 0 while respawning.
  std::string target_name;
  int target_level = 0;
  // Its remaining HP as a fraction in [0, 1].
  double target_hp_fraction = 0.0;
  // Progress toward the next auto-attack as a fraction in [0, 1].
  double attack_fraction = 0.0;
  // The swing being charged, or "Attack" for the bare poke. Empty while
  // respawning, there being no swing coming to name.
  std::string attack_name;
  // The player's remaining HP, rounded up so a sliver still reads as 1 rather
  // than as death, and what it tops out at under the params the step ran on.
  int player_hp = 0;
  int player_max_hp = 0;
  // That HP as a fraction in [0, 1] of what it tops out at.
  double player_hp_fraction = 0.0;
  // Progress toward the next respawn beat as a fraction in [0, 1], and
  // whether the encounter has a beat at all. A boss does not, and stays at 0.
  double respawn_fraction = 0.0;
  bool respawns = false;
  // The engaged window as HP bars, one per distinct type the next swing will
  // hit, in the order they appear in the queue.
  std::vector<EngagedGroup> engaged_groups;
  // Every mob still standing, in queue order -- the whole roster rather than
  // the engaged window.
  std::vector<MobStatus> roster;

  // Clears the picture for a step with no encounter. The step's tallies are
  // left alone: they were cleared as it opened, and kills_this_step keeps the
  // slot per mob type a caller reads back by index.
  void ClearPicture() {
    target_name.clear();
    target_level = 0;
    target_hp_fraction = 0.0;
    attack_fraction = 0.0;
    attack_name.clear();
    player_hp = 0;
    player_max_hp = 0;
    player_hp_fraction = 0.0;
    respawn_fraction = 0.0;
    respawns = false;
    engaged_groups.clear();
    roster.clear();
  }
};

}  // namespace ms

#endif  // MS_SRC_COMBAT_FIGHT_VIEW_H_
