#ifndef MS_SRC_FRONTEND_UTIL_H_
#define MS_SRC_FRONTEND_UTIL_H_

#include <string>

#include "src/protos/equip.pb.h"

namespace ms {

std::string PadRight(const std::string& s, int width);
std::string PadLeft(const std::string& s, int width);
void AppendStat(std::string& out, int val, const std::string& name);
std::string FormatJobCategories(const EquipPrototype& proto);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_UTIL_H_
