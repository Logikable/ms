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

// How deep a requirement chain is followed before the plan is given up on.
// Nothing in the books stacks four deep; the limit is only there so a cycle
// in the data cannot hang a sim.
constexpr int kMaxRequirementDepth = 4;

// Whether `skill` is bought out of the V Point pool rather than the SP book.
bool IsNode(const Skill& skill) {
  return skill.v_node() != V_NODE_KIND_UNSPECIFIED;
}

// The climbs worth pricing a node at, as levels above where it stands. The
// ladder's own band edges, read off the step costs rather than restated here,
// plus the whole of what the pool could pay for -- which is what reaches a
// perk sitting above the nearest edge.
std::vector<int> NodeRungs(const CharacterInstance& character,
                           const Skill& skill) {
  std::vector<int> rungs;
  int level = character.skill_level(skill);
  for (int next = level + 1; next <= skill.max_level(); ++next) {
    if (next == level + 1 || next == skill.max_level() ||
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

// One skill's best climb, as it was last priced. `score` on an unpriced offer
// is what it was worth before the last purchase -- an upper bound on what it
// is worth now, since a next level never pays more than the last did, which is
// what lets the search skip pricing everything plainly behind.
struct Offer {
  const Skill* skill = nullptr;
  int levels = 0;
  double score = std::numeric_limits<double>::max();
  double rate = 0.0;  // what the character would be taking off the fight
  bool measured = false;
};

// Prices every climb `offer` could make and keeps the best per point. Pricing
// is where the whole cost of an allocation is: one call plays a fight.
using PriceOffer = std::function<void(GameState&, double held, Offer*)>;

// Makes the purchase an offer names, once the search has settled on it.
using TakeOffer = std::function<void(GameState&, const Offer&)>;

// The greedy both pools share: take the best value per point, over and over,
// and stop when nothing left to buy pays anything.
//
// LAZY, which is the only reason either allocation is affordable. Pricing an
// offer plays a fight, and a book holds five hundred skills -- pricing them all
// again after every purchase is most of what a sim's runtime used to be. So a
// stale score is kept as an upper bound and only the leader is re-priced: what
// it drops behind is already fresh, and what it stays ahead of cannot overtake
// it.
//
// The bound holds while a purchase only ever makes the rest worth LESS, which
// is the usual shape -- the points that remain buy smaller and smaller lifts.
// Where a purchase makes another worth MORE, which is real enough in a book
// full of skills that boost each other, the bound can seat the order wrongly
// and the plan comes out a little different. That is the trade this makes on
// purpose: an exact sweep is an order of magnitude dearer and no sim here is
// deciding anything to the precision the difference lives at.
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
    // The leader has not been priced since the last purchase, so its score is
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

// Prices every climb a node could make, per V Point.
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

// Prices a book skill, per SP. One level and the whole skill: a skill meant to
// replace the one being swung is worth nothing at its first level and
// everything at its last, and a chooser offered only the first would never buy
// it.
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
      // Only what was bought needs putting back. A skill the book cannot sell
      // -- maxed, unaffordable, its requirement out of reach -- left the
      // character untouched.
      state.character.RestoreFrom(before, state.equips, state.items);
    }
    if (offer->skill->max_level() <= 1) {
      break;  // both tries are the same one
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
    // The matrix has its own pool and its own allocator.
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

void SpendBookWithToggles(GameState& state, const SkillRate& rate) {
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

// Empties the matrix back into the pool. GMS charges nothing to reset one and
// refunds every point, which is what lets this be a PLAN rather than a running
// total: called again on a character the gear has since changed, it re-decides
// from nothing instead of adding to a ranking taken against somebody weaker.
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

void SpendVMatrix(GameState& state, const SkillRate& rate) {
  if (!state.character.v_matrix_unlocked()) {
    return;
  }
  RefundMatrix(state);
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
