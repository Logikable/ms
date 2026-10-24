/* Every stat the character has on one screen, two per row, under the same name,
 * level and combat power heading as the Character panel.
 *
 * The Character panel shows as many stats as the terminal has room for, and
 * this screen shows the rest. Nothing here can be spent (no AP counter and no
 * [+]), since this screen is for reading.
 *
 * Once Hyper Stats unlock, a Farm/Boss row under the heading shows which Hyper
 * Stat allocation the numbers use, and Left and Right switch between them. It
 * opens on whichever one the Character panel was showing. Once gear presets
 * unlock, the player's own sheet adds Drop: their boss stats in the Drop
 * preset's gear, which is what boss drops roll at.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_ALL_STATS_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_ALL_STATS_PANEL_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/account.h"
#include "src/character/character.h"
#include "src/character/hyper_stats.h"
#include "src/character/stat_preset.h"
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
  // have of them, so the gate uses the level on the sheet. `reader` is the
  // account looking, whose options decide how the sheet is drawn.
  AllStatsPanel(const CharacterInstance& character,
                const AccountInstance* account, const AccountInstance& reader,
                const std::map<std::string, Skill>& skills);
  // The catalog is held by reference, so a temporary one would dangle.
  AllStatsPanel(const CharacterInstance& character,
                const AccountInstance* account, const AccountInstance& reader,
                std::map<std::string, Skill>&& skills) = delete;
  ftxui::Element Render() const;

  // Which allocation the screen shows. Set from the Character panel when the
  // screen opens, so the two never show different numbers for one stat.
  void SetPreset(Activity preset);
  Activity preset() const;
  // Whether the Drop tab is the one showing.
  bool showing_drop() const;

  // Left and Right on the preset row, the only keys this screen handles.
  // Returns whether the event was consumed; false leaves closing the screen to
  // the caller.
  bool OnEvent(const ftxui::Event& event);

  // Whether the preset row is drawn. Public because it adds a row and a rule,
  // which a screen sizing itself around this one has to count.
  bool ShowsPresetBar() const;

 private:
  // One tab on the preset row: the allocation it reads, and the gear preset
  // it overrides, if any.
  struct View {
    std::string label;
    Activity activity = Activity::kFarming;
    std::optional<StatPreset> gear;
  };
  // Farm and Boss once there are two allocations to pick between, which
  // depends on the Hyper Stats level and the autoswap.
  bool ShowsFarmBoss() const;
  // The Drop tab, on the player's own sheet once gear presets unlock. A party
  // member's sheet has no Drop preset to read.
  bool ShowsDrop() const;
  // The row's tabs, left to right. One entry, unlabelled, when there is no
  // row.
  std::vector<View> Views() const;
  View Current() const;

  // The heading and the stat columns, with no window around them.
  ftxui::Element RenderBody() const;
  // `lines` laid out two per row, in order. An odd row's second half is left
  // blank instead of pulling the next row up.
  static ftxui::Element Pairs(const std::vector<StatLine>& lines);

  const CharacterInstance& character_;
  const AccountInstance* account_ = nullptr;
  const AccountInstance& reader_;
  const std::map<std::string, Skill>& skills_;
  // What the Character panel was showing, which the first tab reads when there
  // is no Farm/Boss pair.
  Activity preset_ = Activity::kFarming;
  int tab_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_ALL_STATS_PANEL_H_
