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

// Whether the 6th job is in the game. Off until all of it is built, so master
// keeps shipping hotfixes meanwhile. Turned on by `--define=sixth_job=on`.
#ifdef MS_SIXTH_JOB_ON
inline constexpr bool kSixthJobEnabled = true;
#else
inline constexpr bool kSixthJobEnabled = false;
#endif

}  // namespace ms

#endif  // MS_SRC_BUILD_CONFIG_H_
