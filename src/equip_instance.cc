#include "src/equip_instance.h"

#include <random>

#include "src/equip_stats.h"
#include "src/protos/equip.pb.h"
#include "src/protos/scroll.pb.h"

namespace ms {
namespace {

// Success and destruction rates (parts per thousand) for each star level.
// Index i = attempt from i★ to (i+1)★. Failure = 1000 - success - destroy.
constexpr StarForceRate kRates[kMaxStarForce] = {
    {950, 0},   // 0★
    {900, 0},   // 1★
    {850, 0},   // 2★
    {850, 0},   // 3★
    {800, 0},   // 4★
    {750, 0},   // 5★
    {700, 0},   // 6★
    {650, 0},   // 7★
    {600, 0},   // 8★
    {550, 0},   // 9★
    {500, 0},   // 10★
    {450, 0},   // 11★
    {400, 0},   // 12★
    {350, 0},   // 13★
    {300, 0},   // 14★
    {300, 21},  // 15★
    {300, 21},  // 16★
    {150, 68},  // 17★
    {150, 68},  // 18★
    {150, 85},  // 19★
};

}  // namespace

EquipInstance::EquipInstance(EquipPrototype prototype)
    : prototype_(std::move(prototype)) {
  state_.set_equip_name(prototype_.name());
  state_.set_remaining_upgrade_slots(prototype_.upgrade_slots());
}

EquipInstance::EquipInstance(EquipPrototype prototype, Equip state)
    : prototype_(std::move(prototype)), state_(std::move(state)) {
}

bool EquipInstance::Scroll(const ms::Scroll& scroll, std::mt19937& rng) {
  if (state_.remaining_upgrade_slots() == 0) {
    return false;
  }
  state_.set_remaining_upgrade_slots(state_.remaining_upgrade_slots() - 1);
  std::uniform_int_distribution<int> dist(1, 100);
  if (dist(rng) > scroll.success_rate()) {
    return false;
  }
  const EquipStats scroll_sources[] = {state_.scroll_stats(), scroll.stats()};
  *state_.mutable_scroll_stats() = SumEquipStats(scroll_sources);
  return true;
}

EquipStats EquipInstance::stats() const {
  const EquipStats stat_sources[] = {prototype_.base_stats(),
                                     state_.scroll_stats()};
  return SumEquipStats(stat_sources);
}

StarForceOutcome EquipInstance::StarForce(std::mt19937& rng) {
  int s = state_.stars();
  if (s >= kMaxStarForce) {
    return kStarForceFail;
  }
  const StarForceRate& rate = kRates[s];
  std::uniform_int_distribution<int> dist(1, 1000);
  int roll = dist(rng);
  if (roll <= rate.success_ppt) {
    state_.set_stars(s + 1);
    return kStarForceSuccess;
  }
  if (rate.destroy_ppt > 0 && roll > 1000 - rate.destroy_ppt) {
    return kStarForceDestroy;
  }
  return kStarForceFail;
}

StarForceRate EquipInstance::RateAt(int stars) {
  if (stars < 0 || stars >= kMaxStarForce) {
    return {0, 0};
  }
  return kRates[stars];
}

}  // namespace ms
