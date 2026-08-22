/* Spending a purse on the gear a character stands in, the way a player does.
 *
 * The other half of sim_gear: that one puts the best gear on a character and
 * asks what it is worth, this one asks what it costs and whether the climb
 * ever pays for it. A sim measuring acquisition needs both -- what a ceiling
 * is worth says nothing about when a player reaches it.
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

#include "src/game_state.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

// How far the player means to take what they are wearing.
struct GearPlan {
  // Stars aimed for, held down per item to its own maximum. 15 is the last
  // star an attempt cannot destroy the item at, which is where a player
  // wearing pieces one boss drops stops.
  int star_target = 15;
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

  // Spends what the purse can spare on what the character is wearing, in the
  // order a player does it: the weapon's slots and then its stars, because the
  // weapon opens the maps and the bosses that pay for everything else, then
  // every other slot scrolled, then every other slot starred.
  //
  // Stops at the first thing the purse cannot cover rather than dropping to
  // something cheaper -- a player saving for the weapon does not spend the
  // float on a hat.
  GearSpend Spend(GameState& state);

 private:
  // The scroll `slot`'s item wants, measured once per item and kept.
  const Scroll* ScrollFor(GameState& state, EquipSlot slot);
  // Fills what is left of `slot`'s upgrade slots. False once the purse can no
  // longer pay for the next one.
  bool FillSlots(GameState& state, EquipSlot slot, GearSpend& spend);
  // Stars `slot` toward the plan's target. False on the same terms.
  bool AddStars(GameState& state, EquipSlot slot, GearSpend& spend);

  GearPlan plan_;
  // By the prototype's name, since that is what an item and its replacement
  // disagree on. A slot whose item takes no scroll maps to null.
  std::map<std::string, const Scroll*> chosen_;
};

}  // namespace ms

#endif  // MS_ANALYSIS_GEAR_PLAN_H_
