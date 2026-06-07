#ifndef MS_SRC_FRONTEND_PANEL_UTIL_H_
#define MS_SRC_FRONTEND_PANEL_UTIL_H_

#include <string>

#include "src/protos/equip.pb.h"

namespace ms {

// Pads s to width with trailing spaces, or truncates if longer.
std::string PadRight(const std::string& s, int width);

// Appends "+val label" to out (with "  " separator if non-empty).
// No-op if val <= 0.
void AppendStat(std::string& out, int val, const std::string& label);

// Returns the display name for an equip slot (e.g. "Weapon", "Hat").
std::string FormatSlot(EquipSlot slot);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_PANEL_UTIL_H_
