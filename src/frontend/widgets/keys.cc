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

// Overrides nothing but Focusable(). ComponentBase's own OnRender and OnEvent
// already forward to a lone child, so everything else passes straight through.
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
  // Modulo twice, because C++ gives a negative remainder a negative sign: the
  // first % may land below zero, and adding stops before the second brings it
  // back into the ring. Written for any delta rather than just the one step
  // every caller passes, so a caller that ever wants two is not a special case.
  return ((current + delta) % stops + stops) % stops;
}

int StepTab(const std::vector<int>& tabs, int active, int delta) {
  if (tabs.empty()) {
    return active;
  }
  std::vector<int>::const_iterator at =
      std::find(tabs.begin(), tabs.end(), active);
  if (at == tabs.end()) {
    return tabs.front();
  }
  int next = static_cast<int>(at - tabs.begin()) + delta;
  if (next < 0 || next >= static_cast<int>(tabs.size())) {
    return active;
  }
  return tabs[next];
}

ftxui::Component AlwaysFocusable(ftxui::Component child) {
  return ftxui::Make<AlwaysFocusableComponent>(std::move(child));
}

ftxui::Component WrappingList(ftxui::Component list, int& selected,
                              std::function<int()> count) {
  // Held as a pointer rather than a captured reference: the lambda outlives
  // this call by the life of the component, and a reference captured into it
  // would be one more thing to reason about than an address that cannot itself
  // be rebound.
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
          // Swallowed rather than passed down. An ftxui::Menu with no entries
          // still moves its index on an arrow, which leaves the cursor
          // pointing at row -1 of a list that has no rows -- and the panels
          // above read that index to decide what the player is looking at.
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
        // A step through the middle, which is the menu's own business.
        return false;
      });
}

}  // namespace ms
