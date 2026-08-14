/* InspectPanel is the one screen behind every Inspect entry in the game, for
 * whichever kind of item the cursor was on.
 *
 * An equip (EquipInstance or EquipTrace) gets the full account: star bar,
 * name, level, job categories, per-stat breakdown, remaining upgrade slots.
 * The bar and the slot count are drawn only for an item that accepts that
 * upgrade -- an empty row of either would promise something the item cannot do.
 * A stackable Use or Etc item has none of that -- what it has is a sentence
 * saying what it is, so that is what it gets.
 *
 * A piece of a set gets a second card beside the first, listing the whole set
 * and what each of its tiers pays.
 *
 * One panel rather than two because it is one screen to the player, reached
 * the same way from either list, and two would be free to drift apart.
 * SetItem(nullptr) of either kind renders a placeholder.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_INSPECT_PANEL_H_

#include <string>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

class InspectPanel {
 public:
  // Points the panel at an item to describe. The two overloads are exclusive:
  // setting one forgets the other, so the panel always describes exactly the
  // item the cursor was last on.
  void SetItem(const EquipTabItem* item);
  void SetItem(const ItemPrototype* item);
  // Teaches the panel which sets exist and how many pieces of one are worn.
  // Left unset, an item is described on its own and no set card appears --
  // which is what a test with no sets in play wants.
  void UseCharacter(const CharacterInstance& character);
  ftxui::Element Render() const;

 private:
  // The body for each kind. Both are wrapped in the same window by Render, so
  // the two screens cannot drift apart in their framing.
  ftxui::Element RenderEquip() const;
  ftxui::Element RenderStackable() const;
  // The set the inspected item is a piece of, or nullptr for an item that
  // belongs to none -- which is every item but a handful.
  const EquipSet* SetOfItem() const;
  // The card beside the item: what the set is made of, and what each tier
  // pays. Tiers the worn pieces do not reach are dimmed.
  ftxui::Element RenderSetEffect(const EquipSet& set) const;

  static ftxui::Element FormatJobCategories(const EquipPrototype& proto);
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
  const ItemPrototype* stackable_ = nullptr;
  const CharacterInstance* character_ = nullptr;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_INSPECT_PANEL_H_
