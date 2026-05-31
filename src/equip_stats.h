#ifndef MS_SRC_EQUIP_STATS_H_
#define MS_SRC_EQUIP_STATS_H_

#include <initializer_list>

#include "src/equip.pb.h"

namespace ms {

EquipStats SumEquipStats(std::initializer_list<EquipStats> sources);

}  // namespace ms

#endif  // MS_SRC_EQUIP_STATS_H_
