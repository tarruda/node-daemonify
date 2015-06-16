#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>

#include <msgpack.h>
#include <uv.h>

#ifndef _WIN32
# include <unistd.h>
#endif

#include "daemonify.h"
#include "util.h"
#include "log.h"

#define UNUSED(x) ((void)(x))

typedef struct stream Stream;
typedef struct write_request WriteRequest;
typedef struct daemonify Daemonify;
typedef void(*eof_cb)(Stream *stream);
typedef void(*read_cb)(Stream *stream, char *buf, size_t size);
typedef void(*write_cb)(Daemonify *daemonify);

struct timer_check_data {
  int remaining_tries;
  union {
    const char *socket_addr;
    int pid;
  } data;
};

struct stream {
  Daemonify *daemonify;
  uv_handle_type type;
  read_cb on_read;
  eof_cb on_eof;
  uv_stream_t *uv;
  int fd;
  union {
    uv_tty_t tty;
    uv_pipe_t pipe;
    union {
      uv_fs_t read_req;
      uv_buf_t uvbuf;
    } fs;
  } data;
};

struct write_request {
  Daemonify *daemonify;
  write_cb on_write;
  union {
    uv_write_t pipe_write;
    uv_fs_t fs_write;
  } req;
  char data[];
};

struct daemonify {
  uv_loop_t loop;
  Stream in, out, err, socket;
  msgpack_unpacker *unpacker;
  msgpack_sbuffer sbuffer;
  char dirbuf[4096];
  const char *socket_addr, *module;
  int status;
  char **argv;
  int argc;
  char fs_read_buf[0xffff];
};

static void uv_alloc(uv_handle_t *handle, size_t suggested, uv_buf_t *buf) {
  UNUSED(handle);
  buf->base = xmalloc(suggested);
  buf->len = suggested;
}

static void uv_read(uv_stream_t *stream, ssize_t cnt, const uv_buf_t *buf) {
  Stream *s = stream->data;
  if (cnt <= 0) {
    if (cnt != UV_ENOBUFS && cnt != 0) {
      DLOG("stream (fd %d) closed: %s(%zd)", s->fd, uv_strerror((int)cnt), cnt);
      // Read error of EOF, which we simply treat as EOF.
      uv_read_stop(stream);
      s->on_eof(s);
    }
    goto cleanup;
  }

  s->on_read(s, buf->base, (size_t)cnt);

cleanup:
  xfree(buf->base);
}

static void stream_fs_read(uv_fs_t *req) {
  Stream *s = req->data;
  Daemonify *daemonify = s->daemonify;
  int64_t res = req->result;
  uv_fs_req_cleanup(req);

  if (res <= 0) {
    if (res < 0) {
      DLOG("failed to read file (fd %d): %s(%" PRId64 ")", s->fd,
           uv_strerror((int)res), res);
    }
    s->on_eof(s);
    return;
  }

  s->on_read(s, daemonify->fs_read_buf, (size_t)res);
  req = xmalloc(sizeof(uv_fs_t));
  req->data = s;
  uv_fs_read(&daemonify->loop, req, s->fd,
      &s->data.fs.uvbuf, 1, -1, stream_fs_read);
}

static void walk_cb(uv_handle_t *handle, void *arg) {
  UNUSED(arg);
  if (!uv_is_closing(handle)) {
    uv_close(handle, NULL);
  }
}

static void adp_exit(Daemonify *daemonify, int status) {
  daemonify->status = status;
  uv_walk(&daemonify->loop, walk_cb, NULL);
}

static void stream_close_cb(uv_handle_t *handle) {
#ifndef _WIN32
  Stream *stream = handle->data;
  if (stream->fd >= 0) close(stream->fd);
#endif
}

static void stream_close(Stream *stream) {
  if (stream->uv && !uv_is_closing((uv_handle_t *)stream->uv)) {
    uv_close((uv_handle_t *)stream->uv, stream_close_cb);
  }
}

static void stream_write_end(WriteRequest *r, int status) {
  if (status) {
    DLOG("error writing: %s(%d)", uv_strerror(status), status);
  } else if (r->on_write) {
    r->on_write(r->daemonify);
  }

  xfree(r);
}

static void stream_fs_write_end(uv_fs_t *req) {
  stream_write_end(req->data, req->result > 0 ? 0 : (int)req->result);
}

