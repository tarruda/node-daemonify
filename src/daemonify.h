#ifndef DAEMONIFY_H
#define DAEMONIFY_H

#ifdef _WIN32
#define MAXPATHLEN 256
#else
#define MAXPATHLEN 1024
#endif

typedef struct daemon_config {
  const char *node_executable, *socket_name, *socket_addr;
  const char *pid_file_path, *exe_path, *daemon_module;
} DaemonConfig;


int daemonify_main(DaemonConfig *config, const char *module, int argc,
    char **argv);

int daemonify_kill(DaemonConfig *config);


#endif  // DAEMONIFY_H
