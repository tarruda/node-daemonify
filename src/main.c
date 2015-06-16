#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uv.h>

#include "arg.h"
#include "util.h"
#include "daemonify.h"


static void usage(char *progname) {
  fprintf(stderr,
          "Usage: %s [-N node_executable] [-D daemon_module] "
          "[-K] [-L socket_name | -S socket_addr] "
          "[-P pid_file_path] MODULE_NAME [MODULE_ARGS...]\n",
          progname);
  exit(255);
}

int main(int argc, char **argv) {
  char exepath[MAXPATHLEN];
  size_t exepath_size = sizeof(exepath);

  if (uv_exepath(exepath, &exepath_size)) {
    exepath_size = 0;
  }

  DaemonConfig config = {
    .node_executable = NULL,
    .daemon_module = NULL,
    .socket_name = NULL,
    .socket_addr = NULL,
    .pid_file_path = NULL,
    .exe_path = exepath_size ? exepath : NULL
  };

  char *progname;
  bool kill = false;

  if (!exepath_size || !strcmp(basename(exepath), basename(*argv))) {
    // Called with the normal program name or could't get the exe path. Either
    // way we try to parse options before proceeding.
    GETOPTS(progname) {
      case 'N':
        config.node_executable = OPTARG;
        break;
      case 'D':
        config.daemon_module = xstrdup(OPTARG);
        break;
      case 'L':
        config.socket_name = OPTARG;
        if (config.socket_addr) usage(progname);
        break;
      case 'S':
        config.socket_addr = xstrdup(OPTARG);
        if (config.socket_name) usage(progname);
        break;
      case 'P':
        config.pid_file_path = xstrdup(OPTARG);
        break;
      case 'K':
        kill = true;
        break;
      default:
        usage(progname);
        break;
    }
  }

  int rv;
  if (kill) {
    rv = daemonify_kill(&config);
  } else {
    argc--;
    char *module = *argv++;
    rv = daemonify_main(&config, module, argc, argv);
  }

  // make valgrind happy
  xfree((void *)config.socket_addr);
  xfree((void *)config.pid_file_path);
  xfree((void *)config.daemon_module);

  return rv;
}
