/* InspectPanel renders full details for a single EquipInstance: name, level,
 * job categories, per-stat breakdown (total and base+scroll components), and
 * remaining upgrade slots. Used in the scroll screen (right side) and future
 * upgrade screens. SetItem(nullptr) renders a placeholder.
 */
#ifndef MS_SRC_FRONTEND_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_INSPECT_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/protos/equip.pb.h"

namespace ms {

class InspectPanel {
 public:
  void SetItem(const EquipInstance* item);
  ftxui::Element Render() const;

 private:
  static std::string FormatJobCategories(const EquipPrototype& proto);
  static std::string FormatEquipType(EquipType type);
  // Returns "Stage N (name)" or empty string if unspecified.
  static std::string FormatAttackSpeed(AttackSpeed speed);
  // Returns "+total (base +scroll)" or empty string if both are zero.
  static std::string StatLine(const std::string& label, int base, int scroll);

  const EquipInstance* item_ = nullptr;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_INSPECT_PANEL_H_
