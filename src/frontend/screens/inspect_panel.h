/* InspectPanel is the one screen behind every Inspect entry in the game, for
 * whatever kind of item the cursor was on.
 *
 * An equip (EquipInstance or EquipTrace) gets the full view: star bar, name,
 * level, job categories, per-stat breakdown and remaining upgrade slots. The
 * bar and the slot count are drawn only for an item that can take that upgrade,
 * since an empty row of either would promise something the item can't do. A
 * stackable Use or Etc item has none of that, just a sentence saying what it
 * is, so that is what it gets.
 *
 * A set piece gets a second card beside the first, listing the whole set and
 * what each tier grants. An item being compared with what the player is wearing
 * gets a third card, to its left: the same card the item gets, titled Equipped
 * and with no set card of its own. Any card scrolls when it outgrows the
 * terminal, and Tab passes the arrows to the next one. Only one section of each
 * card moves (the stats on the item, the tiers on the set), and the card's name
 * stays put. See ScrollCard.
 *
 * A ring or a pendant fits several slots, and the Equipped card then has a tab
 * bar at the top: one chip per slot of the family, which the arrows move
 * between, so the player can compare the item with each of the four rings they
 * wear rather than only the one Equip would replace.
 *
 * Three cards don't always fit. The set card gives way: it is squeezed into the
 * space left and dropped entirely once that is too little to read. The two
 * items being compared are the point of the screen, so neither is ever cut.
 *
 * One panel rather than two because it is one screen to the player, reached the
 * same way from either list, and two would drift apart. SetItem(nullptr) of
 * either kind shows a placeholder.
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
  // Which card has the arrows. The values aren't the drawing order: the
  // equipped card is to the left of the item.
  enum Card { kItemCard, kSetCard, kEquippedCard };

  // The slots the Equipped card offers (see SetComparison).
  struct ComparisonSlots {
    std::vector<const EquipTabItem*> worn;
    int active = 0;
  };

  // Sets the item to describe. The two overloads are exclusive: setting one
  // clears the other, so the panel always describes the item the cursor was
  // last on. Either one also clears the comparison, so a screen that wants one
  // sets it every time.
  void SetItem(const EquipTabItem* item);
  void SetItem(const ItemPrototype* item);
  // The item to compare the inspected one with, drawn to its left. nullptr, the
  // default, means no comparison: an empty slot, or an item that is itself the
  // one worn.
  void SetComparison(const EquipTabItem* equipped);
  // The same, for an item with a family of slots to choose from: a ring fits
  // any of four, a pendant either of two, and the card has a tab bar to move
  // between them. `worn` holds the player's item in each slot of the family in
  // slot order, with nullptr for an empty slot, and `active` is the one shown.
  // The card is drawn even when every slot is empty, since the bar shows which
  // slots are being offered.
  void SetComparison(ComparisonSlots slots);
  // What equipping the inspected item would do to the player's combat power,
  // shown above its required level. Empty, the default, draws neither row,
  // which suits a screen showing an item the player isn't comparing, and is
  // what the Equipped card beside it always gets.
  void SetCombatPowerDelta(std::optional<int> delta);
  // Tells the panel which sets exist and how many pieces of each are worn. If
  // unset, an item is described alone and no set card appears, which suits
  // tests with no sets.
  void UseCharacter(const CharacterInstance& character);
  // The most rows either card may take, borders included. Beyond this it
  // scrolls. Zero, the default, means no limit, which suits tests and cards
  // with plenty of room. It isn't read from the terminal here, for the reason
  // CharacterPanel gives: tests draw the panel at whatever size they choose.
  void SetMaxRows(int rows);
  // The most columns all the cards together may take. Zero, the default, means
  // no limit, which suits tests and leaves every card at full width.
  void SetMaxColumns(int columns);
  // Scrolls the focused card. None of the cards has a cursor, so a key moves
  // the page itself (see ScrollCard).
  void ScrollBy(int delta);
  // Scrolls sideways, for the set card when it has been squeezed. Does nothing
  // on a card drawn at its own width.
  void ScrollXBy(int delta);
  // Passes the arrows `step` cards around the ring, in drawing order. Returns
  // false and does nothing when only one card is on screen.
  bool SwapCard(int step);
  // Every card back to the top, with the item card holding the arrows. Not the
  // equipped card, even though it is drawn first, because the player came to
  // read the item they asked about. Call when the screen opens.
  void Reset();
  // True while the inspected item belongs to a set. Whether the set card is
  // drawn is a separate question, since the width may have removed it, and the
  // last render answers that.
  bool HasSetCard() const;
  Card focused_card() const {
    return focus_;
  }
  ftxui::Element Render() const;
  // The item's card alone, for a screen that already has a panel beside it,
  // since three windows in a row would leave none of them enough width. `title`
  // names the window; a screen showing the same item twice says which is which,
  // as Star Force titles its cards Before and After.
  ftxui::Element RenderItemOnly(bool focused = false,
                                const std::string& title = " Inspect ") const;

 private:
  // The card beside the item, with its tab bar when the item has a family of
  // slots. An active slot with nothing in it shows a bar over "(empty)": that
  // slot costs the player nothing, and the delta beside it says what the item
  // would add.
  ftxui::Element RenderComparison(bool focused) const;
  // The slot the bar is on, or nullptr for an empty one.
  const EquipTabItem* Compared() const;
  // Whether the Equipped card is drawn at all. It isn't for a single empty
  // slot, since there is nothing to compare with or say about it.
  bool HasComparisonCard() const;
  // One card for `item`, whatever kind it is: the same framing for the
  // inspected item and the one it is compared with.
  ftxui::Element RenderCard(const ScrollCard& card, const EquipTabItem* item,
                            const std::string& title, bool focused) const;
  // The cards on screen, in drawing order, which the ring moves through.
  std::vector<Card> DrawnCards() const;
  // The card with the arrows, so a key doesn't need to name all three.
  const ScrollCard& FocusedCard() const;
  ScrollCard& FocusedCard();
  // The set card, squeezed into `room` columns, or left out of the result when
  // that is too little for its rows to read.
  std::vector<ftxui::Element> SetCardIn(const EquipSet& set, int room) const;
  // The body for each kind. All three share the same card framing, so the
  // screens can't drift apart.
  CardRows EquipRows(const EquipTabItem& item) const;
  ftxui::Element RenderStackable() const;
  // An Arcane Symbol's card. It has its own body rather than the equip one:
  // what a symbol grants comes from its level and the wearer's job, so none of
  // an equip's rows apply.
  CardRows SymbolRows(const EquipTabItem& item) const;
  // The symbol's four values: its progress, and what its level grants.
  std::vector<CardRow> SymbolStatRows(const EquipTabItem& item,
                                      int level) const;
  // The equip body in parts, top to bottom. The rows that can't be split are
  // built and measured first. Then the star bar and job categories are each
  // split over two lines if one line would widen the panel.
  std::vector<CardRow> HeadRows(const EquipTabItem& item) const;
  // The combat power number and its label, right-aligned. Empty for any card
  // but the inspected item's, and for an item with no delta set.
  std::vector<CardRow> DeltaRows(const EquipTabItem& item) const;
  std::vector<CardRow> JobRows(const EquipTabItem& item, int fixed) const;
  std::vector<CardRow> StarRows(const EquipTabItem& item, int fixed) const;
  std::vector<CardRow> StatRows(const EquipTabItem& item) const;
  std::vector<CardRow> SlotRows(const EquipTabItem& item) const;
  // The potential lines, below the upgrade history. Empty for an item with no
  // potential, which is every item until it is cubed.
  std::vector<CardRow> PotentialRows(const EquipTabItem& item) const;
  // The set the inspected item belongs to, or nullptr. Most items belong to
  // none.
  const EquipSet* SetOfItem() const;
  // The card beside the item: the set's pieces, and what each tier grants.
  // Tiers the worn pieces don't reach are dimmed.
  CardRows SetRows(const EquipSet& set) const;
  // One member's rows: the slot, named once, and every piece that can fill it.
  // Each piece is lit separately, so a slot holding two of its alternates shows
  // both as worn.
  std::vector<CardRow> MemberRows(const EquipSetMember& member) const;

  // `count` job categories starting at `from`, as one row. The ones this item
  // isn't for are dimmed, and all of them are listed either way.
  static ftxui::Element JobRow(const EquipPrototype& proto, int from,
                               int count);
  // Returns "Stage N (name)", or an empty string if unspecified.
  static std::string FormatAttackSpeed(AttackSpeed speed);
  // Returns a coloured hbox with the stat line, or nullptr if all values are
  // zero. Total and base are in the default colour, scrolls in purple, and star
  // force in gold.
  static ftxui::Element StatLine(const std::string& label, int base, int scroll,
                                 int sf = 0);
  // `count` stars starting at `from`, in groups of 5: filled (★) up to `stars`,
  // empty (☆) after. Filled stars are gold and empty stars dark grey.
  static ftxui::Element StarBar(int stars, int from, int count);
  // A symbol's twenty levels in groups of 5: filled (◆) up to `level`, empty
  // (◇) after. Filled pips are purple and empty ones dark grey.
  static ftxui::Element SymbolBar(int level);

  const EquipTabItem* item_ = nullptr;
  ComparisonSlots compare_;
  std::optional<int> delta_;
  const ItemPrototype* stackable_ = nullptr;
  const CharacterInstance* character_ = nullptr;
  int max_columns_ = 0;
  ScrollCard item_card_;
  ScrollCard set_card_;
  ScrollCard compare_card_;
  // Mutable, like the flag below: narrowing the terminal while the screen is
  // open can remove the set card while it has the arrows, and Render is where
  // that is noticed. ScrollCard clamps its own offset for the same reason.
  mutable Card focus_ = kItemCard;
  // Whether the last render had room for the set card. The ring moves through
  // what is on screen, and the width is only known once the cards are built.
  mutable bool set_drawn_ = false;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_SCREENS_INSPECT_PANEL_H_
