/* TextField is the shared one-line text entry: a row the player selects,
 * presses Enter to edit, types into, and presses Enter again to keep.
 *
 * Editing starts from an empty buffer, not the old text, so the common case of
 * replacing the text needs no deleting. Leaving with nothing typed keeps the
 * old text, so Escape, Up, Down and an empty Enter all do the same thing.
 *
 * The field holds only the text being typed. The caller decides what the text
 * is for, takes the entry once, and stores it.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_TEXT_FIELD_H_
#define MS_SRC_FRONTEND_WIDGETS_TEXT_FIELD_H_

#include <string>

#include "ftxui/component/event.hpp"

namespace ms {

// What an event did to the field. kPending covers both a keystroke that changed
// the buffer and one that was ignored. Either way editing continues.
enum class TextEntry {
  kPending,
  // Enter on something typed. The caller takes text() and stores it.
  kCommitted,
  // Escape, or a key that leaves with nothing typed. Nothing to store.
  kCancelled,
};

class TextField {
 public:
  // `max_length` is the most characters the field holds. Extra ones are dropped
  // instead of scrolling the row, since callers size their panels by it.
  explicit TextField(int max_length) : max_length_(max_length) {
  }

  // Opens the field on an empty buffer.
  void BeginEdit();
  void EndEdit();
  bool editing() const {
    return editing_;
  }
  // What has been typed. Still readable right after kCommitted, which is when
  // the caller reads it. Cleared by a cancel and by the next BeginEdit.
  const std::string& text() const {
    return text_;
  }

  // Handles one key. Only letters, digits and spaces are accepted: names are
  // shown in fixed-width rows and read by other players, so no punctuation that
  // could disguise one. Backspace and Delete both erase, since a player may
  // have bound Backspace to Cancel.
  //
  // Ends the edit on kCommitted and kCancelled. Up and Down cancel and are then
  // left to the caller, which decides whether the arrow also moves the cursor.
  TextEntry OnEvent(const ftxui::Event& event);

 private:
  int max_length_;
  bool editing_ = false;
  std::string text_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_TEXT_FIELD_H_
