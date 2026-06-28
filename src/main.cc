#include <map>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "src/equip_instance.h"
#include "src/frontend/tui.h"
#include "src/game_state.h"
#include "src/item.h"
#include "src/proto_loader.h"
#include "src/protos/equip.pb.h"
#include "src/protos/map.pb.h"
#include "src/protos/mob.pb.h"
#include "src/protos/scroll.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace {

using bazel::tools::cpp::runfiles::Runfiles;

}  // namespace

int main(int argc, char** argv) {
  std::string err;
  std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &err));
  if (!runfiles) {
    LOG(FATAL) << "Could not create Runfiles: " << err;
  }

  std::map<std::string, ms::EquipPrototype> equips =
      ms::LoadTextProtoDir<ms::EquipPrototype>(
          runfiles->Rlocation("ms/data/equip"));
  std::map<std::string, ms::Scroll> scrolls =
      ms::LoadTextProtoDir<ms::Scroll>(runfiles->Rlocation("ms/data/scrolls"));
  std::map<std::string, ms::ItemPrototype> items =
      ms::LoadTextProtoDir<ms::ItemPrototype>(
          runfiles->Rlocation("ms/data/items"));
  std::map<std::string, ms::Mob> mobs =
      ms::LoadTextProtoDir<ms::Mob>(runfiles->Rlocation("ms/data/mobs"));
  std::map<std::string, ms::MapData> maps =
      ms::LoadTextProtoDir<ms::MapData>(runfiles->Rlocation("ms/data/maps"));

  ms::GameState state(std::move(equips), std::move(scrolls), std::move(items),
                      std::move(mobs), std::move(maps));

  // Generic low-level weapons for scrolling/star force experimentation.
  state.character.PickUp(
      std::make_unique<ms::EquipInstance>(state.equips.at("sword")));
  state.character.Equip(0);
  state.character.PickUp(
      std::make_unique<ms::EquipInstance>(state.equips.at("long_sword")));
  state.character.PickUp(
      std::make_unique<ms::EquipInstance>(state.equips.at("sabre")));

  // Fully scrolled Fafnir at 20★ — for testing high-star-force on a finished
  // endgame weapon.
  ms::Equip fafnir_state;
  fafnir_state.set_equip_name("Fafnir Mistilteinn");
  fafnir_state.set_remaining_upgrade_slots(0);
  fafnir_state.set_scroll_successes(8);
  fafnir_state.set_stars(20);
  fafnir_state.mutable_scroll_stats()->set_attack(40);
  fafnir_state.mutable_scroll_stats()->set_str(16);
  state.character.PickUp(std::make_unique<ms::EquipInstance>(
      state.equips.at("fafnir_mistilteinn"), fafnir_state));

  // Fafnir trace at 22★ (destroyed during star force) — the source trace for
  // testing trace recovery.
  ms::Equip fafnir_trace_state;
  fafnir_trace_state.set_equip_name("Fafnir Mistilteinn");
  fafnir_trace_state.set_remaining_upgrade_slots(0);
  fafnir_trace_state.set_scroll_successes(8);
  fafnir_trace_state.set_stars(22);
  fafnir_trace_state.mutable_scroll_stats()->set_attack(40);
  fafnir_trace_state.mutable_scroll_stats()->set_str(16);
  state.character.PickUp(std::make_unique<ms::EquipTrace>(
      state.equips.at("fafnir_mistilteinn"), fafnir_trace_state));

  // Fresh Fafnir — the base item consumed when recovering the trace above.
  state.character.PickUp(std::make_unique<ms::EquipInstance>(
      state.equips.at("fafnir_mistilteinn")));

  // No map-selection UI yet; default to the starter map so farming runs.
  state.current_map = "right_around_lith_harbor";

  ms::Tui(state).Run();
  return 0;
}
