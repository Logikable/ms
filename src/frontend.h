#ifndef MS_SRC_FRONTEND_H_
#define MS_SRC_FRONTEND_H_

#include <functional>
#include <string>
#include <vector>

namespace ms {

struct Command {
  std::string name;
  std::function<std::string(std::vector<std::string>)> fn;
};

// Minimal readline-backed command loop.
// Commands are registered with a leading slash (e.g. "scroll" matches
// "/scroll"). Ctrl-D exits. No history, completion, or key bindings.
class Frontend {
 public:
  explicit Frontend(std::string prompt);
  ~Frontend();

  void Register(Command command);
  void Run();

 private:
  void Dispatch(const std::string& line);

  std::string prompt_;
  std::vector<Command> commands_;
};

}  // namespace ms

#endif  // MS_SRC_FRONTEND_H_
