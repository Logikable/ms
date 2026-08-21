/* Small shared helpers for sim output. Nothing here is game logic. */
#ifndef MS_ANALYSIS_SIM_FORMAT_H_
#define MS_ANALYSIS_SIM_FORMAT_H_

#include <string>
#include <vector>

namespace ms {

// Writes `value` as 1.23k / 4.56M / 7.89B, up to Q. Meso reads in the hundreds
// of millions by the end, and a star force run past 20 in the quadrillions,
// which no column width survives.
void FormatShort(double value, char* out, int size);

// Parses "40,110,200" into levels, checking each is inside the EXP table.
// LOG(FATAL)s with `flag_name` in the message if the list is empty or a level
// is out of range.
std::vector<int> ParseLevels(const std::string& spec,
                             const std::string& flag_name);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_FORMAT_H_
