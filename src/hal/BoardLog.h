#pragma once

#include <cstdarg>
#include <cstdio>

namespace awtrix {

// How hal/ code logs without depending on system/: the entry point hands the board a sink
// (system/BoardLogSink.h). A verbose line is a diagnostic, such as the PIO, state machine and DMA
// claims; the sink decides whether the shared log keeps it.
using BoardLogSink = void (*)(bool verbose, const char* line);

inline void boardLogf(BoardLogSink sink, bool verbose, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));

inline void boardLogf(BoardLogSink sink, bool verbose, const char* fmt, ...) {
  if (!sink) return;
  char line[120];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  sink(verbose, line);
}

}
