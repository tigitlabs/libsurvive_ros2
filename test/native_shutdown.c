// SPDX-License-Identifier: MIT
// Exercise the real native cleanup with a synthetic driver, without USB hardware.
#define _GNU_SOURCE
#define SURVIVE_ENABLE_FULL_API
#include <libsurvive/survive.h>
#include <libsurvive/survive_api.h>
#include <survive_default_devices.h>

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void (*native_context_lock)(SurviveContext *);
static sem_t button_lock_attempted;
static atomic_int observe_button_lock;
static atomic_int button_callbacks;

static void require(int condition, const char * message)
{
  if (!condition) {
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
  }
}

static int synthetic_driver(SurviveContext * ctx)
{
  SurviveObject * object = survive_create_device(ctx, "TST", NULL, "TS0", NULL);
  require(object != NULL, "Cannot create synthetic device");
  survive_add_object(ctx, object);
  return 0;
}

// Interpose the exported function to observe the real button service after its
// RUNNING check, immediately before it waits on the held context semaphore.
// This scheduling barrier replaces timing-dependent sleeps in the regression.
void survive_get_ctx_lock(SurviveContext * ctx)
{
  // os_generic's POSIX thread handle is a pointer to pthread_t. Checking the
  // handle avoids depending on optional upstream thread-name support.
  if (atomic_load(&observe_button_lock) && ctx->buttonservicethread != NULL &&
    pthread_equal(pthread_self(), *(pthread_t *)ctx->buttonservicethread) &&
    atomic_exchange(&observe_button_lock, 0))
  {
    sem_post(&button_lock_attempted);
  }
  native_context_lock(ctx);
}

static void count_button(
  SurviveObject * object, enum SurviveInputEvent event, enum SurviveButton button,
  const enum SurviveAxis * axes, const SurviveAxisVal_t * values)
{
  (void)object;
  (void)event;
  (void)button;
  (void)axes;
  (void)values;
  atomic_fetch_add(&button_callbacks, 1);
}

static void queue_button(SurviveContext * ctx)
{
  ButtonQueueEntry * event = &ctx->buttonQueue.entry[0];
  event->isPopulated = 1;
  event->so = ctx->objs[0];
  event->eventType = SURVIVE_INPUT_EVENT_BUTTON_DOWN;
  ctx->buttonQueue.nextWriteIndex = 1;
  atomic_store(&observe_button_lock, 1);
  sem_post(ctx->buttonQueue.buttonservicesem);

  struct timespec deadline;
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += 1;
  int result;
  do {
    result = sem_timedwait(&button_lock_attempted, &deadline);
  } while (result != 0 && errno == EINTR);
  require(result == 0, "Button service did not reach the context-lock barrier");
}

int main(int argc, char ** argv)
{
  require(argc == 3, "Expected idle|queued|polling and a temporary config prefix");
  const int queued = strcmp(argv[1], "queued") == 0;
  const int polling = strcmp(argv[1], "polling") == 0;
  require(queued || polling || strcmp(argv[1], "idle") == 0, "Unknown test case");

  void * symbol = dlsym(RTLD_NEXT, "survive_get_ctx_lock");
  require(symbol != NULL, "Native context-lock function was not found");
  _Static_assert(sizeof(symbol) == sizeof(native_context_lock), "POSIX function pointer size");
  memcpy(&native_context_lock, &symbol, sizeof(symbol));
  require(sem_init(&button_lock_attempted, 0, 0) == 0, "Cannot initialize test barrier");
  RegisterDriver("DriverRegShutdownTest", (survive_driver_fn)synthetic_driver);

  for (int iteration = 0; iteration < 10; ++iteration) {
    char config[4096];
    const int size = snprintf(config, sizeof(config), "%s-%d.json", argv[2], iteration);
    require(size > 0 && (size_t)size < sizeof(config), "Temporary config path is too long");
    remove(config);
    char * args[] = {
      "native-shutdown-test", "--shutdowntest", "1", "--htcvive", "0",
      "--threaded-posers", "0",
      "--disable-calibrate", "1", "--configfile", config,
      "--init-configfile", config, "--v", "0"
    };
    SurviveSimpleContext * simple = survive_simple_init(sizeof(args) / sizeof(args[0]), args);
    require(simple != NULL, "Cannot initialize synthetic driver");
    SurviveContext * ctx = survive_simple_get_ctx(simple);
    require(ctx->objs_ct == 1, "Expected exactly one synthetic object");
    survive_install_button_fn(ctx, count_button);
    atomic_store(&button_callbacks, 0);

    if (queued) {
      queue_button(ctx);
    } else if (polling) {
      survive_simple_start_thread(simple);
      usleep(20000);
    }

    survive_simple_close(simple);
    require(atomic_load(&button_callbacks) == 0, "A button callback ran during close");
    remove(config);
  }
  sem_destroy(&button_lock_attempted);
  printf("Completed 10 %s close cycles\n", argv[1]);
  return EXIT_SUCCESS;
}
