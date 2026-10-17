#pragma once

#include "platform_log/Output.h"
#include "system/Log.h"

namespace awtrix {

// Board and driver lines go through the shared log, so /api/v1/logs and the web UI show them. A
// verbose line enters the log only with debugMode on, as logdbg() does; without it the serial port
// still prints it, as it always has.
inline void boardLogLine(bool verbose, const char* line) {
  if (!verbose) logf("%s", line);
  else if (logbuf::verbose()) logdbg("%s", line);
  else writeLogLine(line);
}

}
