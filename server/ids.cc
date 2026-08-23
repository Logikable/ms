#include "server/ids.h"

#include <random>
#include <string>

namespace ms {

std::string RandomHexId(std::mt19937& rng, int characters) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::uniform_int_distribution<int> digit(0, 15);
  std::string id;
  id.reserve(characters);
  for (int i = 0; i < characters; ++i) {
    id.push_back(kDigits[digit(rng)]);
  }
  return id;
}

}  // namespace ms
