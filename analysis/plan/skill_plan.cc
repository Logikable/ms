#include "analysis/plan/skill_plan.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character/matrix/hexa_matrix.h"
#include "src/character/matrix/hexa_stat.h"
#include "src/character/matrix/sol_erda.h"
#include "src/character/matrix/v_matrix.h"
#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// How deep a requirement chain is followed before the plan gives up. No book
// goes four deep; the limit only stops a cycle in the data from hanging a sim.
constexpr int kMaxRequirementDepth = 4;

// Whether `skill` is bought with V Points rather than SP.
bool IsNode(const Skill& skill) {
  return skill.v_node() != V_NODE_KIND_UNSPECIFIED;
}

// Level increases worth pricing a node at: the ladder's own band edges, read
// from the step costs, plus the most the pool could pay for.
std::vector<int> NodeRungs(const CharacterInstance& character,
                           const Skill& skill) {
  std::vector<int> rungs;
  int level = character.skill_level(skill);
  for (int next = level + 1; next <= SkillMaxLevel(skill); ++next) {
    if (next == level + 1 || next == SkillMaxLevel(skill) ||
        VNodeStepCost(skill.v_node(), next) !=
            VNodeStepCost(skill.v_node(), next + 1)) {
      rungs.push_back(next - level);
    }
  }
  // Each rung plays a fight, so one already listed isn't priced twice.
  int affordable = character.LevelsAffordable(skill);
  if (affordable > 0 &&
      std::find(rungs.begin(), rungs.end(), affordable) == rungs.end()) {
    rungs.push_back(affordable);
  }
  return rungs;
}

// One skill's best purchase, as last priced. For an unpriced offer, `score` is
// its value before the last purchase: an upper bound now, since a further level
// never pays more than the previous one did.
struct Offer {
  const Skill* skill = nullptr;
  int levels = 0;
  double score = std::numeric_limits<double>::max();
  double rate = 0.0;  // the character's damage rate after the purchase
  bool measured = false;
  // The rate the character had when `score` was priced, which makes the score
  // a share a later call can rescale; see MatrixMemo. Zero when no rung was
  // affordable, since a score of nothing then says nothing about next time.
  double held = 0.0;
  bool afforded = false;  // whether any purchase priced was affordable
};

// Prices every purchase `offer` could make and keeps the best per point.
// Pricing is where all the cost of an allocation goes: each call plays a fight.
using PriceOffer = std::function<void(GameState&, double held, Offer*)>;

// Makes the purchase an offer describes, once the search has chosen it.
using TakeOffer = std::function<void(GameState&, const Offer&)>;

// The greedy loop both pools share: buy the best value per point until nothing
// left pays.
//
// It's lazy, which is the only reason either allocation is affordable: pricing
// an offer plays a fight. A stale score is kept as an upper bound and only the
// leader is re-priced. When one purchase makes another worth more, the order
// can come out wrong; an exact sweep would cost ten times as much.
void SpendGreedily(GameState& state, const SkillRate& rate,
                   std::vector<Offer>& offers, const PriceOffer& price,
                   const TakeOffer& take, MatrixMemo* memo = nullptr) {
  double held = rate(state);
  if (memo != nullptr) {
    for (Offer& offer : offers) {
      std::map<std::string, double>::const_iterator last =
          memo->relative.find(offer.skill->name());
      if (last != memo->relative.end()) {
        offer.score = last->second * held;
      }
    }
  }
  while (true) {
    std::vector<Offer>::iterator best = std::max_element(
        offers.begin(), offers.end(),
        [](const Offer& a, const Offer& b) { return a.score < b.score; });
    if (best == offers.end() || (best->measured && best->score <= 0.0)) {
      break;
    }
    // The leader hasn't been priced since the last purchase, so its score is
    // only the bound. Price it and look again.
    if (!best->measured) {
      price(state, held, &*best);
      best->held = best->afforded ? held : 0.0;
      continue;
    }
    take(state, *best);
    held = best->rate;
    for (Offer& offer : offers) {
      offer.measured = false;
    }
  }
  if (memo == nullptr) {
    return;
  }
  for (const Offer& offer : offers) {
    if (offer.held > 0.0) {
      memo->relative[offer.skill->name()] = offer.score / offer.held;
    }
  }
}

