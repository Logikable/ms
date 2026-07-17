/* Constants shared across the combat module. Anything used by only one file in
 * here belongs in that file's anonymous namespace instead -- this header is for
 * the handful of values several parts of combat must agree on.
 */
#ifndef MS_SRC_COMBAT_CONSTANTS_H_
#define MS_SRC_COMBAT_CONSTANTS_H_

namespace ms {

// GMS global respawn tick: every 7.56s the server refills up to one mob per
// spawn point. A map's full-clear kill cap is spawn_count / this.
constexpr double kRespawnIntervalSeconds = 7.56;

// Our game runs this many times slower than GMS; the one global pacing knob.
// Everything with a duration -- swings, respawns, kill cycles -- is stretched
// by it.
constexpr double kGameSpeedFactor = 2.0;

// The action-delay quantization grain: GMS rounds attack delays up to whole
// units of this. Not a simulation tick -- nothing here is stepped by it.
constexpr int kTickMs = 30;

}  // namespace ms

#endif  // MS_SRC_COMBAT_CONSTANTS_H_
