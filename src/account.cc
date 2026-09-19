#include "src/account.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include "src/protos/account.pb.h"

namespace ms {

AccountInstance::AccountInstance(Account account)
    : account_(std::move(account)) {
}

bool AccountInstance::Seen(const std::string& key) const {
  const google::protobuf::RepeatedPtrField<std::string>& seen =
      account_.seen_keys();
  return std::find(seen.begin(), seen.end(), key) != seen.end();
}

void AccountInstance::MarkSeen(const std::string& key) {
  if (Seen(key)) {
    return;
  }
  account_.add_seen_keys(key);
}

void AccountInstance::SetMultiplayerAccount(const std::string& account_id,
                                            const std::string& token) {
  account_.set_multiplayer_account_id(account_id);
  account_.set_multiplayer_token(token);
}

void RecordProgress(Account& account, int level, int job_stage) {
  account.set_max_level(std::max(account.max_level(), level));
  account.set_max_job_stage(std::max(account.max_job_stage(), job_stage));
}

void AccountInstance::RecordProgress(int level, int job_stage) {
  ms::RecordProgress(account_, level, job_stage);
}

Account AccountInstance::ToProto() const {
  Account saved = account_;
  *saved.mutable_bank() = bank_.ToProto();
  return saved;
}

void AccountInstance::RestoreBank(
    const Bank& saved,
    const std::map<std::string, const EquipPrototype*>& equips,
    const std::map<std::string, const ItemPrototype*>& items) {
  bank_.RestoreFrom(saved, equips, items);
}

}  // namespace ms