// Prices every level increase a node could make, per V Point.
void PriceNode(GameState& state, double held, const SkillRate& rate,
               Offer* offer) {
  Character before = state.character.ToProto();
  offer->measured = true;
  offer->score = 0.0;
  offer->levels = 0;
  offer->afforded = false;
  for (int levels : NodeRungs(state.character, *offer->skill)) {
    int cost = state.character.VNodeCostFor(*offer->skill, levels);
    if (cost <= 0 || !state.character.LearnSkill(*offer->skill, levels)) {
      continue;
    }
    offer->afforded = true;
    double measured = rate(state);
    double score = (measured - held) / cost;
    if (score > offer->score) {
      offer->score = score;
      offer->rate = measured;
      offer->levels = levels;
    }
    state.character.RestoreFrom(before, state.equips, state.items);
  }
}

// Prices a book skill per SP, at one level and at max level. A skill meant to
// replace the current attack is worth nothing at level 1 and everything at max,
// so a chooser only offered level 1 would never buy it.
void PriceSkill(GameState& state,
                const std::map<std::string, const Skill*>& named, double held,
                const SkillRate& rate, Offer* offer) {
  Character before = state.character.ToProto();
  offer->measured = true;
  offer->score = 0.0;
  offer->levels = 0;
  for (int levels : {1, offer->skill->max_level()}) {
    int points = BuySkill(state, *offer->skill, named, levels);
    if (points > 0) {
      double measured = rate(state);
      double score = (measured - held) / points;
      if (score > offer->score) {
        offer->score = score;
        offer->rate = measured;
        offer->levels = levels;
      }
      // Only a successful purchase needs undoing. A skill the book couldn't
      // sell (maxed, unaffordable, or with an unreachable requirement) left the
      // character unchanged.
      state.character.RestoreFrom(before, state.equips, state.items);
    }
    if (offer->skill->max_level() <= 1) {
      break;  // both tries would be the same
    }
  }
}

int BuyDeep(GameState& state, const Skill& skill,
            const std::map<std::string, const Skill*>& named, int levels,
            int depth) {
  int spent = 0;
  if (depth < kMaxRequirementDepth && skill.has_required_skill() &&
      !state.character.MeetsSkillRequirement(skill)) {
    std::map<std::string, const Skill*>::const_iterator req =
        named.find(skill.required_skill().skill_name());
    // A HEXA node is SpendHexaMatrix's to buy, with Sol Erda, never as a
    // book skill's prerequisite.
    if (req == named.end() ||
        req->second->hexa_node() != HEXA_NODE_KIND_UNSPECIFIED) {
      return 0;
    }
    while (!state.character.MeetsSkillRequirement(skill)) {
      int step = BuyDeep(state, *req->second, named, 1, depth + 1);
      if (step == 0) {
        return spent;
      }
      spent += step;
    }
  }
  for (int i = 0; i < levels && state.character.LearnSkill(skill); ++i) {
    ++spent;
  }
  return spent;
}

// The toggle skills this character has, by name. If this roster hasn't changed,
// the last decision still stands.
std::string ToggleRoster(const GameState& state) {
  std::string roster;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (entry.second.toggle() && state.character.HasBookFor(entry.second)) {
      roster += entry.first;
      roster += '\n';
    }
  }
  return roster;
}

// What a whole matrix costs, which sets how many Fragments a Sol Erda is
// worth to the spender.
constexpr double kMatrixFragments = 48011.0;
constexpr double kMatrixSolErda = 1672.0;

