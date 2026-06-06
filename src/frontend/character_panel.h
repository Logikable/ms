/* CharacterPanel renders the character stats pane: level, job name, base stats
 * (HP/MP/STR/DEX/INT/LUK) including equipment bonuses, equipment-derived
 * combat stats (ATT/MATT), and unspent AP. Produces a new ftxui Element on
 * each Render() call.
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
  // Total rendered width including the AP balcony (main 26 + balcony 6).
  static constexpr int kTotalWidth = 32;

  explicit CharacterPanel(const CharacterInstance& character);
  ftxui::Element Render() const;

 private:
  static std::string JobName(Job job);
  // Formats two label/value pairs on one line; left field is padded to 12
  // chars so the right label aligns regardless of left-side value width.
  static std::string StatLine(const std::string& l1, int v1,
                              const std::string& l2, int v2);

  const CharacterInstance& character_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_CHARACTER_PANEL_H_
