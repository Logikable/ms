#include "src/frontend/widgets/keys.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/screen.hpp"
#include "src/frontend/widgets/screen_text.h"

namespace ms {
namespace {

ftxui::Screen RenderSized(ftxui::Element element, int width, int height) {
  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                               ftxui::Dimension::Fixed(height));
  ftxui::Render(screen, std::move(element));
  return screen;
}

// --- StepCursor ---

TEST(StepCursorTest, WalksTheRingOneStopAtATime) {
  EXPECT_EQ(StepCursor(0, 1, 4), 1);
  EXPECT_EQ(StepCursor(2, 1, 4), 3);
  EXPECT_EQ(StepCursor(3, -1, 4), 2);
}

// The whole point of it. Down off the last stop lands on the first, and Up off
// the first lands on the last.
TEST(StepCursorTest, ComesOutTheOtherEndAtEitherEdge) {
  EXPECT_EQ(StepCursor(3, 1, 4), 0);
  EXPECT_EQ(StepCursor(0, -1, 4), 3);
}

// A ring with one place to stand is every step a no-op -- a panel whose list is
// empty, where the tab bar is the only stop there is.
TEST(StepCursorTest, AOneStopRingGoesNowhere) {
  EXPECT_EQ(StepCursor(0, 1, 1), 0);
  EXPECT_EQ(StepCursor(0, -1, 1), 0);
}

// Nowhere to stand at all: answered rather than left to the caller, because
// every list here can be empty and none of them wants its own check.
TEST(StepCursorTest, AnEmptyRingAnswersZero) {
  EXPECT_EQ(StepCursor(0, -1, 0), 0);
  EXPECT_EQ(StepCursor(3, 1, -1), 0);
}

// C++ hands a negative dividend a negative remainder, so a single modulo would
// answer -1 here and put the cursor off the list.
TEST(StepCursorTest, NeverAnswersBelowZero) {
  for (int stops = 1; stops <= 8; ++stops) {
    for (int current = 0; current < stops; ++current) {
      EXPECT_GE(StepCursor(current, -1, stops), 0)
          << "stops=" << stops << " current=" << current;
      EXPECT_LT(StepCursor(current, -1, stops), stops)
          << "stops=" << stops << " current=" << current;
    }
  }
}

// A cursor left pointing past the end of a list that shrank under it -- a tab
// switched, an item sold -- is folded back in rather than walked further off.
TEST(StepCursorTest, FoldsACurrentFromOutsideTheRingBackIn) {
  EXPECT_EQ(StepCursor(9, 1, 4), 2);
  EXPECT_EQ(StepCursor(-3, 0, 4), 1);
}

// Any delta, not just the one step every caller passes today.
TEST(StepCursorTest, TakesMoreThanOneStopAtATime) {
  EXPECT_EQ(StepCursor(0, 3, 4), 3);
  EXPECT_EQ(StepCursor(0, 5, 4), 1);
  EXPECT_EQ(StepCursor(0, -5, 4), 3);
}

// --- WrappingList ---

namespace {

// A three-row ftxui::Menu wrapped for cycling, sharing `selected` and
// `entries` with the caller so a test can move one and read the other.
ftxui::Component WrappedMenu(std::vector<std::string>& entries, int& selected) {
  return WrappingList(ftxui::Menu(&entries, &selected), selected, [&entries]() {
    return static_cast<int>(entries.size());
  });
}

}  // namespace

TEST(WrappingListTest, LeavesTheStepsThroughTheMiddleToTheMenu) {
  std::vector<std::string> entries = {"a", "b", "c"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowDown));
  EXPECT_EQ(selected, 1);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 0);
}

TEST(WrappingListTest, UpOffTheFirstRowLandsOnTheLast) {
  std::vector<std::string> entries = {"a", "b", "c"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 2);
}

TEST(WrappingListTest, DownOffTheLastRowLandsOnTheFirst) {
  std::vector<std::string> entries = {"a", "b", "c"};
  int selected = 2;
  ftxui::Component list = WrappedMenu(entries, selected);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowDown));
  EXPECT_EQ(selected, 0);
}

