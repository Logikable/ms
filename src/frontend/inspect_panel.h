/* InspectPanel renders full details for a single equip item (EquipInstance or
 * EquipTrace): star bar, name, level, job categories, per-stat breakdown, and
 * remaining upgrade slots. Used in the scroll screen and inspect screens.
 * SetItem(nullptr) renders a placeholder.
 */
#ifndef MS_SRC_FRONTEND_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_INSPECT_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/item.h"
#include "src/protos/equip.pb.h"

namespace ms {

class InspectPanel {
 public:
  void SetItem(const EquipTabItem* item);
  ftxui::Element Render() const;

 private:
  static ftxui::Element FormatJobCategories(const EquipPrototype& proto);
  static std::string FormatEquipType(EquipType type);
  // Returns "Stage N (name)" or empty string if unspecified.
  static std::string FormatAttackSpeed(AttackSpeed speed);
  // Returns a colored hbox with the stat line, or nullptr if all are zero.
  // Total and base are default color; scroll is amber; SF is periwinkle.
  static ftxui::Element StatLine(const std::string& label, int base, int scroll,
                                 int sf = 0);
  // Returns filled (★) and empty (☆) stars in groups of 5 up to max_stars.
  // Filled stars are gold; empty stars are dark gray.
  static ftxui::Element StarBar(int stars, int max_stars);

  const EquipTabItem* item_ = nullptr;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_INSPECT_PANEL_H_
