#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <env.h>
#include <uv.h>
#include <forza.h>

static uv_timer_t uptime_timer;
static time_t start_time;

static char* lib_path;

static unsigned short* ports;
static size_t port_count = 0;

#define PATHMAX 1024

void uptime__process_options_cb(uv_process_options_t* options) {
#if (__APPLE__ && __MACH__)
  options->env = env_set(options->env, "DYLD_FORCE_FLAT_NAMESPACE", "1");
  options->env = env_set(options->env, "DYLD_INSERT_LIBRARIES", lib_path);
#else
  options->env = env_set(options->env, "LD_PRELOAD", lib_path);
#endif
}

void uptime__on_ipc_data(char* data) {
  unsigned short port;

  if (sscanf(data, "port=%hu\n", &port) == 1) {
    ++port_count;
    ports = realloc(ports, sizeof(*ports) * port_count);
    ports[port_count - 1] = port;
  }
}

void uptime__send_uptime(uv_timer_t *timer, int status) {
  forza_metric_t* metric;
  time_t now = time(NULL);
  int i;

  for (i = 0; i < port_count; i++) {
    metric = forza_new_metric();
    metric->service = "health/process/uptime";
    metric->metric = (double) (now - start_time);
    metric->meta->port = ports[i];
    forza_send(metric);
    forza_free_metric(metric);
  }
}

void uptime__process_exit_cb(int exit_status, int term_singal) {
  uv_timer_stop(&uptime_timer);
}

int uptime_init(forza_plugin_t* plugin) {
  size_t size = PATHMAX / sizeof(*lib_path);
  uv_loop_t* loop = uv_default_loop();

  start_time = time(NULL);

  //
  // Assuming nobody will run this in the last second of New Year's Eve in 1969.
  //
  if (start_time == -1) {
    return -1;
  }

  lib_path = malloc(size);

  if (uv_exepath(lib_path, &size) != 0) {
    fprintf(stderr, "uv_exepath: %s\n", uv_strerror(uv_last_error(loop)));
    return 1;
  }

#if (__APPLE__ && __MACH__)
  strcpy(strrchr(lib_path, '/') + 1, "libinterposed.dylib");
#else
  strcpy(strrchr(lib_path, '/') + 1, "libinterposed.so");
#endif


  plugin->process_exit_cb = uptime__process_exit_cb;
  plugin->process_options_cb = uptime__process_options_cb;
  plugin->ipc_data_cb = uptime__on_ipc_data;

  uv_timer_init(uv_default_loop(), &uptime_timer);
  uv_timer_start(&uptime_timer, uptime__send_uptime, 0, 5000);

  return 0;
}
