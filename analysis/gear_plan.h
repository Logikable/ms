/* Spends a character's meso on their gear, the way a player does.
 *
 * Every slot that could be scrolled and every item that could take another star
 * is priced against what it would add, and the best is bought. So the shopper
 * stops where the price stops being worth paying.
 *
 * Spell Traces are charged as meso rather than kept, since the shop is their
 * only source and only scrolling spends them.
 */
#ifndef MS_ANALYSIS_GEAR_PLAN_H_
#define MS_ANALYSIS_GEAR_PLAN_H_

#include <array>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "analysis/cube_plan.h"
#include "analysis/flame_plan.h"
#include "analysis/yardstick.h"
#include "src/character/character_stats.h"
#include "src/game_state.h"
#include "src/item/equip_instance.h"
#include "src/item/flame.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {

struct SoulRolls;

// How far the player intends to upgrade what they wear.
struct GearPlan {
  // A star limit the shopper won't pass regardless of value. It exists so runs
  // can be compared at a fixed limit; the shopper doesn't need it, since star
  // prices rise faster than their gains and it stops well before this.
  int star_ceiling = kMaxStarForce;
  // Whether the shopper cubes at all. Off is for comparison: the same climb
  // with all cube meso left for stars.
  bool cubes = true;
  // Whether the shopper uses Rebirth Flames, off for the same comparison.
  bool flames = true;
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
  int64_t cubes = 0;         // every cube, whatever it rolled
  int64_t flames = 0;        // every flame, whatever it rolled
  int64_t symbols = 0;       // Arcane Symbol level-ups
  int64_t replacements = 0;  // copies bought to replace destroyed pieces
  int64_t copies = 0;        // copies bought to give farming its own piece
  int slots_filled = 0;
  int stars_gained = 0;
  // Every star force roll, and the ones that failed or destroyed the piece.
  int star_attempts = 0;
  int star_fails = 0;
  int star_destroys = 0;
  int symbol_levels = 0;
  // Cubes bought, and those whose roll was kept. Tracked separately because a
  // choosing cube buys a chance, not an outcome: the gap is meso that bought
  // nothing. A replacing cube's roll is always kept.
  int cubes_bought = 0;
  int cubes_kept = 0;
  // The same, by cube, indexed by CubeType; and the cubes on farm-only pieces.
  std::array<int, std::size(kCubes)> bought_by_cube{};
  std::array<int, std::size(kCubes)> kept_by_cube{};
  int farm_cubes_bought = 0;
  int farm_cubes_kept = 0;
  // Flames bought and kept, indexed by FlameType. A Burning roll is always
  // kept.
  std::array<int, std::size(kFlames)> bought_by_flame{};
  std::array<int, std::size(kFlames)> kept_by_flame{};
  // Flames bought on each slot, by FlameType.
  std::map<EquipSlot, std::array<int, std::size(kFlames)>> flames_by_slot;
  // Slots farming was given its own piece in; see GearShopper::SplitOffers.
  int farm_splits = 0;
  // Souls rolled onto the boss weapon, which cost shards rather than meso.
  int souls = 0;
  // Pieces destroyed and restored, and meso from bag items sold to make room
  // and pay for it.
  int booms = 0;
  int64_t sold = 0;

  int64_t meso() const {
    return scrolls + stars + replacements + copies + cubes + flames + symbols;
  }