double HexaCost(const HexaPrice& price) {
  return static_cast<double>(price.fragments) +
         static_cast<double>(price.energy) / kEnergyPerSolErda *
             (kMatrixFragments / kMatrixSolErda);
}

// One level, the levels up to the next multiple of 10, and the most the purse
// pays for.
std::vector<int> HexaRungs(const CharacterInstance& character,
                           const Skill& skill) {
  const int level = character.skill_level(skill);
  const int room = MaxHexaNodeLevel(skill.hexa_node()) - level;
  std::vector<int> rungs;
  if (room <= 0) {
    return rungs;
  }
  rungs.push_back(1);
  const int to_band = std::min(room, 10 - level % 10);
  if (to_band > 1) {
    rungs.push_back(to_band);
  }
  const int affordable = character.LevelsAffordable(skill);
  if (affordable > 1 &&
      std::find(rungs.begin(), rungs.end(), affordable) == rungs.end()) {
    rungs.push_back(affordable);
  }
  return rungs;
}

void PriceHexaNode(GameState& state, double held, const SkillRate& rate,
                   Offer* offer) {
  Character before = state.character.ToProto();
  offer->measured = true;
  offer->score = 0.0;
  offer->levels = 0;
  offer->afforded = false;
  for (int levels : HexaRungs(state.character, *offer->skill)) {
    // An Origin's first level is free; a free level still has to pay.
    const double cost = std::max(
        1.0, HexaCost(state.character.HexaNodePriceFor(*offer->skill, levels)));
    if (!state.character.LearnSkill(*offer->skill, levels)) {
      continue;
    }
    offer->afforded = true;
    const double measured = rate(state);
    const double score = (measured - held) / cost;
    if (score > offer->score) {
      offer->score = score;
      offer->rate = measured;
      offer->levels = levels;
    }
    state.character.RestoreFrom(before, state.equips, state.items);
  }
}

constexpr HexaStat kHexaStatChoices[] = {
    HEXA_STAT_CRIT_DAMAGE, HEXA_STAT_BOSS_DAMAGE, HEXA_STAT_IGNORE_DEFENSE,
    HEXA_STAT_DAMAGE,      HEXA_STAT_ATTACK,      HEXA_STAT_MAIN_STAT};

// `held` plus a node of `primary`, `first` and `second` at `primary_level`,
// if the rules allow one: no primary twice, no additional stat thrice.
bool WithHexaStatNode(const HexaStats& held, HexaStat primary, HexaStat first,
                      HexaStat second, int primary_level, HexaStats* out) {
  *out = held;
  HexaStatNode& node = *out->add_nodes();
  node.set_primary(primary);
  node.set_first(first);
  node.set_second(second);
  node.set_primary_level(primary_level);
  return HexaStatsAllowed(*out);
}

// Opens the next HEXA Stat node: the primary that measures best at level 10
// among those still allowed, then the two best additional stats the rules
// allow, in that order.
bool OpenBestHexaStatNode(GameState& state, const SkillRate& rate) {
  CharacterInstance& character = state.character;
  const HexaStats held = character.hexa_stats();
  std::vector<std::pair<double, HexaStat>> ranked;
  for (HexaStat stat : kHexaStatChoices) {
    // Any two others that make the node whole; at no level they add nothing.
    bool measured = false;
    for (HexaStat first : kHexaStatChoices) {
      for (HexaStat second : kHexaStatChoices) {
        HexaStats trial;
        if (measured || first >= second ||
            !WithHexaStatNode(held, stat, first, second, kHexaStatMaxLevel,
                              &trial) ||
            !character.SetHexaStats(trial)) {
          continue;
        }
        ranked.push_back({rate(state), stat});
        character.SetHexaStats(held);
        measured = true;
      }
    }
  }
  std::sort(ranked.rbegin(), ranked.rend());
  // The primary is the best measured; the additional stats the best of the
  // rest by the same measure, a stat used as a primary before included.
  std::vector<HexaStat> order;
  for (const std::pair<double, HexaStat>& entry : ranked) {
    order.push_back(entry.second);
  }
  for (HexaStat stat : kHexaStatChoices) {
    if (std::find(order.begin(), order.end(), stat) == order.end()) {
      order.push_back(stat);
    }
  }
  for (const std::pair<double, HexaStat>& primary : ranked) {
    for (std::size_t a = 0; a < order.size(); ++a) {
      for (std::size_t b = a + 1; b < order.size(); ++b) {
        HexaStats trial;
        if (WithHexaStatNode(held, primary.second, order[a], order[b], 0,
                             &trial)) {
          return character.OpenHexaStatNode(primary.second, order[a], order[b]);
        }
      }
    }
  }
  return false;
}

