#include "src/save_migration.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "google/protobuf/unknown_field_set.h"
#include "src/item/currency.h"
#include "src/item/item.h"
#include "src/protos/character.pb.h"
#include "src/protos/equip.pb.h"
#include "src/protos/item.pb.h"
#include "src/protos/save.pb.h"

namespace ms {
namespace {

// Character.seen_tabs as version 1 numbered it. The field is reserved now, so a
// version 1 character parses it into unknown fields, and this number is the
// only way to find it.
constexpr int kSeenTabsField = 14;

// The seen keys a version 1 character had, read back from the raw fields.
void CopySeenTabs(const Character& character, Account& account) {
  const google::protobuf::UnknownFieldSet& unknown =
      character.GetReflection()->GetUnknownFields(character);
  for (int i = 0; i < unknown.field_count(); ++i) {
    const google::protobuf::UnknownField& field = unknown.field(i);
    if (field.number() == kSeenTabsField &&
        field.type() == google::protobuf::UnknownField::TYPE_LENGTH_DELIMITED) {
      account.add_seen_keys(field.length_delimited());
    }
  }
}

// Version 1 held one character, and stored account data (bindings and seen
// keys) on that character. It all becomes the account's first and only slot,
// and the account's unlocks are set from that character's progress: what the
// player unlocked stays unlocked, and a second character starts with it.
SaveGame UpgradeFromV1(const SaveGameV1& old) {
  SaveGame save;
  CharacterSave* slot = save.add_characters();
  *slot->mutable_character() = old.character();
  slot->set_current_map(old.current_map());
  slot->set_created_unix_seconds(old.created_unix_seconds());
  slot->set_playtime_seconds(old.playtime_seconds());
  save.set_offline_character(0);
  save.set_last_seen_unix_seconds(old.last_seen_unix_seconds());

  Account* account = save.mutable_account();
  *account->mutable_keybinds() = old.keybinds();
  account->set_max_level(old.character().level());
  account->set_max_job_stage(old.character().job_stage());
  CopySeenTabs(old.character(), *account);
  return save;
}

// Version 2 stored currencies as stacks on the Etc tab, where each used one of
// its 128 slots and was capped by its max_stack. They become balances. A name
// no longer in data/ is dropped, just as a stack naming a missing item already
// was.
void UpgradeFromV2(const std::map<std::string, ItemPrototype>& items,
                   SaveGame& save) {
  for (CharacterSave& slot : *save.mutable_characters()) {
    Character& character = *slot.mutable_character();
    std::vector<StackableStack> drops;
    for (const StackableStack& stack : character.stacks()) {
      const ItemPrototype* proto = FindItemByName(items, stack.name());
      if (proto != nullptr && IsCurrency(*proto)) {
        (*character.mutable_currencies())[stack.name()] +=
            static_cast<int64_t>(stack.count());
        continue;
      }
      drops.push_back(stack);
    }
    character.clear_stacks();
    for (const StackableStack& drop : drops) {
      *character.add_stacks() = drop;
    }
  }
}

// Version 3 items got their extra slots from Golden Hammers. Now every item
// that takes scrolls has them built in, so each gains the slots it wasn't
// hammered for, open and unscrolled, even on a starred item: its stars wait
// until the player scrolls them, as they would for a fresh drop.
void OpenBuiltInSlots(const std::map<std::string, EquipPrototype>& equips,
                      Equip& equip) {
  const EquipPrototype* proto = FindEquipByName(equips, equip.equip_name());
  if (proto != nullptr && TakesUpgradeSlots(*proto)) {
    equip.set_remaining_upgrade_slots(equip.remaining_upgrade_slots() +
                                      kBuiltInUpgradeSlots -
                                      equip.legacy_hammers());
  }
  equip.clear_legacy_hammers();
}

// Every Equip anywhere under `message`: worn, in the bag, in a preset, in the
// bank. Walked by reflection so a place to keep an item added later isn't
// missed.
void UpgradeFromV3(const std::map<std::string, EquipPrototype>& equips,
                   google::protobuf::Message& message) {
  if (message.GetDescriptor() == Equip::descriptor()) {
    OpenBuiltInSlots(equips, static_cast<Equip&>(message));
    return;
  }
  const google::protobuf::Reflection* reflection = message.GetReflection();
  std::vector<const google::protobuf::FieldDescriptor*> fields;
  reflection->ListFields(message, &fields);
  for (const google::protobuf::FieldDescriptor* field : fields) {
    if (field->cpp_type() !=
        google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      continue;
    }
    if (!field->is_repeated()) {
      UpgradeFromV3(equips, *reflection->MutableMessage(&message, field));
      continue;
    }
    for (int i = 0; i < reflection->FieldSize(message, field); ++i) {
      UpgradeFromV3(equips,
                    *reflection->MutableRepeatedMessage(&message, field, i));
    }
  }
}

}  // namespace

bool UpgradeSave(int version, const std::string& bytes,
                 const std::map<std::string, ItemPrototype>& items,
                 const std::map<std::string, EquipPrototype>& equips,
                 SaveGame& save) {
  if (version < 2) {
    SaveGameV1 old;
    if (!old.ParseFromString(bytes)) {
      return false;
    }
    save = UpgradeFromV1(old);
  } else if (!save.ParseFromString(bytes)) {
    return false;
  }
  if (version < 3) {
    UpgradeFromV2(items, save);
  }
  if (version < 4) {
    UpgradeFromV3(equips, save);
  }
  return true;
}

}  // namespace ms
