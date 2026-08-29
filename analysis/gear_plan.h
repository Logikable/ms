/* Spending a purse on the gear a character stands in, the way a player does.
 *
 * The other half of sim_gear: that one puts the best gear on a character and
 * asks what it is worth, this one asks what it costs and whether the climb
 * ever pays for it. A sim measuring acquisition needs both -- what a ceiling
 * is worth says nothing about when a player reaches it.
 *
 * What to buy next is decided rather than listed: every slot that could be
 * scrolled and every item that could take another star is priced against what
 * it would add, and the best of them is bought. So the shopper stops where the
 * price stops being worth paying rather than where a number told it to, and
 * the answer moves on its own when a price or a stat table does.
 *
 * Traces are charged as meso rather than kept in the bag. The shop is their
 * only source and nothing else spends them, so a stack of them is a way of
 * holding meso rather than a thing of its own.
 */
#ifndef MS_ANALYSIS_GEAR_PLAN_H_
#define MS_ANALYSIS_GEAR_PLAN_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

// How far the player means to take what they are wearing.
struct GearPlan {
  // A star the shopper will not go past whatever the arithmetic says. Here so
  // a run can be pinned against another, not because the shopper needs it: the
  // price of a star already climbs faster than what it buys, and the shopper
  // walks away long before this bites.
  int star_ceiling = kMaxStarForce;
  // The success rate of the scrolls bought, as a whole percent. A lower rate
  // pays more per slot it lands and wastes the rest, which on a drop-only
  // piece is a slot nothing gets back.
  int scroll_rate = 100;
};

// What one pass of spending came to.
struct GearSpend {
  int64_t meso = 0;
  int slots_filled = 0;
  int stars_gained = 0;
};

// Spends a purse on gear, remembering which scroll each item wants: the
// measurement behind that choice costs a swing per candidate, and the answer
// only moves when the item does.
class GearShopper {
 public:
  explicit GearShopper(const GearPlan& plan) : plan_(plan) {
  }

  // Spends what the purse can spare on what the character is wearing, buying
  // the best value on offer over and over until nothing left is affordable or
  // worth having.
  GearSpend Spend(GameState& state);

 private:
  // One thing the purse could buy next.
  struct Candidate {
    EquipSlot slot = EQUIP_SLOT_UNSPECIFIED;
    // A star when set, one of the item's upgrade slots when not.
    bool star = false;
    // The scroll an upgrade slot would be filled with; null for a star.
    const Scroll* scroll = nullptr;
    // Meso this is expected to take, the attempts that land nothing included.
    int64_t cost = 0;
    // Combat power it would add. Both halves of the ratio, so the pick is a
    // comparison rather than a rule.
    int gain = 0;
  };

  // The scroll `slot`'s item wants, measured once per item and kept.
  const Scroll* ScrollFor(GameState& state, EquipSlot slot);
  // Everything the character could buy for what they are wearing, priced and
  // valued. Affordability is left to the caller: what the purse cannot cover
  // today it may cover next level, and the list says what is on offer.
  std::vector<Candidate> Offers(GameState& state);
  // Buys the affordable candidate with the most combat power per meso, and
  // says whether it bought anything.
  bool BuyBest(GameState& state, GearSpend& spend);

  GearPlan plan_;
  // By the prototype's name, since that is what an item and its replacement
  // disagree on. A slot whose item takes no scroll maps to null.
  std::map<std::string, const Scroll*> chosen_;
};

}  // namespace ms

#endif  // MS_ANALYSIS_GEAR_PLAN_H_
