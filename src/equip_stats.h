#ifndef MS_SRC_EQUIP_STATS_H_
#define MS_SRC_EQUIP_STATS_H_

#include "absl/types/span.h"
#include "src/equip.pb.h"

namespace ms {

// Returns the field-wise sum of all EquipStats in `sources`.
EquipStats SumEquipStats(absl::Span<const EquipStats> sources);

}  // namespace ms

#endif  // MS_SRC_EQUIP_STATS_H_