// `node` of `held` set to `primary`, `first` and `second`, keeping its slot
// levels, if the rules allow it.
bool WithHexaStatArrangement(const HexaStats& held, int node, HexaStat primary,
                             HexaStat first, HexaStat second, HexaStats* out) {
  *out = held;
  HexaStatNode& slots = *out->mutable_nodes(node);
  slots.set_primary(primary);
  slots.set_first(first);
  slots.set_second(second);
  return HexaStatsAllowed(*out);
}

// What `stat` alone adds in `slot` of `node` at that slot's level, over the
// node at no levels; the other two places are any stats the rules allow. -1
// when the rules keep `stat` out of that slot.
double HexaStatSlotGain(GameState& state, const SkillRate& rate,
                        const HexaStats& held, int node, HexaStatSlot slot,
                        HexaStat stat, double bare) {
  const HexaStatNode& levels = held.nodes(node);
  for (HexaStat a : kHexaStatChoices) {
    for (HexaStat b : kHexaStatChoices) {
      // `a` and `b` fill the two places `stat` leaves, in order.
      HexaStat primary = slot == HexaStatSlot::kPrimary ? stat : a;
      HexaStat first = slot == HexaStatSlot::kFirst
                           ? stat
                           : (slot == HexaStatSlot::kPrimary ? a : b);
      HexaStat second = slot == HexaStatSlot::kSecond ? stat : b;
      HexaStats trial;
      if (!WithHexaStatArrangement(held, node, primary, first, second,
                                   &trial)) {
        continue;
      }
      HexaStatNode& bared = *trial.mutable_nodes(node);
      bared.set_primary_level(
          slot == HexaStatSlot::kPrimary ? levels.primary_level() : 0);
      bared.set_first_level(slot == HexaStatSlot::kFirst ? levels.first_level()
                                                         : 0);
      bared.set_second_level(
          slot == HexaStatSlot::kSecond ? levels.second_level() : 0);
      state.character.SetHexaStats(trial);
      const double measured = rate(state);
      state.character.SetHexaStats(held);
      return measured - bare;
    }
  }
  return -1.0;
}

