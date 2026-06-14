/* StarForcePanel renders the star force attempt screen for a single item.
 * Shows the item name, current star count, next-attempt probabilities, and
 * a prompt to attempt. SetItem(nullptr) renders a placeholder. The panel is
 * display-only; events are handled by TuiController.
 */
#ifndef MS_SRC_FRONTEND_STAR_FORCE_PANEL_H_
#define MS_SRC_FRONTEND_STAR_FORCE_PANEL_H_

#include "ftxui/dom/elements.hpp"
#include "src/equip_instance.h"
#include "src/frontend/types.h"

namespace ms {

class StarForcePanel {
 public:
  void SetItem(const EquipInstance* item);
  ftxui::Element Render() const;
  ftxui::Element RenderResult(const StarForceResult& r) const;

 private:
  const EquipInstance* item_ = nullptr;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_STAR_FORCE_PANEL_H_
