#include "src/frontend/widgets/marquee.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace ms {
namespace {

using std::chrono::milliseconds;

// The name a skill row was widened by before the column was fixed: 22 columns
// wanting 17, so five characters have to slide past.
constexpr char kLong[] = "Final Attack: Crossbow";
constexpr int kWidth = 17;
constexpr int kSteps = 5;

std::string At(milliseconds elapsed) {
  return ScrollingWindow(kLong, kWidth, elapsed);
}

// A name that fits is a name that never moves, however long it is looked at:
// a column of short names must not start shuffling because one row is
// selected.
TEST(MarqueeTest, ShortNamesArePaddedAndStayPut) {
  EXPECT_EQ(ScrollingWindow("Iron Body", 12, milliseconds(0)), "Iron Body   ");
  EXPECT_EQ(ScrollingWindow("Iron Body", 12, milliseconds(60000)),
            "Iron Body   ");
  // Exactly filling the column is still short enough to sit still.
  EXPECT_EQ(ScrollingWindow("Iron Body", 9, milliseconds(9000)), "Iron Body");
}

TEST(MarqueeTest, AnUnselectedRowShowsTheHeadCut) {
  EXPECT_EQ(At(milliseconds(0)), "Final Attack: Cro");
}

// The head is held still first, so the beginning of the name can be read
// before it leaves.
TEST(MarqueeTest, TheHeadIsHeldBeforeItSlides) {
  EXPECT_EQ(At(kMarqueePause - milliseconds(1)), "Final Attack: Cro");
  EXPECT_EQ(At(kMarqueePause), "inal Attack: Cros");
}

// One character a step, the window sliding along the name.
TEST(MarqueeTest, ItSlidesOneCharacterAStep) {
  EXPECT_EQ(At(kMarqueePause + kMarqueeStep), "nal Attack: Cross");
  EXPECT_EQ(At(kMarqueePause + kMarqueeStep * 2), "al Attack: Crossb");
  EXPECT_EQ(At(kMarqueePause + kMarqueeStep * 3), "l Attack: Crossbo");
  EXPECT_EQ(At(kMarqueePause + kMarqueeStep * 4), " Attack: Crossbow");
}

// The slide is the offsets between the two ends, so it takes one step fewer
// than there are characters to get past.
constexpr milliseconds kSlide = kMarqueeStep * (kSteps - 1);
constexpr milliseconds kCycle = kMarqueePause * 2 + kSlide;

// The tail arrives and stays put for the pause -- the whole point of sliding
// is to be able to read the end, which needs longer than one step.
TEST(MarqueeTest, TheTailIsHeldOnceItArrives) {
  EXPECT_EQ(At(kMarqueePause + kSlide), " Attack: Crossbow");
  EXPECT_EQ(At(kCycle - milliseconds(1)), " Attack: Crossbow");
}

// And then it begins again from the head rather than stopping at the end.
TEST(MarqueeTest, ItStartsOverAfterTheTailPause) {
  EXPECT_EQ(At(kCycle), At(milliseconds(0)));
  EXPECT_EQ(At(kCycle + kMarqueePause), At(kMarqueePause));
  // Still cycling many turns later, not run off the end of the string.
  EXPECT_EQ(At(kCycle * 20 + kMarqueeStep), At(kMarqueeStep));
}

// The window never runs past the end of the name, which is what a step count
// taken straight from the clock would do.
TEST(MarqueeTest, TheWindowIsAlwaysFullWidth) {
  for (int ms = 0; ms < 20000; ms += 50) {
    EXPECT_EQ(static_cast<int>(At(milliseconds(ms)).size()), kWidth) << ms;
  }
}

// Degenerate callers rather than a crash: a column with no room shows nothing,
// and a clock that ran backwards shows the head.
TEST(MarqueeTest, NoRoomAndNoTimeAreBothAnswerable) {
  EXPECT_EQ(ScrollingWindow(kLong, 0, milliseconds(5000)), "");
  EXPECT_EQ(ScrollingWindow(kLong, -3, milliseconds(5000)), "");
  EXPECT_EQ(At(milliseconds(-5000)), "Final Attack: Cro");
}

}  // namespace
}  // namespace ms
