/* SoulPanel is the left half of the soul screen: the souls the purse can make,
 * and the question one of them asks. It is laid out like the flaming screen.
 *
 * A row is a boss with at least 10 shards; its Quantity is the souls they
 * make. Confirm rolls a soul onto the weapon and leaves the window open, as a
 * Red Cube does, so the player can roll again. The question stays on the
 * shard it was opened for even once that shard's row has left the list, and
 * greys Confirm when the shards run short.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_SOUL_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_SOUL_PANEL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/reroll_prompt.h"
#include "src/item/currency.h"
#include "src/item/equip_instance.h"
#include "src/protos/item.pb.h"

namespace ms {

// The most souls a row's Quantity shows.
inline constexpr int kMaxSoulsShown = 999;

// The question's width inside its border, fixed so it doesn't resize as a
// soul is added or rolled. It fits the longest boss's prompt and every soul's
// lines; screen_fit_test checks the catalog against it.
inline constexpr int kSoulDialogWidth = 36;

class SoulPanel {
 public:
  // The weapon and the purse whose shards make its souls. Called every frame,
  // so a roll's new soul and smaller balance arrive here.
  void SetItem(const EquipInstance* item, const CurrencyPurse& purse);
  // Cursor on the first soul, question closed. Call when the screen opens.
  void Reset();
  // The list. `focused` highlights the title; it is off while the item card
  // beside it has the arrows.
  ftxui::Element Render(bool focused) const;
  // The question, for the caller to centre over both columns.
  ftxui::Element RenderConfirm() const;
  bool IsConfirming() const {
    return prompt_.open();
  }
  // Moves along the list, wrapping. Ignored while the question is open.
  void MoveCursor(int delta);
  // Enter on an empty list opens nothing.
  RerollAction OnEvent(ftxui::Event event);
  // The shard the question is about, or the one under the cursor. Null for an
  // empty list.
  const ItemPrototype* selected_shard() const;

 private:
  struct Row {
    ItemPrototype shard;
    int64_t count = 0;
  };

  bool Affordable() const;
  std::vector<ftxui::Element> CurrentSoulRows() const;

  const EquipInstance* item_ = nullptr;
  std::vector<Row> rows_;
  int selected_ = 0;
  // The shard the question was opened for and its balance now.
  std::optional<Row> asked_;
  RerollPrompt prompt_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_SOUL_PANEL_H_
