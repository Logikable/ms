#ifndef MS_SRC_ITEM_EQUIP_STATS_H_
#define MS_SRC_ITEM_EQUIP_STATS_H_

#include "absl/types/span.h"
#include "src/protos/equip.pb.h"

namespace ms {

// Returns the field-wise sum of all EquipStats in `sources`, except ignore
// defense, which combines multiplicatively and rounds to a whole percent.
EquipStats SumEquipStats(absl::Span<const EquipStats> sources);

}  // namespace ms

#endif  // MS_SRC_ITEM_EQUIP_STATS_H_
