#pragma once

#include <string>

namespace bridge
{

// Reads command lines from the USB serial console. Lines are handed to the main loop,
// which runs them through the shared dispatcher and prints the JSON reply.
void startConsole();
bool takeConsoleLine(std::string &line);
void consoleReply(const std::string &json);

} // namespace bridge
