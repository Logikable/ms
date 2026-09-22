/* The Jukebox screen: every track the game carries, and what is playing now.
 *
 * Two windows down the screen. Now Playing carries the track's title over the
 * place it belongs to, a scrubbing bar beside them, and the four buttons: back
 * five seconds, play or pause, on five seconds, and the mode the music is in.
 * Under it the song list -- title, place, length -- with Enter playing the row
 * the cursor is on.
 *
 * Tab moves between the two, Left and Right walk the buttons, and Up and Down
 * walk whichever list holds the cursor. The mode button opens a box over
 * itself, which is the only place the three modes are named.
 *
 * The panel drives the MusicDirector and writes the mode onto the account.
 * Everything it draws about the live track it asks the director for; it holds
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
  kRewind,
  kPlayPause,
  kSkip,
  kMode,
};
inline constexpr int kJukeboxButtonCount = 4;

// One row of the song list.
struct Song {
  // The track's own name, which is what the player is asked to play.
  std::string_view track;
  std::string title;
  // The map, or the boss, this track belongs to. Empty for a track nothing in
  // the data names.
  std::string place;
  int duration_ms = 0;
};

class JukeboxPanel {
 public:
  // Content columns inside both windows, and rows the song list is drawn to.
  // Fixed, so the screen is one size whatever the build carries.
  static constexpr int kContentWidth = 88;
  static constexpr int kListRows = 20;

  JukeboxPanel(const GameState& state, MusicDirector& director,
               AccountInstance& account);

  // Puts the cursor on the track playing and the focus on the song list. Call
  // when the screen opens.
  void Reset();
  // Tab: the song list and the button row are the two halves.
  void SwitchHalf();
  // Up and Down, in whichever list holds the cursor.
  void MoveRow(int delta);
  // Left and Right along the buttons. Taken by an open mode box as the way
  // out of it, the box hanging from a button the arrows still belong to.
  void MoveColumn(int delta);
  // Enter: plays the song under the cursor, presses the button under it, or
  // takes the mode the open box is showing.
  void Activate();
  // Escape. True where it was spent closing the mode box, which leaves the
  // screen standing.
  bool DismissedBox();
  ftxui::Element Render() const;

  // The song list, in the order it is drawn.
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
  // The list's columns, left to right. The caret, the two names either side
  // of a gap, and the length pushed right.
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

  // Every track the build carries, by title, each against the map or boss
  // that names it. Read once: neither the data nor the build's music moves.
  void BuildSongs(const GameState& state);
  // The mode the box is on, or the account's while it is shut.
  JukeboxMode ModeAt(int row) const;
  void PressButton();

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
  // Where the render put the mode button and the screen's own top-left, so
  // the box can hang from the button rather than from a guessed column.
  mutable ftxui::Box mode_button_box_;
  mutable ftxui::Box panel_box_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_JUKEBOX_PANEL_H_
