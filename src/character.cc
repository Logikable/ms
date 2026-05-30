#include "src/character.h"

#include "absl/log/log.h"

namespace ms {

namespace {

constexpr int kApPerLevel = 5;
constexpr int kApJobAdvancementBonus = 5;

}  // namespace

CharacterInstance::CharacterInstance(Character character)
    : character_(std::move(character)) {}

void CharacterInstance::LevelUp() {
  character_.set_level(character_.level() + 1);
  character_.set_ap(character_.ap() + kApPerLevel);
}

void CharacterInstance::AdvanceJob(Job next_job) {
  int stage = character_.job_stage() + 1;
  character_.set_job_stage(stage);
  character_.set_job(next_job);
  if (stage == 3 || stage == 4) {
    character_.set_ap(character_.ap() + kApJobAdvancementBonus);
  }
}

bool CharacterInstance::AllocateStat(StatField field, int amount) {
  if (field == STAT_FIELD_UNSPECIFIED) return false;
  if (amount > character_.ap()) return false;
  AllocatedStats* stats = character_.mutable_allocated_stats();
  switch (field) {
    case STAT_FIELD_STR: stats->set_str(stats->str() + amount); break;
    case STAT_FIELD_DEX: stats->set_dex(stats->dex() + amount); break;
    case STAT_FIELD_INT: stats->set_int_(stats->int_() + amount); break;
    case STAT_FIELD_LUK: stats->set_luk(stats->luk() + amount); break;
    case STAT_FIELD_HP:  stats->set_hp(stats->hp() + amount); break;
    case STAT_FIELD_MP:  stats->set_mp(stats->mp() + amount); break;
    default: return false;
  }
  character_.set_ap(character_.ap() - amount);
  return true;
}

}  // namespace ms
