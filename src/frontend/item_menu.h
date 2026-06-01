/* ItemMenu is a positioned context menu overlay. It renders as a bordered
 * list of options anchored at a given (row, col) via dbox layering. The
 * caller is responsible for event dispatch — call Up(), Down(), and
 * selected() to drive navigation; Reset() when the menu is reopened.
 */
#ifndef MS_SRC_FRONTEND_ITEM_MENU_H_
#define MS_SRC_FRONTEND_ITEM_MENU_H_

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace ms {

class ItemMenu {
 public:
  explicit ItemMenu(std::vector<std::string> options);
  // Returns a positioned element for dbox layering. The element includes
  // top/left padding so the menu box appears at (row, col).
  ftxui::Element Render(int row, int col) const;
  void Up();
  void Down();
  void Reset();
  int selected() const;

 private:
  std::vector<std::string> options_;
  int selected_ = 0;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_ITEM_MENU_H_
