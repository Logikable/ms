/* RerollPrompt is the question a reroll screen asks: Confirm buys another roll
 * and leaves the window open, and a choosing roll adds Keep ↑ and Keep ↓ above
 * Confirm and Cancel, a square the arrows walk. The cubing and flaming screens
 * share it, so their keys can't drift apart.
 *
 * It tracks only the cursor. The caller owns the roll on offer and says
 * whether there is one, and clears it on kKeepBefore and kClosed.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_REROLL_PROMPT_H_
#define MS_SRC_FRONTEND_WIDGETS_REROLL_PROMPT_H_

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/widgets/confirm_prompt.h"

namespace ms {

// What a key did in the question.
enum class RerollAction {
  kNone,
  // Confirm: buy another roll.
  kReroll,
  // A choosing roll's Keep buttons. Keep After is the caller's to apply.
  kKeepBefore,
  kKeepAfter,
  // Cancel or Escape. A pending After is dropped, keeping the item as it was.
  kClosed,
};

class RerollPrompt {
 public:
  bool open() const {
    return confirm_.open();
  }
  void Close();
  // Moves the cursor off a Confirm the purse no longer covers.
  void FocusCancel() {
    confirm_.FocusCancel();
  }
  // Enter on a closed prompt opens it, with Confirm greyed if `affordable` is
  // false. Escape on a closed prompt is the caller's, so it is checked before
  // this. `has_after` says whether a choosing roll is on offer, which is what
  // lets the cursor up to the Keep row.
  RerollAction OnEvent(ftxui::Event event, bool affordable, bool has_after);
  // The Keep row, dimmed while there is nothing to keep.
  ftxui::Element KeepButtons(bool has_after) const;
  // The Confirm row's highlight: none while the cursor is on the Keep row.
  ConfirmFocus confirm_focus() const;

 private:
  enum class KeepFocus { kNone, kBefore, kAfter };

  RerollAction OnKeepEvent(const ftxui::Event& event, bool affordable);
  // Back to the Confirm row, on Confirm unless the purse is short.
  void FocusConfirmRow(bool cancel, bool affordable);

  KeepFocus keep_focus_ = KeepFocus::kNone;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_REROLL_PROMPT_H_
