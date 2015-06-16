#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <uv.h>

#ifndef LOG_H
#define LOG_H

static inline FILE *open_log(void) {
  char *log_file = getenv("DAEMONIFY_LOG_FILE");
  if (!log_file)
#ifdef DEBUG
    log_file = "daemonify.log";
#else
    return stderr;
#endif
  return fopen(log_file, "a");
}

static inline void close_log(FILE *f) {
  if (f != stderr) fclose(f);
}

static inline void log_message(FILE *f, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(f, fmt, args);
  va_end(args);
}

static inline void log_nl(FILE *f) {
  fputc('\n', f);
}

#define DO_LOG(...)                             \
  do {                                          \
    FILE *f = open_log();                       \
    if (f) {                                    \
      log_message(f, __VA_ARGS__);              \
      log_nl(f);                                \
      close_log(f);                             \
    }                                           \
  } while (0)                                            

#define DO_LOG_MPACK(message, obj)              \
  do {                                          \
    FILE *f = open_log();                       \
    if (f) {                                    \
      log_message(f, "%s: ", message);          \
      msgpack_object_print(f, obj);             \
      log_nl(f);                                \
      close_log(f);                             \
    }                                           \
  } while (0)                                            

#define DO_LOG_SBUFFER(message, buf)                              \
  do {                                                            \
    FILE *f = open_log();                                         \
    msgpack_unpacked unpacked;                                    \
    msgpack_unpacked_init(&unpacked);                             \
    msgpack_unpack_next(&unpacked, buf.data, buf.size, NULL);     \
    log_message(f, "%s: ", message);                              \
    msgpack_object_print(f, unpacked.data);                       \
    log_nl(f);                                                    \
    close_log(f);                                                 \
    msgpack_unpacked_destroy(&unpacked);                          \
  } while (0)

#ifdef DEBUG

#define DLOG(...) DO_LOG("debug: " __VA_ARGS__)
#define DLOG_MPACK(...) DO_LOG_MPACK("debug: " __VA_ARGS__)
#define DLOG_SBUFFER(...) DO_LOG_SBUFFER("debug: " __VA_ARGS__)

#else

#define DLOG(...)
#define DLOG_MPACK(...)
#define DLOG_SBUFFER(...)

#endif  // DEBUG

#define ELOG(...) DO_LOG("error: " __VA_ARGS__)

#endif  // LOG_H