// Reselects `node` onto the arrangement that measures best, its slot levels
// kept: each stat priced alone in each slot, the best sum the rules allow
// measured whole, and taken only if it beats what the node holds. Free here,
// so worth it whenever the rolls landed levels on a weak stat.
void ReselectBestHexaStatNode(GameState& state, const SkillRate& rate,
                              int node) {
  CharacterInstance& character = state.character;
  const HexaStats held = character.hexa_stats();
  const HexaStatNode& current = held.nodes(node);
  HexaStats bare = held;
  bare.mutable_nodes(node)->set_primary_level(0);
  bare.mutable_nodes(node)->set_first_level(0);
  bare.mutable_nodes(node)->set_second_level(0);
  character.SetHexaStats(bare);
  const double bare_rate = rate(state);
  character.SetHexaStats(held);
  std::map<std::pair<HexaStatSlot, HexaStat>, double> gain;
  for (HexaStatSlot slot :
       {HexaStatSlot::kPrimary, HexaStatSlot::kFirst, HexaStatSlot::kSecond}) {
    for (HexaStat stat : kHexaStatChoices) {
      gain[{slot, stat}] =
          HexaStatSlotGain(state, rate, held, node, slot, stat, bare_rate);
    }
  }
  double best = -1.0;
  HexaStat pick[3] = {current.primary(), current.first(), current.second()};
  for (HexaStat primary : kHexaStatChoices) {
    for (HexaStat first : kHexaStatChoices) {
      for (HexaStat second : kHexaStatChoices) {
        HexaStats trial;
        if (!WithHexaStatArrangement(held, node, primary, first, second,
                                     &trial)) {
          continue;
        }
        const double sum = gain[{HexaStatSlot::kPrimary, primary}] +
                           gain[{HexaStatSlot::kFirst, first}] +
                           gain[{HexaStatSlot::kSecond, second}];
        if (sum > best) {
          best = sum;
          pick[0] = primary;
          pick[1] = first;
          pick[2] = second;
        }
      }
    }
  }
  if (pick[0] == current.primary() && pick[1] == current.first() &&
      pick[2] == current.second()) {
    return;
  }
  const double held_rate = rate(state);
  character.ReselectHexaStatNode(node, pick[0], pick[1], pick[2]);
  if (rate(state) <= held_rate) {
    character.SetHexaStats(held);
  }
}

void SpendHexaStats(GameState& state, const SkillRate& rate) {
  CharacterInstance& character = state.character;
  bool rolled = false;
  while (true) {
    const HexaStats& stats = character.hexa_stats();
    const int last = stats.nodes_size() - 1;
    if (last >= 0 &&
        HexaStatNodeLevels(stats.nodes(last)) < kHexaStatNodeLevels) {
      const int64_t price = HexaStatEnhancePrice(stats.nodes(last));
      if (price <= character.sol_erda_fragments() &&
          character.EnhanceHexaStatNode(last)) {
        rolled = true;
        continue;
      }
    }
    // Only the node being rolled is rearranged: a full one was rearranged
    // when its last level landed.
    if (rolled) {
      ReselectBestHexaStatNode(state, rate, last);
      rolled = false;
    }
    if (last >= 0 &&
        HexaStatNodeLevels(stats.nodes(last)) < kHexaStatNodeLevels) {
      return;
    }
    if (!HexaStatNextNodeOpens(stats, character.proto().level(),
                               character.proto().job_stage())) {
      return;
    }
    const HexaStatPrice open = HexaStatOpenPrice(stats.nodes_size());
    if (open.energy > character.sol_erda_energy() ||
        open.fragments > character.sol_erda_fragments() ||
        !OpenBestHexaStatNode(state, rate)) {
      return;
    }
  }
}

}  // namespace

std::map<std::string, const Skill*> SkillsByName(const GameState& state) {
  std::map<std::string, const Skill*> named;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    named[entry.second.name()] = &entry.second;
  }
  return named;
}

int BuySkill(GameState& state, const Skill& skill,
             const std::map<std::string, const Skill*>& named, int levels) {
  return BuyDeep(state, skill, named, levels, 0);
}

void SpendBook(GameState& state, const SkillRate& rate) {
  std::map<std::string, const Skill*> named = SkillsByName(state);
  std::vector<Offer> offers;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    // The V and HEXA Matrices and Noblesse have their own pools and their
    // own allocators.
    if (!IsNode(entry.second) && entry.second.guild() != GUILD_SKILL_NOBLESSE &&
        entry.second.hexa_node() == HEXA_NODE_KIND_UNSPECIFIED) {
      offers.push_back({&entry.second});
    }
  }
  SpendGreedily(
      state, rate, offers,
      [&named, &rate](GameState& inner, double held, Offer* offer) {
        PriceSkill(inner, named, held, rate, offer);
      },
      [&named](GameState& inner, const Offer& offer) {
        BuySkill(inner, *offer.skill, named, offer.levels);
      });
}

