#include "src/account.h"

#include <algorithm>
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

void AccountInstance::RecordProgress(int level, int job_stage) {
  account_.set_max_level(std::max(account_.max_level(), level));
  account_.set_max_job_stage(std::max(account_.max_job_stage(), job_stage));
}

}  // namespace ms
