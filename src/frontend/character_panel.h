/* CharacterPanel renders the character stats pane: level, job name, base stats
 * (HP/MP/STR/DEX/INT/LUK) including equipment bonuses, equipment-derived
 * combat stats (ATT/MATT), and unspent AP. Produces a new ftxui Element on
 * each Render() call.
 */
#ifndef MS_SRC_FRONTEND_CHARACTER_PANEL_H_
#define MS_SRC_FRONTEND_CHARACTER_PANEL_H_

#include <functional>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character.h"
#include "src/frontend/types.h"
#include "src/protos/character.pb.h"

namespace ms {

class CharacterPanel {
 public:
  // Total rendered width including the AP balcony (main 28 + balcony 7).
  static constexpr int kTotalWidth = 35;

  explicit CharacterPanel(const CharacterInstance& character, int& panel_focus);
  ftxui::Element Render() const;
  ftxui::Component MakeComponent(std::function<void()> on_ap);

 private:
  // Formats one stat row padded to kContentWidth. Shows "(base+bonus)" when
  // bonus > 0; omits the breakdown when bonus is zero.
  static std::string StatRow(const std::string& label, int base, int bonus);

  const CharacterInstance& character_;
  int& panel_focus_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CHARACTER_PANEL_H_
