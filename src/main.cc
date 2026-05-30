#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "src/character.h"
#include "src/equip.pb.h"
#include "src/equip_instance.h"
#include "src/frontend.h"
#include "src/proto_loader.h"
#include "src/scroll.pb.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace ms {
namespace {

// Returns EQUIP_SLOT_UNSPECIFIED for unrecognised names.
EquipSlot SlotFromName(const std::string& name) {
  if (name == "primary_weapon") { return EQUIP_SLOT_PRIMARY_WEAPON; }
  return EQUIP_SLOT_UNSPECIFIED;
}

struct GameState {
  explicit GameState(EquipPrototype sword_proto_arg, Scroll watt_scroll_arg)
      : sword_proto(std::move(sword_proto_arg)),
        watt_scroll(std::move(watt_scroll_arg)),
        character(Character{}),
        rng(std::random_device{}()) {
    character.PickUp(sword_proto);
    character.Equip(EQUIP_SLOT_PRIMARY_WEAPON, 0);
  }

  GameState(const GameState&) = delete;
  GameState& operator=(const GameState&) = delete;

  EquipPrototype sword_proto;
  Scroll watt_scroll;
  CharacterInstance character;
  std::mt19937 rng;
};

std::string FormatEquip(const EquipInstance& item) {
  std::ostringstream out;
  const EquipStats& s = item.stats();
  out << item.prototype().name()
      << "  [" << item.proto().remaining_upgrade_slots() << " upgrade slots]\n";
  if (s.attack())       { out << "  ATT:  " << s.attack()       << "\n"; }
  if (s.magic_attack()) { out << "  MATT: " << s.magic_attack() << "\n"; }
  if (s.str())          { out << "  STR:  " << s.str()          << "\n"; }
  if (s.dex())          { out << "  DEX:  " << s.dex()          << "\n"; }
  if (s.int_())         { out << "  INT:  " << s.int_()         << "\n"; }
  if (s.luk())          { out << "  LUK:  " << s.luk()          << "\n"; }
  if (s.max_hp())       { out << "  HP:   " << s.max_hp()       << "\n"; }
  if (s.def())          { out << "  DEF:  " << s.def()          << "\n"; }
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

  ms::EquipPrototype sword_proto;
  ms::LoadTextProto(runfiles->Rlocation("ms/data/equip/sword.textproto"),
                    &sword_proto);

  ms::Scroll watt_scroll;
  ms::LoadTextProto(
      runfiles->Rlocation("ms/data/scrolls/weapon_att_100_t1.textproto"),
      &watt_scroll);

  ms::GameState state(std::move(sword_proto), std::move(watt_scroll));
  ms::Frontend frontend("> ");

  frontend.Register({
      "equip",
      [&state](std::vector<std::string> args) -> std::string {
        if (args.size() < 3) {
          return "Usage: /equip <slot> <inventory_index>";
        }
        ms::EquipSlot slot = ms::SlotFromName(args[1]);
        if (slot == ms::EQUIP_SLOT_UNSPECIFIED) {
          return "Unknown slot '" + args[1] + "'.";
        }
        int index = 0;
        try {
          index = std::stoi(args[2]);
        } catch (...) {
          return "Invalid index '" + args[2] + "'.";
        }
        if (!state.character.Equip(slot, index)) {
          return "Could not equip item at index " + args[2] + ".";
        }
        return "Equipped.";
      },
  });

  frontend.Register({
      "unequip",
      [&state](std::vector<std::string> args) -> std::string {
        if (args.size() < 2) {
          return "Usage: /unequip <slot>";
        }
        ms::EquipSlot slot = ms::SlotFromName(args[1]);
        if (slot == ms::EQUIP_SLOT_UNSPECIFIED) {
          return "Unknown slot '" + args[1] + "'.";
        }
        if (!state.character.Unequip(slot)) {
          return "Nothing equipped in slot '" + args[1] + "'.";
        }
        return "Unequipped.";
      },
  });

  frontend.Register({
      "scroll",
      [&state](std::vector<std::string>) -> std::string {
        const auto& equipped = state.character.proto().equipped();
        if (!equipped.count(ms::EQUIP_SLOT_PRIMARY_WEAPON)) {
          return "No weapon equipped.";
        }
        const ms::Equip& equip_state =
            equipped.at(ms::EQUIP_SLOT_PRIMARY_WEAPON);
        if (equip_state.remaining_upgrade_slots() == 0) {
          return "No upgrade slots remaining on " +
                 equip_state.equip_name() + ".";
        }
        bool success = state.character.ScrollEquipped(
            ms::EQUIP_SLOT_PRIMARY_WEAPON, state.sword_proto,
            state.watt_scroll, state.rng);
        ms::EquipInstance item(
            state.sword_proto,
            state.character.proto().equipped().at(ms::EQUIP_SLOT_PRIMARY_WEAPON));
        std::ostringstream out;
        out << (success ? "Success! " : "Failed.  ");
        out << ms::FormatEquip(item);
        return out.str();
      },
  });

  frontend.Run();
  return 0;
}
