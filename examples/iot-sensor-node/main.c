/*
 * IoT Sensor Node — Example using the complete micro-toolkit.
 *
 * This example simulates a temperature/humidity sensor node that:
 *
 *   1. Boots and loads config from flash (microconf)
 *   2. Manages device lifecycle with a state machine (microfsm)
 *   3. Buffers sensor events from ISR via ring buffer (micoring)
 *   4. Retries MQTT connections with exponential backoff (microres)
 *   5. Encodes telemetry in compact CBOR format (microcbor)
 *   6. Logs everything with structured logging (microlog)
 *   7. Provides a debug shell for runtime inspection (microsh)
 *
 * This is a simulation that runs on Linux/macOS for demonstration.
 * On real hardware, replace the platform stubs with actual HAL calls.
 *
 * Build:
 *   gcc -std=c99 -Wall -Wextra \
 *       -I../../microfsm/include -I../../microres/include \
 *       -I../../microconf/include -I../../microlog/include \
 *       -I../../microsh/include -I../../microcbor/include \
 *       -I../../micoring/include \
 *       ../../microfsm/src/mfsm.c ../../microres/src/mres.c \
 *       ../../microconf/src/mconf.c ../../microlog/src/mlog.c \
 *       ../../microsh/src/msh.c ../../microcbor/src/mcbor.c \
 *       ../../micoring/src/mring.c \
 *       main.c -o sensor_node
 *
 * Run:
 *   ./sensor_node
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── Include the entire toolkit ────────────────────────────────────────── */

#include "mfsm.h"    /* State machine */
#include "mres.h"    /* Resilience (retry, circuit breaker) */
#include "mconf.h"   /* Configuration */
#include "mlog.h"    /* Logging */
#include "msh.h"     /* Debug shell */
#include "mcbor.h"   /* CBOR serialization */
#include "mring.h"   /* Ring buffer */

/* ═══════════════════════════════════════════════════════════════════════════
 * Platform stubs (replace with HAL on real hardware)
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint32_t platform_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void platform_sleep(uint32_t ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000 };
    nanosleep(&ts, NULL);
}

/* Simulated flash storage */
static uint8_t fake_flash[512];

static int flash_read(uint32_t offset, void *buf, uint32_t len)
{
    memcpy(buf, fake_flash + offset, len);
    return 0;
}

static int flash_write(uint32_t offset, const void *buf, uint32_t len)
{
    memcpy(fake_flash + offset, buf, len);
    return 0;
}

static const mconf_io_t flash_io = {
    .read = flash_read, .write = flash_write, .erase = NULL
};

/* Shell output */
static void shell_print(const char *str, void *ctx)
{
    (void)ctx;
    fputs(str, stdout);
    fflush(stdout);
}

/* Log output */
static void log_write(const char *buf, uint16_t len, mlog_level_t level,
                       void *ctx)
{
    (void)len; (void)level; (void)ctx;
    fputs(buf, stderr);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration (microconf)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char     mqtt_host[48];
    uint16_t mqtt_port;
    bool     mqtt_tls;
    char     device_id[24];
    uint32_t report_interval_ms;
    float    temp_offset;
    uint8_t  log_level;
} app_config_t;

static const char     DEF_HOST[]     = "broker.local";
static const uint16_t DEF_PORT       = 1883;
static const bool     DEF_TLS        = false;
static const char     DEF_DEVICE[]   = "sensor-01";
static const uint32_t DEF_INTERVAL   = 5000;
static const float    DEF_OFFSET     = 0.0f;
static const uint8_t  DEF_LOG        = 2;

enum {
    CFG_HOST, CFG_PORT, CFG_TLS, CFG_DEVICE, CFG_INTERVAL,
    CFG_OFFSET, CFG_LOG, CFG_COUNT
};

static const mconf_entry_t config_entries[] = {
    MCONF_ENTRY(app_config_t, mqtt_host,          MCONF_TYPE_STR,   DEF_HOST),
    MCONF_ENTRY(app_config_t, mqtt_port,          MCONF_TYPE_U16,   &DEF_PORT),
    MCONF_ENTRY(app_config_t, mqtt_tls,           MCONF_TYPE_BOOL,  &DEF_TLS),
    MCONF_ENTRY(app_config_t, device_id,          MCONF_TYPE_STR,   DEF_DEVICE),
    MCONF_ENTRY(app_config_t, report_interval_ms, MCONF_TYPE_U32,   &DEF_INTERVAL),
    MCONF_ENTRY(app_config_t, temp_offset,        MCONF_TYPE_FLOAT, &DEF_OFFSET),
    MCONF_ENTRY(app_config_t, log_level,          MCONF_TYPE_U8,    &DEF_LOG),
};

