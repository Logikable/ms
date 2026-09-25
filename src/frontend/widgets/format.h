/* Turns numbers and strings into text for columns.
 *
 * Nothing here knows about the game (no protos, characters or items). It is
 * padding, wrapping, and the four ways this game formats a number.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_FORMAT_H_
#define MS_SRC_FRONTEND_WIDGETS_FORMAT_H_

#include <cstdint>
#include <string>
#include <vector>

namespace ms {

// Pads s to `width` screen columns with trailing spaces, or cuts it if longer.
// Counts columns rather than bytes, so a name with a multibyte character lines
// up with the ones above it; see text_columns.h.
std::string PadRight(const std::string& s, int width);

// Pads s to `width` screen columns with leading spaces, right-aligning it.
// Unlike PadRight this never cuts: it is for numbers, and dropping digits from
// a number too wide for its column would show the wrong number.
std::string PadLeft(const std::string& s, int width);

// Breaks `text` into as few lines as fit `width`, balanced so they are about
// the same length rather than filling each line and leaving one word over.
// `tail` is the space the last line leaves for a value beside it; `indent` is
// the margin on later lines, which makes a wrapped name read as one name. A
// word too long for the line runs over rather than being cut.
std::vector<std::string> WrapBalanced(const std::string& text, int width,
                                      int tail = 0, int indent = 0);

// A drop rate as a percent: "40%", "10%", "0.025%". Up to three decimals with
// trailing zeros trimmed, because rare drops differ in the third decimal and
// whole percents would show them as the same chance. A positive rate too small
// even for that shows as "<0.001%" rather than zero.
std::string DropChance(double per_kill);

// Formats an integer with thousands-separator commas (e.g. 1234567 ->
// "1,234,567"). Handles negatives.
std::string FormatWithCommas(int64_t n);

// A large number in short form: "5.6M", "500M", "1570M", "2.34B". A unit is
// only used from two thousand of the unit below, so the number stays in a unit
// the reader can judge easily. Under two million it is written out with commas.
std::string FormatCompact(int64_t n);

// Seconds as m:ss: "0:04", "2:47", "10:00". Rounded up, so the last second of a
// countdown shows 0:01 until it is over.
std::string FormatClock(double seconds);

// Formats a meso amount as the coin icon followed by the comma-separated value
// (e.g. "🪙 1,234,567"). Use this everywhere meso is shown.
std::string FormatMeso(int64_t meso);

// The same for a spell trace balance: "📜 30,000". A balance puts its mark
// first, like meso; a price in traces puts it after the number instead, since
// the number is what the column is read for.
std::string FormatSpellTraces(int64_t traces);

// Appends "+val label" to out (with a "  " separator if out isn't empty). Does
// nothing if val <= 0.
void AppendStat(std::string& out, int val, const std::string& label);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_FORMAT_H_
