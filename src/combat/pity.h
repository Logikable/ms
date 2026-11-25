/* Boss drop pity: a copy certain after enough kills without one.
 *
 * Counted per boss and drop for the whole account, every difficulty of the
 * boss together, since a player farms one boss on whichever difficulty they
 * can clear. A copy from an ordinary roll counts the same as a pity one. Once
 * a drop has paid its `pity_copies`, only its rate is left.
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

// Whether the next clear of `boss` is certain to pay `drop`.
bool PityDue(const PityRecord& record, const std::string& boss,
             const MobDrop& drop);

// Counts a clear of `boss` that paid `drop` or didn't. A drop with no pity
// isn't counted.
void RecordPity(PityRecord& record, const std::string& boss,
                const MobDrop& drop, bool paid);

// How far along a drop's pity is, for the drop list: kills toward the pity, or
// for a drop certain several times over, copies toward that many. Nothing for
// a drop with no pity or one whose pity is spent.
struct PityProgress {
  int count = 0;
  int of = 0;
};
std::optional<PityProgress> ProgressOf(const PityRecord& record,
                                       const std::string& boss,
                                       const MobDrop& drop);

}  // namespace ms

#endif  // MS_SRC_COMBAT_PITY_H_
