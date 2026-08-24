/* TextField is the shared one-line text entry: a row the player steps onto,
 * presses Enter to edit, types into, and presses Enter again to keep.
 *
 * Editing starts from an empty buffer rather than from the old text, so the
 * common case -- replacing what is there -- is no keystrokes of deletion.
 * Leaving with nothing typed is how the old text is kept, which makes Escape,
 * Up, Down and an empty Enter all say the same thing.
 *
 * The field holds only the text being typed. What it is a name FOR belongs to
 * the caller, which is handed the entry once and stores it wherever it lives.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_TEXT_FIELD_H_
#define MS_SRC_FRONTEND_WIDGETS_TEXT_FIELD_H_

#include <string>

#include "ftxui/component/event.hpp"

namespace ms {

// What an event did to the field. kPending covers both a keystroke that
// changed the buffer and one that was swallowed; either way editing goes on.
enum class TextEntry {
  kPending,
  // Enter on something typed. The caller takes text() and stores it.
  kCommitted,
  // Escape, or a key that leaves with nothing typed. Nothing to store.
  kCancelled,
};

class TextField {
 public:
  // `max_length` is the most characters the field will hold; further ones are
  // dropped rather than scrolling the row, since a caller sizes its panel by
  // this.
  explicit TextField(int max_length) : max_length_(max_length) {
  }

  // Opens the field on an empty buffer.
  void BeginEdit();
  void EndEdit();
  bool editing() const {
    return editing_;
  }
  // What has been typed. Still readable right after kCommitted, which is when
  // the caller takes it; cleared by a cancel and by the next BeginEdit.
  const std::string& text() const {
    return text_;
  }

  // Handles one key. Only letters, digits and spaces are taken -- a name is
  // shown in fixed-width rows and read back by other players, so punctuation
  // that could disguise one is not on offer. A space needs a character before
  // it, and the spaces on the end of a commit are dropped. Backspace and
  // Delete both erase, since a player who has bound Backspace to Cancel would
  // otherwise have no way to.
  //
  // Ends the edit on kCommitted and kCancelled. Up and Down cancel and are
  // then left to the caller, which still holds the event and decides whether
  // an arrow also steps its cursor off the row.
  TextEntry OnEvent(const ftxui::Event& event);

 private:
  int max_length_;
  bool editing_ = false;
  std::string text_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_TEXT_FIELD_H_
