/* What this build of the game is: the switches decided when it is compiled
 * rather than when it is run.
 */
#ifndef MS_SRC_BUILD_CONFIG_H_
#define MS_SRC_BUILD_CONFIG_H_

namespace ms {

// Whether the game shows anything about multiplayer. With it off there is no
// connection and no party screens -- nothing to say a server exists. Turned
// off by `--define=multiplayer=off`.
#ifdef MS_MULTIPLAYER_OFF
inline constexpr bool kMultiplayerEnabled = false;
#else
inline constexpr bool kMultiplayerEnabled = true;
#endif

// Whether this build has music. A build with this off carries no tracks and
// never opens an audio device, and its Options screen has no volume rows.
//
// Turned off by `--define=audio=off`. It is also the only way to build
// without bgm/, whose tracks are gitignored.
#ifdef MS_AUDIO_OFF
inline constexpr bool kAudioEnabled = false;
#else
inline constexpr bool kAudioEnabled = true;
#endif

}  // namespace ms

#endif  // MS_SRC_BUILD_CONFIG_H_
