/* BoxPanel is the screen Open leads to from a box on the Etc tab: a Name /
 * Type / Level list of what the box holds for this character's class, and the
 * question that trades one box for the row picked. A ring box lists each ring
 * once with the levels it can roll, then the Etc items it offers instead.
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

// One row of the list. Exactly one of the last three is set.
struct BoxChoice {
  std::string name;
  // A piece off a token box's shelf.
  const EquipPrototype* equip = nullptr;
  // A ring box's ring, by the skill it grants; its level is rolled on opening.
  std::string ring_skill;
  // An Etc item a ring box offers instead, by data file stem.
  std::string item_key;
};

class BoxPanel {
 public:
  BoxPanel(const CharacterInstance& character,
           const std::map<std::string, EquipPrototype>& equips,
           const std::map<std::string, ItemPrototype>& items);
  // The catalogs are held by reference, so temporary ones would dangle.
  BoxPanel(const CharacterInstance& character,
           std::map<std::string, EquipPrototype>&& equips,
           const std::map<std::string, ItemPrototype>& items) = delete;
  BoxPanel(const CharacterInstance& character,
           const std::map<std::string, EquipPrototype>& equips,
           std::map<std::string, ItemPrototype>&& items) = delete;

  // Lists what `box` holds for this character's class and puts the cursor on
  // the first row. Call when the screen opens.
  void Reset(const ItemPrototype& box);
  // Moves the cursor on Up and Down. Returns true if the event was consumed.
  bool OnEvent(const ftxui::Event& event);
  // The row under the cursor, or nullptr when the box holds nothing for this
  // class.
  const BoxChoice* selected() const;
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
  void ListRingBox();
  ftxui::Element RenderRow(const BoxChoice& choice, bool on_cursor) const;

  const CharacterInstance& character_;
  const std::map<std::string, EquipPrototype>& equips_;
  const std::map<std::string, ItemPrototype>& items_;
  ItemPrototype box_;
  std::vector<BoxChoice> stock_;
  int selected_ = 0;
  ConfirmPrompt confirm_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOX_PANEL_H_