static const mconf_schema_t config_schema = {
    .entries     = config_entries,
    .num_entries = CFG_COUNT,
    .version     = 1,
    .data_size   = sizeof(app_config_t),
};

/* ═══════════════════════════════════════════════════════════════════════════
 * State machine (microfsm)
 * ═══════════════════════════════════════════════════════════════════════════ */

enum {
    ST_INIT, ST_CONNECTING, ST_ONLINE, ST_PUBLISHING,
    ST_BACKOFF, ST_ERROR, ST_COUNT
};

enum {
    EV_BOOT_DONE, EV_CONNECTED, EV_PUBLISH, EV_PUB_ACK,
    EV_DISCONNECT, EV_RETRY, EV_FATAL
};

/* Forward declarations */
typedef struct app_context app_ctx_t;
static void on_enter_connecting(void *ctx);
static void on_enter_online(void *ctx);
static void on_enter_backoff(void *ctx);
static void on_enter_error(void *ctx);
static bool guard_can_retry(void *ctx);

static const mfsm_state_t fsm_states[] = {
    [ST_INIT]       = { .name = "INIT"       },
    [ST_CONNECTING] = { .name = "CONNECTING", .on_enter = on_enter_connecting },
    [ST_ONLINE]     = { .name = "ONLINE",     .on_enter = on_enter_online     },
    [ST_PUBLISHING] = { .name = "PUBLISHING"  },
    [ST_BACKOFF]    = { .name = "BACKOFF",    .on_enter = on_enter_backoff    },
    [ST_ERROR]      = { .name = "ERROR",      .on_enter = on_enter_error      },
};

static const mfsm_transition_t fsm_transitions[] = {
    { ST_INIT,       EV_BOOT_DONE,  ST_CONNECTING, NULL,            NULL },
    { ST_CONNECTING, EV_CONNECTED,  ST_ONLINE,     NULL,            NULL },
    { ST_ONLINE,     EV_PUBLISH,    ST_PUBLISHING, NULL,            NULL },
    { ST_PUBLISHING, EV_PUB_ACK,   ST_ONLINE,     NULL,            NULL },
    { ST_CONNECTING, EV_DISCONNECT, ST_BACKOFF,    NULL,            NULL },
    { ST_ONLINE,     EV_DISCONNECT, ST_BACKOFF,    NULL,            NULL },
    { ST_PUBLISHING, EV_DISCONNECT, ST_BACKOFF,    NULL,            NULL },
    { ST_BACKOFF,    EV_RETRY,      ST_CONNECTING, guard_can_retry, NULL },
    { ST_BACKOFF,    EV_RETRY,      ST_ERROR,      NULL,            NULL },
};

static const mfsm_def_t fsm_def = {
    .states          = fsm_states,
    .num_states      = ST_COUNT,
    .transitions     = fsm_transitions,
    .num_transitions = sizeof(fsm_transitions) / sizeof(fsm_transitions[0]),
    .initial         = ST_INIT,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Application context
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t event_type;
    float   value;
} sensor_event_t;

struct app_context {
    /* Subsystems */
    mfsm_t          fsm;
    mres_breaker_t  breaker;
    app_config_t    config;
    msh_t           shell;

    /* Sensor event ring buffer */
    mring_t         event_ring;
    uint8_t         event_storage[8 * sizeof(sensor_event_t)];

    /* State */
    int             retry_count;
    uint32_t        last_publish;
    int             publish_count;
};

static app_ctx_t app;

/* ═══════════════════════════════════════════════════════════════════════════
 * FSM callbacks
 * ═══════════════════════════════════════════════════════════════════════════ */

static void on_enter_connecting(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    MLOG_INFO("FSM", "Connecting to %s:%d ...",
              a->config.mqtt_host, a->config.mqtt_port);
}

static void on_enter_online(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    a->retry_count = 0;
    MLOG_INFO("FSM", "Online and ready (device: %s)", a->config.device_id);
}

