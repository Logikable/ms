#include "src/frontend/screens/jukebox_panel.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/audio/track_title.h"
#include "src/audio/tracks.h"
#include "src/frontend/widgets/chrome.h"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/format.h"
#include "src/frontend/widgets/keys.h"
#include "src/map_level.h"

namespace ms {
namespace {

// The three modes in the order the box lists them, and the one name each goes
// by -- the button shows the same word the box does.
constexpr JukeboxMode kModes[] = {
    JUKEBOX_MODE_FOLLOW_MAP,
    JUKEBOX_MODE_PLAYLIST,
    JUKEBOX_MODE_SHUFFLE,
};
constexpr int kModeCount = 3;

// The mode button reads "[Mode: <name> ▾]", and the box that opens under it is
// only as wide as the widest name: every entry is padded to it so the box is a
// rectangle.
constexpr int kModeNameWidth = 10;

std::string ModeName(JukeboxMode mode) {
  switch (mode) {
    case JUKEBOX_MODE_FOLLOW_MAP:
      return "Follow Map";
    case JUKEBOX_MODE_PLAYLIST:
      return "Playlist";
    case JUKEBOX_MODE_SHUFFLE:
      return "Shuffle";
    default:
      return "Follow Map";
  }
}

std::string ModeEntry(JukeboxMode mode) {
  return PadRight(ModeName(mode), kModeNameWidth);
}

int IndexOfMode(JukeboxMode mode) {
  for (int i = 0; i < kModeCount; ++i) {
    if (kModes[i] == mode) {
      return i;
    }
  }
  return 0;
}

// Where a boss's own tracks belong: the boss, and the phases the track plays
// through. A phase naming no track carries on with the one before it, and a
// track that plays every phase is the boss's name alone.
std::map<std::string, std::string> BossPlaces(const Boss& boss) {
  std::map<std::string, std::set<int>> phases;
  int longest = 0;
  for (const BossDifficulty& difficulty : boss.difficulties()) {
    std::string playing;
    int number = 0;
    for (const BossPhase& phase : difficulty.phases()) {
      ++number;
      if (!phase.bgm().empty()) {
        playing = phase.bgm();
      }
      if (!playing.empty()) {
        phases[playing].insert(number);
      }
    }
    longest = std::max(longest, number);
  }
  std::map<std::string, std::string> places;
  for (const auto& [track, numbers] : phases) {
    std::string label;
    for (int number : numbers) {
      label += (label.empty() ? " P" : "/") + std::to_string(number);
    }
    places[track] = boss.name() +
                    (static_cast<int>(numbers.size()) == longest ? "" : label);
  }
  return places;
}

// Past this much of a track, the song-before mark starts it over instead of
// going back a song -- what every player does.
constexpr float kRestartAfterSeconds = 5.0f;

// A transport mark wears no brackets, so the invert IS the cursor on it.
ftxui::Element TransportMark(const std::string& mark, bool focused) {
  ftxui::Element element = ftxui::text(mark);
  return focused ? std::move(element) | ftxui::inverted : element;
}

// Seconds as mm:ss, counting the minutes past sixty rather than wrapping.
std::string Clock(int seconds) {
  seconds = std::max(seconds, 0);
  std::string secs = std::to_string(seconds % 60);
  return std::to_string(seconds / 60) + ":" +
         std::string(2 - secs.size(), '0') + secs;
}

}  // namespace

JukeboxPanel::JukeboxPanel(const GameState& state, MusicDirector& director,
                           AccountInstance& account)
    : director_(director),
      account_(account),
      mode_menu_(
          {ModeEntry(kModes[0]), ModeEntry(kModes[1]), ModeEntry(kModes[2])}) {
  BuildSongs(state);
}

void JukeboxPanel::BuildSongs(const GameState& state) {
  // Where each track belongs. A map wins over a boss, and the LOWEST map wins
  // among the several that can share one region's music -- which is the one a
  // player met it on.
  std::map<std::string, std::pair<double, std::string>> places;
  for (const auto& [key, map] : state.maps) {
    if (map.bgm().empty()) {
      continue;
    }
    double level = MapLevel(state.mobs, map);
    auto it = places.find(map.bgm());
    if (it == places.end() || level < it->second.first) {
      places[map.bgm()] = {level, map.name()};
    }
  }
  for (const auto& [key, boss] : state.bosses) {
    for (const auto& [track, place] : BossPlaces(boss)) {
      if (!places.count(track)) {
        places[track] = {0.0, place};
      }
    }
  }
  for (std::string_view track : TracksByTitle()) {
    Song song;
    song.track = track;
    song.title = TrackTitle(track);
    auto place = places.find(std::string(track));
    if (place != places.end()) {
      song.place = place->second.second;
    }
    if (std::optional<TrackData> data = BgmTrack(track); data.has_value()) {
      song.duration_ms = data->duration_ms;
    }
    songs_.push_back(std::move(song));
  }
}

void JukeboxPanel::Reset() {
  on_buttons_ = false;
  button_ = 0;
  mode_box_open_ = false;
  row_ = 0;
  for (int i = 0; i < static_cast<int>(songs_.size()); ++i) {
    if (songs_[i].track == director_.playing()) {
      row_ = i;
      break;
    }
  }
}

void JukeboxPanel::SwitchHalf() {
  // An open box belongs to the button it hangs from, so leaving the row puts
  // it away rather than leaving it standing over the list.
  mode_box_open_ = false;
  on_buttons_ = !on_buttons_;
}

JukeboxButton JukeboxPanel::selected_button() const {
  return static_cast<JukeboxButton>(button_);
}

JukeboxMode JukeboxPanel::ModeAt(int row) const {
  return kModes[std::clamp(row, 0, kModeCount - 1)];
}

void JukeboxPanel::MoveRow(int delta) {
  if (mode_box_open_) {
    // Up on the first entry lands on the last: the box is a ring like every
    // other list.
    if (delta < 0) {
      mode_menu_.Up();
    } else {
      mode_menu_.Down();
    }
    return;
  }
  if (on_buttons_ || songs_.empty()) {
    return;
  }
  row_ = StepCursor(row_, delta, static_cast<int>(songs_.size()));
}

void JukeboxPanel::MoveColumn(int delta) {
  if (mode_box_open_) {
    mode_box_open_ = false;
    return;
  }
  if (!on_buttons_) {
    return;
  }
  button_ = StepCursor(button_, delta, kJukeboxButtonCount);
}

void JukeboxPanel::StepTrack(int delta) {
  if (songs_.empty()) {
    return;
  }
  int playing = 0;
  for (int i = 0; i < static_cast<int>(songs_.size()); ++i) {
    if (songs_[i].track == director_.playing()) {
      playing = i;
      break;
    }
  }
  int total = static_cast<int>(songs_.size());
  director_.Play(songs_[StepCursor(playing, delta, total)].track);
}

void JukeboxPanel::PressButton() {
  switch (selected_button()) {
    case JukeboxButton::kPrevious:
      if (director_.position_seconds() >= kRestartAfterSeconds &&
          !director_.playing().empty()) {
        director_.Play(director_.playing());
        return;
      }
      StepTrack(-1);
      return;
    case JukeboxButton::kNext:
      StepTrack(1);
      return;
    case JukeboxButton::kRewind:
      director_.Nudge(-kSkipSeconds);
      return;
    case JukeboxButton::kPlayPause:
      director_.TogglePause();
      return;
    case JukeboxButton::kSkip:
      director_.Nudge(kSkipSeconds);
      return;
    case JukeboxButton::kMode:
      mode_menu_.Reset();
      for (int i = 0; i < IndexOfMode(account_.jukebox_mode()); ++i) {
        mode_menu_.Down();
      }
      mode_box_open_ = true;
      return;
  }
}

void JukeboxPanel::Activate() {
  if (mode_box_open_) {
    account_.SetJukeboxMode(ModeAt(mode_menu_.selected()));
    mode_box_open_ = false;
    return;
  }
  if (on_buttons_) {
    PressButton();
    return;
  }
  if (!songs_.empty()) {
    director_.Play(songs_[row_].track);
  }
}

bool JukeboxPanel::DismissedBox() {
  if (!mode_box_open_) {
    return false;
  }
  mode_box_open_ = false;
  return true;
}

ftxui::Element JukeboxPanel::RenderIdentity() const {
  const std::string& track = director_.playing();
  std::string title = track.empty() ? "(nothing playing)" : TrackTitle(track);
  std::string place;
  for (const Song& song : songs_) {
    if (song.track == track) {
      place = song.place;
      break;
    }
  }
  ftxui::Element column = ftxui::vbox({
      ftxui::text(" " + PadRight(title, kIdentityWidth - 1)),
      ftxui::text(" " + PadRight(place, kIdentityWidth - 1)) | ftxui::dim,
  });
  return std::move(column) |
         ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kIdentityWidth);
}

