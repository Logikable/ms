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
#include "src/frontend/testing/screen_text.h"

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

// Down from the last stop goes to the first, and Up from the first goes to the
// last.
TEST(StepCursorTest, ComesOutTheOtherEndAtEitherEdge) {
  EXPECT_EQ(StepCursor(3, 1, 4), 0);
  EXPECT_EQ(StepCursor(0, -1, 4), 3);
}

// In a ring with one stop every step does nothing. This is a panel with an
// empty list, where the tab bar is the only stop.
TEST(StepCursorTest, AOneStopRingGoesNowhere) {
  EXPECT_EQ(StepCursor(0, 1, 1), 0);
  EXPECT_EQ(StepCursor(0, -1, 1), 0);
}

// An empty ring returns zero instead of leaving it to the caller, because every
// list here can be empty and none of them should need its own check.
TEST(StepCursorTest, AnEmptyRingAnswersZero) {
  EXPECT_EQ(StepCursor(0, -1, 0), 0);
  EXPECT_EQ(StepCursor(3, 1, -1), 0);
}

// In C++ a negative number's remainder is negative, so a single modulo would
// return -1 here and put the cursor off the list.
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

// A cursor left past the end of a list that shrank (a tab switched, an item
// sold) is brought back into range instead of moving further off.
TEST(StepCursorTest, FoldsACurrentFromOutsideTheRingBackIn) {
  EXPECT_EQ(StepCursor(9, 1, 4), 2);
  EXPECT_EQ(StepCursor(-3, 0, 4), 1);
}

// Any delta works, not just the single step every caller passes today.
TEST(StepCursorTest, TakesMoreThanOneStopAtATime) {
  EXPECT_EQ(StepCursor(0, 3, 4), 3);
  EXPECT_EQ(StepCursor(0, 5, 4), 1);
  EXPECT_EQ(StepCursor(0, -5, 4), 3);
}

// --- StepTabRing ---

// The bar holds only the tabs the character has reached, so its entries need
// not be 0, 1, 2. A step moves one place along the bar, not one tab number.
TEST(StepTabRingTest, StepsAlongTheBarRatherThanTheTabNumbers) {
  std::vector<int> tabs = {0, 3, 5};
  EXPECT_EQ(StepTabRing(tabs, {0, false}, 1).tab, 3);
  EXPECT_EQ(StepTabRing(tabs, {5, false}, -1).tab, 3);
  EXPECT_FALSE(StepTabRing(tabs, {0, false}, 1).on_door);
}

// The door is the stop after the last tab, and the bar wraps through it.
TEST(StepTabRingTest, TheDoorClosesTheRing) {
  std::vector<int> tabs = {0, 1};
  TabStop door = StepTabRing(tabs, {1, false}, 1);
  EXPECT_TRUE(door.on_door);
  EXPECT_EQ(door.tab, 1) << "the door shows no list, so the tab stands still";
  EXPECT_EQ(StepTabRing(tabs, door, 1).tab, 0) << "right off the door wraps";
  EXPECT_FALSE(StepTabRing(tabs, door, 1).on_door);
  EXPECT_EQ(StepTabRing(tabs, door, -1).tab, 1) << "and left steps back";
  EXPECT_TRUE(StepTabRing(tabs, {0, false}, -1).on_door)
      << "left off the first tab reaches the door";
}

// A tab that isn't on the bar at all. Nothing hides one today, but landing on
// the first beats landing on a tab the player can't see.
TEST(StepTabRingTest, ATabOffTheBarLandsOnTheFirst) {
  EXPECT_EQ(StepTabRing({1, 2}, {7, false}, 1).tab, 1);
  EXPECT_EQ(StepTabRing({}, {7, false}, 1).tab, 7)
      << "with no bar there is nothing to land on";
}

// --- WrappingList ---

namespace {

// A three-row ftxui::Menu wrapped to cycle. It shares `selected` and `entries`
// with the caller, so a test can change one and read the other.
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

TEST(WrappingListTest, EitherEndRollsRoundToTheOther) {
  std::vector<std::string> entries = {"a", "b", "c"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 2);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowDown));
  EXPECT_EQ(selected, 0);
}

// The count is read on each keypress, not once. These lists lose rows under the
// cursor (an item sold, a filter narrowed), and a wrap using the old length
// would send the cursor past the end.
TEST(WrappingListTest, AsksHowLongTheListIsEveryTime) {
  std::vector<std::string> entries = {"a", "b", "c", "d", "e"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  entries = {"a", "b"};
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 1) << "the last row of the list as it is now";
}

// There is no end to wrap from, so the key is consumed. Passing it down would
// cause harm: an ftxui::Menu with no entries still moves its index, and the
// cursor ends up at row -1 of an empty list.
TEST(WrappingListTest, SwallowsTheKeyOnAnEmptyList) {
  std::vector<std::string> entries;
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowUp));
  EXPECT_EQ(selected, 0);
  EXPECT_TRUE(list->OnEvent(ftxui::Event::ArrowDown));
  EXPECT_EQ(selected, 0);
}

// A list of one wraps onto itself. The cursor is at both ends, and either key
// leaves it in place instead of seeming to move it.
TEST(WrappingListTest, ASingleRowGoesNowhere) {
  std::vector<std::string> entries = {"only"};
  int selected = 0;
  ftxui::Component list = WrappedMenu(entries, selected);
  list->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(selected, 0);
  list->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(selected, 0);
}

// Every key except an edge step passes through, so wrapping a list costs it no
// other key.
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

// A component that reports itself unfocusable, as ftxui::Menu does with no
// entries. Renderer() without a child is the simplest one.
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
// whether it is focusable and drops every key when it isn't, so an unwrapped
// panel never sees the event.
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