  void Add(const GearSpend& other) {
    scrolls += other.scrolls;
    stars += other.stars;
    cubes += other.cubes;
    flames += other.flames;
    symbols += other.symbols;
    replacements += other.replacements;
    copies += other.copies;
    slots_filled += other.slots_filled;
    stars_gained += other.stars_gained;
    star_attempts += other.star_attempts;
    star_fails += other.star_fails;
    star_destroys += other.star_destroys;
    symbol_levels += other.symbol_levels;
    cubes_bought += other.cubes_bought;
    cubes_kept += other.cubes_kept;
    for (size_t i = 0; i < bought_by_cube.size(); ++i) {
      bought_by_cube[i] += other.bought_by_cube[i];
      kept_by_cube[i] += other.kept_by_cube[i];
    }
    for (size_t i = 0; i < bought_by_flame.size(); ++i) {
      bought_by_flame[i] += other.bought_by_flame[i];
      kept_by_flame[i] += other.kept_by_flame[i];
    }
    for (const auto& [slot, counts] : other.flames_by_slot) {
      for (size_t i = 0; i < counts.size(); ++i) {
        flames_by_slot[slot][i] += counts[i];
      }
    }
    farm_cubes_bought += other.farm_cubes_bought;
    farm_cubes_kept += other.farm_cubes_kept;
    farm_splits += other.farm_splits;
    souls += other.souls;
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
  // %drop potential line is worth (see CubeIncome). Without it, cubes are
  // valued on combat power alone. The shelf's own power_per_meso is kept: the
  // familiar plan reads it between this and the next pass, and a zero there
  // makes every Familiar Cube free.
  void SetIncome(const CubeIncome& income) {
    const double rate = income_.power_per_meso;
    income_ = income;
    income_.power_per_meso = rate;
  }

  // Spends what the character can spare on what they wear, buying the best
  // value on offer until nothing is affordable or worthwhile. First sells bag
  // items worth nothing to keep, so the next drop doesn't land in a full bag.
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

  // Damage a meso buys on this shelf, from the best offer of the last pass; see
  // //analysis:drop_value. Zero until the shopper has priced a round.
  double power_per_meso() const {
    return income_.power_per_meso;
  }

  // The character's damage against the target fight, in the units offers are
  // valued in, so a purchase off the shelf can be priced alongside them.
  double Power(GameState& state);

 private:
  // One thing the character could buy next.
  struct Candidate {
    EquipSlot slot = EQUIP_SLOT_UNSPECIFIED;
    // A star when set; otherwise one of the item's upgrade slots.
    bool star = false;
    // The last star of the run a star offer starts.
    int star_to = 0;
    // One level of an Arcane Symbol, priced at its meso cost and valued at the
    // stat it gives. The duplicates it uses aren't bought: they drop, and
    // CollectSymbols has already applied them.
    bool symbol = false;
    // When set, the symbol levels bought in this order instead, to reach a
    // bracket of the target fight's Arcane Force; see ForceRunOffer.
    std::vector<EquipSlot> symbol_run;
    // A cube on one of the slot's potentials, valued as the run its stopping
    // rule expects (see //analysis:cube_plan). Which cube is `cube_type`.
    bool cube = false;
    CubeType cube_type = CubeType::kRed;
    CubeProgram cube_program;
    // A run of Rebirth Flames on the boss piece in the slot; which is
    // `flame_type`.
    bool flame = false;
    FlameType flame_type = FlameType::kBurning;
    FlameProgram flame_program;
    // A copy of what both presets wear in `slot`, put on for farming so a
    // meso line can be cubed onto it. Priced with the cube run that follows.
    bool split = false;
    // Whose piece the offer is for: the boss gear's, or a farm-only piece's.
    // Symbols and splits are always the boss gear's.
    StatPreset gear = kBossGear;
    // The scroll an upgrade slot would be filled with; null for a star.
    const Scroll* scroll = nullptr;
    // Expected meso cost, including attempts that fail.
    int64_t cost = 0;
    // The meso the purse must hold to start, when that is less than `cost`:
    // one cube of a run bought a cube at a time.
    int64_t outlay = 0;
    // Damage it would add against the target fight (see //analysis:yardstick).
    // A double because damage at the cap runs into billions.
    double gain = 0.0;
  };

  // The character as they are now, which every offer is measured against.
  // Computing it needs a rebuild, so it's done once per pass rather than per
  // piece.
  struct Basis {
    const ItemPrototype* trace = nullptr;
    // The gear offers are for, and what it wears and is granted.
    StatPreset gear = kBossGear;
    DerivedStats derived;
    EquipStats worn;
    // The fight judged against, and the character's current damage in it.
    Yardstick yard;
    double power = 0.0;
    // Converts a damage gain into the units offers rank in. One for the boss
    // gear. For the farm gear, farming is bound by damage rather than respawn,
    // so a gain is that share of the farm income over the run left.
    double scale = 1.0;
  };

  // The farm gear's basis: its damage against a crowd, scaled to income at
  // `power_per_meso`. Its scale is zero when income isn't known.
  Basis FarmBasis(GameState& state, const Basis& boss, double power_per_meso);

  // The scroll and star offers for one worn piece. Each returns nothing when
  // the piece has no such offer.
  std::optional<Candidate> ScrollOffer(GameState& state, const Basis& basis,
                                       EquipSlot slot, int level,
                                       int open_slots);
  std::optional<Candidate> StarOffer(GameState& state, const Basis& basis,
                                     EquipSlot slot, int level, int stars);
  // The offer for one level of the Arcane Symbol worn in `slot`. Nothing when
  // the slot holds no symbol or doesn't have the duplicates the next level
  // needs, since meso alone can't level one.
  std::optional<Candidate> SymbolOffer(GameState& state, const Basis& basis,
                                       EquipSlot slot);
  // The cheapest symbol levels that lift the target fight's Arcane Force
  // factor a bracket or more, at whichever bracket pays most per meso. Force
  // steps, so one level alone usually reads as nothing; this is the star run's
  // answer to the same problem. Nothing when no fight asks for force, it is
  // already met at 150%, or the duplicates can't reach the next bracket.
  std::optional<Candidate> ForceRunOffer(GameState& state, const Basis& basis);
  // Cube offers for every slot that takes one, priced against `best`: the
  // combat power a meso buys elsewhere on the shelf. Income lines convert at
  // this rate.
  std::vector<Candidate> CubeOffers(GameState& state, double best);
  // Flame offers for every boss piece that takes one, priced against `best`
  // as cubes are.
  std::vector<Candidate> FlameOffers(GameState& state, double best);
  // Scroll and star offers for one piece in `basis.gear`; see ScrollOffer and
  // StarOffer.
  void PieceOffers(GameState& state, const Basis& basis, EquipSlot slot,
                   std::vector<Candidate>& offers);
  // An offer for each accessory both presets wear that a bag or shop copy
  // could split, valued with a trial split: the cube run the copy would take,
  // less the farm damage it gives up by starting bare. Held for the rest of a
  // Spend, since every trial rebuilds the character.
  std::vector<Candidate> SplitOffers(GameState& state, const Basis& farm);

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
  bool BuyCube(GameState& state, EquipSlot slot, StatPreset gear, CubeType cube,
               const CubeProgram& program, GearSpend& spend);
  bool BuyFlame(GameState& state, EquipSlot slot, FlameType flame,
                const FlameProgram& program, GearSpend& spend);
  // The main-track cube run on the farm piece in `slot` with the best value per
  // meso, out of the cubes on the shelf.
  CubeProgram BestMainCubeProgram(const GameState& state, EquipSlot slot);
  bool BuyScroll(GameState& state, const Candidate& candidate,
                 GearSpend& spend);
  // Taps `slot` until it holds `to` stars or the purse runs dry.
  bool BuyStar(GameState& state, EquipSlot slot, StatPreset gear, int to,
               GearSpend& spend);
  bool BuySplit(GameState& state, EquipSlot slot, GearSpend& spend);
  bool BuySymbol(GameState& state, EquipSlot slot, GearSpend& spend);
  // Levels each slot of `run` in turn until the purse refuses. True if any
  // level was bought.
  bool BuySymbolRun(GameState& state, const std::vector<EquipSlot>& run,
                    GearSpend& spend);
  // Rolls souls onto the boss weapon while the shards' odds beat the soul it
  // holds (see //analysis:soul_plan), each line measured against the
  // yardstick.
  void ApplySouls(GameState& state, GearSpend& spend);
  // What each of the seven lines would make the boss weapon worth, by tier,
  // for every tier in `hand`. Leaves the weapon's own soul on it.
  std::map<SoulTier, std::vector<double>> SoulLineWorths(GameState& state,
                                                         const SoulRolls& hand);
  // Sells bag items held for nothing: pieces the character can't wear at all,
  // and spares beyond what booms could ever use.
  void SellSpares(GameState& state, GearSpend& spend);
  // Restores a destroyed piece from the trace the boom left and a spare copy.
  // False if nothing can cover it, which ends the run.
  bool RecoverBoom(GameState& state, StatPreset owner,
                   const EquipPrototype& proto, GearSpend& spend);

  GearPlan plan_;
  GearSpend life_;
  CubeIncome income_;
  // The yardstick every offer in a pass is judged against. Computing it plays a
  // fight, and nothing a pass buys changes which attacks the character relies
  // on.
  HeldYardstick yard_;
  // Random stream for cube valuations. Separate from the character's, so
  // measuring what a cube might roll never changes what the game rolls.
  std::mt19937 rng_{20260901};

  // What ApplySouls last decided to keep: the level, the soul held and the
  // rolls in hand. Measuring seven lines a tier is skipped until one changes.
  std::string souls_settled_;

  // Keyed by prototype name, since that is what distinguishes an item from its
  // replacement. A slot whose item takes no scroll maps to null.
  std::map<std::string, const Scroll*> chosen_;
  // SplitOffers' result for the current Spend, cleared by a split.
  std::optional<std::vector<Candidate>> splits_;
};

}  // namespace ms

#endif  // MS_ANALYSIS_GEAR_PLAN_H_
