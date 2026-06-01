/* CharacterPanel renders the character stats pane: level, job name, and the
 * four base stats (STR/DEX/INT/LUK). Produces a new ftxui Element on each
 * Render() call.
 */
#ifndef MS_SRC_FRONTEND_CHARACTER_PANEL_H_
#define MS_SRC_FRONTEND_CHARACTER_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/protos/character.pb.h"

namespace ms {

class CharacterPanel {
 public:
  explicit CharacterPanel(const CharacterInstance& character);
  ftxui::Element Render() const;

 private:
  static std::string JobName(Job job);

  const CharacterInstance& character_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CHARACTER_PANEL_H_
