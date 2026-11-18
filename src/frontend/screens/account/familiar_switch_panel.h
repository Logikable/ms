/* FamiliarSwitchPanel is the left half of the Switch screen, opened from a
 * row of the Familiar tab: every familiar on the roster, with the Familiar
 * tab's columns and an In Use column ticking the ones the preset already
 * summons. The familiar under the cursor is drawn on a card beside it, which
 * the caller places.
 *
 * The cursor moves through the roster and then [Close] below a rule, one
 * ring. Enter on a familiar asks "Equip <Name>?"; the row's own familiar is
 * dimmed, since switching it for itself changes nothing.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_FAMILIAR_SWITCH_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_FAMILIAR_SWITCH_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/protos/familiar.pb.h"

namespace ms {

// The Switch screen's list, borders included: 120 columns less the card.
inline constexpr int kFamiliarSwitchWidth = 83;

class FamiliarSwitchPanel {
 public:
  // What a key did.
  enum class Action { kNone, kClose, kEquip };

  // Opens on the row being replaced, `current`, with the question closed.
  void Reset(const std::string& current);
  // The book the rows read and the familiars the preset summons, every frame.
  void SetFamiliars(const FamiliarBook* book, std::vector<std::string> in_use);
  // The most rows the panel may take, borders included. Zero means no limit.
  void SetMaxRows(int rows) {
    max_rows_ = rows;
  }
  // The familiar under the cursor, or empty on [Close].
  std::string selected() const;
  ftxui::Element Render(bool focused) const;
  // The question, for the caller to centre over the screen.
  ftxui::Element RenderConfirm() const;
  bool IsConfirming() const {
    return confirm_.open();
  }
  // kEquip once the question is confirmed, for selected(); kClose on [Close]
  // or Escape outside the question.
  Action OnEvent(const ftxui::Event& event);

 private:
  int Stops() const;
  bool OnClose() const;

  const FamiliarBook* book_ = nullptr;
  std::vector<std::string> in_use_;
  std::string current_;
  int cursor_ = 0;
  int max_rows_ = 0;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_FAMILIAR_SWITCH_PANEL_H_
