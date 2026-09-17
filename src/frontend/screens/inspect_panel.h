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
 * and what each of its tiers pays. An item being weighed against what the
 * player is already wearing gets a third, to the LEFT of it: the same card the
 * item gets, titled Equipped and with no set card of its own. Any card scrolls
 * when it outgrows the terminal; Tab hands the arrows to the next one. Only
 * one section of each card moves -- the stats on the item, the tiers on the
 * set -- and what names either card stays where it is. See ScrollCard.
 *
 * Three cards do not always fit. The set card is the one that gives: squeezed
 * into what is left, and dropped entirely once that is too little for its
 * rows to read. The two items being compared are the point of the screen, so
 * neither of them is ever the one cut.
 *
 * One panel rather than two because it is one screen to the player, reached
 * the same way from either list, and two would be free to drift apart.
 * SetItem(nullptr) of either kind renders a placeholder.
 */
#ifndef MS_SRC_FRONTEND_SCREENS_INSPECT_PANEL_H_
#define MS_SRC_FRONTEND_SCREENS_INSPECT_PANEL_H_

#include <optional>
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
  // Which of the cards the arrows are reaching. The values are not the order
  // they are drawn in: the equipped card sits to the LEFT of the item.
  enum Card { kItemCard, kSetCard, kEquippedCard };

  // Points the panel at an item to describe. The two overloads are exclusive:
  // setting one forgets the other, so the panel always describes exactly the
  // item the cursor was last on. Either forgets what it was being compared
  // with, so a screen that wants a comparison asks for one every time.
  void SetItem(const EquipTabItem* item);
  void SetItem(const ItemPrototype* item);
  // The item to weigh the inspected one against, drawn to its left. nullptr,
  // the default, is no comparison at all -- an empty slot, or an item that is
  // itself the one worn.
  void SetComparison(const EquipTabItem* equipped);
  // What putting the inspected item on would do to the player's combat power,
  // written over its required level. Empty, the default, draws neither row --
  // which is what a screen showing an item the player is not weighing wants,
  // and what the Equipped card beside it always gets.
  void SetCombatPowerDelta(std::optional<int> delta);
  // Teaches the panel which sets exist and how many pieces of one are worn.
  // Left unset, an item is described on its own and no set card appears --
  // which is what a test with no sets in play wants.
  void UseCharacter(const CharacterInstance& character);
  // The rows either card may take, borders included. Past this it scrolls.
  // Zero, the default, is no limit -- what a test wants, and what a card with
  // room to spare gets. Not read from the terminal here, for the reason
  // CharacterPanel gives: tests draw the panel at whatever size they choose.
  void SetMaxRows(int rows);
  // The columns all the cards together may take. Zero, the default, is no
  // limit -- what a test wants, and what leaves every card its full width.
  void SetMaxColumns(int columns);
  // Moves the focused card's view. There is no cursor on any of them, so a
  // key moves the page itself; see ScrollCard.
  void ScrollBy(int delta);
  // Moves it sideways, for the set card when it has been squeezed. Nothing on
  // a card drawn at its own width.
  void ScrollXBy(int delta);
  // Hands the arrows `step` cards along the ring, in the order they are drawn.
  // Refused, and false, when this is the only card on screen.
  bool SwapCard(int step);
  // Every card back to the top, with the item card holding the arrows -- not
  // the equipped card, though it is drawn first: the item the player asked
  // about is the one they came to read. Call when the screen opens.
  void Reset();
  // True while the inspected item belongs to a set. Whether the set card is
  // DRAWN is a second question -- the width may have taken it -- and that one
  // is answered by the last render.
  bool HasSetCard() const;
  Card focused_card() const {
    return focus_;
  }
  ftxui::Element Render() const;
  // The item's card alone, for a screen that already has a panel beside it:
  // three windows in a row leaves none of them the width they need. `title`
  // names the window -- a screen showing the same item twice says which is
  // which, as Star Force titles its cards Before and After.
  ftxui::Element RenderItemOnly(bool focused = false,
                                const std::string& title = " Inspect ") const;

 private:
  // One card, whichever kind of thing `item` is: the same framing for the
  // inspected item and for the one it is weighed against.
  ftxui::Element RenderCard(const ScrollCard& card, const EquipTabItem* item,
                            const std::string& title, bool focused) const;
  // The cards on screen, in the order they are drawn. What the ring walks.
  std::vector<Card> DrawnCards() const;
  // The card the arrows are on, so a key does not name the three of them.
  const ScrollCard& FocusedCard() const;
  ScrollCard& FocusedCard();
  // The set card, squeezed into `room` columns or left out of the returned
  // vector when that is too little for its rows to read.
  std::vector<ftxui::Element> SetCardIn(const EquipSet& set, int room) const;
  // The body for each kind. All three are framed by the same card, so the
  // screens cannot drift apart in their framing.
  CardRows EquipRows(const EquipTabItem& item) const;
  ftxui::Element RenderStackable() const;
  // An Arcane Symbol's card. Its own body rather than the equip one: what a
  // symbol grants comes from its level and the wearer's job, so none of the
  // rows an equip carries has anything to say about it.
  CardRows SymbolRows(const EquipTabItem& item) const;
  // The symbol's four figures: where it stands, and what that level pays.
  std::vector<CardRow> SymbolStatRows(const EquipTabItem& item,
                                      int level) const;
  // The equip body in parts, head to foot. The rows that cannot FOLD are built
  // and measured first; the star bar and the job categories are then folded
  // onto two lines each if leaving them on one would widen the panel.
  std::vector<CardRow> HeadRows(const EquipTabItem& item) const;
  // The combat power figure and its label, right-aligned. Empty for any card
  // but the inspected item's, and for an item with no delta set.
  std::vector<CardRow> DeltaRows(const EquipTabItem& item) const;
  std::vector<CardRow> JobRows(const EquipTabItem& item, int fixed) const;
  std::vector<CardRow> StarRows(const EquipTabItem& item, int fixed) const;
  std::vector<CardRow> StatRows(const EquipTabItem& item) const;
  std::vector<CardRow> SlotRows(const EquipTabItem& item) const;
  // The rolled lines, under what the item has spent. Empty for an item
  // carrying no potential, which is every item until a cube goes in.
  std::vector<CardRow> PotentialRows(const EquipTabItem& item) const;
  // The set the inspected item is a piece of, or nullptr for an item that
  // belongs to none -- which is every item but a handful.
  const EquipSet* SetOfItem() const;
  // The card beside the item: what the set is made of, and what each tier
  // pays. Tiers the worn pieces do not reach are dimmed.
  CardRows SetRows(const EquipSet& set) const;
  // One member's rows: the slot, named once, and every piece that fills it.
  // Each piece is lit on its own, so a slot holding two of its alternates
  // shows both as worn.
  std::vector<CardRow> MemberRows(const EquipSetMember& member) const;

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
  // A symbol's twenty levels, in groups of 5: filled (◆) up to `level`, empty
  // (◇) after it. Filled pips are periwinkle; empty ones are dark gray.
  static ftxui::Element SymbolBar(int level);

  const EquipTabItem* item_ = nullptr;
  const EquipTabItem* compare_ = nullptr;
  std::optional<int> delta_;
  const ItemPrototype* stackable_ = nullptr;
  const CharacterInstance* character_ = nullptr;
  int max_columns_ = 0;
  ScrollCard item_card_;
  ScrollCard set_card_;
  ScrollCard compare_card_;
  // Held with the flag below: a terminal narrowed under an open screen can
  // take the set card out from under the arrows, and Render is where that is
  // found out. ScrollCard clamps its own offset for the same reason.
  mutable Card focus_ = kItemCard;
  // Whether the last render had room for the set card. The ring walks what is
  // on screen, and the width is only known once the cards are built.
  mutable bool set_drawn_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_INSPECT_PANEL_H_
