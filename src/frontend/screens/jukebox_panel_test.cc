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
#include "src/game_state.h"
#include "src/protos/boss.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {
namespace {

// Three tracks this build carries, so the rows under test have a real title
// and a real length behind them.
constexpr char kTreetops[] = "AboveTheTreetops";
constexpr char kFloral[] = "FloralLife";
constexpr char kHonTale[] = "HonTale";

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

// Two maps share Floral Life, and the lower-level one is the place it goes
// under. Hon Tale is a boss's alone.
GameState MakeState() {
  Boss horntail;
  horntail.set_name("Horntail");
  BossPhase* phase = horntail.add_difficulties()->add_phases();
  phase->set_bgm(kHonTale);
  return GameState(
      {}, {}, {}, {{"snail", MobAt("Snail", 1)}, {"drake", MobAt("Drake", 40)}},
      {{"ellinia", MapWith("Ellinia", kFloral, "snail")},
       {"deep_ellinia", MapWith("Deep Ellinia", kFloral, "drake")},
       {"lith", MapWith("Lith Harbor", kTreetops, "snail")}},
      /*skills=*/{}, GameMode::kPlay, /*test=*/{}, /*seed=*/std::nullopt,
      /*sets=*/{}, {{"horntail", horntail}});
}

class JukeboxPanelTest : public testing::Test {
 protected:
  void SetUp() override {
    if (!kAudioEnabled) {
      GTEST_SKIP() << "built with --define=audio=off";
    }
    panel_.Reset();
  }

  // Centred in the smallest terminal the game is laid out for, which is where
  // the screen actually stands. Dimension::Fit is no good here: it clips to
  // the terminal running the test, and this box's is 80 columns.
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

  // Puts the focus on the buttons and the cursor on `button`.
  void SelectButton(JukeboxButton button) {
    panel_.SwitchHalf();
    panel_.MoveColumn(static_cast<int>(button) -
                      static_cast<int>(panel_.selected_button()));
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

// A region's music is shared, and the place it goes under is the map a player
// meets it on -- the lowest of them, not whichever sorted first.
TEST_F(JukeboxPanelTest, TheLowestMapWinsASharedTrackAndABossKeepsItsOwn) {
  EXPECT_EQ(SongFor(kFloral).place, "Ellinia");
  EXPECT_EQ(SongFor(kHonTale).place, "Horntail");
}

TEST_F(JukeboxPanelTest, OpensOnWhatIsPlayingWithTheListInFocus) {
  director_.Play(kFloral);
  panel_.Reset();
  EXPECT_FALSE(panel_.on_buttons());
  EXPECT_EQ(panel_.songs()[panel_.selected_row()].track, kFloral);
}

TEST_F(JukeboxPanelTest, EnterOnASongPlaysIt) {
  SelectTrack(kFloral);
  panel_.Activate();
  EXPECT_EQ(director_.playing(), kFloral);
  // Through once: what follows it is the mode's business.
  EXPECT_FALSE(player_.looping());
}

TEST_F(JukeboxPanelTest, TabMovesToTheButtonsAndTheArrowsRingRound) {
  EXPECT_FALSE(panel_.on_buttons());
  panel_.SwitchHalf();
  EXPECT_TRUE(panel_.on_buttons());
  EXPECT_EQ(panel_.selected_button(), JukeboxButton::kRewind);
  panel_.MoveColumn(-1);
  EXPECT_EQ(panel_.selected_button(), JukeboxButton::kMode);
  panel_.MoveColumn(1);
  EXPECT_EQ(panel_.selected_button(), JukeboxButton::kRewind);

  // Up and Down belong to the list, so they do nothing over here.
  int row = panel_.selected_row();
  panel_.MoveRow(1);
  EXPECT_EQ(panel_.selected_row(), row);
}

TEST_F(JukeboxPanelTest, PlayPauseHoldsTheMusicAndLetsItGo) {
  director_.Play(kFloral);
  SelectButton(JukeboxButton::kPlayPause);
  panel_.Activate();
  EXPECT_TRUE(director_.paused());
  panel_.Activate();
  EXPECT_FALSE(director_.paused());
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
  // The box opens with the cursor on the mode in use, so Enter alone changes
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
  // Nothing left to shut, so the next Escape is the screen's.
  EXPECT_FALSE(panel_.DismissedBox());
}

// The box hangs from the button, so leaving the row puts it away rather than
// leaving it standing over the list.
TEST_F(JukeboxPanelTest, WalkingOffTheModeButtonShutsTheBox) {
  SelectButton(JukeboxButton::kMode);
  panel_.Activate();
  ASSERT_TRUE(panel_.mode_box_open());
  panel_.MoveColumn(1);
  EXPECT_FALSE(panel_.mode_box_open());
  // The arrow was spent on the box, so the cursor is still on Mode.
  EXPECT_EQ(panel_.selected_button(), JukeboxButton::kMode);

  panel_.Activate();
  ASSERT_TRUE(panel_.mode_box_open());
  panel_.SwitchHalf();
  EXPECT_FALSE(panel_.mode_box_open());
}

// The first mode lands ON the button rather than below it, and the names line
// up: the box opens where the value it replaces was standing.
TEST_F(JukeboxPanelTest, TheBoxOpensOverTheButtonItHangsFrom) {
  account_.SetJukeboxMode(JUKEBOX_MODE_FOLLOW_MAP);
  SelectButton(JukeboxButton::kMode);
  ScreenPos shut = FindOnScreen(Draw(), "Follow Map");
  ASSERT_GE(shut.x, 0);

  panel_.Activate();
  ScreenPos open = FindOnScreen(Draw(), "Follow Map");
  EXPECT_EQ(open.y, shut.y);
  EXPECT_EQ(open.x, shut.x);
}

TEST_F(JukeboxPanelTest, EveryRowKeepsItsRightGutter) {
  EXPECT_TRUE(RowsTouchingTheRightBorder(panel_.Render()).empty());
}

}  // namespace
}  // namespace ms