ftxui::Element JukeboxPanel::RenderScrubBar() const {
  float length = director_.length_seconds();
  float position = director_.position_seconds();
  float filled = length > 0.0f ? std::clamp(position / length, 0.0f, 1.0f) : 0;
  return ftxui::hbox({
      ftxui::text(PadLeft(Clock(static_cast<int>(position)), kTimeWidth) + " "),
      ProgressBar(filled, kTheme, "") |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kBarWidth),
      ftxui::text(PadLeft(Clock(static_cast<int>(length)), kTimeWidth)),
  });
}

ftxui::Element JukeboxPanel::RenderButtons() const {
  // Both labels are held to five columns, so pressing the button does not
  // shuffle the row it sits in.
  std::string play = director_.paused() ? "Play " : "Pause";
  auto on = [this](JukeboxButton button) {
    return on_buttons_ && selected_button() == button;
  };
  ftxui::Element mode = ActionButton(
      "Mode: " + PadRight(ModeName(account_.jukebox_mode()), kModeNameWidth) +
          " ▾",
      on(JukeboxButton::kMode));
  return ftxui::hbox({
      ftxui::text("  "),
      TransportMark("❙◂", on(JukeboxButton::kPrevious)),
      ftxui::text("  "),
      TransportMark("◂◂", on(JukeboxButton::kRewind)),
      ftxui::text("  "),
      ActionButton(play, on(JukeboxButton::kPlayPause)),
      ftxui::text("  "),
      TransportMark("▸▸", on(JukeboxButton::kSkip)),
      ftxui::text("  "),
      TransportMark("▸❙", on(JukeboxButton::kNext)),
      ftxui::filler(),
      std::move(mode) | ftxui::reflect(mode_button_box_),
      ftxui::text("  "),
  });
}

