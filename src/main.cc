#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "src/equip.pb.h"
#include "src/equip_instance.h"
#include "src/frontend.h"
#include "src/proto_loader.h"
#include "src/scroll.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

Scroll MakeWattScroll() {
  Scroll s;
  s.set_name("100% Weapon ATT");
  s.set_success_rate(100);
  s.set_tier(SCROLL_TIER_1);
  s.mutable_stats()->set_attack(1);
  return s;
}

// Non-copyable/movable: EquipInstance holds a const Equip& reference.
// sword_proto must be declared before sword.
struct GameState {
  explicit GameState(Equip sword_proto_arg)
      : sword_proto(std::move(sword_proto_arg)),
        sword(sword_proto),
        watt_scroll(MakeWattScroll()),
        rng(std::random_device{}()) {}

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

int main(int argc, char** argv) {
  std::string err;
  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles(
      bazel::tools::cpp::runfiles::Runfiles::Create(argv[0], &err));
  if (!runfiles) {
    LOG(FATAL) << "Could not create Runfiles: " << err;
  }

  ms::Equip sword_proto;
  ms::LoadTextProto(runfiles->Rlocation("ms/data/equip/sword.textproto"),
                    &sword_proto);

  ms::GameState state(std::move(sword_proto));
  ms::Frontend frontend("> ");

  frontend.Register({
      "scroll",
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
