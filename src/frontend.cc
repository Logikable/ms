#include "src/frontend.h"

#include <readline/readline.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace ms {

Frontend::Frontend(std::string prompt) : prompt_(std::move(prompt)) {
}

Frontend::~Frontend() = default;

void Frontend::Register(Command command) {
  commands_.push_back(std::move(command));
}

std::string Frontend::HelpText() const {
  size_t max_len = 0;
  for (const Command& cmd : commands_) {
    if (cmd.name.size() > max_len) {
      max_len = cmd.name.size();
    }
  }
  std::ostringstream out;
  for (const Command& cmd : commands_) {
    out << "/" << cmd.name << std::string(max_len - cmd.name.size() + 2, ' ')
        << cmd.description << "\n";
  }
  return out.str();
}

std::string Frontend::Process(const std::string& line) {
  std::string stripped =
      (!line.empty() && line[0] == '/') ? line.substr(1) : line;
  std::vector<std::string> tokens;
  std::istringstream iss(stripped);
  std::string tok;
  while (iss >> tok) {
    tokens.push_back(std::move(tok));
  }
  if (tokens.empty()) {
    return "";
  }
  for (const Command& cmd : commands_) {
    if (cmd.name == tokens[0]) {
      return cmd.fn(tokens);
    }
  }
  return "Unknown command '/" + tokens[0] + "'.";
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
    if (line.empty()) {
      continue;
    }
    std::string result = Process(line);
    if (!result.empty()) {
      std::cout << result << "\n";
    }
  }
}

}  // namespace ms
