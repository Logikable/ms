#include "analysis/skill_plan.h"

#include <map>
#include <string>
#include <utility>

#include "src/protos/character.pb.h"
#include "src/protos/skill.pb.h"

namespace ms {
namespace {

// How deep a requirement chain is followed before the plan is given up on.
// Nothing in the books stacks four deep; the limit is only there so a cycle
// in the data cannot hang a sim.
constexpr int kMaxRequirementDepth = 4;

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
        }
        state.character.RestoreFrom(before, state.equips, state.items);
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

}  // namespace ms