static void stream_pipe_write_end(uv_write_t *req, int status) {
  stream_write_end(req->data, status);
}

static void stream_write_start(Stream *stream, const char *data, size_t size,
    write_cb on_write) {
  WriteRequest *r = xmalloc(sizeof(WriteRequest) + size);
  r->on_write = on_write;
  r->daemonify = stream->daemonify;
  memcpy(r->data, data, size);
  uv_buf_t uvbuf = {.base = r->data, .len = size};

  int status;
  if (stream->type == UV_FILE) {
    r->req.fs_write.data = r;
    assert(uvbuf.len <= INT64_MAX);
    status = uv_fs_write(&stream->daemonify->loop, &r->req.fs_write, stream->fd,
        &uvbuf, 1, -1, stream_fs_write_end);
  } else {
    r->req.pipe_write.data = r;
    status = uv_write(&r->req.pipe_write, stream->uv, &uvbuf, 1,
        stream_pipe_write_end);
  }

  if (status) {
    DLOG("failed to write: %s(%zd)", uv_strerror((int)status), status);
    xfree(r);
  }
}

static void stdin_read(Stream *stream, char *data, size_t size) {
  msgpack_packer pac;
  msgpack_packer_init(&pac, &stream->daemonify->sbuffer, msgpack_sbuffer_write);
  // send [0, data]
  msgpack_pack_array(&pac, 2);
  msgpack_pack_int(&pac, 0);
  msgpack_pack_bin(&pac, size);
  msgpack_pack_bin_body(&pac, data, size);
  DLOG_SBUFFER("client -> server", stream->daemonify->sbuffer);
  stream_write_start(&stream->daemonify->socket, stream->daemonify->sbuffer.data,
      stream->daemonify->sbuffer.size, NULL);
  msgpack_sbuffer_clear(&stream->daemonify->sbuffer);
}

static void stdin_eof(Stream *stream) {
  msgpack_packer pac;
  msgpack_packer_init(&pac, &stream->daemonify->sbuffer, msgpack_sbuffer_write);
  // send [0, nil] to signal EOF
  msgpack_pack_array(&pac, 2);
  msgpack_pack_int(&pac, 0);
  msgpack_pack_nil(&pac);
  DLOG("stdin -> socket: EOF");
  stream_write_start(&stream->daemonify->socket, stream->daemonify->sbuffer.data,
      stream->daemonify->sbuffer.size, NULL);
  msgpack_sbuffer_clear(&stream->daemonify->sbuffer);
}

static void socket_eof(Stream *stream) {
  Daemonify *daemonify = stream->daemonify;
  stream_close(&daemonify->socket);
  stream_close(&daemonify->in);
  stream_close(&daemonify->out);
  stream_close(&daemonify->err);
}

static void socket_message(Daemonify *daemonify, msgpack_object msg) {
  if (msg.type == MSGPACK_OBJECT_ARRAY && msg.via.array.size == 2) {
    int code = (int)msg.via.array.ptr[0].via.u64;

    if (code == 3) {
      // module exited, store the return value and close the socket
      daemonify->status = (int)msg.via.array.ptr[1].via.u64; 
      DLOG("socket EOF");
      socket_eof(&daemonify->socket);
      return;
    }

    if (code == 1 || code == 2) {
      msgpack_object obj = msg.via.array.ptr[1];
      if (obj.type == MSGPACK_OBJECT_NIL) {
        // stdout or stderr closed
        stream_close(!code ? &daemonify->in : code == 1 ?
            &daemonify->out : &daemonify->err);
        return;
      } else if (obj.type == MSGPACK_OBJECT_BIN) {
        // forward data
        DLOG_MPACK("server -> client", msg);
        stream_write_start(
            code == 1 ? &daemonify->out : &daemonify->err,
            msg.via.array.ptr[1].via.bin.ptr,
            msg.via.array.ptr[1].via.bin.size, NULL);
        return;
      }
    }
  }

  DLOG_MPACK("unrecognized message: ", msg);
}

