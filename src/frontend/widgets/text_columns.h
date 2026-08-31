/* Text measured and cut the way a terminal draws it: in columns, not bytes.
 *
 * A byte is not a column. A multibyte character is several bytes of one
 * column, and a fullwidth one is several bytes of two. Padding or cutting a
 * string by std::string::size() therefore lands a column short of where it
 * looks like it should, and can cut a character in half -- which draws as
 * nothing a terminal can render.
 *
 * Everything in the game that lines a row up into columns goes through here:
 * PadRight and PadLeft in format, and the sliding name in marquee.
 */
#ifndef MS_SRC_FRONTEND_WIDGETS_TEXT_COLUMNS_H_
#define MS_SRC_FRONTEND_WIDGETS_TEXT_COLUMNS_H_

#include <string>

namespace ms {

// The screen columns `text` takes.
int TextColumns(const std::string& text);

// `count` columns of `text`, starting at column `from`. Always exactly `count`
// columns: spaces fill wherever the text does not reach -- past its end, and
// into a fullwidth character an edge of the window falls inside. Empty for a
// `count` of none.
std::string ColumnWindow(const std::string& text, int from, int count);

}  // namespace ms

#endif  // MS_SRC_FRONTEND_WIDGETS_TEXT_COLUMNS_H_
