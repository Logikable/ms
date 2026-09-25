/* Settings decided when this build is compiled, not when it's run.
 */
#ifndef MS_SRC_BUILD_CONFIG_H_
#define MS_SRC_BUILD_CONFIG_H_

namespace ms {

// Whether the game shows anything about multiplayer. With it off, there's no
// connection and no party screens, and nothing mentions a server. Turned off by
// `--define=multiplayer=off`.
#ifdef MS_MULTIPLAYER_OFF
inline constexpr bool kMultiplayerEnabled = false;
#else
inline constexpr bool kMultiplayerEnabled = true;
#endif

// Whether this build has music. With it off, the build includes no tracks,
// never opens an audio device, and has no volume rows on the Options screen.
//
// Turned off by `--define=audio=off`. It's also the only way to build without
// bgm/, whose tracks aren't checked in.
#ifdef MS_AUDIO_OFF
inline constexpr bool kAudioEnabled = false;
#else
inline constexpr bool kAudioEnabled = true;
#endif

}  // namespace ms

#endif  // MS_SRC_BUILD_CONFIG_H_
