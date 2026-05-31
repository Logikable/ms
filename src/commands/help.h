/* Help command: /help
 * Lists all registered commands and their descriptions. help.cc implements
 * RegisterHelpCommand.
 */
#ifndef MS_COMMANDS_HELP_H_
#define MS_COMMANDS_HELP_H_

#include "src/frontend.h"

namespace ms {

void RegisterHelpCommand(Frontend& frontend);

}  // namespace ms

#endif  // MS_COMMANDS_HELP_H_
