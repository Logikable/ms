#include "src/frontend/screens/keybinds_panel.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/string.hpp"
#include "src/frontend/widgets/colors.h"
#include "src/frontend/widgets/panel_util.h"

namespace ms {
namespace {

// What an open slot shows, and what one waiting for a key shows.
constexpr char kEmptySlot[] = "—";
constexpr char kCapturing[] = "Press…";
// The action column, wide enough for "Previous Panel" and a gutter.
constexpr int kActionWidth = 17;

std::string SlotHeader(int slot) {
  return "Key " + std::to_string(slot + 1);
}

}  // namespace

KeybindsPanel::KeybindsPanel(const KeyMap& keys) : keys_(keys) {
}

void KeybindsPanel::Reset() {
  row_ = 0;
  slot_ = 1;
  capturing_ = false;
  message_.clear();
}

void KeybindsPanel::MoveRow(int delta) {
  message_.clear();
  // The Close button is one more stop after the actions.
  row_ = StepCursor(row_, delta, kKeyActionCount + 1);
}

void KeybindsPanel::MoveSlot(int delta) {
  message_.clear();
  if (on_close()) {
    return;
  }
  // Slot 0 is locked, so the row is a ring of the two the player owns.
  slot_ = 1 + StepCursor(slot_ - 1, delta, kKeySlots - 1);
}

KeyAction KeybindsPanel::selected_action() const {
  if (on_close()) {
    return KEY_ACTION_UNSPECIFIED;
  }
  return kKeyActions[row_];
}

void KeybindsPanel::StartCapture() {
  message_.clear();
  capturing_ = true;
}

void KeybindsPanel::StopCapture() {
  capturing_ = false;
}

void KeybindsPanel::ShowRefusal(const std::string& message) {
  message_ = message;
}

int KeybindsPanel::SlotWidth() const {
  int width = static_cast<int>(ftxui::string_width(kCapturing));
  for (int i = 0; i < kKeyActionCount; ++i) {
    for (int slot = 0; slot < kKeySlots; ++slot) {
      std::string label = keys_.Label(kKeyActions[i], slot);
      width = std::max(width, static_cast<int>(ftxui::string_width(label)));
    }
  }
  // A gutter, so two keys side by side do not read as one.
  return width + 2;
}

ftxui::Element KeybindsPanel::RenderCell(const std::string& label, bool locked,
                                         bool selected, int width) const {
  ftxui::Element cell =
      ftxui::text(label) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width);
  if (locked) {
    // Dim says the door does not open: the cursor steps over these.
    return std::move(cell) | ftxui::dim;
  }
  if (selected && capturing_) {
    return std::move(cell) | ftxui::color(kYellow) | ftxui::inverted;
  }
  if (selected) {
    return std::move(cell) | ftxui::inverted;
  }
  return cell;
}

ftxui::Element KeybindsPanel::RenderRow(KeyAction action, int row,
                                        int width) const {
  ftxui::Elements cells;
  cells.push_back(ftxui::text(" " + KeyActionName(action)) |
                  ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kActionWidth));
  for (int slot = 0; slot < kKeySlots; ++slot) {
    bool selected = row == row_ && slot == slot_ && !on_close();
    std::string label = keys_.Label(action, slot);
    if (label.empty()) {
      label = kEmptySlot;
    }
    if (selected && capturing_) {
      label = kCapturing;
    }
    cells.push_back(RenderCell(label, KeyMap::Locked(slot), selected, width));
  }
  return ftxui::hbox(std::move(cells));
}

ftxui::Element KeybindsPanel::RenderFooter() const {
  if (!message_.empty()) {
    // Red is the reason the key did not land.
    return CenteredRow(ftxui::text(message_) | ftxui::color(kRed));
  }
  return CenteredRow(ftxui::text("Enter to rebind · Esc to unbind") |
                     ftxui::dim);
}

ftxui::Element KeybindsPanel::Render() const {
  int width = SlotWidth();
  ftxui::Elements rows;

  ftxui::Elements header;
  header.push_back(ftxui::text(" Action") |
                   ftxui::size(ftxui::WIDTH, ftxui::EQUAL, kActionWidth));
  for (int slot = 0; slot < kKeySlots; ++slot) {
    header.push_back(ftxui::text(SlotHeader(slot)) |
                     ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width));
  }
  rows.push_back(ftxui::hbox(std::move(header)) | ftxui::color(kTheme));
  rows.push_back(ThemedSeparator());
  for (int i = 0; i < kKeyActionCount; ++i) {
    rows.push_back(RenderRow(kKeyActions[i], i, width));
  }
  rows.push_back(ThemedSeparator());
  rows.push_back(CenteredRow(ActionButton("Close", on_close())));
  rows.push_back(ThemedSeparator());
  rows.push_back(RenderFooter());
  return ThemedWindow(" Keybinds ", ftxui::vbox(std::move(rows)));
}

}  // namespace ms
