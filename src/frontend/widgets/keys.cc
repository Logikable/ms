#include "src/frontend/widgets/keys.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"

namespace ms {
namespace {

// Overrides only Focusable(). ComponentBase's own OnRender and OnEvent already
// forward to a single child, so everything else passes through.
class AlwaysFocusableComponent : public ftxui::ComponentBase {
 public:
  explicit AlwaysFocusableComponent(ftxui::Component child) {
    Add(std::move(child));
  }

  bool Focusable() const override {
    return true;
  }
};

}  // namespace

int StepCursor(int current, int delta, int stops) {
  if (stops <= 0) {
    return 0;
  }
  // Modulo twice, because in C++ a negative number's remainder is negative. The
  // first % may give a value below zero, and adding `stops` before the second
  // brings it back into range. It works for any delta, not only the single step
  // every caller passes today.
  return ((current + delta) % stops + stops) % stops;
}

TabStop StepTabRing(const std::vector<int>& tabs, TabStop from, int delta) {
  if (tabs.empty()) {
    return from;
  }
  int door = static_cast<int>(tabs.size());
  int at = door;
  if (!from.on_door) {
    std::vector<int>::const_iterator it =
        std::find(tabs.begin(), tabs.end(), from.tab);
    if (it == tabs.end()) {
      return {tabs.front(), false};
    }
    at = static_cast<int>(it - tabs.begin());
  }
  // The door is the stop after the last tab, so the whole bar is one ring of
  // tabs.size() + 1 stops that StepCursor walks.
  int next = StepCursor(at, delta, door + 1);
  return next == door ? TabStop{from.tab, true} : TabStop{tabs[next], false};
}

ftxui::Component AlwaysFocusable(ftxui::Component child) {
  return ftxui::Make<AlwaysFocusableComponent>(std::move(child));
}

ftxui::Component WrappingList(ftxui::Component list, int& selected,
                              std::function<int()> count) {
  // A pointer rather than a captured reference, because the lambda lives as
  // long as the component, well past this call. A pointer is simpler to reason
  // about.
  int* cursor = &selected;
  return ftxui::CatchEvent(
      std::move(list), [cursor, count = std::move(count)](ftxui::Event event) {
        bool up = event == ftxui::Event::ArrowUp;
        bool down = event == ftxui::Event::ArrowDown;
        if (!up && !down) {
          return false;
        }
        int stops = count();
        if (stops <= 0) {
          // Consumed instead of passed down. An ftxui::Menu with no entries
          // still moves its index on an arrow, which leaves the cursor at row
          // -1 of an empty list, and the panels above read that index to decide
          // what the player is looking at.
          return true;
        }
        if (up && *cursor <= 0) {
          *cursor = StepCursor(0, -1, stops);
          return true;
        }
        if (down && *cursor >= stops - 1) {
          *cursor = StepCursor(stops - 1, 1, stops);
          return true;
        }
        // A step within the list, which the menu handles itself.
        return false;
      });
}

}  // namespace ms
