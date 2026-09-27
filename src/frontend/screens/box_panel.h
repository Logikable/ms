/* BoxPanel is the screen Open leads to from a box on the Etc tab: a Name /
 * Type / Level list of what the box holds for this character's class, and the
 * question that trades one box for the row picked.
 *
 * The panel only displays. It moves its own cursor and holds the confirmation;
 * the controller opens the box when the player confirms.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BOX_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BOX_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

class BoxPanel {
 public:
  BoxPanel(const CharacterInstance& character,
           const std::map<std::string, EquipPrototype>& equips);
  // The catalog is held by reference, so a temporary one would dangle.
  BoxPanel(const CharacterInstance& character,
           std::map<std::string, EquipPrototype>&& equips) = delete;

  // Lists what `box` holds for this character's class and puts the cursor on
  // the first row. Call when the screen opens.
  void Reset(const ItemPrototype& box);
  // Moves the cursor on Up and Down. Returns true if the event was consumed.
  bool OnEvent(const ftxui::Event& event);
  // The piece under the cursor, or nullptr when the box holds nothing for this
  // class.
  const EquipPrototype* selected() const;
  const ItemPrototype& box() const {
    return box_;
  }
  ftxui::Element Render() const;

  // "Pick <piece>?" for selected(), starting on [Confirm]: the list was the
  // choice, and this only checks it.
  void OpenConfirm();
  ConfirmChoice OnConfirmEvent(ftxui::Event event);
  ftxui::Element RenderConfirm() const;

 private:
  const CharacterInstance& character_;
  const std::map<std::string, EquipPrototype>& equips_;
  ItemPrototype box_;
  std::vector<std::string> stock_;
  int selected_ = 0;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOX_PANEL_H_
