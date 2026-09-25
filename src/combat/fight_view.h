/* What a fight publishes for its readers: what the last step did, and what the
 * fight looks like now.
 *
 * Separate from the fight because the fight never reads it back. The step fills
 * all of it at the end, so a panel drawing the fight or code paying out rewards
 * needs no CombatParams, and can't query the simulation directly.
 */
#ifndef MS_SRC_COMBAT_FIGHT_VIEW_H_
#define MS_SRC_COMBAT_FIGHT_VIEW_H_

#include <cstdint>
#include <string>
#include <vector>

namespace ms {

// One living mob, for callers that draw one bar per mob. `id` is assigned on
// arrival and never reused, so a bar keeps its mob while the ones beside it
// die.
struct MobStatus {
  int id = 0;
  int type = 0;  // index into CombatParams::types
  std::string name;
  double hp_fraction = 0.0;
};

// One HP bar in the combat panel: all mobs of one type within reach, merged
// into an average HP fraction and a count.
struct EngagedGroup {
  std::string name;
  int level = 0;
  int count = 0;
  double hp_fraction = 0.0;
};

// Everything one step of a fight publishes. Damage lines are kept in the
// ledger; see CombatSim::damage_lines_this_step().
struct FightView {
  // What this step did, cleared at its start. Kills are indexed like the
  // params.types passed in.
  std::vector<int64_t> kills_this_step;
  // Damage dealt including overkill: the numbers the player saw, not the HP the
  // monsters actually lost.
  double damage_this_step = 0.0;
  // True on the step the player's HP hit 0. Only reported, like kills; the
  // reward code decides what dying costs.
  bool died_this_step = false;
  // True on the step a respawn happened, even if nothing spawned. Callers can
  // use it to line up with the respawn cycle.
  bool respawned_this_step = false;

  // What the fight looks like now; ClearPicture resets all of it. The target's
  // name and level are empty and 0 while waiting for a respawn.
  std::string target_name;
  int target_level = 0;
  // The target's remaining HP, from 0 to 1.
  double target_hp_fraction = 0.0;
  // Progress toward the next attack, from 0 to 1.
  double attack_fraction = 0.0;
  // The attack being charged, or "Attack" for a basic attack. Empty while
  // waiting for a respawn.
  std::string attack_name;
  // Number of active buffs, shown as dots on the charge bar.
  int buff_count = 0;
  // The player's HP, rounded up so a sliver shows as 1 rather than 0, and their
  // max HP under the step's params.
  int player_hp = 0;
  int player_max_hp = 0;
  // HP as a fraction of max, from 0 to 1.
  double player_hp_fraction = 0.0;
  // Progress toward the next respawn, from 0 to 1, and whether the encounter
  // respawns at all. Bosses don't, and stay at 0.
  double respawn_fraction = 0.0;
  bool respawns = false;
  // HP bars for mobs within reach, one per type the next attack will hit, in
  // queue order.
  std::vector<EngagedGroup> engaged_groups;
  // Every living mob in queue order, not just those within reach.
  std::vector<MobStatus> roster;

  // Clears the picture for a step with no encounter. Leaves the step's tallies
  // alone: they were cleared at the start, and kills_this_step keeps one entry
  // per mob type for callers that index into it.
  void ClearPicture() {
    target_name.clear();
    target_level = 0;
    target_hp_fraction = 0.0;
    attack_fraction = 0.0;
    attack_name.clear();
    buff_count = 0;
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
