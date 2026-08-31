/* Turning a number or a string into the text a column shows.
 *
 * Nothing here knows what the game is -- no proto, no character, no item. It
 * is padding, wrapping, and the four ways this game writes a number down.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_FORMAT_H_
#define MS_SRC_FRONTEND_WIDGETS_FORMAT_H_

#include <cstdint>
#include <string>
#include <vector>

namespace ms {

// Pads s to `width` screen columns with trailing spaces, or cuts it to them if
// it is longer. Columns rather than bytes, so a name with a multibyte
// character in it lines up with the ones above it; see text_columns.h.
std::string PadRight(const std::string& s, int width);

// Pads s to `width` screen columns with LEADING spaces, right-aligning it in
// the column. Unlike PadRight this never cuts: it is for numbers, and dropping
// digits off a number that outgrew its column would quietly show the wrong
// one.
std::string PadLeft(const std::string& s, int width);

// Breaks `text` into as few lines as fit `width` columns, balanced so the
// lines come out near enough the same length: "Aquatic Letter" over "Eye
// Accessory" rather than as much as fits and one word left over.
//
// `tail` is what the LAST line leaves free, for a value that sits beside it --
// a rate, a price. `indent` is the margin every line after the first is
// returned with, which is what makes a wrapped name read as one name rather
// than as two rows. A word too long for the line gets one to itself and runs
// over rather than being cut: half a name names nothing.
std::vector<std::string> WrapBalanced(const std::string& text, int width,
                                      int tail = 0, int indent = 0);

// A drop rate as a percent: "40%", "10%", "0.025%". Up to three decimals, with
// trailing zeros trimmed, because the rare drops are three decimals apart and
// a whole percent reads them as the same chance. A positive rate too small
// even for that reads as "<0.001%" rather than as none at all.
std::string DropChance(double per_kill);

// Formats an integer with thousands-separator commas (e.g. 1234567 ->
// "1,234,567"). Handles negatives.
std::string FormatWithCommas(int64_t n);

// Formats a big number short: "5.6M", "500M", "1570M", "2.34B". A unit is
// only taken up once the value reaches two thousand of the one below it, so a
// number keeps the unit a reader can still weigh it in -- 1570M rather than
// 1.57B. The units are M, B, T and Q; anything under two million is written
// out with commas.
std::string FormatCompact(int64_t n);

// Formats a meso amount as the coin indicator followed by the comma-separated
// value (e.g. "🪙 1,234,567"). Use everywhere meso is shown.
std::string FormatMeso(int64_t meso);

// Appends "+val label" to out (with "  " separator if non-empty).
// No-op if val <= 0.
void AppendStat(std::string& out, int val, const std::string& label);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_FORMAT_H_
