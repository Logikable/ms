/* Boss drop pity: kills that pay a copy without a roll.
 *
 * Counted per boss and drop for the whole account, every difficulty of the
 * boss together, since a player farms one boss on whichever difficulty they
 * can clear. A drop with pity 10 pays one copy on the tenth kill whatever
 * the nine before paid; pity 1 over five copies pays one on each of the first
 * five. Every other kill rolls the drop's rate.
 */
#ifndef MS_SRC_COMBAT_PITY_H_
#define MS_SRC_COMBAT_PITY_H_

#include <optional>
#include <string>

#include "google/protobuf/repeated_ptr_field.h"
#include "src/protos/account.pb.h"
#include "src/protos/mob.pb.h"

namespace ms {

using PityRecord = google::protobuf::RepeatedPtrField<BossPity>;

// The catalog key `drop` is counted under.
std::string DropKey(const MobDrop& drop);

// Whether the next clear of `boss` pays `drop` by pity: one copy, unrolled.
bool PityDue(const PityRecord& record, const std::string& boss,
             const MobDrop& drop);

// Counts a clear of `boss` toward `drop`. A drop with no pity isn't counted.
void RecordPity(PityRecord& record, const std::string& boss,
                const MobDrop& drop);

// How far along a drop's pity is, for the drop list: kills so far against the
// last kill pity pays. Nothing for a drop with no pity or one past it.
struct PityProgress {
  int count = 0;
  int of = 0;
};
std::optional<PityProgress> ProgressOf(const PityRecord& record,
                                       const std::string& boss,
                                       const MobDrop& drop);

}  // namespace ms

#endif  // MS_SRC_COMBAT_PITY_H_
