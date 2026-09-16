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
// and events untouched. Container::Tab drops every key when its active child
// says it is not, and an ftxui::Menu says that on an empty list -- taking the
// tab bar above it deaf as well.
ftxui::Component AlwaysFocusable(ftxui::Component child);

// Wraps a list so its cursor WRAPS: Up on the first row lands on the last.
// Only the two edges are taken. `selected` must outlive the component, and
// `count` is asked per keypress, these lists gaining rows under the cursor.
//
// Wrong tool for a list under a tab bar: there the bar is a stop in the same
// ring, so those panels count it as stop 0 and call StepCursor.
ftxui::Component WrappingList(ftxui::Component list, int& selected,
                              std::function<int()> count);

// Where a cursor lands after stepping `delta` in a ring of `stops`, coming out
// the other end rather than stopping. EVERY list walks with this -- if you are
// writing `std::max(0, sel - 1)`, write this instead. A list under a tab bar
// counts the bar as stop 0, which makes both its edges one rule.
int StepCursor(int current, int delta, int stops);

// Where the cursor stands on a tab bar that ends in a door -- Expand -- rather
// than in a wall: on one of the pages the bar lists, or out on the door.
struct TabStop {
  int tab;
  bool on_door;
};

// The stop `from` becomes after stepping `delta` along a bar of `tabs` with
// the door past their right end. A RING: Right off the door comes round to the
// first tab. The door shows no list, so stepping onto it leaves the tab where
// it was, and a tab not on the bar answers the first.
TabStop StepTabRing(const std::vector<int>& tabs, TabStop from, int delta);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_KEYS_H_