static void socket_read(Stream *stream, char *data, size_t size) {
  msgpack_unpacker *unpacker = stream->daemonify->unpacker;

  msgpack_unpacker_reserve_buffer(unpacker, size);
  memcpy(msgpack_unpacker_buffer(unpacker), data, size);
  msgpack_unpacker_buffer_consumed(unpacker, size);

  msgpack_unpacked unpacked;
  msgpack_unpacked_init(&unpacked);
  msgpack_unpack_return result;

  while ((result = msgpack_unpacker_next(unpacker, &unpacked)) ==
      MSGPACK_UNPACK_SUCCESS) {
    socket_message(stream->daemonify, unpacked.data);
  }

  if (result == MSGPACK_UNPACK_PARSE_ERROR) {
    ELOG("received invalid msgpack data");
  } else if (result == MSGPACK_UNPACK_NOMEM_ERROR) {
    ELOG("out of memory");
    abort();
  }
}

static void init_stdio_stream(Daemonify *daemonify, Stream *stream, int fd) {
  stream->daemonify = daemonify;
  stream->fd = fd;
  stream->uv = NULL;

  switch (stream->type = uv_guess_handle(fd)) {
    case UV_TTY:
      uv_tty_init(&daemonify->loop, &stream->data.tty, fd, fd == 0);
      stream->data.tty.data = stream;
      stream->uv = (uv_stream_t *)&stream->data.tty;
      if (fd == 0)
        uv_read_start(stream->uv, uv_alloc, uv_read);
      break;
    case UV_NAMED_PIPE:
      uv_pipe_init(&daemonify->loop, &stream->data.pipe, 0);
      stream->data.pipe.data = stream;
      stream->uv = (uv_stream_t *)&stream->data.pipe;
      uv_pipe_open(&stream->data.pipe, fd);
      if (fd == 0)
        uv_read_start(stream->uv, uv_alloc, uv_read);
      break;
    case UV_FILE:
      stream->data.fs.uvbuf.base = daemonify->fs_read_buf;
      stream->data.fs.uvbuf.len = sizeof(daemonify->fs_read_buf);
      if (fd == 0) {
        uv_fs_t *req = xmalloc(sizeof(uv_fs_t));
        req->data = stream;
        uv_fs_read(&daemonify->loop, req, fd,
            &stream->data.fs.uvbuf, 1, -1, stream_fs_read);
      }
      break;
    default:
      abort();
  }

}

static void start_reading(Daemonify *daemonify) {
  init_stdio_stream(daemonify, &daemonify->in, 0);
  init_stdio_stream(daemonify, &daemonify->out, 1);
  init_stdio_stream(daemonify, &daemonify->err, 2);
  daemonify->in.on_read = stdin_read;
  daemonify->in.on_eof = stdin_eof;
  uv_read_start((uv_stream_t *)&daemonify->socket.data.pipe, uv_alloc, uv_read);
}

static void socket_send_process_data(Daemonify *daemonify) {
  msgpack_packer pac;
  msgpack_packer_init(&pac, &daemonify->sbuffer, msgpack_sbuffer_write);
  // argv
  msgpack_pack_map(&pac, 2);
  msgpack_pack_str(&pac, 4);
  msgpack_pack_str_body(&pac, "argv", 4);
  msgpack_pack_array(&pac, (size_t)daemonify->argc + 1);
  msgpack_pack_str(&pac, strlen(daemonify->module));
  msgpack_pack_str_body(&pac, daemonify->module, strlen(daemonify->module));
  for (int i = 0; i < daemonify->argc; i++ ){
    msgpack_pack_str(&pac, strlen(daemonify->argv[i]));
    msgpack_pack_str_body(&pac, daemonify->argv[i], strlen(daemonify->argv[i]));
  }
  // cwd
  msgpack_pack_str(&pac, 3);
  msgpack_pack_str_body(&pac, "cwd", 3);
  msgpack_pack_str(&pac, strlen(daemonify->dirbuf));
  msgpack_pack_str_body(&pac, daemonify->dirbuf, strlen(daemonify->dirbuf));
  stream_write_start(&daemonify->socket, daemonify->sbuffer.data,
      daemonify->sbuffer.size,
      start_reading);
  DLOG_SBUFFER("client -> server", daemonify->sbuffer);
  msgpack_sbuffer_clear(&daemonify->sbuffer);
}

static void socket_connect(uv_connect_t* req, int status) {
  Stream *stream = ((uv_stream_t *)req->handle)->data;
  Daemonify *daemonify = stream->daemonify;
  xfree(req);

  if (status) {
    ELOG("failed to connect to \"%s\": %s(%d)", daemonify->socket_addr,
        uv_strerror(status), status);
    adp_exit(daemonify, 255);
    return;
  }

  DLOG("connected, sending process data");
  socket_send_process_data(daemonify);
}