ftxui::Element JukeboxPanel::RenderNowPlaying() const {
  ftxui::Element right =
      ftxui::vbox({
          RenderScrubBar(),
          RenderButtons(),
      }) |
      ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth - kIdentityWidth);
  return ThemedWindow(" Now Playing ",
                      ftxui::hbox({RenderIdentity(), std::move(right)}),
                      on_buttons_);
}

ftxui::Element JukeboxPanel::RenderHeader() const {
  return ftxui::text(std::string(kCaretWidth, ' ') +
                     PadRight("Song", kTitleWidth + kCellGap) +
                     PadRight("Map", kPlaceWidth + kCellGap) +
                     PadLeft("Length", kLengthWidth));
}

ftxui::Element JukeboxPanel::RenderSong(const Song& song,
                                        bool on_cursor) const {
  // The caret is the cursor; the track playing is a whole row in the theme's
  // colour, its place a dimmer shade of the same. Two marks, so a row can
  // carry both at once.
  ftxui::Element row = ftxui::hbox({
      ftxui::text(on_cursor ? "> " : "  "),
      ftxui::text(PadRight(song.title, kTitleWidth + kCellGap)),
      ftxui::text(PadRight(song.place, kPlaceWidth + kCellGap)) | ftxui::dim,
      ftxui::text(PadLeft(Clock(song.duration_ms / 1000), kLengthWidth)),
  });
  if (song.track != director_.playing()) {
    return row;
  }
  return std::move(row) | ftxui::color(kTheme);
}

ftxui::Element JukeboxPanel::RenderList() const {
  int total = static_cast<int>(songs_.size());
  int first = ScrollWindowStart(total, row_, kListRows);
  std::vector<ftxui::Element> cells = ScrollBarCells(total, first, kListRows);
  std::vector<ftxui::Element> rows = {RenderHeader(), ThemedSeparator()};
  for (int i = 0; i < kListRows; ++i) {
    if (first + i >= total) {
      rows.push_back(i == 0 ? EmptyState("no music in this build", kCaretWidth)
                            : ftxui::text(""));
      continue;
    }
    ftxui::Element row =
        RenderSong(songs_[first + i], !on_buttons_ && first + i == row_);
    if (cells.empty()) {
      rows.push_back(std::move(row));
      continue;
    }
    rows.push_back(ftxui::hbox({
        std::move(row) |
            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth - 1),
        std::move(cells[i]) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1),
    }));
  }
  return ThemedWindow(
      " Jukebox ",
      ftxui::vbox(std::move(rows)) |
          ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kContentWidth),
      !on_buttons_);
}

ftxui::Element JukeboxPanel::Render() const {
  ftxui::Element screen = ftxui::vbox({
                              RenderNowPlaying(),
                              RenderList(),
                          }) |
                          ftxui::reflect(panel_box_);
  if (!mode_box_open_) {
    return screen;
  }
  // The box hangs clear under the button -- the row below it, so the button is
  // left whole -- with its right border against the button's right edge. Both
  // boxes are in screen coordinates and the overlay floats from the screen's
  // corner, so the panel's corner comes off.
  return ftxui::dbox({
      std::move(screen),
      Floating(mode_menu_.Render(
          mode_button_box_.y_min - panel_box_.y_min + 1,
          mode_button_box_.x_max - panel_box_.x_min - mode_menu_.Width() + 1)),
  });
}

}  // namespace ms
