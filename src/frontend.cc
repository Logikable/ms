#include "src/frontend.h"

#include <readline/history.h>
#include <readline/readline.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

namespace ms {
namespace {

// File-scope state for readline C callback bridge.
// readline requires plain function pointers with no closure support, so we
// keep one pointer to the active Frontend and one key-tracking int here.
Frontend* g_instance = nullptr;
int g_last_key = 0;

char* CompletionShim(const char* text, int state) {
  return g_instance->CompleteEntry(text, state);
}

char** AttemptCompletion(const char* text, int /*start*/, int /*end*/) {
  rl_attempted_completion_over = 1;  // Suppress default filename completion.
  return rl_completion_matches(text, CompletionShim);
}

// Bound to left arrow. On the second consecutive press with an empty line,
// displays recent history instead of moving the cursor.
int LeftArrowShim(int count, int key) {
  if (g_last_key == key && rl_end == 0) {
    g_instance->ShowHistory();
    rl_on_new_line();
    rl_forced_update_display();
    g_last_key = 0;
    return 0;
  }
  g_last_key = key;
  return rl_backward_char(count, key);
}

}  // namespace

Frontend::Frontend(std::string prompt) : prompt_(std::move(prompt)) {
  g_instance = this;
  rl_attempted_completion_function = AttemptCompletion;
  // zsh-style: up/down search history filtered by the current line prefix.
  rl_bind_keyseq("\e[A", rl_history_search_backward);
  rl_bind_keyseq("\e[B", rl_history_search_forward);
  rl_bind_keyseq("\e[D", LeftArrowShim);
  using_history();
}

Frontend::~Frontend() {
  g_instance = nullptr;
}

void Frontend::Register(Command command) {
  commands_.push_back(std::move(command));
}

void Frontend::Run() {
  while (true) {
    char* raw = readline(prompt_.c_str());
    if (raw == nullptr) {  // EOF (Ctrl-D)
      std::cout << "\n";
      break;
    }
    std::string line(raw);
    free(raw);
    g_last_key = 0;  // Any accepted line resets double-press tracking.
    if (line.empty()) continue;
    add_history(line.c_str());
    if (line == "quit" || line == "exit") {
      std::cout << "Goodbye!\n";
      break;
    }
    Dispatch(line);
  }
}

char* Frontend::CompleteEntry(const char* text, int state) {
  // readline calls this repeatedly: state==0 to reset, then state>0 for each
  // subsequent match. Static idx persists between calls within one completion.
  static size_t idx;
  if (state == 0) idx = 0;
  const std::string prefix(text);
  while (idx < commands_.size()) {
    const std::string& name = commands_[idx++].name;
    if (name.rfind(prefix, 0) == 0) {
      return strdup(name.c_str());
    }
  }
  return nullptr;
}

void Frontend::ShowHistory() const {
  HIST_ENTRY** hist = history_list();
  if (hist == nullptr) {
    std::cout << "\n  (no history)\n";
    return;
  }
  int start = std::max(0, history_length - 10);
  std::cout << "\n";
  for (int i = start; i < history_length; ++i) {
    std::cout << "  " << hist[i]->line << "\n";
  }
}

void Frontend::Dispatch(const std::string& line) {
  std::vector<std::string> tokens;
  std::istringstream iss(line);
  std::string tok;
  while (iss >> tok) tokens.push_back(std::move(tok));
  if (tokens.empty()) return;

  if (tokens[0] == "help") {
    std::cout << BuildHelp();
    return;
  }

  for (const Command& cmd : commands_) {
    if (cmd.name == tokens[0]) {
      std::string result = cmd.fn(tokens);
      if (!result.empty()) std::cout << result << "\n";
      return;
    }
  }
  std::cout << "Unknown command '" << tokens[0] << "'. Type 'help' for help.\n";
}

std::string Frontend::BuildHelp() const {
  std::ostringstream out;
  out << "  help          Show this message\n";
  out << "  quit / exit   Exit the game\n";
  for (const Command& cmd : commands_) {
    out << "  " << cmd.name;
    // Pad to 14 chars.
    int pad = 14 - static_cast<int>(cmd.name.size());
    if (pad > 0) out << std::string(pad, ' ');
    out << cmd.description << "\n";
  }
  return out.str();
}

}  // namespace ms
