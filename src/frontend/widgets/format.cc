#include "src/frontend/widgets/format.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "src/frontend/widgets/text_columns.h"

namespace ms {

std::string PadRight(const std::string& s, int width) {
  return ColumnWindow(s, 0, width);
}

std::string PadLeft(const std::string& s, int width) {
  int columns = TextColumns(s);
  if (columns >= width) {
    return s;
  }
  return std::string(width - columns, ' ') + s;
}

std::vector<std::string> WrapBalanced(const std::string& text, int width,
                                      int tail, int indent) {
  std::vector<std::string> words;
  std::istringstream stream(text);
  std::string word;
  while (stream >> word) {
    words.push_back(word);
  }
  if (words.empty()) {
    return {""};
  }
  // best[i] is the cost of laying out the words from i on: the lines it takes,
  // then the longest of them. Fewest lines wins first, and the balance is
  // settled among the layouts that take the same number.
  int count = static_cast<int>(words.size());
  std::vector<std::pair<int, int>> best(count + 1, {0, 0});
  std::vector<int> next(count + 1, count);
  for (int i = count - 1; i >= 0; --i) {
    best[i] = {count + 1, 0};
    // Every line but the first carries the margin, and a line starting at any
    // word but the first is never the first line.
    int line = i > 0 ? indent : 0;
    for (int j = i; j < count; ++j) {
      line += static_cast<int>(words[j].size()) + (j > i ? 1 : 0);
      // The last line of all is the one that shares its row with the value.
      int room = j + 1 == count ? width - tail : width;
      if (line > room && j > i) {
        break;
      }
      std::pair<int, int> cost = {best[j + 1].first + 1,
                                  std::max(line, best[j + 1].second)};
      if (cost < best[i]) {
        best[i] = cost;
        next[i] = j + 1;
      }
    }
  }
  std::vector<std::string> lines;
  for (int i = 0; i < count; i = next[i]) {
    std::string line = (i > 0 ? std::string(indent, ' ') : "") + words[i];
    for (int j = i + 1; j < next[i]; ++j) {
      line += " " + words[j];
    }
    lines.push_back(std::move(line));
  }
  return lines;
}

std::string DropChance(double per_kill) {
  double pct = std::clamp(per_kill, 0.0, 1.0) * 100.0;
  if (pct > 0.0 && pct < 0.001) {
    return "<0.001%";
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.3f", pct);
  std::string text(buffer);
  // Trailing zeros carry no information here, and the point with nothing
  // after it carries less.
  text.erase(text.find_last_not_of('0') + 1);
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text + "%";
}

std::string FormatWithCommas(int64_t n) {
  std::string digits = std::to_string(n < 0 ? -n : n);
  int pos = static_cast<int>(digits.size()) - 3;
  while (pos > 0) {
    digits.insert(pos, ",");
    pos -= 3;
  }
  return n < 0 ? "-" + digits : digits;
}

std::string FormatCompact(int64_t n) {
  struct Unit {
    int64_t scale;
    const char* suffix;
  };
  static const Unit kUnits[] = {
      {1000000000000000LL, "Q"},
      {1000000000000LL, "T"},
      {1000000000LL, "B"},
      {1000000LL, "M"},
  };
  int64_t magnitude = n < 0 ? -n : n;
  for (const Unit& unit : kUnits) {
    if (magnitude < 2 * unit.scale) {
      continue;
    }
    double value = static_cast<double>(magnitude) / unit.scale;
    // Three digits of it, wherever the point falls, and no trailing zeros: a
    // column of numbers is read for its size, not its last decimal.
    int decimals = value >= 100.0 ? 0 : (value >= 10.0 ? 1 : 2);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    std::string text = buf;
    if (text.find('.') != std::string::npos) {
      text.erase(text.find_last_not_of('0') + 1);
      if (text.back() == '.') {
        text.pop_back();
      }
    }
    return (n < 0 ? "-" : "") + text + unit.suffix;
  }
  return FormatWithCommas(n);
}

std::string FormatClock(double seconds) {
  int total = static_cast<int>(std::ceil(std::max(0.0, seconds)));
  int minutes = total / 60;
  int rest = total % 60;
  std::string text = std::to_string(minutes) + ":";
  if (rest < 10) {
    text += "0";
  }
  return text + std::to_string(rest);
}

std::string FormatMeso(int64_t meso) {
  return "🪙 " + FormatWithCommas(meso);
}

void AppendStat(std::string& out, int val, const std::string& label) {
  if (val <= 0) {
    return;
  }
  if (!out.empty()) {
    out += "  ";
  }
  out += "+" + std::to_string(val) + " " + label;
}

}  // namespace ms