static void init_socket(Daemonify *daemonify) {
  uv_connect_t *req = xmalloc(sizeof(uv_connect_t));
  daemonify->socket.fd = -1;
  daemonify->socket.type = UV_NAMED_PIPE;
  daemonify->socket.daemonify = daemonify;
  daemonify->socket.on_read = socket_read;
  daemonify->socket.on_eof = socket_eof;
  uv_pipe_init(&daemonify->loop, &daemonify->socket.data.pipe, 0);
  daemonify->socket.data.pipe.data = &daemonify->socket;
  daemonify->socket.uv = (uv_stream_t *)&daemonify->socket.data.pipe;
  uv_pipe_connect(req, &daemonify->socket.data.pipe, daemonify->socket_addr,
      socket_connect);
}

static void check_socket(uv_timer_t *timer) {
  uv_fs_t request;
  struct timer_check_data *data = timer->data;

  if (!--data->remaining_tries || !uv_fs_stat(timer->loop, &request,
        data->data.socket_addr, NULL)) {
    uv_timer_stop(timer);
    uv_close((uv_handle_t *)timer, NULL);
  }
  uv_fs_req_cleanup(&request);
}

static bool start_daemon(uv_loop_t *loop, DaemonConfig *config) {
#ifndef _WIN32
  if (!create_directories_for(config->socket_addr))
    return false;
#endif

  daemon_set_defaults(config);

  uv_fs_t req;
  bool pid_file_exists = !uv_fs_stat(loop, &req, config->pid_file_path, NULL);
  uv_fs_req_cleanup(&req);

  if (pid_file_exists) {
    ELOG("pid file \"%s\" already exists, remove it manually",
         config->pid_file_path);
    return false;
  }

  char node_executable[MAXPATHLEN], socket_addr[MAXPATHLEN], eval[MAXPATHLEN];
  char pid_file_path[MAXPATHLEN];
  snprintf(node_executable, sizeof(node_executable), "%s", config->node_executable);
  snprintf(socket_addr, sizeof(socket_addr), "%s", config->socket_addr);
  snprintf(eval, sizeof(eval), "require('%s');", config->daemon_module);
  snprintf(pid_file_path, sizeof(pid_file_path), "%s", config->pid_file_path);

  char *argv[] = {
    node_executable,
    "--eval",
    eval,
    socket_addr,
    pid_file_path,
    NULL
  };

  DLOG("spawning %s --eval \"%s\" \"%s\" \"%s\"", argv[0], eval,
       socket_addr, pid_file_path);
  uv_stdio_container_t stdio[3];
  stdio[0].flags = UV_IGNORE;
  stdio[1].flags = UV_IGNORE;
  stdio[2].flags = UV_IGNORE;

  uv_process_options_t proc_options;
  proc_options.exit_cb = NULL;
  proc_options.file = node_executable;
  proc_options.args = argv;
  proc_options.flags = UV_PROCESS_DETACHED | UV_PROCESS_WINDOWS_HIDE;
  proc_options.stdio_count = 3;
  proc_options.stdio = stdio;
  proc_options.env = NULL;
  proc_options.cwd = NULL;

  uv_process_t proc;
  int st;

  if ((st = uv_spawn(loop, &proc, &proc_options))) {
    ELOG("failed to spawn daemon: %s(%d)", uv_strerror((int)st), st);
    return false;
  }

  uv_unref((uv_handle_t *)&proc);
  uv_close((uv_handle_t *)&proc, NULL);
  struct timer_check_data timer_data = {
    .remaining_tries = 20,
    .data.socket_addr = socket_addr
  };
  uv_timer_t timer;
  uv_timer_init(loop, &timer);
  timer.data = &timer_data;
  uv_timer_start(&timer, check_socket, 50, 100);
  uv_run(loop, UV_RUN_DEFAULT);

  if (!timer_data.remaining_tries) {
    ELOG("daemon did not create socket in time");
    return false;
  }

  return true;
}

static bool ensure_daemon(DaemonConfig *config)
{
  uv_loop_t loop;
  uv_loop_init(&loop);
  uv_fs_t request;
  bool socket_missing = uv_fs_stat(&loop, &request, config->socket_addr, NULL);
  uv_fs_req_cleanup(&request);
  bool rv = true;

  if (socket_missing)
    rv = start_daemon(&loop, config);

  uv_run(&loop, UV_RUN_DEFAULT);
  if (uv_loop_close(&loop)) abort();
  return rv;
}

