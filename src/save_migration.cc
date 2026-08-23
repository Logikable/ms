#include "src/save_migration.h"

#include <string>

#include "google/protobuf/unknown_field_set.h"
#include "src/protos/character.pb.h"
#include "src/protos/save.pb.h"

namespace ms {
namespace {

// Character.seen_tabs as version 1 numbered it. The field is reserved now, so
// a version 1 character parses it into its unknown fields and this is the only
// name left to find it by.
constexpr int kSeenTabsField = 14;

// The seen keys a version 1 character carried, read back off the wire.
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

// Version 1 held one character, and held the account's business -- the
// bindings and the seen-key list -- on that character. Everything becomes the
// first and only slot of the account, whose unlocks are seeded from how far
// that character got: the player has opened what they opened, and a second
// character starts with it.
SaveGame UpgradeFromV1(const SaveGameV1& old) {
  SaveGame save;
  CharacterSave* slot = save.add_characters();
  *slot->mutable_character() = old.character();
  slot->set_current_map(old.current_map());
  slot->set_created_unix_seconds(old.created_unix_seconds());
  slot->set_playtime_seconds(old.playtime_seconds());
  save.set_active_character(0);
  save.set_last_seen_unix_seconds(old.last_seen_unix_seconds());

  Account* account = save.mutable_account();
  *account->mutable_keybinds() = old.keybinds();
  account->set_max_level(old.character().level());
  account->set_max_job_stage(old.character().job_stage());
  CopySeenTabs(old.character(), *account);
  return save;
}

}  // namespace

bool UpgradeSave(int version, const std::string& bytes, SaveGame& save) {
  if (version >= 2) {
    return save.ParseFromString(bytes);
  }
  SaveGameV1 old;
  if (!old.ParseFromString(bytes)) {
    return false;
  }
  save = UpgradeFromV1(old);
  return true;
}

}  // namespace ms
