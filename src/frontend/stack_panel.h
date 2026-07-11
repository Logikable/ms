/* StackPanel renders the character's Use/Etc item stacks (farmed drops) as a
 * read-only list, one row per stack. Display-only for now: no navigation or
 * actions, so it does not participate in the Tab focus cycle. Produces a new
 * ftxui Element on each Render() call.
 */
#ifndef MS_SRC_FRONTEND_STACK_PANEL_H_
#define MS_SRC_FRONTEND_STACK_PANEL_H_

#include "ftxui/dom/elements.hpp"
#include "src/character.h"

namespace ms {

class StackPanel {
 public:
  explicit StackPanel(const CharacterInstance& character);
  ftxui::Element Render() const;

 private:
  const CharacterInstance& character_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_STACK_PANEL_H_
