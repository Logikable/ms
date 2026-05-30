#include "src/frontend.h"

#include <readline/readline.h>

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace ms {

Frontend::Frontend(std::string prompt) : prompt_(std::move(prompt)) {}

Frontend::~Frontend() = default;

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
    if (line.empty()) { continue; }
    if (!line.empty() && line[0] == '/') { line = line.substr(1); }
    Dispatch(line);
  }
}

void Frontend::Dispatch(const std::string& line) {
  std::vector<std::string> tokens;
  std::istringstream iss(line);
  std::string tok;
  while (iss >> tok) { tokens.push_back(std::move(tok)); }
  if (tokens.empty()) { return; }

  for (const Command& cmd : commands_) {
    if (cmd.name == tokens[0]) {
      std::string result = cmd.fn(tokens);
      if (!result.empty()) { std::cout << result << "\n"; }
      return;
    }
  }
  std::cout << "Unknown command '/" << tokens[0] << "'.\n";
}

}  // namespace ms
