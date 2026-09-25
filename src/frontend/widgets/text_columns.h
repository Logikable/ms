/* Text measured and cut the way a terminal draws it: in columns, not bytes.
 *
 * A multibyte character is several bytes but one column, and a fullwidth one is
 * several bytes and two columns. Padding or cutting by std::string::size()
 * therefore lands short of where it should, and can split a character in half,
 * which a terminal can't draw.
 *
 * Everything that aligns text into columns goes through here: PadRight and
 * PadLeft in format, and the scrolling name in marquee.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_TEXT_COLUMNS_H_
#define MS_SRC_FRONTEND_WIDGETS_TEXT_COLUMNS_H_

#include <string>

namespace ms {

// The screen columns `text` takes.
int TextColumns(const std::string& text);

// `count` columns of `text`, starting at column `from`. Always exactly `count`
// columns: spaces fill in past the end of the text and where a window edge
// falls inside a fullwidth character. Empty when `count` is zero.
std::string ColumnWindow(const std::string& text, int from, int count);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_TEXT_COLUMNS_H_
