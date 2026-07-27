#ifndef MS_SRC_FRONTEND_PANEL_UTIL_H_
#define MS_SRC_FRONTEND_PANEL_UTIL_H_

#include <cstdint>
#include <string>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/frontend/colors.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"

namespace ms {

// True for any "go back" key (Escape or Backspace).
inline bool IsBack(const ftxui::Event& e) {
  return e == ftxui::Event::Escape || e == ftxui::Event::Backspace;
}

// True for any "confirm / advance" key (Enter or Space).
inline bool IsForward(const ftxui::Event& e) {
  return e == ftxui::Event::Return || e == ftxui::Event::Character(' ');
}

// A single displayable stat: its label and how to read it from an EquipStats.
struct DisplayStat {
  const char* label;
  int (EquipStats::*fn)() const;
  int GetFrom(const EquipStats& s) const {
    return (s.*fn)();
  }
};

// Canonical display order for equip stats. Zero-value fields are typically
// hidden by callers. Update this array to add or reorder stats site-wide.
inline const DisplayStat kDisplayStats[] = {
    {"STR", &EquipStats::str},    {"DEX", &EquipStats::dex},
    {"INT", &EquipStats::int_},   {"LUK", &EquipStats::luk},
    {"HP", &EquipStats::max_hp},  {"MP", &EquipStats::max_mp},
    {"ATT", &EquipStats::attack}, {"MATT", &EquipStats::magic_attack},
    {"DEF", &EquipStats::def},
};

// Pads s to width with trailing spaces, or truncates if longer.
std::string PadRight(const std::string& s, int width);

// Formats an integer with thousands-separator commas (e.g. 1234567 ->
// "1,234,567"). Handles negatives.
std::string FormatWithCommas(int64_t n);

// Formats a meso amount as the coin indicator followed by the comma-separated
// value (e.g. "🪙 1,234,567"). Use everywhere meso is shown.
std::string FormatMeso(int64_t meso);

// Appends "+val label" to out (with "  " separator if non-empty).
// No-op if val <= 0.
void AppendStat(std::string& out, int val, const std::string& label);

// Returns the display name for an equip slot (e.g. "Weapon"). Returns ""
// for slot types not yet implemented.
std::string FormatSlot(EquipSlot slot);

// Returns "All" for universal items or a slash-separated list of job category
// names (e.g. "Warrior/Thief"). Also returns "All" when the list is empty.
std::string FormatJobCategories(const EquipPrototype& proto);

// Returns the display name for a job (e.g. "Swordman"), or "Unknown" for a job
// not yet given a name.
std::string JobName(Job job);

// Returns the short display label for an AP stat field (e.g. "STR"), or "" for
// STAT_FIELD_UNSPECIFIED.
std::string StatFieldName(StatField field);

// Formats a single item list entry: name (26 cols), slot (10 cols), info
// (padded to 20 cols), and scroll pass/left/restore counts. Pass -1 for all
// three scroll values to render "-" (use for non-upgradeable items).
std::string FormatItemEntry(const std::string& name, EquipSlot slot,
                            const std::string& info, int scroll_pass,
                            int scroll_left, int scroll_restore);

// A one-row progress bar filled to frac (clamped to [0, 1]) in `fill`, with the
// remainder in kBarEmpty. `label` is centered over the bar, dark on the filled
// side and light on the unfilled side; pass "" for an unlabelled bar.
//
// Writes pixels directly rather than using ftxui::gauge, which ignores color
// decorators and cannot carry a label without a dbox overwriting one or the
// other.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label);
// Overload holding the label to one color the whole way across, for a fill
// dark enough to read that color against. The label then keeps its color as
// the bar moves, rather than turning over a character at a time. Don't reach
// for this on a light fill -- that's what the two-tone default is for.
ftxui::Element ProgressBar(float frac, ftxui::Color fill,
                           const std::string& label, ftxui::Color label_color);

// Wraps content in a bordered window with the game's steel-blue theme color on
// the border and title. Content foreground is set to white; explicitly colored
// elements (gold stars, amber SF, etc.) and ThemedSeparator override it. Pass
// focused=true to invert the title into a solid chip, marking the panel that
// currently holds focus.
// The one way a panel says it has nothing to show: the reason in parentheses,
// e.g. " (empty)". Use "empty" for a list with no contents and a specific
// reason ("no item", "no matching items") only where it tells the player
// something they couldn't already see. `gutter` is the leading indent, so the
// row can line up with the cursor column of the list it stands in for.
ftxui::Element EmptyState(const std::string& what, int gutter = 1);

// One chip of a tab bar in the game's one tab style: the label padded by a
// space either side, theme-colored, and highlighted when it is the active tab.
// An active chip goes white while its row holds focus and keeps the theme-blue
// invert otherwise, which is how the player tells which bar the arrow keys are
// reaching. Pass row_focused=true unconditionally for a bar that is the only
// thing on its screen.
ftxui::Element TabChip(const std::string& label, bool active, bool row_focused);

// Renders a bracketed button in the game's one button style, inverted when
// focused. Every button the player can land on is drawn with this -- the
// confirm bar, the amount selector's [1]/[MAX], the [+] beside a stat, and the
// always-selected [Continue] on the result screens.
ftxui::Element ActionButton(const std::string& label, bool focused);

ftxui::Element ThemedWindow(const std::string& title, ftxui::Element content,
                            bool focused = false);

// Returns a horizontal separator rule in the theme border color.
ftxui::Element ThemedSeparator();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANEL_UTIL_H_
