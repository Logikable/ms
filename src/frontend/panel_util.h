#ifndef MS_SRC_FRONTEND_PANEL_UTIL_H_
#define MS_SRC_FRONTEND_PANEL_UTIL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/frontend/colors.h"
#include "src/protos/equip.pb.h"

namespace ms {

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

// Appends "+val label" to out (with "  " separator if non-empty).
// No-op if val <= 0.
void AppendStat(std::string& out, int val, const std::string& label);

// Returns the display name for an equip slot (e.g. "Weapon"). Returns ""
// for slot types not yet implemented.
std::string FormatSlot(EquipSlot slot);

// Returns "All" for universal items or a slash-separated list of job category
// names (e.g. "Warrior/Thief"). Also returns "All" when the list is empty.
std::string FormatJobCategories(const EquipPrototype& proto);

// Formats a single item list entry: name (26 cols), slot (10 cols), info
// (padded to 20 cols), and scroll pass/left/restore counts. Pass -1 for all
// three scroll values to render "-" (use for non-upgradeable items).
std::string FormatItemEntry(const std::string& name, EquipSlot slot,
                            const std::string& info, int scroll_pass,
                            int scroll_left, int scroll_restore);

// Renders a [Confirm] / [Cancel] button row. The selected button is inverted.
// cancel_selected=false highlights Confirm; true highlights Cancel.
ftxui::Element ConfirmBar(bool cancel_selected);

// Renders the confirm bar in a titled window. Intended to appear below the
// main action panel so height is reserved only when confirmation is pending.
ftxui::Element ConfirmWindow(bool cancel_selected);

// Wraps content in a bordered window with the game's steel-blue theme color on
// the border and title. Content foreground is set to white; explicitly colored
// elements (gold stars, amber SF, etc.) and ThemedSeparator override it.
ftxui::Element ThemedWindow(const std::string& title, ftxui::Element content);

// Returns a horizontal separator rule in the theme border color.
ftxui::Element ThemedSeparator();

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANEL_UTIL_H_
