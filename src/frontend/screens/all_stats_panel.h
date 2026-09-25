/* Every stat the character has on one screen, two per row, under the same name,
 * level and combat power heading as the Character panel.
 *
 * The Character panel shows as many stats as the terminal has room for, and
 * this screen shows the rest. Nothing here can be spent (no AP counter and no
 * [+]), since this screen is for reading.
 *
 * Once Hyper Stats unlock, a Farm/Boss row under the heading shows which Hyper
 * Stat allocation the numbers use, and Left and Right switch between them. It
 * opens on whichever one the Character panel was showing.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_ALL_STATS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_ALL_STATS_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/frontend/widgets/stat_rows.h"
#include "src/protos/skill.pb.h"

namespace ms {

class AllStatsPanel {
 public:
  // Two stat columns side by side, each a label and a right-aligned value. The
  // value column is wider than the Character panel's because an AP stat here
  // shows its breakdown before its total.
  static constexpr int kColumnWidth = 30;
  static constexpr int kContentWidth = 2 * kColumnWidth;
  static constexpr int kTotalWidth = kContentWidth + 2;

  // `account` is the player's own, for the level gate on the Farm/Boss row.
  // Null for someone else's sheet, such as a party member's, which is all we
  // have of them, so the gate uses the level on the sheet.
  AllStatsPanel(const CharacterInstance& character,
                const AccountInstance* account,
                const std::map<std::string, Skill>& skills);
  // The catalog is held by reference, so a temporary one would dangle.
  AllStatsPanel(const CharacterInstance& character,
                const AccountInstance* account,
                std::map<std::string, Skill>&& skills) = delete;
  ftxui::Element Render() const;

  // Which allocation the screen shows. Set from the Character panel when the
  // screen opens, so the two never show different numbers for one stat.
  void SetPreset(Activity preset) {
    preset_ = preset;
  }
  Activity preset() const {
    return preset_;
  }

  // Left and Right on the Farm/Boss row, the only keys this screen handles.
  // Returns whether the event was consumed; false leaves closing the screen to
  // the caller.
  bool OnEvent(const ftxui::Event& event);

  // Whether the Farm/Boss row is drawn, which depends on the Hyper Stats level.
  // Public because it adds a row and a rule, which a screen sizing itself
  // around this one has to count.
  bool ShowsPresetBar() const;

 private:
  // The heading and the stat columns, with no window around them.
  ftxui::Element RenderBody() const;
  // `lines` laid out two per row, in order. An odd row's second half is left
  // blank instead of pulling the next row up.
  static ftxui::Element Pairs(const std::vector<StatLine>& lines);

  const CharacterInstance& character_;
  const AccountInstance* account_ = nullptr;
  const std::map<std::string, Skill>& skills_;
  Activity preset_ = Activity::kFarming;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_ALL_STATS_PANEL_H_