// The count is asked at the keypress, not taken once. These lists lose rows
// under the cursor -- an item sold, a filter narrowed -- and a wrap that
// remembered the old length would send the cursor off the end of the new one.
TEST(WrappingListTest, AsksHowLongTheListIsEveryTime) {
  std::vector<std::string> entries = {"a", "b", "c", "d", "e"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  entries = {"a", "b"};
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 1) << "the last row of the list as it is now";
}

// Nothing to be at either end of, so the key is swallowed. Handing it down
// instead is not harmless: an ftxui::Menu with no entries still moves its
// index, and the cursor ends up at row -1 of a list that has no rows.
TEST(WrappingListTest, SwallowsTheKeyOnAnEmptyList) {
  std::vector<std::string> entries;
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 0);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowDown));
  EXPECT_EQ(selected, 0);
}

// A list of one is a ring of one: the cursor is at both ends at once, and
// either key leaves it where it is rather than appearing to move.
TEST(WrappingListTest, ASingleRowGoesNowhere) {
  std::vector<std::string> entries = {"only"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  list->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(selected, 0);
  list->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(selected, 0);
}

// Everything that is not an edge step passes through untouched, so wrapping a
// list does not cost it any other key.
TEST(WrappingListTest, PassesEveryOtherKeyThrough) {
  std::vector<std::string> entries = {"a", "b", "c"};
  int selected = 0;
  bool seen = false;
  ftxui::Component list = WrappingList(
      ftxui::CatchEvent(ftxui::Menu(&entries, &selected),
                        [&seen](ftxui::Event) {
                          seen = true;
                          return false;
                        }),
      selected, [&entries]() { return static_cast<int>(entries.size()); });
  list->OnEvent(ftxui::Event::Character('x'));
  EXPECT_TRUE(seen);
}

// --- AlwaysFocusable ---

namespace {

// A component that reports itself unfocusable, as ftxui::Menu does when it has
// no entries. Renderer() without a child is the shortest one to hand.
ftxui::Component UnfocusableComponent() {
  return ftxui::Renderer([]() { return ftxui::text("PANEL"); });
}

}  // namespace

TEST(AlwaysFocusableTest, ReportsFocusableWhereTheChildDoesNot) {
  ftxui::Component child = UnfocusableComponent();
  ASSERT_FALSE(child->Focusable());
  EXPECT_TRUE(AlwaysFocusable(child)->Focusable());
}

TEST(AlwaysFocusableTest, DrawsWhatTheChildDraws) {
  ftxui::Component wrapped = AlwaysFocusable(UnfocusableComponent());
  EXPECT_EQ(ScreenRow(RenderSized(wrapped->Render(), 10, 1), 0, 0, 5), "PANEL");
}

TEST(AlwaysFocusableTest, PassesEventsToTheChild) {
  bool seen = false;
  ftxui::Component wrapped = AlwaysFocusable(
      ftxui::CatchEvent(UnfocusableComponent(), [&seen](ftxui::Event) {
        seen = true;
        return true;
      }));
  EXPECT_TRUE(wrapped->OnEvent(ftxui::Event::ArrowRight));
  EXPECT_TRUE(seen);
}

// The reason the wrapper exists. Container::Tab asks only its active child
// whether it is focusable and drops every key when the answer is no, so an
// unwrapped panel never sees the event at all.
TEST(AlwaysFocusableTest, ATabContainerReachesTheChild) {
  bool seen_bare = false;
  int selector = 0;
  ftxui::Component bare =
      ftxui::Container::Tab({ftxui::CatchEvent(UnfocusableComponent(),
                                               [&seen_bare](ftxui::Event) {
                                                 seen_bare = true;
                                                 return true;
                                               })},
                            &selector);
  ASSERT_FALSE(bare->OnEvent(ftxui::Event::ArrowRight));
  ASSERT_FALSE(seen_bare) << "the container is expected to drop this one";

  bool seen_wrapped = false;
  ftxui::Component wrapped = ftxui::Container::Tab(
      {AlwaysFocusable(ftxui::CatchEvent(UnfocusableComponent(),
                                         [&seen_wrapped](ftxui::Event) {
                                           seen_wrapped = true;
                                           return true;
                                         }))},
      &selector);
  EXPECT_TRUE(wrapped->OnEvent(ftxui::Event::ArrowRight));
  EXPECT_TRUE(seen_wrapped);
}

}  // namespace
}  // namespace ms
