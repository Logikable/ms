/* The Jukebox screen: every track in the game, and what is playing now.
 *
 * Two windows, one above the other. Now Playing shows the track's title over
 * the place it belongs to, a progress bar beside them, and the transport row:
 * previous track, back five seconds, play or pause, forward five seconds, next
 * track, and the music mode. Below it is the song list (title, place, length),
 * where Enter plays the row under the cursor.
 *
 * Tab switches between the two windows, Left and Right move along the buttons,
 * and Up and Down move through whichever list has the cursor. The mode button
 * opens a box below itself, the only place the three modes are named.
 *
 * The panel controls the MusicDirector and writes the mode to the account. It
 * asks the director for everything it shows about the current track and keeps
 * no clock of its own.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_JUKEBOX_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_JUKEBOX_PANEL_H_

#include <string>
#include <string_view>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "src/account.h"
#include "src/audio/music_director.h"
#include "src/frontend/widgets/item_menu.h"
#include "src/game_state.h"

namespace ms {

// The stops along the Now Playing row, left to right.
enum class JukeboxButton {
  kPrevious,
  kRewind,
  kPlayPause,
  kSkip,
  kNext,
  kMode,
};
inline constexpr int kJukeboxButtonCount = 6;

// One row of the song list.
struct Song {
  // The track's own name, which is what the player asks to play.
  std::string_view track;
  std::string title;
  // The map or boss this track belongs to. Empty for a track the data doesn't
  // name.
  std::string place;
  int duration_ms = 0;
};

class JukeboxPanel {
 public:
  // The content width inside both windows, and the rows the song list is drawn
  // to. Fixed, so the screen is the same size whatever music the build has.
  static constexpr int kContentWidth = 88;
  static constexpr int kListRows = 20;

  JukeboxPanel(const GameState& state, MusicDirector& director,
               AccountInstance& account);

  // Puts the cursor on the playing track and the focus on the song list. Call
  // when the screen opens.
  void Reset();
  // Tab: switches between the song list and the button row.
  void SwitchHalf();
  // Up and Down, in whichever list has the cursor.
  void MoveRow(int delta);
  // Left and Right along the buttons. An open mode box uses them as the way
  // out, since the box hangs from a button the arrows still belong to.
  void MoveColumn(int delta);
  // Enter: plays the song under the cursor, presses the button under it, or
  // selects the mode highlighted in the open box.
  void Activate();
  // Escape. Returns true if it closed the mode box, which leaves the screen
  // open.
  bool DismissedBox();
  ftxui::Element Render() const;

  // The song list, in drawing order.
  const std::vector<Song>& songs() const {
    return songs_;
  }
  bool on_buttons() const {
    return on_buttons_;
  }
  JukeboxButton selected_button() const;
  bool mode_box_open() const {
    return mode_box_open_;
  }
  int selected_row() const {
    return row_;
  }

 private:
  // The list's columns, left to right: the caret, the two names with a gap
  // between, and the length right-aligned.
  static constexpr int kCaretWidth = 2;
  static constexpr int kTitleWidth = 32;
  static constexpr int kCellGap = 2;
  static constexpr int kPlaceWidth = 42;
  static constexpr int kLengthWidth = 6;
  // Now Playing's two columns: the title over the place, and the bar over the
  // buttons.
  static constexpr int kIdentityWidth = 34;
  static constexpr int kTimeWidth = 5;
  static constexpr int kBarWidth = 42;

  // Every track in the build, by title, each with the map or boss that uses it.
  // Built once, since neither the data nor the build's music changes.
  void BuildSongs(const GameState& state);
  // The mode the box is on, or the account's mode while it is closed.
  JukeboxMode ModeAt(int row) const;
  void PressButton();
  // The song `delta` places from the one playing, in list order and wrapping at
  // either end. Plays it.
  void StepTrack(int delta);

  ftxui::Element RenderNowPlaying() const;
  ftxui::Element RenderIdentity() const;
  ftxui::Element RenderScrubBar() const;
  ftxui::Element RenderButtons() const;
  ftxui::Element RenderList() const;
  ftxui::Element RenderHeader() const;
  ftxui::Element RenderSong(const Song& song, bool on_cursor) const;

  MusicDirector& director_;
  AccountInstance& account_;
  std::vector<Song> songs_;
  // The three modes, in the order the box lists them.
  ItemMenu mode_menu_;

  int row_ = 0;
  bool on_buttons_ = false;
  int button_ = 0;
  bool mode_box_open_ = false;
  // Where the render placed the mode button and the screen's top-left corner,
  // so the box can hang from the button rather than a guessed column.
  mutable ftxui::Box mode_button_box_;
  mutable ftxui::Box panel_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_JUKEBOX_PANEL_H_
