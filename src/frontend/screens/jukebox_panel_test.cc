#include "src/frontend/screens/jukebox_panel.h"

#include <gtest/gtest.h>

#include <optional>
#include <random>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/audio/track_title.h"
#include "src/build_config.h"
#include "src/frontend/placement.h"
#include "src/frontend/testing/screen_text.h"
#include "src/frontend/widgets/colors.h"
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Three tracks this build has, so the rows under test have a real title and
// length.
constexpr char kTreetops[] = "AboveTheTreetops";
constexpr char kFloral[] = "FloralLife";
constexpr char kHonTale[] = "HonTale";
constexpr char kCave[] = "CaveOfHontale";
constexpr char kTeaParty[] = "JoyfulTeaParty";

Mob MobAt(const std::string& name, int level) {
  Mob mob;
  mob.set_name(name);
  mob.set_level(level);
  return mob;
}

MapData MapWith(const std::string& name, const std::string& bgm,
                const std::string& mob) {
  MapData map;
  map.set_name(name);
  map.set_bgm(bgm);
  Spawn* spawn = map.add_spawns();
  spawn->set_mob(mob);
  spawn->set_count(10);
  return map;
}

// Horntail as the data defines him: the cave theme from phase 1, continued
// through a phase naming none, and his own theme from phase 3.
Boss MakeHorntail() {
  Boss horntail;
  horntail.set_name("Horntail");
  BossDifficulty* normal = horntail.add_difficulties();
  normal->add_phases()->set_bgm(kCave);
  normal->add_phases();
  normal->add_phases()->set_bgm(kHonTale);
  return horntail;
}

// One theme for both of Pierre's phases, which is the whole fight.
Boss MakePierre() {
  Boss pierre;
  pierre.set_name("Pierre");
  BossDifficulty* normal = pierre.add_difficulties();
  normal->add_phases()->set_bgm(kTeaParty);
  normal->add_phases();
  return pierre;
}

// Two maps share Floral Life, and it is listed under the lower-level one. The
// boss themes belong to the bosses alone.
GameState MakeState() {
  return GameState(
      {}, {}, {}, {{"snail", MobAt("Snail", 1)}, {"drake", MobAt("Drake", 40)}},
      {{"ellinia", MapWith("Ellinia", kFloral, "snail")},
       {"deep_ellinia", MapWith("Deep Ellinia", kFloral, "drake")},
       {"lith", MapWith("Lith Harbor", kTreetops, "snail")}},
      /*skills=*/{}, GameMode::kPlay, /*test=*/{}, /*seed=*/std::nullopt,
      /*sets=*/{}, {{"horntail", MakeHorntail()}, {"pierre", MakePierre()}});
}

class JukeboxPanelTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!kAudioEnabled) {
      GTEST_SKIP() << "built with --define=audio=off";
    }
    panel_.Reset();
  }

  // Centred in the smallest supported terminal, as on the real screen.
  // Dimension::Fit doesn't work here: it clips to the terminal running the
  // test, which reports 80 columns.
  ftxui::Screen Draw() {
    ftxui::Screen screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(kMinTerminalColumns),
                              ftxui::Dimension::Fixed(kMinTerminalRows));
    ftxui::Render(screen, Centred(panel_.Render()));
    return screen;
  }

  std::string Text() {
    return ScreenText(Draw());
  }

  // Puts the cursor on the row playing `track`.
  void SelectTrack(const std::string& track) {
    panel_.Reset();
    for (int i = 0; i < static_cast<int>(panel_.songs().size()); ++i) {
      if (panel_.songs()[i].track == track) {
        panel_.MoveRow(i - panel_.selected_row());
        return;
      }
    }
    FAIL() << track << " is not in the song list";
  }

  // Puts the focus on the buttons and the cursor on `button`. SwitchHalf
  // toggles, so calling it twice would put the focus back on the list.
  void SelectButton(JukeboxButton button) {
    if (!panel_.on_buttons()) {
      panel_.SwitchHalf();
    }
    panel_.MoveColumn(static_cast<int>(button) -
                      static_cast<int>(panel_.selected_button()));
  }

  // The position of `track` in the drawn list.
  int RowOf(const std::string& track) {
    for (int i = 0; i < static_cast<int>(panel_.songs().size()); ++i) {
      if (panel_.songs()[i].track == track) {
        return i;
      }
    }
    ADD_FAILURE() << track << " is not in the song list";
    return 0;
  }

  const Song& SongFor(const std::string& track) {
    for (const Song& song : panel_.songs()) {
      if (song.track == track) {
        return song;
      }
    }
    ADD_FAILURE() << track << " is not in the song list";
    return panel_.songs().front();
  }

  GameState state_ = MakeState();
  AccountInstance account_;
  MusicPlayer player_{MusicPlayer::Backend::kNull};
  std::mt19937 rng_{5};
  MusicDirector director_{player_, rng_};
  JukeboxPanel panel_{state_, director_, account_};
};

