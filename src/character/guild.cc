#include "src/character/guild.h"

#include <map>
#include <string>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/protos/account.pb.h"
#include "src/protos/boss.pb.h"

namespace ms {

int NoblesseSpEarned(
    const google::protobuf::RepeatedPtrField<SoloClear>& clears,
    const std::map<std::string, Boss>& bosses) {
  // The highest difficulty beaten per boss, by its index in the file.
  std::map<std::string, int> highest;
  for (const SoloClear& clear : clears) {
    std::map<std::string, Boss>::const_iterator boss =
        bosses.find(clear.boss());
    if (boss == bosses.end()) {
      continue;
    }
    for (int i = 0; i < boss->second.difficulties_size(); ++i) {
      if (boss->second.difficulties(i).name() == clear.difficulty()) {
        std::map<std::string, int>::iterator it = highest.find(clear.boss());
        if (it == highest.end() || it->second < i) {
          highest[clear.boss()] = i;
        }
      }
    }
  }
  int earned = 0;
  for (const std::pair<const std::string, int>& entry : highest) {
    const Boss& boss = bosses.at(entry.first);
    for (int i = 0; i <= entry.second; ++i) {
      if (!boss.difficulties(i).coming_soon()) {
        ++earned;
      }
    }
  }
  return earned;
}

}  // namespace ms