static void on_enter_backoff(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    a->retry_count++;
    MLOG_WARN("FSM", "Connection lost, retry %d/5", a->retry_count);
}

static void on_enter_error(void *ctx)
{
    (void)ctx;
    MLOG_ERROR("FSM", "%s", "Fatal: max retries exceeded");
}

static bool guard_can_retry(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    return a->retry_count < 5;
}

/* FSM trace → microlog */
static void fsm_trace(mfsm_state_id from, mfsm_event_id event,
                       mfsm_state_id to, void *ctx)
{
    (void)ctx;
    MLOG_DEBUG("FSM", "%s --(%d)--> %s",
               fsm_states[from].name, event, fsm_states[to].name);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Simulated MQTT publish (with resilience)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int simulate_mqtt_publish(void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    /* Simulate: 80% success rate */
    int r = rand() % 10;
    if (r < 8) {
        a->publish_count++;
        return 0;
    }
    return -1;  /* simulated failure */
}

static void publish_telemetry(app_ctx_t *a, float temp, float hum)
{
    /* Encode as CBOR */
    uint8_t cbor_buf[64];
    mcbor_enc_t enc;
    mcbor_enc_init(&enc, cbor_buf, sizeof(cbor_buf));

    mcbor_enc_map(&enc, 4);
    mcbor_enc_str(&enc, "device"); mcbor_enc_str(&enc, a->config.device_id);
    mcbor_enc_str(&enc, "temp");   mcbor_enc_float(&enc, temp + a->config.temp_offset);
    mcbor_enc_str(&enc, "hum");    mcbor_enc_float(&enc, hum);
    mcbor_enc_str(&enc, "ts");     mcbor_enc_uint(&enc, platform_clock() / 1000);

    uint32_t cbor_size = mcbor_enc_size(&enc);
    MLOG_DEBUG("CBOR", "Encoded %lu bytes (JSON would be ~60)", (unsigned long)cbor_size);

    /* Publish through circuit breaker */
    int result = mres_breaker_call(&a->breaker, simulate_mqtt_publish, a,
                                    platform_clock);

    if (result == MRES_OK) {
        MLOG_INFO("MQTT", "Published #%d (%.1f°C, %.0f%%)",
                  a->publish_count, temp, hum);
    } else if (result == MRES_ERR_OPEN) {
        uint32_t wait = mres_breaker_remaining_ms(&a->breaker, platform_clock);
        MLOG_WARN("MQTT", "Breaker OPEN, retry in %lu ms", (unsigned long)wait);
    } else {
        MLOG_ERROR("MQTT", "Publish failed: %d", result);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Shell commands (microsh)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int cmd_status(int argc, const char **argv, void *ctx)
{
    (void)argc; (void)argv;
    app_ctx_t *a = (app_ctx_t *)ctx;
    char buf[128];

    snprintf(buf, sizeof(buf),
             "State:     %s\r\n"
             "Breaker:   %s (%d failures)\r\n"
             "Published: %d messages\r\n"
             "Events:    %lu/%lu in ring\r\n",
             mfsm_state_name(&a->fsm),
             mres_breaker_state_name(&a->breaker),
             a->breaker.failure_count,
             a->publish_count,
             (unsigned long)mring_count(&a->event_ring),
             (unsigned long)mring_capacity(&a->event_ring));
    shell_print(buf, NULL);
    return 0;
}

static int cmd_conf(int argc, const char **argv, void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    char buf[128];

    if (argc < 2) {
        shell_print("Usage: conf list | conf get <key>\r\n", NULL);
        return 0;
    }

    if (strcmp(argv[1], "list") == 0) {
        for (uint8_t i = 0; i < config_schema.num_entries; i++) {
            snprintf(buf, sizeof(buf), "  [%d] %s (%s)\r\n",
                     i, config_entries[i].key,
                     mconf_type_name(config_entries[i].type));
            shell_print(buf, NULL);
        }
        return 0;
    }

    if (strcmp(argv[1], "get") == 0 && argc >= 3) {
        int idx = mconf_find(&config_schema, argv[2]);
        if (idx < 0) {
            shell_print("Key not found\r\n", NULL);
            return -1;
        }

        const mconf_entry_t *e = &config_entries[idx];
        switch (e->type) {
        case MCONF_TYPE_STR: {
            char val[64];
            mconf_get_str(&config_schema, &a->config, (uint8_t)idx, val, sizeof(val));
            snprintf(buf, sizeof(buf), "%s = \"%s\"\r\n", argv[2], val);
            break;
        }
        case MCONF_TYPE_U16: {
            uint16_t val;
            mconf_get_u16(&config_schema, &a->config, (uint8_t)idx, &val);
            snprintf(buf, sizeof(buf), "%s = %u\r\n", argv[2], val);
            break;
        }
        case MCONF_TYPE_U32: {
            uint32_t val;
            mconf_get_u32(&config_schema, &a->config, (uint8_t)idx, &val);
            snprintf(buf, sizeof(buf), "%s = %lu\r\n", argv[2], (unsigned long)val);
            break;
        }
        case MCONF_TYPE_FLOAT: {
            float val;
            mconf_get_float(&config_schema, &a->config, (uint8_t)idx, &val);
            snprintf(buf, sizeof(buf), "%s = %.3f\r\n", argv[2], (double)val);
            break;
        }
        case MCONF_TYPE_BOOL: {
            bool val;
            mconf_get_bool(&config_schema, &a->config, (uint8_t)idx, &val);
            snprintf(buf, sizeof(buf), "%s = %s\r\n", argv[2], val ? "true" : "false");
            break;
        }
        default:
            snprintf(buf, sizeof(buf), "%s = (unsupported type)\r\n", argv[2]);
            break;
        }
        shell_print(buf, NULL);
        return 0;
    }

    shell_print("Usage: conf list | conf get <key>\r\n", NULL);
    return 0;
}

static int cmd_log_level(int argc, const char **argv, void *ctx)
{
    (void)ctx;
    if (argc < 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Current level: %s\r\n",
                 mlog_level_name(mlog_global()->global_level));
        shell_print(buf, NULL);
        return 0;
    }

    mlog_level_t level = MLOG_INFO;
    if      (strcmp(argv[1], "trace") == 0) level = MLOG_TRACE;
    else if (strcmp(argv[1], "debug") == 0) level = MLOG_DEBUG;
    else if (strcmp(argv[1], "info")  == 0) level = MLOG_INFO;
    else if (strcmp(argv[1], "warn")  == 0) level = MLOG_WARN;
    else if (strcmp(argv[1], "error") == 0) level = MLOG_ERROR;
    else {
        shell_print("Levels: trace, debug, info, warn, error\r\n", NULL);
        return -1;
    }

    mlog_set_level(NULL, level);
    char buf[64];
    snprintf(buf, sizeof(buf), "Log level set to %s\r\n", mlog_level_name(level));
    shell_print(buf, NULL);
    return 0;
}

static int cmd_breaker(int argc, const char **argv, void *ctx)
{
    app_ctx_t *a = (app_ctx_t *)ctx;
    char buf[128];

    if (argc >= 2 && strcmp(argv[1], "reset") == 0) {
        mres_breaker_reset(&a->breaker);
        shell_print("Breaker reset to CLOSED\r\n", NULL);
        return 0;
    }

    snprintf(buf, sizeof(buf), "Breaker: %s (failures: %d/%d)\r\n",
             mres_breaker_state_name(&a->breaker),
             a->breaker.failure_count,
             a->breaker.policy->failure_threshold);
    shell_print(buf, NULL);

    if (a->breaker.state == MRES_BREAKER_OPEN) {
        uint32_t remaining = mres_breaker_remaining_ms(&a->breaker, platform_clock);
        snprintf(buf, sizeof(buf), "Recovery in: %lu ms\r\n", (unsigned long)remaining);
        shell_print(buf, NULL);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    srand((unsigned)time(NULL));
    memset(&app, 0, sizeof(app));
    memset(fake_flash, 0xFF, sizeof(fake_flash));

    /* ── 1. Init logging (microlog) ────────────────────────────────────── */

    mlog_t *log = mlog_global();
    mlog_init(log);
    mlog_set_clock(log, platform_clock);

    mlog_backend_t log_be = {
        .write = log_write,
        .ctx   = NULL,
        .level = MLOG_DEBUG,
        .color = true,
    };
    mlog_add_backend(log, &log_be);

    MLOG_INFO("BOOT", "%s", "=== IoT Sensor Node starting ===");

    /* ── 2. Load config (microconf) ────────────────────────────────────── */

    mconf_err_t conf_err = mconf_load(&config_schema, &app.config, &flash_io);
    if (conf_err == MCONF_OK) {
        MLOG_INFO("CONF", "%s", "Config loaded from flash");
    } else {
        MLOG_WARN("CONF", "Using defaults (%s)", mconf_err_str(conf_err));
    }

    MLOG_INFO("CONF", "Host: %s:%d, Device: %s, Interval: %lu ms",
              app.config.mqtt_host, app.config.mqtt_port,
              app.config.device_id,
              (unsigned long)app.config.report_interval_ms);

    /* ── 3. Init ring buffer (micoring) ────────────────────────────────── */

    mring_init(&app.event_ring, app.event_storage, 8, sizeof(sensor_event_t));
    MLOG_DEBUG("RING", "Event ring: %lu slots", (unsigned long)mring_capacity(&app.event_ring));

    /* ── 4. Init state machine (microfsm) ──────────────────────────────── */

    mfsm_validate(&fsm_def);
    mfsm_init(&app.fsm, &fsm_def, &app);
    mfsm_set_trace(&app.fsm, fsm_trace);

    /* ── 5. Init circuit breaker (microres) ────────────────────────────── */

    static const mres_breaker_policy_t breaker_policy = {
        .failure_threshold   = 3,
        .recovery_timeout_ms = 10000,
        .half_open_max_calls = 1,
    };
    mres_breaker_init(&app.breaker, &breaker_policy);

    /* ── 6. Init debug shell (microsh) ─────────────────────────────────── */

    msh_init(&app.shell, shell_print, &app);
    msh_register(&app.shell, "status",  "Show device status",    cmd_status);
    msh_register(&app.shell, "conf",    "Config: list | get <key>", cmd_conf);
    msh_register(&app.shell, "log",     "Set log level",         cmd_log_level);
    msh_register(&app.shell, "breaker", "Circuit breaker status", cmd_breaker);

    /* ── 7. Boot sequence ──────────────────────────────────────────────── */

    MLOG_INFO("BOOT", "%s", "All subsystems initialized");
    mfsm_dispatch(&app.fsm, EV_BOOT_DONE);

    /* Simulate successful connection */
    platform_sleep(500);
    mfsm_dispatch(&app.fsm, EV_CONNECTED);

    /* ── 8. Main loop (simulation) ─────────────────────────────────────── */

    MLOG_INFO("MAIN", "%s", "Entering main loop (5 cycles)...");
    printf("\n");

    for (int cycle = 0; cycle < 5; cycle++) {
        /* Simulate sensor readings */
        float temp = 20.0f + (float)(rand() % 100) / 10.0f;
        float hum  = 40.0f + (float)(rand() % 400) / 10.0f;

        /* Push to ring buffer (simulating ISR) */
        sensor_event_t evt = { .event_type = EV_PUBLISH, .value = temp };
        mring_push(&app.event_ring, &evt);

        /* Process events from ring */
        sensor_event_t out;
        while (mring_pop(&app.event_ring, &out) == MRING_OK) {
            mfsm_dispatch(&app.fsm, EV_PUBLISH);
            publish_telemetry(&app, temp, hum);
            mfsm_dispatch(&app.fsm, EV_PUB_ACK);
        }

        platform_sleep(1000);
    }

    /* ── 9. Save config before shutdown ────────────────────────────────── */

    mconf_save(&config_schema, &app.config, &flash_io);
    MLOG_INFO("CONF", "%s", "Config saved to flash");

    /* ── 10. Show final status ─────────────────────────────────────────── */

    printf("\n");
    MLOG_INFO("MAIN", "%s", "=== Simulation complete ===");
    MLOG_INFO("MAIN", "Final state: %s", mfsm_state_name(&app.fsm));
    MLOG_INFO("MAIN", "Published: %d messages", app.publish_count);
    MLOG_INFO("MAIN", "Breaker: %s", mres_breaker_state_name(&app.breaker));

    /* ── Show shell demo ───────────────────────────────────────────────── */

    printf("\n--- Shell demo ---\n");
    msh_prompt(&app.shell);
    msh_exec(&app.shell, "status");
    printf("\n");
    msh_exec(&app.shell, "conf list");
    printf("\n");
    msh_exec(&app.shell, "conf get mqtt_host");
    msh_exec(&app.shell, "conf get report_interval_ms");
    msh_exec(&app.shell, "breaker");

    return 0;
}
