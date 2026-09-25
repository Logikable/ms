/* Spends a character's meso on their gear, the way a player does.
 *
 * This is the other half of sim_gear: that file puts the best gear on a
 * character and measures its worth; this one measures what it costs and whether
 * the climb ever pays for it. A sim measuring acquisition needs both, since the
 * value of top gear says nothing about when a player gets it.
 *
 * What to buy next is decided, not listed. Every slot that could be scrolled
 * and every item that could take another star is priced against what it would
 * add, and the best is bought. So the shopper stops where the price stops being
 * worth paying, and its choices follow any change to a price or stat table.
 *
 * Spell Traces are charged as meso rather than kept. The shop is their only
 * source and only scrolling spends them, so a stack of them is just stored
 * meso.
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
#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

// How far the player intends to upgrade what they wear.
struct GearPlan {
  // A star limit the shopper won't pass regardless of value. It exists so runs
  // can be compared at a fixed limit; the shopper doesn't need it, since star
  // prices rise faster than their gains and it stops well before this.
  int star_ceiling = kMaxStarForce;
  // Whether the shopper cubes at all. Off is for comparison: the same climb
  // with all cube meso left for stars.
  bool cubes = true;
  // Success rate of the scrolls bought, as a whole percent. A lower rate gives
  // more per successful slot and wastes the rest, and on a drop-only piece a
  // wasted slot is gone for good.
  int scroll_rate = 100;
};

// One pass of spending, with meso split by what it went on, so a tuning pass
// can see where it went.
struct GearSpend {
  int64_t scrolls = 0;       // Spell Traces, at the shop's price
  int64_t stars = 0;         // every attempt, failures included
  int64_t hammers = 0;       // golden hammers, for the slots they open
  int64_t cubes = 0;         // every cube, whatever it rolled
  int64_t symbols = 0;       // Arcane Symbol level-ups
  int64_t replacements = 0;  // copies bought to replace destroyed pieces
  int slots_filled = 0;
  int stars_gained = 0;
  int symbol_levels = 0;
  int hammers_driven = 0;
  // Cubes bought, and those whose roll was kept. Tracked separately because a
  // cube buys a chance, not an outcome: the gap is meso that bought nothing,
  // the trap a keep-better rule invites.
  int cubes_bought = 0;
  int cubes_kept = 0;
  // Pieces destroyed and restored, and meso from bag items sold to make room
  // and pay for it.
  int booms = 0;
  int64_t sold = 0;

  int64_t meso() const {
    return scrolls + stars + hammers + replacements + cubes + symbols;
  }

  void Add(const GearSpend& other) {
    scrolls += other.scrolls;
    stars += other.stars;
    hammers += other.hammers;
    cubes += other.cubes;
    symbols += other.symbols;
    replacements += other.replacements;
    slots_filled += other.slots_filled;
    stars_gained += other.stars_gained;
    symbol_levels += other.symbol_levels;
    hammers_driven += other.hammers_driven;
    cubes_bought += other.cubes_bought;
    cubes_kept += other.cubes_kept;
    booms += other.booms;
    sold += other.sold;
  }
};

// Spends meso on gear, remembering which scroll each item wants. Choosing a
// scroll costs a measured fight per candidate, and the answer only changes when
// the item does.
class GearShopper {
 public:
  explicit GearShopper(const GearPlan& plan) : plan_(plan) {
  }

  // Time left in the run and the current income, which decide what a %meso or
  // %drop potential line is worth (see CubeIncome). Set at each look, alongside
  // the buff decisions. Without it, the shopper ignores income lines and values
  // cubes on combat power alone.
  void SetIncome(const CubeIncome& income) {
    income_ = income;
  }

  // Spends what the character can spare on what they wear, buying the best
  // value on offer repeatedly until nothing left is affordable or worthwhile.
  // First sells bag items that are worth nothing to keep: the sale is income,
  // and the space keeps the next drop from landing in a full bag.
  GearSpend Spend(GameState& state);

  // Everything this shopper has bought over its lifetime, where the character's
  // spending is read from.
  const GearSpend& life() const {
    return life_;
  }

  // The yardstick this shopper judges against. Exposed so every other plan
  // priced alongside the shelf uses the same one, and the fight behind them all
  // is played once per kit rather than once per plan.
  HeldYardstick& yardstick() {
    return yard_;
  }

  // Damage a meso buys on this shelf, from the best offer of the last pass.
  // This rate turns unsellable drops into meso (see //analysis:drop_value).
  // Zero until the shopper has priced a round, which values drops at nothing
  // rather than guessing.
  double power_per_meso() const {
    return income_.power_per_meso;
  }

 private:
  // One thing the character could buy next.
  struct Candidate {
    EquipSlot slot = EQUIP_SLOT_UNSPECIFIED;
    // A star when set; otherwise one of the item's upgrade slots.
    bool star = false;
    // A golden hammer: the slot it opens and the scroll that fills it, priced
    // and valued together. A hammer alone is worth nothing; what's being bought
    // is the scroll it makes room for.
    bool hammer = false;
    // One level of an Arcane Symbol, priced at its meso cost and valued at the
    // stat it gives. The duplicates it uses aren't bought: they drop, and
    // CollectSymbols has already applied them.
    bool symbol = false;
    // A cube on the slot's potential, priced at kCubeCost and valued at how
    // much one reroll is expected to beat the item's current lines.
    bool cube = false;
    // The scroll an upgrade slot would be filled with; null for a star.
    const Scroll* scroll = nullptr;
    // Expected meso cost, including attempts that fail.
    int64_t cost = 0;
    // Damage it would add against the target fight (see //analysis:yardstick).
    // Both cost and gain are kept, so the choice is a comparison rather than a
    // rule. A double because damage at the cap runs into billions, beyond the
    // int this once was.
    double gain = 0.0;
  };

  // The character as they are now, which every offer is measured against.
  // Computing it needs a rebuild, so it's done once per pass rather than per
  // piece.
  struct Basis {
    const ItemPrototype* trace = nullptr;
    DerivedStats derived;
    EquipStats worn;
    // The target fight, and the character's current damage against it.
    Yardstick yard;
    double power = 0.0;
  };

  // The scroll and star offers for one worn piece. Each returns nothing when
  // the piece has no such offer.
  std::optional<Candidate> ScrollOffer(GameState& state, const Basis& basis,
                                       EquipSlot slot, int level,
                                       int open_slots, bool can_hammer);
  std::optional<Candidate> StarOffer(GameState& state, const Basis& basis,
                                     EquipSlot slot, int level, int stars);
  // The offer for one level of the Arcane Symbol worn in `slot`. Nothing when
  // the slot holds no symbol or doesn't have the duplicates the next level
  // needs, since meso alone can't level one.
  std::optional<Candidate> SymbolOffer(GameState& state, const Basis& basis,
                                       EquipSlot slot);
  // Cube offers for every slot that takes one, priced against `best`: the
  // combat power a meso buys elsewhere on the shelf. Income lines are converted
  // at this rate to rank against damage lines, which is why this is its own
  // pass.
  std::vector<Candidate> CubeOffers(GameState& state, double best);

  // The scroll `slot`'s item wants, measured once per item and cached.
  const Scroll* ScrollFor(GameState& state, EquipSlot slot);
  // Everything the character could buy for their gear, priced and valued.
  // Affordability is left to the caller, since what they can't afford now they
  // may afford next level.
  std::vector<Candidate> Offers(GameState& state);
  // Buys the affordable offer with the most combat power per meso, moving to
  // the next one whenever the bag or meso refuses. Returns whether it bought
  // anything.
  bool BuyBest(GameState& state, GearSpend& spend);
  // Pays for one offer and applies it. Returns false if it was refused.
  bool BuyOffer(GameState& state, const Candidate& candidate, GearSpend& spend);
  // One function per kind of offer. False means the bag or meso refused, and
  // BuyBest tries the next offer.
  bool BuyCube(GameState& state, EquipSlot slot, GearSpend& spend);
  bool BuyHammer(GameState& state, EquipSlot slot, GearSpend& spend);
  bool BuyScroll(GameState& state, const Candidate& candidate,
                 GearSpend& spend);
  bool BuyStar(GameState& state, EquipSlot slot, GearSpend& spend);
  bool BuySymbol(GameState& state, EquipSlot slot, GearSpend& spend);
  // Sells bag items held for nothing: pieces the character can't wear at all,
  // and spares beyond what booms could ever use.
  void SellSpares(GameState& state, GearSpend& spend);
  // Restores a destroyed piece from the trace the boom left and a spare copy.
  // False if nothing can cover it, which ends the run.
  bool RecoverBoom(GameState& state, EquipSlot slot,
                   const EquipPrototype& proto, GearSpend& spend);

  GearPlan plan_;
  GearSpend life_;
  CubeIncome income_;
  // The yardstick every offer in a pass is judged against. Kept rather than
  // recomputed per offer, since computing it plays a fight and nothing a pass
  // buys (a star, a scroll, a cube, a symbol level) changes which attacks the
  // character relies on. See HeldYardstick.
  HeldYardstick yard_;
  // Random stream for cube valuations. Separate from the character's, so
  // measuring what a cube might roll never changes what the game rolls.
  std::mt19937 rng_{20260901};

  // Keyed by prototype name, since that is what distinguishes an item from its
  // replacement. A slot whose item takes no scroll maps to null.
  std::map<std::string, const Scroll*> chosen_;
};

}  // namespace ms

#endif  // MS_ANALYSIS_GEAR_PLAN_H_
