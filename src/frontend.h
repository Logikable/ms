#ifndef MS_SRC_FRONTEND_H_
#define MS_SRC_FRONTEND_H_

#include <functional>
#include <string>
#include <vector>

namespace ms {

struct Command {
  std::string name;
  std::string description;
  // Receives tokenized args (args[0] is the command name).
  // Returns text to print, or empty string for no output.
  std::function<std::string(std::vector<std::string>)> fn;
};

// Readline-backed command-line frontend.
// Features:
//   - Up/Down: history prefix search (zsh-style)
//   - Tab: command name completion
//   - Left arrow x2 on empty line: display recent history
// Only one Frontend may be active at a time (readline uses global state).
class Frontend {
 public:
  explicit Frontend(std::string prompt);
  ~Frontend();

  void Register(Command command);

  // Runs the input loop until "quit", "exit", or EOF.
  void Run();

  // Called by readline C shims; not for external use.
  char* CompleteEntry(const char* text, int state);
  void ShowHistory() const;

 private:
  void Dispatch(const std::string& line);
  std::string BuildHelp() const;

  std::string prompt_;
  std::vector<Command> commands_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_H_
