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
 * and what each of its tiers pays. Either card scrolls when it outgrows the
 * terminal; Tab hands the arrows to the other one. Only one section of each
 * card moves -- the stats on the item, the tiers on the set -- and what names
 * either card stays where it is. See ScrollCard.
 *
 * One panel rather than two because it is one screen to the player, reached
 * the same way from either list, and two would be free to drift apart.
 * SetItem(nullptr) of either kind renders a placeholder.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_INSPECT_PANEL_H_

#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "src/character/character.h"
#include "src/frontend/widgets/scroll_card.h"
#include "src/item/item.h"
#include "src/protos/equip.pb.h"
#include "src/protos/equip_set.pb.h"
#include "src/protos/item.pb.h"

namespace ms {

class InspectPanel {
 public:
  // Which of the two cards the arrows are reaching.
  enum Card { kItemCard, kSetCard };

  // Points the panel at an item to describe. The two overloads are exclusive:
  // setting one forgets the other, so the panel always describes exactly the
  // item the cursor was last on.
  void SetItem(const EquipTabItem* item);
  void SetItem(const ItemPrototype* item);
  // Teaches the panel which sets exist and how many pieces of one are worn.
  // Left unset, an item is described on its own and no set card appears --
  // which is what a test with no sets in play wants.
  void UseCharacter(const CharacterInstance& character);
  // The rows either card may take, borders included. Past this it scrolls.
  // Zero, the default, is no limit -- what a test wants, and what a card with
  // room to spare gets. Not read from the terminal here, for the reason
  // CharacterPanel gives: tests draw the panel at whatever size they choose.
  void SetMaxRows(int rows);
  // Moves the focused card's view. There is no cursor on either card, so a
  // key moves the page itself; see ScrollCard.
  void ScrollBy(int delta);
  // Hands the arrows to the other card. Refused, and false, when there is no
  // second card or when it has nothing to scroll: focus should never land
  // where the arrows would do nothing.
  bool SwapCard();
  // Both cards back to the top, with the item card holding the arrows. Call
  // when the screen opens.
  void Reset();
  // True while the inspected item belongs to a set, which is what a screen
  // asks before offering Tab at all.
  bool HasSetCard() const;
  Card focused_card() const {
    return focus_;
  }
  // True while the item card has more rows than it can draw. For a screen
  // that shares the arrows between this card and a panel of its own.
  bool ItemOverflows() const;

  ftxui::Element Render() const;
  // The item's card alone, with no set card beside it. What a screen that
  // already has a panel of its own next to the item asks for: three windows
  // in a row leaves none of them the width they need. `focused` lights the
  // title, for a screen where the card takes turns holding the arrows.
  ftxui::Element RenderItemOnly(bool focused = false) const;

 private:
  // The body for each kind. All three are framed by the same card, so the
  // screens cannot drift apart in their framing.
  CardRows EquipRows() const;
  ftxui::Element RenderStackable() const;
  // An Arcane Symbol's card. Its own body rather than the equip one: what a
  // symbol grants comes from its level and the wearer's job, so none of the
  // rows an equip carries has anything to say about it.
  CardRows SymbolRows() const;
  // The equip body in parts, head to foot. The rows an item cannot fold are
  // built first and measured; the two that can -- the star bar and the job
  // categories -- are then folded onto two lines each if leaving them on one
  // is what would make the panel wide. `fixed` is the width the rest of the
  // card already needs.
  std::vector<CardRow> HeadRows() const;
  std::vector<CardRow> JobRows(int fixed) const;
  std::vector<CardRow> StarRows(int fixed) const;
  std::vector<CardRow> StatRows() const;
  std::vector<CardRow> SlotRows() const;
  // The set the inspected item is a piece of, or nullptr for an item that
  // belongs to none -- which is every item but a handful.
  const EquipSet* SetOfItem() const;
  // The card beside the item: what the set is made of, and what each tier
  // pays. Tiers the worn pieces do not reach are dimmed.
  CardRows SetRows(const EquipSet& set) const;

  // `count` job categories from `from`, as one row: dimmed for the ones this
  // item is not for, and every one of them listed either way.
  static ftxui::Element JobRow(const EquipPrototype& proto, int from,
                               int count);
  // Returns "Stage N (name)" or empty string if unspecified.
  static std::string FormatAttackSpeed(AttackSpeed speed);
  // Returns a colored hbox with the stat line, or nullptr if all are zero.
  // Total and base are default color; scroll is periwinkle; SF is gold.
  static ftxui::Element StatLine(const std::string& label, int base, int scroll,
                                 int sf = 0);
  // `count` stars from `from`, in groups of 5: filled (★) up to `stars`, empty
  // (☆) after it. Filled stars are gold; empty stars are dark gray.
  static ftxui::Element StarBar(int stars, int from, int count);

  const EquipTabItem* item_ = nullptr;
  const ItemPrototype* stackable_ = nullptr;
  const CharacterInstance* character_ = nullptr;
  ScrollCard item_card_;
  ScrollCard set_card_;
  Card focus_ = kItemCard;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_INSPECT_PANEL_H_
