/* Random names for the things the server hands out: an account, the token
 * that proves it, and a party.
 */
#ifndef MS_SERVER_IDS_H_
#define MS_SERVER_IDS_H_

#include <random>
#include <string>

namespace ms {

// A hex id `characters` long, drawn from `rng`. Short ones are meant to be
// read in a log line; a token is long enough that guessing one is not worth
// trying.
std::string RandomHexId(std::mt19937& rng, int characters);

}  // namespace ms

#endif  // MS_SERVER_IDS_H_
