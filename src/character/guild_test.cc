#include "src/character/guild.h"

#include <map>
#include <string>

#include "google/protobuf/repeated_ptr_field.h"
#include "gtest/gtest.h"
#include "src/protos/account.pb.h"
#include "src/protos/boss.pb.h"

namespace ms {
namespace {

// A boss with Normal and Hard, and a Chaos not built yet.
std::map<std::string, Boss> Catalog() {
  Boss boss;
  boss.add_difficulties()->set_name("Normal");
  boss.add_difficulties()->set_name("Hard");
  BossDifficulty* chaos = boss.add_difficulties();
  chaos->set_name("Chaos");
  chaos->set_coming_soon(true);
  return {{"lotus", boss}};
}

google::protobuf::RepeatedPtrField<SoloClear> Clears(
    std::initializer_list<std::pair<std::string, std::string>> pairs) {
  google::protobuf::RepeatedPtrField<SoloClear> clears;
  for (const std::pair<std::string, std::string>& pair : pairs) {
    SoloClear* clear = clears.Add();
    clear->set_boss(pair.first);
    clear->set_difficulty(pair.second);
  }
  return clears;
}

TEST(GuildTest, ABeatenDifficultyPaysForEveryOneBelowIt) {
  std::map<std::string, Boss> bosses = Catalog();
  EXPECT_EQ(NoblesseSpEarned(Clears({}), bosses), 0);
  EXPECT_EQ(NoblesseSpEarned(Clears({{"lotus", "Normal"}}), bosses), 1);
  EXPECT_EQ(NoblesseSpEarned(Clears({{"lotus", "Hard"}}), bosses), 2);
  EXPECT_EQ(NoblesseSpEarned(Clears({{"lotus", "Hard"}, {"lotus", "Normal"}}),
                             bosses),
            2)
      << "a difficulty is paid for once";
  EXPECT_EQ(NoblesseSpEarned(Clears({{"lotus", "Chaos"}}), bosses), 2)
      << "a coming-soon difficulty pays nothing itself";
  EXPECT_EQ(NoblesseSpEarned(
                Clears({{"lotus", "Extreme"}, {"damien", "Normal"}}), bosses),
            0)
      << "a clear the catalog no longer names";
}

}  // namespace
}  // namespace ms
