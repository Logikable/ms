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
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "analysis/cube_plan.h"
#include "src/character/character_stats.h"
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
  // Whether the shopper cubes at all. Off is the counterfactual: what the
  // same climb comes to with every cube's meso left for the stars.
  bool cubes = true;
  // The success rate of the scrolls bought, as a whole percent. A lower rate
  // pays more per slot it lands and wastes the rest, which on a drop-only
  // piece is a slot nothing gets back.
  int scroll_rate = 100;
};

// What one pass of spending came to, meso split by what it went on -- "the
// shop took 4B" says nothing a tuning pass can act on.
struct GearSpend {
  int64_t scrolls = 0;       // spell traces, at the shop's price for them
  int64_t stars = 0;         // every attempt, the failures included
  int64_t hammers = 0;       // golden hammers, for the slots they open
  int64_t cubes = 0;         // every cube, whatever it rolled
  int64_t replacements = 0;  // copies bought to put a destroyed piece back
  int slots_filled = 0;
  int stars_gained = 0;
  int hammers_driven = 0;
  // Cubes bought, and the ones whose roll the character kept. The two apart
  // because a cube is a chance rather than a purchase: the gap is the meso
  // that bought nothing, which is the trap a keep-better rule invites.
  int cubes_bought = 0;
  int cubes_kept = 0;
  // Pieces destroyed and put back, and what the bag was cleared of to pay for
  // the room and the meso.
  int booms = 0;
  int64_t sold = 0;

  int64_t meso() const {
    return scrolls + stars + hammers + replacements + cubes;
  }

  void Add(const GearSpend& other) {
    scrolls += other.scrolls;
    stars += other.stars;
    hammers += other.hammers;
    cubes += other.cubes;
    replacements += other.replacements;
    slots_filled += other.slots_filled;
    stars_gained += other.stars_gained;
    hammers_driven += other.hammers_driven;
    cubes_bought += other.cubes_bought;
    cubes_kept += other.cubes_kept;
    booms += other.booms;
    sold += other.sold;
  }
};

// Spends a purse on gear, remembering which scroll each item wants: the
// measurement behind that choice costs a swing per candidate, and the answer
// only moves when the item does.
class GearShopper {
 public:
  explicit GearShopper(const GearPlan& plan) : plan_(plan) {
  }

  // What the run has left to earn and what it earns, which is the whole of
  // what a %meso or %drop potential line is worth -- see CubeIncome. Set at
  // each look, beside the pot decisions. A shopper never told stays blind to
  // the income lines and prices a cube on combat power alone.
  void SetIncome(const CubeIncome& income) {
    income_ = income;
  }

  // Spends what the purse can spare on what the character is wearing, buying
  // the best value on offer over and over until nothing left is affordable or
  // worth having. Clears the bag of what it is holding for nothing first: the
  // sale is income like any other, and the room is what keeps the next drop
  // from falling on a full bag.
  GearSpend Spend(GameState& state);

  // Everything this shopper has bought over its life, which is where the
  // purse's outgoings are read off.
  const GearSpend& life() const {
    return life_;
  }

 private:
  // One thing the purse could buy next.
  struct Candidate {
    EquipSlot slot = EQUIP_SLOT_UNSPECIFIED;
    // A star when set, one of the item's upgrade slots when not.
    bool star = false;
    // A golden hammer: the slot it opens and the scroll that fills it, priced
    // and valued as one thing. On its own a hammer is worth nothing -- what
    // the purse is buying is the scroll it makes room for.
    bool hammer = false;
    // A cube into the slot's potential, priced at kCubeCost and valued at what
    // one reroll is expected to beat the item's own lines by.
    bool cube = false;
    // The scroll an upgrade slot would be filled with; null for a star.
    const Scroll* scroll = nullptr;
    // Meso this is expected to take, the attempts that land nothing included.
    int64_t cost = 0;
    // Combat power it would add. Both halves of the ratio, so the pick is a
    // comparison rather than a rule.
    int gain = 0;
  };

  // What every offer is measured against: the character as they stand. Held
  // together because working one out costs a rebuild, so it is done once per
  // pass over the gear rather than once per piece.
  struct Basis {
    const ItemPrototype* trace = nullptr;
    DerivedStats derived;
    EquipStats worn;
    int power = 0;
  };

  // The two things one worn piece could be sold next. Each returns nothing
  // where the piece has nothing of that kind to offer.
  std::optional<Candidate> ScrollOffer(GameState& state, const Basis& basis,
                                       EquipSlot slot, int level,
                                       int open_slots, bool can_hammer);
  std::optional<Candidate> StarOffer(GameState& state, const Basis& basis,
                                     EquipSlot slot, int level, int stars);
  // The cube offer for every slot that takes one, priced against `best`: what
  // a meso buys in combat power on the rest of the shelf, which is the rate an
  // income line has to be converted at to be ranked beside a damage one. A
  // pass of its own for that reason.
  std::vector<Candidate> CubeOffers(GameState& state, double best);

  // The scroll `slot`'s item wants, measured once per item and kept.
  const Scroll* ScrollFor(GameState& state, EquipSlot slot);
  // Everything the character could buy for what they are wearing, priced and
  // valued. Affordability is left to the caller: what the purse cannot cover
  // today it may cover next level, and the list says what is on offer.
  std::vector<Candidate> Offers(GameState& state);
  // Buys the affordable candidate with the most combat power per meso, trying
  // the next one down wherever the bag or the purse refuses, and says whether
  // it bought anything.
  bool BuyBest(GameState& state, GearSpend& spend);
  // Pays for one offer and puts it on. False for one that was refused.
  bool BuyOffer(GameState& state, const Candidate& candidate, GearSpend& spend);
  // Sells the copies the bag is holding for nothing: a piece the character
  // cannot wear at all, and spares past what a boom could ever use.
  void SellSpares(GameState& state, GearSpend& spend);
  // Puts a destroyed piece back on, out of the trace the boom left and a spare
  // body. False where nothing can cover it, which ends the run.
  bool RecoverBoom(GameState& state, EquipSlot slot,
                   const EquipPrototype& proto, GearSpend& spend);

  GearPlan plan_;
  GearSpend life_;
  CubeIncome income_;
  // The draws the cube valuations come off. Its own stream rather than the
  // character's, so measuring what a cube might roll never moves what the
  // game rolls.
  std::mt19937 rng_{20260901};

  // By the prototype's name, since that is what an item and its replacement
  // disagree on. A slot whose item takes no scroll maps to null.
  std::map<std::string, const Scroll*> chosen_;
};

}  // namespace ms

#endif  // MS_ANALYSIS_GEAR_PLAN_H_
