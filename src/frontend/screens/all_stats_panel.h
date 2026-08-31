/* Every stat the character has, on one screen, two to a row, under the same
 * name, level and combat power heading the Character panel carries.
 *
 * The Character panel shows as many of them as the terminal leaves room for
 * and stops; this is where the rest of them are. Nothing here is spendable --
 * no AP counter and no [+] -- because this screen is for reading.
 *
 * From level 140 a Farm/Boss row under the heading says which Hyper Stat
 * allocation the numbers are read through, and Left/Right move between them.
 * It opens on whichever one the Character panel was showing.
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
  // Two stat columns side by side, each a label and a right-aligned value.
  // The value column is wider than the Character panel's: an AP stat here
  // carries its breakdown in front of its total.
  static constexpr int kColumnWidth = 30;
  static constexpr int kContentWidth = 2 * kColumnWidth;
  static constexpr int kTotalWidth = kContentWidth + 2;

  // `account` is the player's own, for the level gate on the Farm/Boss row.
  // Null for a sheet that is not theirs -- a party member's, whose allocation
  // is not ours to switch between.
  AllStatsPanel(const CharacterInstance& character,
                const AccountInstance* account,
                const std::map<std::string, Skill>& skills);
  ftxui::Element Render() const;

  // Which allocation the screen reads. Set from the Character panel when the
  // screen opens, so the two never show different numbers for one stat.
  void SetPreset(StatPreset preset) {
    preset_ = preset;
  }
  StatPreset preset() const {
    return preset_;
  }

  // Left/Right on the Farm/Boss row, which is the only thing this screen
  // takes. Returns whether the event was consumed; false leaves closing the
  // screen to the caller.
  bool OnEvent(const ftxui::Event& event);

 private:
  // Whether the Farm/Boss row is drawn at all -- Hyper Stats' own level.
  bool ShowsPresetBar() const;
  // The heading and the stat columns, with no window around them.
  ftxui::Element RenderBody() const;
  // `lines` laid out two to a row, in the order they arrive. An odd row's
  // second half is left blank rather than pulling the next row up.
  static ftxui::Element Pairs(const std::vector<StatLine>& lines);

  const CharacterInstance& character_;
  const AccountInstance* account_ = nullptr;
  const std::map<std::string, Skill>& skills_;
  StatPreset preset_ = StatPreset::kFarming;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_ALL_STATS_PANEL_H_
