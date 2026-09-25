#include "analysis/skill_plan.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "src/character/v_matrix.h"
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

// Level increases worth pricing a node at, relative to its current level. These
// are the ladder's own band edges, read from the step costs rather than
// restated here, plus the most the pool could pay for, which reaches a perk
// above the nearest edge.
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
  int affordable = character.LevelsAffordable(skill);
  if (affordable > 0) {
    rungs.push_back(affordable);
  }
  return rungs;
}

// One skill's best purchase, as last priced. For an unpriced offer, `score` is
// its value before the last purchase. That is an upper bound on its value now,
// since a further level never pays more than the previous one did, and it lets
// the search skip pricing offers that are clearly behind.
struct Offer {
  const Skill* skill = nullptr;
  int levels = 0;
  double score = std::numeric_limits<double>::max();
  double rate = 0.0;  // the character's damage rate after the purchase
  bool measured = false;
};

// Prices every purchase `offer` could make and keeps the best per point.
// Pricing is where all the cost of an allocation goes: each call plays a fight.
using PriceOffer = std::function<void(GameState&, double held, Offer*)>;

// Makes the purchase an offer describes, once the search has chosen it.
using TakeOffer = std::function<void(GameState&, const Offer&)>;

// The greedy loop both pools share: buy the best value per point, repeatedly,
// until nothing left pays.
//
// It's lazy, which is the only reason either allocation is affordable: pricing
// an offer plays a fight, and the catalog holds hundreds of skills. A stale
// score is kept as an upper bound and only the leader is re-priced. Offers it
// drops behind are already fresh, and offers it stays ahead of can't overtake
// it.
//
// The bound holds while each purchase makes the others worth less, which is the
// usual case. When one purchase makes another worth more, the order can come
// out wrong. That tradeoff is deliberate: an exact sweep would cost ten times
// as much, for precision no decision here needs.
void SpendGreedily(GameState& state, const SkillRate& rate,
                   std::vector<Offer>& offers, const PriceOffer& price,
                   const TakeOffer& take) {
  double held = rate(state);
  while (true) {
    std::vector<Offer>::iterator best = std::max_element(
        offers.begin(), offers.end(),
        [](const Offer& a, const Offer& b) { return a.score < b.score; });
    if (best == offers.end() || (best->measured && best->score <= 0.0)) {
      return;
    }
    // The leader hasn't been priced since the last purchase, so its score is
    // only the bound. Price it and look again.
    if (!best->measured) {
      price(state, held, &*best);
      continue;
    }
    take(state, *best);
    held = best->rate;
    for (Offer& offer : offers) {
      offer.measured = false;
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
  for (int levels : NodeRungs(state.character, *offer->skill)) {
    int cost = state.character.VNodeCostFor(*offer->skill, levels);
    if (cost <= 0 || !state.character.LearnSkill(*offer->skill, levels)) {
      continue;
    }
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
    if (req == named.end()) {
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
    // The V Matrix has its own pool and its own allocator.
    if (!IsNode(entry.second)) {
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
    // The toggles settled on last time are still on (the character keeps them),
    // so this is the same allocation, run once instead of twice. A character
    // with no toggles lands here on the first look, which is right: there's
    // nothing to try both ways.
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

// Empties the matrix back into the pool. GMS resets a node for free and refunds
// every point, so this can be a fresh plan rather than an addition: called
// again after gear changes, it decides from scratch instead of building on a
// ranking made for a weaker character.
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

void SpendVMatrix(GameState& state, const SkillRate& rate, bool replan) {
  if (!state.character.v_matrix_unlocked()) {
    return;
  }
  if (replan) {
    RefundMatrix(state);
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
      });
}

}  // namespace ms