void SpendBookWithToggles(GameState& state, const SkillRate& rate,
                          ToggleChoice* choice) {
  std::string roster = ToggleRoster(state);
  if (choice != nullptr && choice->roster == roster) {
    // The toggles settled on last time are still on, so this is the same
    // allocation, run once instead of twice.
    SpendBook(state, rate);
    return;
  }
  if (choice != nullptr) {
    choice->roster = roster;
  }
  Character start = state.character.ToProto();
  SpendBook(state, rate);
  double off_rate = rate(state);
  Character off_book = state.character.ToProto();

  state.character.RestoreFrom(start, state.equips, state.items);
  std::map<std::string, const Skill*> named = SkillsByName(state);
  bool thrown = false;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (!entry.second.toggle() ||
        BuySkill(state, entry.second, named, 1) <= 0) {
      continue;
    }
    thrown = state.character.ToggleSkill(entry.second) || thrown;
  }
  if (thrown) {
    SpendBook(state, rate);
    if (rate(state) > off_rate) {
      return;
    }
  }
  state.character.RestoreFrom(off_book, state.equips, state.items);
}

// Empties the matrix back into the pool. GMS resets a node for free, so after
// gear changes this decides from scratch rather than building on a ranking made
// for a weaker character.
void RefundMatrix(GameState& state) {
  Character proto = state.character.ToProto();
  int64_t refund = 0;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (!IsNode(entry.second)) {
      continue;
    }
    google::protobuf::Map<std::string, int32_t>::const_iterator held =
        proto.skill_levels().find(entry.second.name());
    if (held == proto.skill_levels().end() || held->second <= 0) {
      continue;
    }
    refund += VNodeCost(entry.second.v_node(), 0, held->second);
    proto.mutable_skill_levels()->erase(entry.second.name());
  }
  if (refund <= 0) {
    return;
  }
  proto.set_v_points(proto.v_points() + refund);
  state.character.RestoreFrom(proto, state.equips, state.items);
}

void SpendVMatrix(GameState& state, const SkillRate& rate, bool replan,
                  MatrixMemo* memo) {
  if (!state.character.v_matrix_unlocked()) {
    return;
  }
  if (replan) {
    RefundMatrix(state);
    if (memo != nullptr) {
      memo->relative.clear();
    }
  } else if (state.character.proto().v_points() <= 0) {
    return;
  }
  std::vector<Offer> offers;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (IsNode(entry.second)) {
      offers.push_back({&entry.second});
    }
  }
  SpendGreedily(
      state, rate, offers,
      [&rate](GameState& inner, double held, Offer* offer) {
        PriceNode(inner, held, rate, offer);
      },
      [](GameState& inner, const Offer& offer) {
        inner.character.LearnSkill(*offer.skill, offer.levels);
      },
      memo);
}

void SpendHexaMatrix(GameState& state, const SkillRate& rate, MatrixMemo* memo,
                     const SkillRate* stat_rate) {
  if (state.character.sol_erda_fragments() <= 0) {
    return;
  }
  SpendHexaStats(state, stat_rate != nullptr ? *stat_rate : rate);
  std::vector<Offer> offers;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    // Unreachable nodes too: a purchase can meet another's requirement. One
    // still closed fails to learn and is priced without a fight.
    if (entry.second.hexa_node() != HEXA_NODE_KIND_UNSPECIFIED) {
      offers.push_back({&entry.second});
    }
  }
  SpendGreedily(
      state, rate, offers,
      [&rate](GameState& inner, double held, Offer* offer) {
        PriceHexaNode(inner, held, rate, offer);
      },
      [](GameState& inner, const Offer& offer) {
        inner.character.LearnSkill(*offer.skill, offer.levels);
      },
      memo);
}

}  // namespace ms
