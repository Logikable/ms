/* BoxPanel is the screen Open leads to from a box on the Etc tab: a Name /
 * Type / Level list of what the box holds for this character's class, an
 * Inspect / Select / Close menu on a row, and the question that trades one box
 * for the row picked. A ring box lists each ring once with the levels it can
 * roll, then the Etc items it offers instead.
 *
 * The panel only displays. It moves its own cursor and holds the menu and the
 * confirmation; the controller opens the box when the player confirms.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_BOX_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_BOX_PANEL_H_

#include <map>
#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/types.h"
#include "src/frontend/widgets/confirm_prompt.h"
#include "src/frontend/widgets/item_menu.h"
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

// The entries of a row's menu, in display order.
enum BoxMenuItem : int {
  kBoxMenuInspect = 0,
  kBoxMenuSelect = 1,
  kBoxMenuClose = 2,
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
  // What Inspect shows for selected(): the piece, or for a ring the lowest
  // level the box rolls, which the row's columns already read. nullptr on an
  // Etc row, which has selected_item() instead.
  const EquipPrototype* selected_equip() const;
  const ItemPrototype* selected_item() const;
  const ItemPrototype& box() const {
    return box_;
  }
  // Draws the menu over the list while it is open.
  ftxui::Element Render() const;

  // Opens the menu on selected(), on Inspect. Does nothing on an empty list.
  void OpenMenu();
  bool menu_open() const {
    return menu_open_;
  }
  // Returns the next screen: kBoxMenu while the menu stays open, kBoxInspect or
  // kBoxConfirm for the chosen entry, and kBoxOpen once it closes.
  Screen OnMenuEvent(const ftxui::Event& event);

  // "Pick <piece>?" for selected(), starting on [Confirm]: the list was the
  // choice, and this only checks it.
  void OpenConfirm();
  ConfirmChoice OnConfirmEvent(ftxui::Event event);
  ftxui::Element RenderConfirm() const;

 private:
  void ListRingBox();
  const EquipPrototype* ChoiceEquip(const BoxChoice& choice) const;
  ftxui::Element RenderRow(const BoxChoice& choice, bool on_cursor) const;

  const CharacterInstance& character_;
  const std::map<std::string, EquipPrototype>& equips_;
  const std::map<std::string, ItemPrototype>& items_;
  ItemPrototype box_;
  std::vector<BoxChoice> stock_;
  int name_width_ = 0;
  int selected_ = 0;
  ConfirmPrompt confirm_;
  ItemMenu menu_;
  bool menu_open_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_BOX_PANEL_H_
