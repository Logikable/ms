/* Keys, cursors and the components that make a list behave.
 *
 * The three predicates name the keys every panel reads, so no panel spells
 * out an ftxui::Event of its own. The rest is the plumbing a list needs to
 * keep its cursor where the player left it.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_KEYS_H_
#define MS_SRC_FRONTEND_WIDGETS_KEYS_H_

#include <functional>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"

namespace ms {

// True for the "go back" key. Every key the player has bound to Cancel
// arrives here as Escape.
inline bool IsBack(const ftxui::Event& e) {
  return e == ftxui::Event::Escape;
}

// True for the "confirm / advance" key, reached the same way.
inline bool IsForward(const ftxui::Event& e) {
  return e == ftxui::Event::Return;
}

// True for either half of the "switch panel" key. It moves the arrows between
// the panels of the main view, and between the two halves of a screen that
// puts an inspect card beside something else.
inline bool IsSwitchPanel(const ftxui::Event& e) {
  return e == ftxui::Event::Tab || e == ftxui::Event::TabReverse;
}

// Wraps `child` so it always reports itself focusable, forwarding rendering
// and events untouched.
//
// Container::Tab drops every key when its active child says it is not
// focusable, and an ftxui::Menu says that whenever its list is empty -- taking
// the tab bar above the list deaf with it.
ftxui::Component AlwaysFocusable(ftxui::Component child);

// Wraps a list so its cursor comes out the other end: Up on the first row
// lands on the last, and back. Only the two edges are taken -- the steps
// between them are the one thing ftxui::Menu gets right. `selected` must
// outlive the component, and `count` is asked per keypress because these lists
// gain and lose rows under the cursor.
//
// Wrong tool for a list under a tab bar: there the bar is a stop in the same
// ring, so those panels count it as stop 0 and call StepCursor.
ftxui::Component WrappingList(ftxui::Component list, int& selected,
                              std::function<int()> count);

// Where a cursor lands after stepping `delta` places in a ring of `stops`,
// coming out the other end rather than stopping. Every list walks with this --
// if you are writing `std::max(0, sel - 1)`, write this instead.
//
// A list under a tab bar counts the bar as stop 0, which makes "Up off the top
// row goes to the bar" and "Up off the bar goes to the last row" one rule. No
// stops answers 0; a `current` outside the ring is folded back into it.
int StepCursor(int current, int delta, int stops);

// The tab `active` becomes after stepping `delta` along a bar showing exactly
// `tabs`, in that order. Unlike the cursor's ring the ends of a bar are walls,
// so a step off either one leaves the tab where it was. A tab that is not on
// the bar at all answers the first one: nothing locks a tab away today --
// levels only go up -- but landing on the first beats landing on a tab the
// player cannot see.
int StepTab(const std::vector<int>& tabs, int active, int delta);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_KEYS_H_