TEST_F(JukeboxPanelTest, ListsEveryTrackWithItsPlaceAndItsLength) {
  ASSERT_EQ(panel_.songs().size(), TracksByTitle().size());
  EXPECT_EQ(SongFor(kTreetops).title, "Above the Treetops");
  EXPECT_EQ(SongFor(kTreetops).place, "Lith Harbor");
  EXPECT_GT(SongFor(kTreetops).duration_ms, 0);

  std::string out = Text();
  EXPECT_NE(out.find("Jukebox"), std::string::npos);
  EXPECT_NE(out.find("Now Playing"), std::string::npos);
  EXPECT_NE(out.find("Above the Treetops"), std::string::npos);
  EXPECT_NE(out.find("Lith Harbor"), std::string::npos);
}

// A region's music is shared, and it is listed under the map a player first
// hears it on: the lowest-level one, not whichever sorted first.
TEST_F(JukeboxPanelTest, TheLowestMapWinsASharedTrackAndABossNamesItsPhases) {
  EXPECT_EQ(SongFor(kFloral).place, "Ellinia");
  EXPECT_EQ(SongFor(kCave).place, "Horntail P1/2");
  EXPECT_EQ(SongFor(kHonTale).place, "Horntail P3");
  // A theme that plays the whole fight names no phase.
  EXPECT_EQ(SongFor(kTeaParty).place, "Pierre");
}

TEST_F(JukeboxPanelTest, OpensOnWhatIsPlayingWithTheListInFocus) {
  director_.Play(kFloral);
  panel_.Reset();
  EXPECT_FALSE(panel_.on_buttons());
  EXPECT_EQ(panel_.songs()[panel_.selected_row()].track, kFloral);
  // In the list, the playing track is a whole row in the theme colour: caret,
  // title, place and length. Checked on the row, not the title, since Now
  // Playing shows the title too.
  ftxui::Screen screen = Draw();
  ScreenPos live = FindOnScreen(screen, "> " + TrackTitle(kFloral));
  ASSERT_GE(live.x, 0);
  bool dimmed = false;
  for (int x = live.x; x < screen.dimx(); ++x) {
    ftxui::Pixel pixel = screen.PixelAt(x, live.y);
    // The row ends at the window's border; the scroll bar before it belongs to
    // the panel, not the row.
    if (pixel.character == "│") {
      break;
    }
    if (pixel.character == " " || pixel.character == "┃") {
      continue;
    }
    EXPECT_EQ(pixel.foreground_color, kTheme) << "column " << x;
    dimmed = dimmed || pixel.dim;
  }
  // The place is the one cell of the row in the dimmed colour.
  EXPECT_TRUE(dimmed);
  EXPECT_NE(ColorOf(screen, TrackTitle(kTreetops)), kTheme);
}

TEST_F(JukeboxPanelTest, EnterOnASongPlaysIt) {
  SelectTrack(kFloral);
  panel_.Activate();
  EXPECT_EQ(director_.playing(), kFloral);
  // Plays once through; what comes next depends on the mode.
  EXPECT_FALSE(player_.looping());
}

TEST_F(JukeboxPanelTest, TabMovesToTheButtonsAndTheArrowsRingRound) {
  EXPECT_FALSE(panel_.on_buttons());
  panel_.SwitchHalf();
  EXPECT_TRUE(panel_.on_buttons());
  EXPECT_EQ(panel_.selected_button(), JukeboxButton::kPrevious);
  panel_.MoveColumn(-1);
  EXPECT_EQ(panel_.selected_button(), JukeboxButton::kMode);
  panel_.MoveColumn(1);
  EXPECT_EQ(panel_.selected_button(), JukeboxButton::kPrevious);

  // Up and Down belong to the list, so they do nothing here.
  int row = panel_.selected_row();
  panel_.MoveRow(1);
  EXPECT_EQ(panel_.selected_row(), row);
}

TEST_F(JukeboxPanelTest, PlayPauseHoldsTheMusicAndLetsItGo) {
  director_.Play(kFloral);
  SelectButton(JukeboxButton::kPlayPause);
  EXPECT_NE(Text().find("[Pause]"), std::string::npos);
  panel_.Activate();
  EXPECT_TRUE(director_.paused());
  // The two labels are the same width, so the row doesn't move under the
  // cursor.
  EXPECT_NE(Text().find("[Play ]"), std::string::npos);
  panel_.Activate();
  EXPECT_FALSE(director_.paused());
}

