#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "src/equip.pb.h"
#include "src/equip_instance.h"
#include "src/equip_stats.h"
#include "src/frontend.h"
#include "src/scroll.pb.h"

namespace ms {
namespace {

// Hardcoded game state until textproto loading is implemented.
// Non-copyable/movable: EquipInstance holds a const Equip& reference.
struct GameState {
  GameState() : sword(sword_proto), rng(std::random_device{}()) {
    sword_proto.set_name("Sword");
    sword_proto.set_required_level(0);
    sword_proto.set_upgrade_slots(9);
    sword_proto.mutable_base_stats()->set_attack(15);
    sword_proto.set_attack_speed(ATTACK_SPEED_FAST_2);
    sword_proto.set_equip_slot(EQUIP_SLOT_PRIMARY_WEAPON);
    sword_proto.set_equip_job_category(EQUIP_JOB_CATEGORY_WARRIOR);
    sword_proto.set_equip_type(EQUIP_TYPE_ONE_HANDED_SWORD);

    watt_scroll.set_name("100% Weapon ATT");
    watt_scroll.set_success_rate(100);
    watt_scroll.set_tier(SCROLL_TIER_1);
    watt_scroll.mutable_stats()->set_attack(1);
  }

  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

  Equip sword_proto;
  EquipInstance sword;
  Scroll watt_scroll;
  std::mt19937 rng;
};

std::string FormatEquip(const EquipInstance& item) {
  std::ostringstream out;
  const EquipStats& s = item.stats();
  out << item.prototype().name()
      << "  [" << item.remaining_upgrade_slots() << " upgrade slots]\n";
  if (s.attack())       out << "  ATT:  " << s.attack()       << "\n";
  if (s.magic_attack()) out << "  MATT: " << s.magic_attack() << "\n";
  if (s.str())          out << "  STR:  " << s.str()          << "\n";
  if (s.dex())          out << "  DEX:  " << s.dex()          << "\n";
  if (s.int_())         out << "  INT:  " << s.int_()         << "\n";
  if (s.luk())          out << "  LUK:  " << s.luk()          << "\n";
  if (s.max_hp())       out << "  HP:   " << s.max_hp()       << "\n";
  if (s.def())          out << "  DEF:  " << s.def()          << "\n";
  return out.str();
}

}  // namespace
}  // namespace ms

int main() {
  ms::GameState state;
  ms::Frontend frontend("> ");

  frontend.Register({
      "inventory",
      "Show equipped items and stats.",
      [&state](std::vector<std::string>) -> std::string {
        return ms::FormatEquip(state.sword);
      },
  });

  frontend.Register({
      "scroll",
      "Apply a 100% Weapon ATT scroll to your sword.",
      [&state](std::vector<std::string>) -> std::string {
        if (state.sword.remaining_upgrade_slots() == 0) {
          return "No upgrade slots remaining on " +
                 state.sword.prototype().name() + ".";
        }
        bool success = state.sword.Scroll(state.watt_scroll, state.rng);
        std::ostringstream out;
        out << (success ? "Success! " : "Failed.  ");
        out << ms::FormatEquip(state.sword);
        return out.str();
      },
  });

  frontend.Run();
  return 0;
}
