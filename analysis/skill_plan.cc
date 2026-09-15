#include "analysis/skill_plan.h"

#include <algorithm>
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

// One node's best climb, as it was last priced. `score` on an unpriced offer
// is what it was worth before the last purchase -- an upper bound on what it
// is worth now, since a node's next level never pays more than its last, which
// is what lets the search skip pricing the ones that are plainly behind.
struct NodeOffer {
  const Skill* node = nullptr;
  int levels = 0;
  double score = std::numeric_limits<double>::max();
  double rate = 0.0;  // what the character would be taking off the fight
  bool measured = false;
};

// Prices every climb `offer` could make and keeps the best per point.
void PriceNode(GameState& state, double held, const SkillRate& rate,
               NodeOffer* offer) {
  Character before = state.character.ToProto();
  offer->measured = true;
  offer->score = 0.0;
  offer->levels = 0;
  for (int levels : NodeRungs(state.character, *offer->node)) {
    int cost = state.character.VNodeCostFor(*offer->node, levels);
    if (cost <= 0 || !state.character.LearnSkill(*offer->node, levels)) {
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
  double held = rate(state);
  while (true) {
    Character before = state.character.ToProto();
    const Skill* best = nullptr;
    int best_levels = 0;
    double best_score = 0.0;
    double best_rate = held;
    for (const std::pair<const std::string, Skill>& entry : state.skills) {
      if (IsNode(entry.second)) {
        continue;  // the matrix has its own pool and its own allocator
      }
      // One level, and the whole skill. A skill meant to replace the one being
      // swung is worth nothing at its first level and everything at its last,
      // and a chooser offered only the first would never buy it.
      for (int levels : {1, entry.second.max_level()}) {
        int points = BuySkill(state, entry.second, named, levels);
        if (points > 0) {
          double measured = rate(state);
          double score = (measured - held) / points;
          if (score > best_score) {
            best_score = score;
            best_rate = measured;
            best_levels = levels;
            best = &entry.second;
          }
          // Only what was bought needs putting back. A skill the book cannot
          // sell -- maxed, unaffordable, its requirement out of reach -- left
          // the character untouched, and rebuilding them from the proto to
          // undo nothing is most of what this loop used to cost.
          state.character.RestoreFrom(before, state.equips, state.items);
        }
        if (entry.second.max_level() <= 1) {
          break;  // both tries are the same one
        }
      }
    }
    if (best == nullptr) {
      return;
    }
    BuySkill(state, *best, named, best_levels);
    held = best_rate;
  }
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

void SpendVMatrix(GameState& state, const SkillRate& rate) {
  if (!state.character.v_matrix_unlocked()) {
    return;
  }
  std::vector<NodeOffer> offers;
  for (const std::pair<const std::string, Skill>& entry : state.skills) {
    if (IsNode(entry.second)) {
      offers.push_back({&entry.second});
    }
  }
  double held = rate(state);
  while (true) {
    std::vector<NodeOffer>::iterator best =
        std::max_element(offers.begin(), offers.end(),
                         [](const NodeOffer& a, const NodeOffer& b) {
                           return a.score < b.score;
                         });
    if (best == offers.end() || (best->measured && best->score <= 0.0)) {
      return;
    }
    // The leader has not been priced since the last purchase, so its score is
    // only the bound. Price it and look again -- what it drops behind is
    // already fresh, and what it stays ahead of cannot overtake it.
    if (!best->measured) {
      PriceNode(state, held, rate, &*best);
      continue;
    }
    state.character.LearnSkill(*best->node, best->levels);
    held = best->rate;
    for (NodeOffer& offer : offers) {
      offer.measured = false;
    }
  }
}

}  // namespace ms
