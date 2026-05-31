#include "src/commands/help.h"

#include <vector>

#include "src/frontend.h"

namespace ms {

void RegisterHelpCommand(Frontend& frontend) {
  frontend.Register({
      "help",
      "List available commands.",
      [&frontend](std::vector<std::string>) -> std::string {
        return frontend.HelpText();
      },
  });
}

}  // namespace ms
