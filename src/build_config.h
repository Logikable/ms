/* What this build of the game is: the switches decided when it is compiled
 * rather than when it is run.
 */
#ifndef MS_SRC_BUILD_CONFIG_H_
#define MS_SRC_BUILD_CONFIG_H_

namespace ms {

// Whether the game shows anything about multiplayer at all. A build with this
// off never opens a connection and shows no party screens: it is the
// single-player game, and there is nothing in it to say a server exists.
//
// Turned off by `--define=multiplayer=off`, which is how a single-player
// build is made. See //src:build_config.
#ifdef MS_MULTIPLAYER_OFF
inline constexpr bool kMultiplayerEnabled = false;
#else
inline constexpr bool kMultiplayerEnabled = true;
#endif

}  // namespace ms

#endif  // MS_SRC_BUILD_CONFIG_H_
