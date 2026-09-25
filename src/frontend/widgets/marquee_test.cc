#include "src/frontend/widgets/marquee.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "ftxui/screen/string.hpp"

namespace ms {
namespace {

using std::chrono::milliseconds;

// A skill name 22 columns long in a 17-column window, so five characters have
// to scroll past.
constexpr char kLong[] = "Final Attack: Crossbow";
constexpr int kWidth = 17;
constexpr int kSteps = 5;

std::string At(milliseconds elapsed) {
  return ScrollingWindow(kLong, kWidth, elapsed);
}

// A name that fits never moves, however long it is selected, so a column of
// short names doesn't shift when one row is selected.
TEST(MarqueeTest, ShortNamesArePaddedAndStayPut) {
  EXPECT_EQ(ScrollingWindow("Iron Body", 12, milliseconds(0)), "Iron Body   ");
  EXPECT_EQ(ScrollingWindow("Iron Body", 12, milliseconds(60000)),
            "Iron Body   ");
  // A name that exactly fills the column still doesn't move.
  EXPECT_EQ(ScrollingWindow("Iron Body", 9, milliseconds(9000)), "Iron Body");
}

TEST(MarqueeTest, AnUnselectedRowShowsTheHeadCut) {
  EXPECT_EQ(At(milliseconds(0)), "Final Attack: Cro");
}

// The start is held still first, so the beginning of the name can be read
// before it moves.
TEST(MarqueeTest, TheHeadIsHeldBeforeItSlides) {
  EXPECT_EQ(At(kMarqueePause - milliseconds(1)), "Final Attack: Cro");
  EXPECT_EQ(At(kMarqueePause), "inal Attack: Cros");
}

// One character per step, with the window moving along the name.
TEST(MarqueeTest, ItSlidesOneCharacterAStep) {
  EXPECT_EQ(At(kMarqueePause + kMarqueeStep), "nal Attack: Cross");
  EXPECT_EQ(At(kMarqueePause + kMarqueeStep * 2), "al Attack: Crossb");
  EXPECT_EQ(At(kMarqueePause + kMarqueeStep * 3), "l Attack: Crossbo");
  EXPECT_EQ(At(kMarqueePause + kMarqueeStep * 4), " Attack: Crossbow");
}

// The scroll covers the offsets between the two ends, so it takes one step
// fewer than there are characters to pass.
constexpr milliseconds kSlide = kMarqueeStep * (kSteps - 1);
constexpr milliseconds kCycle = kMarqueePause * 2 + kSlide;

// The end arrives and stays for the pause. Scrolling exists so the end can be
// read, which takes longer than one step.
TEST(MarqueeTest, TheTailIsHeldOnceItArrives) {
  EXPECT_EQ(At(kMarqueePause + kSlide), " Attack: Crossbow");
  EXPECT_EQ(At(kCycle - milliseconds(1)), " Attack: Crossbow");
}

// Then it starts again from the beginning instead of stopping at the end.
TEST(MarqueeTest, ItStartsOverAfterTheTailPause) {
  EXPECT_EQ(At(kCycle), At(milliseconds(0)));
  EXPECT_EQ(At(kCycle + kMarqueePause), At(kMarqueePause));
  // Still cycling many rounds later, without running off the end of the string.
  EXPECT_EQ(At(kCycle * 20 + kMarqueeStep), At(kMarqueeStep));
}

// The window never runs past the end of the name, which a step count taken
// straight from the clock would do.
TEST(MarqueeTest, TheWindowIsAlwaysFullWidth) {
  for (int ms = 0; ms < 20000; ms += 50) {
    EXPECT_EQ(static_cast<int>(At(milliseconds(ms)).size()), kWidth) << ms;
  }
}

// Bytes aren't columns. Cutting "Emeraude" at ten bytes gives nine columns, and
// cutting inside a character leaves something nothing can draw.
constexpr char kAccented[] = "\u00c9meraude Sabre";

TEST(MarqueeTest, AMultibyteNameIsCutByColumns) {
  EXPECT_EQ(ScrollingWindow(kAccented, 10, milliseconds(0)), "\u00c9meraude S");
  EXPECT_EQ(ScrollingWindow(kAccented, 10, kMarqueePause), "meraude Sa");
  // Whether a name fits is also measured in columns, so the padding isn't a
  // column short.
  EXPECT_EQ(ScrollingWindow(kAccented, 16, milliseconds(0)),
            "\u00c9meraude Sabre  ");
}

// A fullwidth character is one character in two columns, so a window edge can
// fall inside it. The column it can't fill is left blank, rather than cutting
// the character in half or making the row a column wider.
constexpr char kFullwidth[] = "\u9752\u9f8d\u5043\u6708\u5200";

TEST(MarqueeTest, AFullwidthNameKeepsItsColumns) {
  EXPECT_EQ(ScrollingWindow(kFullwidth, 7, milliseconds(0)),
            "\u9752\u9f8d\u5043 ");
  EXPECT_EQ(ScrollingWindow(kFullwidth, 7, kMarqueePause),
            " \u9f8d\u5043\u6708");
  for (int ms = 0; ms < 20000; ms += 50) {
    EXPECT_EQ(
        ftxui::string_width(ScrollingWindow(kFullwidth, 7, milliseconds(ms))),
        7)
        << ms;
  }
}

// Bad input doesn't crash: a zero-width column shows nothing, and a clock that
// ran backwards shows the start.
TEST(MarqueeTest, NoRoomAndNoTimeAreBothAnswerable) {
  EXPECT_EQ(ScrollingWindow(kLong, 0, milliseconds(5000)), "");
  EXPECT_EQ(ScrollingWindow(kLong, -3, milliseconds(5000)), "");
  EXPECT_EQ(At(milliseconds(-5000)), "Final Attack: Cro");
}

// A panel the cursor has left isn't being read, so nothing on it scrolls,
// however long the row had been selected when focus left.
TEST(SelectionClockTest, AnUnfocusedRowHoldsAtZero) {
  SelectionClock clock;
  clock.Follow(3);
  std::this_thread::sleep_for(milliseconds(20));
  EXPECT_GT(clock.Elapsed(), std::chrono::steady_clock::duration::zero());
  clock.Follow(3, /*focused=*/false);
  std::this_thread::sleep_for(milliseconds(20));
  EXPECT_EQ(clock.Elapsed(), std::chrono::steady_clock::duration::zero());
}

// When focus returns the name starts from the beginning, as it does when the
// cursor moves onto a row, so the player never finds a name halfway through.
TEST(SelectionClockTest, FocusReturningStartsTheNameOver) {
  SelectionClock clock;
  clock.Follow(3);
  std::this_thread::sleep_for(milliseconds(200));
  clock.Follow(3, /*focused=*/false);
  clock.Follow(3, /*focused=*/true);
  EXPECT_LT(clock.Elapsed(), milliseconds(200));
}

// Moving the selection still restarts the name, with or without focus.
TEST(SelectionClockTest, AMovedSelectionStartsOver) {
  SelectionClock clock;
  clock.Follow(3);
  std::this_thread::sleep_for(milliseconds(200));
  clock.Follow(4);
  EXPECT_LT(clock.Elapsed(), milliseconds(200));
}

}  // namespace
}  // namespace ms