static bool get_cwd(Daemonify *daemonify) {
  size_t size = sizeof(daemonify->dirbuf);
  int st;
  if ((st = uv_cwd(daemonify->dirbuf, &size))) {
    ELOG("failed to get working directory: %s(%d)", uv_strerror((int)st), st);
    return false;
  }
  return true;
}

int daemonify_main(DaemonConfig *config, const char *module, int argc,
    char **argv) {
  daemon_set_socket(config);

  if (!config->socket_addr || !ensure_daemon(config)) return 255;
  if (!module) return 0;  // only ensure the daemon is running

  Daemonify daemonify;
  memset(&daemonify, 0, sizeof(Daemonify));
  if (!get_cwd(&daemonify)) return 255;
  daemonify.module = module;
  uv_loop_init(&daemonify.loop);
  daemonify.argv = argv;
  daemonify.argc = argc;
  msgpack_sbuffer_init(&daemonify.sbuffer);
  daemonify.unpacker = msgpack_unpacker_new(MSGPACK_UNPACKER_INIT_BUFFER_SIZE);
  daemonify.socket_addr = config->socket_addr;
  init_socket(&daemonify);
  uv_run(&daemonify.loop, UV_RUN_DEFAULT);
  msgpack_sbuffer_destroy(&daemonify.sbuffer);
  msgpack_unpacker_free(daemonify.unpacker);
  if (uv_loop_close(&daemonify.loop)) abort();
  return daemonify.status;
}

static void check_daemon(uv_timer_t *timer) {
  struct timer_check_data *data = timer->data;

  if (!--data->remaining_tries || uv_kill(data->data.pid, 0)) {
    uv_timer_stop(timer);
    uv_close((uv_handle_t *)timer, NULL);
  }
}

int daemonify_kill(DaemonConfig *config) {
  daemon_set_defaults(config);

  if (!config->pid_file_path) {
    ELOG("couldn't determine the pid file path");
    return 255;
  }

  uv_loop_t loop;
  uv_loop_init(&loop);
  uv_fs_t req;
  memset(&req, 0, sizeof(uv_fs_t));

  if (uv_fs_stat(&loop, &req, config->pid_file_path, NULL)) {
    ELOG("pid file doesn't exist");
    goto error;
  }

  uv_fs_req_cleanup(&req);
  int fd = uv_fs_open(&loop, &req, config->pid_file_path, O_RDONLY, 0, NULL);

  if (fd < 0) {
    ELOG("couldn't open pid file \"%s\"", config->pid_file_path);
    goto error;
  }

  uv_fs_req_cleanup(&req);
  char pidbuf[256];
  uv_buf_t uvbuf = {.base = pidbuf, .len = sizeof(pidbuf)};
  ssize_t read = uv_fs_read(&loop, &req, fd, &uvbuf, 1, -1, NULL);

  if (read <= 0) {
    ELOG("failed to read file \"%s\"", config->pid_file_path);
    goto error;
  }

  pidbuf[read] = 0;  // terminate string
  uv_fs_req_cleanup(&req);
  uv_fs_close(&loop, &req, fd, NULL);
  uv_fs_req_cleanup(&req);
  char *endptr;
  long pid = strtol(pidbuf, &endptr, 10);

  if (*endptr != '\0' || pid > INT_MAX || uv_kill((int)pid, SIGTERM)) {
    ELOG("pid file \"%s\" doesn't contain a valid pid", config->pid_file_path);
    goto error;
  }

  struct timer_check_data timer_data = {
    .remaining_tries = 20,
    .data.pid = (int)pid
  };

  // start a timer to check if the process exits correctly
  uv_timer_t timer;
  uv_timer_init(&loop, &timer);
  timer.data = &timer_data;
  uv_timer_start(&timer, check_daemon, 50, 100);
  uv_run(&loop, UV_RUN_DEFAULT);

  if (!timer_data.remaining_tries) {
    ELOG("daemon took too long to exit, probably because some worker is "
         "still doing its job. you can kill the daemon immediately with "
         "kill -9 %d", (int)pid);
    goto error;
  }

  if (uv_loop_close(&loop)) abort();
  return 0;

error:
  uv_fs_req_cleanup(&req);
  if (uv_loop_close(&loop)) abort();
  return 255;
}
