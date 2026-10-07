/* Small shared helpers for sim output. No game logic.
 */
#ifndef MS_ANALYSIS_SIM_FORMAT_H_
#define MS_ANALYSIS_SIM_FORMAT_H_

namespace ms {

// Writes `value` as 1.23k / 4.56M / 7.89B, up to Q. Meso reaches hundreds of
// millions, and a star force run past 20 reaches quadrillions, too wide for any
// fixed column.
void FormatShort(double value, char* out, int size);

}  // namespace ms

#endif  // MS_ANALYSIS_SIM_FORMAT_H_
