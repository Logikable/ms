/* Keys, cursors and the components that make a list behave.
 *
 * The three predicates name the keys every panel reads, so no panel builds its
 * own ftxui::Event. The rest keeps a list's cursor where the player left it.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_KEYS_H_
#define MS_SRC_FRONTEND_WIDGETS_KEYS_H_

#include <functional>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"

namespace ms {

// True for the "go back" key. Every key the player has bound to Cancel arrives
// here as Escape.
inline bool IsBack(const ftxui::Event& e) {
  return e == ftxui::Event::Escape;
}

// True for the "confirm / advance" key, which arrives the same way.
inline bool IsForward(const ftxui::Event& e) {
  return e == ftxui::Event::Return;
}

// True for either half of the "switch panel" key. It moves the arrows between
// the panels of the main view, and between the two halves of a screen that
// shows an inspect card beside something else.
inline bool IsSwitchPanel(const ftxui::Event& e) {
  return e == ftxui::Event::Tab || e == ftxui::Event::TabReverse;
}

// Wraps `child` so it always reports itself focusable, forwarding rendering and
// events unchanged. Container::Tab drops every key when its active child isn't
// focusable, and an ftxui::Menu with an empty list says it isn't, which would
// also deafen the tab bar above it.
ftxui::Component AlwaysFocusable(ftxui::Component child);

// Wraps a list so its cursor wraps: Up on the first row goes to the last. Only
// the two edges are handled. `selected` must outlive the component. `count` is
// read on each keypress, since these lists can gain rows under the cursor.
//
// Don't use it for a list under a tab bar. There the bar is a stop in the same
// ring, so those panels count it as stop 0 and call StepCursor.
ftxui::Component WrappingList(ftxui::Component list, int& selected,
                              std::function<int()> count);

// Where a cursor lands after stepping `delta` in a ring of `stops`, wrapping
// around at the ends. Every list moves its cursor with this, so use it instead
// of `std::max(0, sel - 1)`. A list under a tab bar counts the bar as stop 0,
// so both its edges follow the same rule.
int StepCursor(int current, int delta, int stops);

// Where the cursor is on a tab bar that ends in a door (Expand): on one of the
// bar's pages, or on the door.
struct TabStop {
  int tab;
  bool on_door;
};

// Where `from` lands after stepping `delta` along a bar of `tabs` with the door
// after the last one. The bar wraps: Right from the door goes to the first tab.
// The door shows no list, so stepping onto it keeps the current tab. A tab not
// on the bar is treated as the first.
TabStop StepTabRing(const std::vector<int>& tabs, TabStop from, int delta);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_KEYS_H_
