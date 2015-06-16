#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include <uv.h>

#ifdef _WIN32
# include <windows.h>
# include <Lmcons.h>
#else
# include <unistd.h>
# include <sys/types.h>
#endif

#include "log.h"
#include "util.h"
#include "daemonify.h"


static char *default_socket_addr(const char *socket_name) {
  char buf[MAXPATHLEN];
  
#ifdef _WIN32
  char username[UNLEN + 1];
  DWORD username_len = UNLEN + 1;
  GetUserName(username, &username_len);
  snprintf(buf, sizeof(buf), "\\\\.\\pipe\\node-daemonify-%s-%s", username,
      socket_name);
#else
  snprintf(buf, sizeof(buf), UNIX_TEMPDIR "/node-daemonify-%u/%s", getuid(),
      socket_name);
#endif

  return xstrdup(buf);
}

static char *find_module(const char *exe_path, const char *module_file,
    const char *fallback) {
  // to make the program portable, search for the module in the nearest parent
  // `node_modules` subdirectory. Failing that, use the path hardcoded at
  // compile/install time
  char buf[MAXPATHLEN];
  char filebuf[MAXPATHLEN];
  char *rv = NULL;
  uv_loop_t loop;
  uv_loop_init(&loop);
  uv_fs_t req;

  snprintf(filebuf, sizeof(filebuf),
      "node_modules" DIRSEP "daemonify" DIRSEP "%s", module_file);

  if (exe_path) {
    int len = snprintf(buf, sizeof(buf), "%s", exe_path);
    for (int i = len - 1; i--; ) {
      if (buf[i] != DIRSEP[0]) {
        continue;
      }
      snprintf(buf + i + 1, sizeof(buf) - (size_t)i - 1, "%s", filebuf);
      DLOG("checking %s", buf);
      int status = uv_fs_stat(&loop, &req, buf, NULL);
      uv_fs_req_cleanup(&req);
      if (!status) {
        DLOG("found %s", buf);
        rv = xstrdup(buf); 
        break;
      }
    }
  }

  if (uv_loop_close(&loop)) abort();

  if (!rv) {
    DLOG("using fallback: %s", fallback);
    return xstrdup(fallback);
  }

  return rv;
}

static char *default_pid_file_path(const char *socket_path) {
  char buf[MAXPATHLEN];
  size_t pos = 0;

#ifdef _WIN32
  socket_path = basename(socket_path);
  size_t socket_name_len = strlen(socket_path);
  DWORD dir_len = MAXPATHLEN;
  pos = GetTempPath(&dir_len, buf);
  if (pos >= MAXPATHLEN - socket_name_len - 10)
    return NULL;
  buf[pos++] = DIRSEP;
#endif

  snprintf(buf + pos, sizeof(buf) - pos, "%s.pid", socket_path);
  return xstrdup(buf);
}

char *basename(const char *path) {
  char *base = strrchr(path, DIRSEP[0]);
  return base ? base + 1 : (char *)path;
}

void *xmalloc(size_t size) {
  void *rv = malloc(size);

  if (!rv) {
    ELOG("out of memory");
    abort();
  }

  return rv;
}

char *xstrdup(const char *str) {
  size_t len = strlen(str) + 1;
  return memcpy(xmalloc(len), str, len);
}

void xfree(void *ptr) {
  free(ptr);
}

bool create_directories_for(const char *path) {
  // create all directories required for the socket
  uv_loop_t loop;
  uv_loop_init(&loop);
  char dir[MAXPATHLEN];
  bool rv = true;
  snprintf(dir, sizeof(dir), "%s", path);

  for (char *ptr = dir + 1; *ptr; ptr++) {
    if (*ptr == DIRSEP[0]) {
      *ptr = 0;
      uv_fs_t req;
      int status;
      status = uv_fs_mkdir(&loop, &req, dir, 0700, NULL);
      uv_fs_req_cleanup(&req);
      if (status && status != UV_EEXIST) {
        ELOG("failed to create directory \"%s\": %s(%d)", dir,
            uv_strerror(status), status);
        rv = false;
        goto end;
      }
      *ptr = DIRSEP[0];
    }
  }

end:
  if (uv_loop_close(&loop)) abort();
  return rv;
}

void daemon_set_socket(DaemonConfig *config) {
  if (!config->socket_name)
    config->socket_name = "default";
  if (!config->socket_addr)
    config->socket_addr = default_socket_addr(config->socket_name);
}

void daemon_set_defaults(DaemonConfig *config) {
  daemon_set_socket(config);
  if (!config->node_executable)
    config->node_executable = "node";
  if (!config->daemon_module)
    config->daemon_module = find_module(config->exe_path, "daemon.js",
        DAEMON_MODULE_INSTALL_PATH);
  if (!config->pid_file_path)
    config->pid_file_path = default_pid_file_path(config->socket_addr);
}
