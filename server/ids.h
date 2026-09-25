/* Random ids for accounts, their tokens, and parties. */
#ifndef MS_SERVER_IDS_H_
#define MS_SERVER_IDS_H_

#include <random>
#include <string>

namespace ms {

// Returns a random hex string `characters` long. Short ids are easy to read
// in logs; tokens are long enough that guessing one is impractical.
std::string RandomHexId(std::mt19937& rng, int characters);

}  // namespace ms

#endif  // MS_SERVER_IDS_H_