// The two track buttons move through the song list, wrapping like every other
// list, and the previous-track button restarts a song that is well under way.
TEST_F(JukeboxPanelTest, TheTrackMarksWalkTheListAndRestartASongUnderWay) {
  director_.Play(kFloral);
  int row = RowOf(kFloral);
  ASSERT_GT(row, 0);
  SelectButton(JukeboxButton::kNext);
  panel_.Activate();
  EXPECT_EQ(director_.playing(), panel_.songs()[row + 1].track);
  SelectButton(JukeboxButton::kPrevious);
  panel_.Activate();
  EXPECT_EQ(director_.playing(), panel_.songs()[row].track);

  director_.Nudge(2 * kSkipSeconds);
  ASSERT_GE(director_.position_seconds(), kSkipSeconds);
  panel_.Activate();
  EXPECT_EQ(director_.playing(), panel_.songs()[row].track);
  EXPECT_LT(director_.position_seconds(), kSkipSeconds);

  // Before the start of the list is the end of it.
  director_.Play(panel_.songs().front().track);
  panel_.Activate();
  EXPECT_EQ(director_.playing(), panel_.songs().back().track);
}

TEST_F(JukeboxPanelTest, TheSkipButtonsMoveTheCursorFiveSeconds) {
  director_.Play(kTreetops);
  ASSERT_GT(director_.length_seconds(), 2 * kSkipSeconds);
  SelectButton(JukeboxButton::kSkip);
  panel_.Activate();
  panel_.Activate();
  EXPECT_GE(director_.position_seconds(), 2 * kSkipSeconds - 1.0f);
  SelectButton(JukeboxButton::kRewind);
  panel_.Activate();
  EXPECT_LT(director_.position_seconds(), 2 * kSkipSeconds);
}

TEST_F(JukeboxPanelTest, TheModeBoxOpensOnTheModeInUseAndEnterTakesOne) {
  account_.SetJukeboxMode(JUKEBOX_MODE_PLAYLIST);
  SelectButton(JukeboxButton::kMode);
  EXPECT_NE(Text().find("Mode: Playlist"), std::string::npos);

  panel_.Activate();
  EXPECT_TRUE(panel_.mode_box_open());
  // The box opens with the cursor on the current mode, so Enter alone changes
  // nothing.
  panel_.Activate();
  EXPECT_FALSE(panel_.mode_box_open());
  EXPECT_EQ(account_.jukebox_mode(), JUKEBOX_MODE_PLAYLIST);

  panel_.Activate();
  panel_.MoveRow(1);
  panel_.Activate();
  EXPECT_EQ(account_.jukebox_mode(), JUKEBOX_MODE_SHUFFLE);
}

TEST_F(JukeboxPanelTest, EscapeShutsTheBoxBeforeItShutsTheScreen) {
  SelectButton(JukeboxButton::kMode);
  panel_.Activate();
  ASSERT_TRUE(panel_.mode_box_open());
  EXPECT_TRUE(panel_.DismissedBox());
  EXPECT_FALSE(panel_.mode_box_open());
  // Nothing left to close, so the next Escape is the screen's.
  EXPECT_FALSE(panel_.DismissedBox());
}

// The box hangs from the button, so leaving the row closes it instead of
// leaving it over the list.
TEST_F(JukeboxPanelTest, WalkingOffTheModeButtonShutsTheBox) {
  SelectButton(JukeboxButton::kMode);
  panel_.Activate();
  ASSERT_TRUE(panel_.mode_box_open());
  panel_.MoveColumn(1);
  EXPECT_FALSE(panel_.mode_box_open());
  // The arrow was used on the box, so the cursor is still on Mode.
  EXPECT_EQ(panel_.selected_button(), JukeboxButton::kMode);

  panel_.Activate();
  ASSERT_TRUE(panel_.mode_box_open());
  panel_.SwitchHalf();
  EXPECT_FALSE(panel_.mode_box_open());
}

// The box hangs just below the button: its top border is on the next row, so
// the button stays whole, and its right border lines up with the button's.
TEST_F(JukeboxPanelTest, TheBoxHangsUnderTheButtonItOpensFrom) {
  account_.SetJukeboxMode(JUKEBOX_MODE_FOLLOW_MAP);
  SelectButton(JukeboxButton::kMode);
  ftxui::Screen shut_screen = Draw();
  ScreenPos shut = FindOnScreen(shut_screen, "Follow Map");
  ASSERT_GE(shut.x, 0);
  ScreenPos bracket = FindOnScreen(shut_screen, " ▾]");

  panel_.Activate();
  ftxui::Screen open_screen = Draw();
  ScreenPos open = FindOnScreen(open_screen, "> Follow Map");
  EXPECT_EQ(open.y, shut.y + 2);
  // The whole row of the middle entry. The box is only as wide as the names
  // need, and its right border is under the button's bracket. The middle entry
  // because the box crosses two window borders and ftxui joins its own into
  // those, so the rows on either side read ┤ and ├.
  int close = bracket.x + 2;
  ScreenPos middle = FindOnScreen(open_screen, "  Playlist");
  EXPECT_EQ(middle.y, shut.y + 3);
  EXPECT_EQ(ScreenRow(open_screen, middle.y, middle.x - 1, close + 1),
            "│  Playlist   │");
}

TEST_F(JukeboxPanelTest, EveryRowKeepsItsRightGutter) {
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel_.Render()).empty());
}

}  // namespace
}  // namespace ms
