/* Every stat the character has, on one screen, two to a row, under the same
 * name, level and combat power heading the Character panel carries.
 *
 * The Character panel shows as many of them as the terminal leaves room for
 * and stops; this is where the rest of them are. Nothing here is spendable --
 * no AP counter and no [+] -- because this screen is for reading.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_ALL_STATS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_ALL_STATS_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/widgets/stat_rows.h"
#include "src/protos/skill.pb.h"

namespace ms {

class AllStatsPanel {
 public:
  // Two stat columns side by side, each a label and a right-aligned value.
  // The value column is wider than the Character panel's: an AP stat here
  // carries its breakdown in front of its total.
  static constexpr int kColumnWidth = 30;
  static constexpr int kContentWidth = 2 * kColumnWidth;
  static constexpr int kTotalWidth = kContentWidth + 2;

  AllStatsPanel(const CharacterInstance& character,
                const std::map<std::string, Skill>& skills);
  ftxui::Element Render() const;

 private:
  // `lines` laid out two to a row, in the order they arrive. An odd row's
  // second half is left blank rather than pulling the next row up.
  static ftxui::Element Pairs(const std::vector<StatLine>& lines);

  const CharacterInstance& character_;
  const std::map<std::string, Skill>& skills_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_ALL_STATS_PANEL_H_
