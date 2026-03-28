/*
 * iot-sensor-node — Kompletný ESP32 príklad so VŠETKÝMI 9 micro-toolkit knižnicami.
 *
 * Toto je SKUTOČNÝ príklad — používa reálne API z tvojich GitHub repozitárov:
 *
 *   microconf   → struct-based config s MCONF_ENTRY makrami + CRC flash storage
 *   microfsm    → mfsm_def_t + mfsm_t, table-driven FSM s guard/action callbackmi
 *   micoring    → ISR-safe SPSC ring buffer pre DHT22 senzorové eventy
 *   microres    → mres_breaker_call(op_fn) + mres_breaker_report_* + retry policy
 *   microcbor   → mcbor_enc_str/float/uint + mcbor_enc_size + mcbor_enc_overflow
 *   microlog    → mlog_t + mlog_add_backend + MLOG_INFO/DEBUG makrá (global logger)
 *   microsh     → msh_init + msh_register (per-command), msh_cmd_fn(argc, argv, ctx)
 *   microtimer  → mtimer_create + mtimer_start, callback(timer_id, ctx)
 *   microbus    → mbus_signal/queue_signal/publish(payload), mbus_dispatch
 *
 * Build (Linux):  make -f Makefile.sim && ./sensor_node
 * Build (ESP32):  idf.py build && idf.py flash monitor
 *
 * SPDX-License-Identifier: MIT
 * https://github.com/Vanderhell/micro-toolkit
 */

/* ═══════════════════════════════════════════════════════════════════════════
 * Platform detection
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifdef ESP_PLATFORM
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "esp_timer.h"
  #include "nvs_flash.h"
  #define PLATFORM_ESP32
#else
  #define _POSIX_C_SOURCE 200809L
  #include <time.h>
  #include <unistd.h>
  #define PLATFORM_LINUX
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ── Všetkých 9 knižníc ─────────────────────────────────────────────────── */
#include "mfsm.h"
#include "mres.h"
#include "mconf.h"
#include "mlog.h"
#include "msh.h"
#include "mcbor.h"
#include "mring.h"
#include "mtimer.h"
#include "mbus.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Platform abstraction
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint32_t plat_now_ms(void)
{
#ifdef PLATFORM_ESP32
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

static void plat_sleep_ms(uint32_t ms)
{
#ifdef PLATFORM_ESP32
    vTaskDelay(pdMS_TO_TICKS(ms));
#else
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

/* ═══════════════════════════════════════════════════════════════════════════
 * microlog — backend setup
 * ═══════════════════════════════════════════════════════════════════════════ */

static void shell_print(const char *str, void *ctx) { (void)ctx; fputs(str, stdout); }

static void stdout_write(const char *buf, uint16_t len, mlog_level_t level, void *ctx)
{
    (void)len; (void)level; (void)ctx;
    fputs(buf, stdout);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * microconf — config struct + schema
 *
 * Real API: MCONF_ENTRY macro maps struct fields by offset.
 * Access: mconf_find(schema, "key") → index, then mconf_get_u32(s, d, idx, &out)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char     mqtt_host[32];
    uint32_t mqtt_port;
    char     device_id[32];
    uint32_t report_ms;
    int32_t  temp_offset;
} node_config_t;

/* Default values — pointers required by MCONF_ENTRY */
static const char     CFG_DEF_MQTT_HOST[]  = "broker.local";
static const char     CFG_DEF_DEVICE_ID[]  = "esp32-node-01";
static const uint32_t CFG_DEF_MQTT_PORT    = 1883;
static const uint32_t CFG_DEF_REPORT_MS    = 5000;
static const int32_t  CFG_DEF_TEMP_OFFSET  = 0;

static const mconf_entry_t g_entries[] = {
    MCONF_ENTRY(node_config_t, mqtt_host,   MCONF_TYPE_STR, CFG_DEF_MQTT_HOST),
    MCONF_ENTRY(node_config_t, mqtt_port,   MCONF_TYPE_U32, &CFG_DEF_MQTT_PORT),
    MCONF_ENTRY(node_config_t, device_id,   MCONF_TYPE_STR, CFG_DEF_DEVICE_ID),
    MCONF_ENTRY(node_config_t, report_ms,   MCONF_TYPE_U32, &CFG_DEF_REPORT_MS),
    MCONF_ENTRY(node_config_t, temp_offset, MCONF_TYPE_I32, &CFG_DEF_TEMP_OFFSET),
};

static const mconf_schema_t g_schema = {
    .entries     = g_entries,
    .num_entries = 5,
    .version     = 1,
    .data_size   = sizeof(node_config_t),
};

/* Simulated flash storage */
static uint8_t g_flash[512];

static int flash_read (uint32_t off, void *buf, uint32_t len) { memcpy(buf, g_flash+off, len); return 0; }
static int flash_write(uint32_t off, const void *buf, uint32_t len) { memcpy(g_flash+off, buf, len); return 0; }
static int flash_erase(uint32_t off, uint32_t len) { memset(g_flash+off, 0xFF, len); return 0; }

static const mconf_io_t g_flash_io = {
    .read  = flash_read,
    .write = flash_write,
    .erase = flash_erase,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * microfsm — state machine
 *
 * Real API: mfsm_def_t (const, ROM) + mfsm_t (instance, RAM)
 * Callbacks: mfsm_action_fn(void *user_data) — no mfsm_t* param!
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum { ST_BOOT, ST_CONNECTING, ST_ONLINE, ST_PUBLISHING, ST_ERROR, ST_COUNT } app_state_t;
typedef enum { EV_BOOT_DONE, EV_WIFI_UP, EV_MQTT_UP, EV_PUB_OK, EV_PUB_FAIL, EV_BREAKER_OPEN, EV_RECOVER } app_event_t;

/* Forward declare app_t for callbacks */
typedef struct app_s app_t;

static void on_enter_boot      (void *u) { (void)u; MLOG_INFO("FSM","%s","→ BOOT");       }
static void on_enter_connecting(void *u) { (void)u; MLOG_INFO("FSM","%s","→ CONNECTING"); }
static void on_enter_online    (void *u) { (void)u; MLOG_INFO("FSM","%s","→ ONLINE");     }
static void on_enter_publishing(void *u) { (void)u; MLOG_DEBUG("FSM","%s","→ PUBLISHING"); }
static void on_enter_error     (void *u) { (void)u; MLOG_ERROR("FSM","%s","→ ERROR: circuit breaker OPEN"); }

static const mfsm_state_t g_states[ST_COUNT] = {
    [ST_BOOT]       = { .on_enter = on_enter_boot,       .name = "BOOT"       },
    [ST_CONNECTING] = { .on_enter = on_enter_connecting, .name = "CONNECTING" },
    [ST_ONLINE]     = { .on_enter = on_enter_online,     .name = "ONLINE"     },
    [ST_PUBLISHING] = { .on_enter = on_enter_publishing, .name = "PUBLISHING" },
    [ST_ERROR]      = { .on_enter = on_enter_error,      .name = "ERROR"      },
};

static const mfsm_transition_t g_transitions[] = {
    { ST_BOOT,       EV_BOOT_DONE,    ST_CONNECTING, NULL, NULL },
    { ST_CONNECTING, EV_WIFI_UP,      ST_CONNECTING, NULL, NULL },
    { ST_CONNECTING, EV_MQTT_UP,      ST_ONLINE,     NULL, NULL },
    { ST_ONLINE,     EV_PUB_OK,       ST_PUBLISHING, NULL, NULL },
    { ST_ONLINE,     EV_PUB_FAIL,     ST_ONLINE,     NULL, NULL },
    { ST_PUBLISHING, EV_PUB_OK,       ST_ONLINE,     NULL, NULL },
    { ST_PUBLISHING, EV_PUB_FAIL,     ST_ONLINE,     NULL, NULL },
    { ST_ONLINE,     EV_BREAKER_OPEN, ST_ERROR,      NULL, NULL },
    { ST_PUBLISHING, EV_BREAKER_OPEN, ST_ERROR,      NULL, NULL },
    { ST_ERROR,      EV_RECOVER,      ST_CONNECTING, NULL, NULL },
};

static const mfsm_def_t g_fsm_def = {
    .states          = g_states,
    .num_states      = ST_COUNT,
    .transitions     = g_transitions,
    .num_transitions = sizeof(g_transitions) / sizeof(g_transitions[0]),
    .initial         = ST_BOOT,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Sensor event (ISR → micoring ring buffer → main loop)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint32_t ts_ms;
    float    temp;
    float    hum;
    uint8_t  seq;
} sensor_event_t;

static sensor_event_t g_ring_buf[8];  /* power of 2 */
static mring_t        g_ring;

/* ═══════════════════════════════════════════════════════════════════════════
 * microres — circuit breaker op_fn wrapper
 *
 * Real API: mres_breaker_call(br, op_fn, ctx, clock)
 * op_fn: int (*)(void *ctx) — returns 0=success, negative=fail
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char    *topic;
    const uint8_t *payload;
    size_t         len;
    bool           result;
} mqtt_op_ctx_t;

static int mqtt_publish_op(void *ctx)
{
    mqtt_op_ctx_t *op = (mqtt_op_ctx_t *)ctx;
    /* Simulate 20% failure rate */
    op->result = (rand() % 5) != 0;
    MLOG_DEBUG("MQTT","publish %s [%zu B] → %s",
               op->topic, op->len, op->result ? "OK" : "FAIL");
    return op->result ? 0 : -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Application context
 * ═══════════════════════════════════════════════════════════════════════════ */

struct app_s {
    /* microconf */
    node_config_t           config;

    /* microfsm */
    mfsm_t                  fsm;

    /* microres */
    mres_breaker_t          breaker;
    mres_retry_t            retry;

    /* microlog */
    mlog_t                  log;

    /* microsh */
    msh_t                   shell;

    /* microtimer */
    mtimer_t                timers;
    int                     tid_report;    /* timer IDs */
    int                     tid_watchdog;
    int                     tid_led;

    /* microbus */
    mbus_t                  bus;

    /* Stats */
    uint32_t                publish_ok;
    uint32_t                publish_fail;
    uint32_t                cbor_total;
    uint32_t                watchdog_ticks;
    uint32_t                led_blinks;
    uint8_t                 sensor_seq;

    /* CBOR scratch */
    uint8_t                 cbor_buf[64];
};

static app_t g_app;

/* ═══════════════════════════════════════════════════════════════════════════
 * microtimer callbacks
 *
 * Real API: mtimer_cb_fn(uint8_t timer_id, void *ctx)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void timer_report_cb(uint8_t id, void *ctx)
{
    (void)id;
    app_t *app = (app_t *)ctx;
    sensor_event_t ev = {
        .ts_ms = plat_now_ms(),
        .temp  = 22.0f + (float)(rand() % 60) / 10.0f,
        .hum   = 50.0f + (float)(rand() % 200) / 10.0f,
        .seq   = app->sensor_seq++,
    };
    mring_push(&g_ring, &ev);
    /* ISR-safe deferred bus notify */
    mbus_queue_signal(&app->bus, MBUS_TOPIC_SENSOR);
}

static void timer_watchdog_cb(uint8_t id, void *ctx)
{
    (void)id;
    app_t *app = (app_t *)ctx;
    app->watchdog_ticks++;
    uint32_t ticks = app->watchdog_ticks;
    mbus_publish(&app->bus, MBUS_TOPIC_USER, &ticks, sizeof(ticks));
}

static void timer_led_cb(uint8_t id, void *ctx)
{
    (void)id;
    app_t *app = (app_t *)ctx;
    app->led_blinks++;
    uint8_t state = (uint8_t)(app->led_blinks & 1u);
    mbus_queue(&app->bus, MBUS_TOPIC_USER + 1, &state, sizeof(state));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * microbus subscribers
 *
 * Real API: mbus_handler_fn(const mbus_event_t *event, void *ctx)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void on_watchdog_bus(const mbus_event_t *ev, void *ctx)
{
    (void)ctx;
    uint32_t ticks = 0;
    if (ev->payload_len >= sizeof(ticks))
        memcpy(&ticks, ev->payload, sizeof(ticks));
    MLOG_DEBUG("WDG","Watchdog tick #%lu — system alive", (unsigned long)ticks);
}

static void on_led_bus(const mbus_event_t *ev, void *ctx)
{
    (void)ctx;
    uint8_t state = ev->payload_len ? ev->payload[0] : 0;
    MLOG_DEBUG("LED","LED %s", state ? "▮ ON" : "▯ OFF");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * microcbor encode
 *
 * Real API: mcbor_enc_str (NUL-terminated), mcbor_enc_size, mcbor_enc_overflow
 * ═══════════════════════════════════════════════════════════════════════════ */

static size_t encode_telemetry(app_t *app, const sensor_event_t *ev)
{
    mcbor_enc_t enc;
    mcbor_enc_init(&enc, app->cbor_buf, sizeof(app->cbor_buf));

    mcbor_enc_map(&enc, 6);
    mcbor_enc_str(&enc, "id");   mcbor_enc_str(&enc, app->config.device_id);
    mcbor_enc_str(&enc, "seq");  mcbor_enc_uint(&enc, ev->seq);
    mcbor_enc_str(&enc, "ts");   mcbor_enc_uint(&enc, ev->ts_ms);
    mcbor_enc_str(&enc, "t");    mcbor_enc_float(&enc, ev->temp + (float)app->config.temp_offset);
    mcbor_enc_str(&enc, "h");    mcbor_enc_float(&enc, ev->hum);
    mcbor_enc_str(&enc, "unit"); mcbor_enc_str(&enc, "C");

    if (mcbor_enc_overflow(&enc)) {
        MLOG_ERROR("CBOR","%s","Buffer overflow during encode!");
        return 0;
    }
    return (size_t)mcbor_enc_size(&enc);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * microsh commands
 *
 * Real API: msh_cmd_fn(int argc, const char **argv, void *ctx) → int
 * Register: msh_register(sh, name, help, handler)
 * ═══════════════════════════════════════════════════════════════════════════ */

static int cmd_help(int argc, const char **argv, void *ctx)
{
    (void)argc; (void)argv;
    app_t *app = (app_t *)ctx;
    uint8_t n = msh_command_count(&app->shell);
    for (uint8_t i = 0; i < n; i++) {
        const msh_cmd_t *c = msh_command_at(&app->shell, i);
        printf("  %-12s %s\n", c->name, c->help ? c->help : "");
    }
    return 0;
}

static int cmd_status(int argc, const char **argv, void *ctx)
{
    (void)argc; (void)argv;
    app_t *app = (app_t *)ctx;
    printf("State     : %s\n",   mfsm_state_name(&app->fsm));
    printf("Published : %lu OK / %lu FAIL\n",
           (unsigned long)app->publish_ok, (unsigned long)app->publish_fail);
    printf("CBOR total: %lu B\n",(unsigned long)app->cbor_total);
    printf("Breaker   : %s\n",   mres_breaker_state_name(&app->breaker));
    printf("Ring      : %lu pending\n", (unsigned long)mring_count(&g_ring));
    printf("WDG ticks : %lu\n",  (unsigned long)app->watchdog_ticks);
    printf("LED blinks: %lu\n",  (unsigned long)app->led_blinks);
    return 0;
}

static int cmd_conf(int argc, const char **argv, void *ctx)
{
    app_t *app = (app_t *)ctx;
    if (argc < 2) { printf("Usage: conf list | conf get <key>\n"); return -1; }

    if (!strcmp(argv[1], "list")) {
        for (uint8_t i = 0; i < g_schema.num_entries; i++) {
            const mconf_entry_t *e = &g_schema.entries[i];
            printf("  %-20s", e->key);
            if (e->type == MCONF_TYPE_STR) {
                char buf[32]; mconf_get_str(&g_schema, &app->config, i, buf, sizeof(buf));
                printf(" = \"%s\"\n", buf);
            } else if (e->type == MCONF_TYPE_U32) {
                uint32_t v; mconf_get_u32(&g_schema, &app->config, i, &v);
                printf(" = %lu\n", (unsigned long)v);
            } else {
                int32_t v; mconf_get_i32(&g_schema, &app->config, i, &v);
                printf(" = %ld\n", (long)v);
            }
        }
    } else if (argc >= 3 && !strcmp(argv[1], "get")) {
        int idx = mconf_find(&g_schema, argv[2]);
        if (idx < 0) { printf("Key not found: %s\n", argv[2]); return -1; }
        const mconf_entry_t *e = &g_schema.entries[idx];
        if (e->type == MCONF_TYPE_STR) {
            char buf[32]; mconf_get_str(&g_schema, &app->config, (uint8_t)idx, buf, sizeof(buf));
            printf("%s = \"%s\"\n", argv[2], buf);
        } else if (e->type == MCONF_TYPE_U32) {
            uint32_t v; mconf_get_u32(&g_schema, &app->config, (uint8_t)idx, &v);
            printf("%s = %lu\n", argv[2], (unsigned long)v);
        } else {
            int32_t v; mconf_get_i32(&g_schema, &app->config, (uint8_t)idx, &v);
            printf("%s = %ld\n", argv[2], (long)v);
        }
    }
    return 0;
}

static int cmd_cbor(int argc, const char **argv, void *ctx)
{
    (void)argc; (void)argv;
    app_t *app = (app_t *)ctx;
    sensor_event_t ev = { .ts_ms = plat_now_ms(), .temp = 23.5f, .hum = 61.2f, .seq = 0xFF };
    size_t len = encode_telemetry(app, &ev);
    printf("CBOR payload (%zu bytes):\n  ", len);
    for (size_t i = 0; i < len; i++) printf("%02X ", app->cbor_buf[i]);
    printf("\n");
    return 0;
}

static int cmd_breaker(int argc, const char **argv, void *ctx)
{
    (void)argc; (void)argv;
    app_t *app = (app_t *)ctx;
    printf("Breaker   : %s\n",   mres_breaker_state_name(&app->breaker));
    printf("Failures  : %d/%d\n", app->breaker.failure_count,
           app->breaker.policy ? app->breaker.policy->failure_threshold : 0);
    return 0;
}

static int cmd_bus(int argc, const char **argv, void *ctx)
{
    (void)argc; (void)argv;
    app_t *app = (app_t *)ctx;
    printf("Publishes : %lu\n",  (unsigned long)mbus_publish_count(&app->bus));
    printf("Deliveries: %lu\n",  (unsigned long)mbus_deliver_count(&app->bus));
    printf("Dropped   : %lu\n",  (unsigned long)mbus_drop_count(&app->bus));
    printf("Subscribers: %d\n",  mbus_subscriber_count(&app->bus));
    printf("Queue     : %lu pending\n", (unsigned long)mbus_queue_count(&app->bus));
    return 0;
}

static int cmd_timers(int argc, const char **argv, void *ctx)
{
    (void)argc; (void)argv;
    app_t *app = (app_t *)ctx;
    const char *names[] = {"report","watchdog","led_blink"};
    int ids[] = { app->tid_report, app->tid_watchdog, app->tid_led };
    printf("Timers:\n");
    for (int i = 0; i < 3; i++) {
        if (ids[i] < 0) { printf("  [?] %-12s NOT CREATED\n", names[i]); continue; }
        mtimer_state_t st = mtimer_state(&app->timers, (uint8_t)ids[i]);
        uint32_t fires    = mtimer_fire_count(&app->timers, (uint8_t)ids[i]);
        printf("  [%d] %-12s %s  fires=%lu\n",
               ids[i], names[i], mtimer_state_str(st), (unsigned long)fires);
    }
    printf("Total fires: %lu\n", (unsigned long)mtimer_total_fires(&app->timers));
    return 0;
}

static int cmd_log(int argc, const char **argv, void *ctx)
{
    app_t *app = (app_t *)ctx;
    if (argc < 2) { printf("Usage: log <debug|info|warn|error>\n"); return -1; }
    mlog_level_t lv = MLOG_DEBUG;
    if      (!strcmp(argv[1],"debug")) lv = MLOG_DEBUG;
    else if (!strcmp(argv[1],"info"))  lv = MLOG_INFO;
    else if (!strcmp(argv[1],"warn"))  lv = MLOG_WARN;
    else if (!strcmp(argv[1],"error")) lv = MLOG_ERROR;
    else { printf("Unknown level: %s\n", argv[1]); return -1; }
    mlog_set_level(&app->log, lv);
    printf("Log level → %s\n", argv[1]);
    return 0;
}

static const mres_retry_policy_t g_retry_policy = {
    .max_attempts  = 8,
    .base_delay_ms = 500,
    .max_delay_ms  = 16000,
    .strategy      = MRES_BACKOFF_EXPONENTIAL,
    .jitter        = true,
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Initialisation — all 9 subsystems
 * ═══════════════════════════════════════════════════════════════════════════ */

static void app_init(app_t *app)
{
    /* ── 1. microlog ──────────────────────────────────────────────────────
     * mlog_init + mlog_add_backend + mlog_set_clock
     */
    mlog_init(&app->log);
    mlog_set_clock(&app->log, plat_now_ms);
    mlog_backend_t backend = {
        .write = stdout_write,
        .ctx   = NULL,
        .level = MLOG_DEBUG,
#if MLOG_ENABLE_COLOR
        .color = true,
#endif
    };
    mlog_add_backend(&app->log, &backend);
    /* Point global logger at our instance so MLOG_* macros work */
    *mlog_global() = app->log;
    MLOG_INFO("BOOT","%s","=== IoT Sensor Node -- all 9 micro-toolkit libraries (real API) ===");

    /* ── 2. microconf ────────────────────────────────────────────────────
     * mconf_load falls back to defaults on first boot (no valid flash)
     * Access via mconf_find(schema, key) → index
     */
    mconf_err_t cerr = mconf_load(&g_schema, &app->config, &g_flash_io);
    if (cerr != MCONF_OK) {
        mconf_load_defaults(&g_schema, &app->config);
        MLOG_WARN("CONF","Load failed (%d) — using defaults", cerr);
    } else {
        MLOG_INFO("CONF","%s","Loaded from flash (CRC OK)");
    }
    MLOG_INFO("CONF","%s @ %s:%lu  interval=%lu ms",
              app->config.device_id, app->config.mqtt_host,
              (unsigned long)app->config.mqtt_port,
              (unsigned long)app->config.report_ms);

    /* ── 3. micoring ─────────────────────────────────────────────────────
     * mring_init(ring, buf, capacity, elem_size)
     */
    mring_init(&g_ring, g_ring_buf, 8, sizeof(sensor_event_t));
    MLOG_DEBUG("RING","Ring buffer ready — capacity=%lu, elem=%lu B",
               (unsigned long)mring_capacity(&g_ring),
               (unsigned long)sizeof(sensor_event_t));

    /* ── 4. microres ─────────────────────────────────────────────────────
     * mres_breaker_init(br, policy)  — clock passed to _call/_report later
     * mres_retry_init(retry, policy)
     */
    static const mres_breaker_policy_t brk_policy = {
        .failure_threshold  = 3,
        .recovery_timeout_ms= 10000,
        .half_open_max_calls= 1,
    };
    mres_breaker_init(&app->breaker, &brk_policy);

    mres_retry_init(&app->retry, &g_retry_policy);

    /* ── 5. microfsm ─────────────────────────────────────────────────────
     * mfsm_init(fsm, def, user_data)  — def is const ROM struct
     */
    mfsm_init(&app->fsm, &g_fsm_def, app);

    /* ── 6. microbus ─────────────────────────────────────────────────────
     * mbus_init(bus, clock_fn)
     * mbus_subscribe(bus, topic_uint8, handler, ctx)
     */
    mbus_init(&app->bus, plat_now_ms);
    mbus_subscribe(&app->bus, (uint8_t)MBUS_TOPIC_USER,      on_watchdog_bus, app);
    mbus_subscribe(&app->bus, (uint8_t)(MBUS_TOPIC_USER + 1),on_led_bus,      app);
    MLOG_DEBUG("BUS","Event bus ready — %d subscribers", mbus_subscriber_count(&app->bus));

    /* ── 7. microtimer ───────────────────────────────────────────────────
     * mtimer_init(tm, clock_fn)
     * mtimer_create(tm, name, interval, mode, callback, ctx) → id
     * mtimer_start(tm, id)
     */
    mtimer_init(&app->timers, plat_now_ms);
    app->tid_report   = mtimer_create(&app->timers, "report",   150,   MTIMER_PERIODIC, timer_report_cb,   app);
    app->tid_watchdog = mtimer_create(&app->timers, "watchdog", 10000, MTIMER_PERIODIC, timer_watchdog_cb, app);
    app->tid_led      = mtimer_create(&app->timers, "led_blink",500,   MTIMER_PERIODIC, timer_led_cb,      app);
    mtimer_start(&app->timers, (uint8_t)app->tid_report);
    mtimer_start(&app->timers, (uint8_t)app->tid_watchdog);
    mtimer_start(&app->timers, (uint8_t)app->tid_led);
    MLOG_DEBUG("TMR","%s","3 timers created + started (report=150ms, watchdog=10s, led=500ms)");

    /* ── 8. microsh ──────────────────────────────────────────────────────
     * msh_init(sh, print_fn, ctx)
     * msh_register(sh, name, help, handler) — registers one command at a time
     * msh_set_prompt(sh, str)
     */
    msh_init(&app->shell, shell_print, app);
    msh_set_prompt(&app->shell, "\033[36msensor>\033[0m ");
    msh_register(&app->shell, "help",    "show all commands",     cmd_help);
    msh_register(&app->shell, "status",  "device state/counters", cmd_status);
    msh_register(&app->shell, "conf",    "list | get <key>",      cmd_conf);
    msh_register(&app->shell, "cbor",    "encode + hex dump",     cmd_cbor);
    msh_register(&app->shell, "breaker", "circuit breaker state", cmd_breaker);
    msh_register(&app->shell, "bus",     "event bus stats",       cmd_bus);
    msh_register(&app->shell, "timers",  "active timers",         cmd_timers);
    msh_register(&app->shell, "log",     "set log level",         cmd_log);
    MLOG_INFO("SHELL","Debug shell ready — %d commands registered",
              msh_command_count(&app->shell));

    /* ── 9. Save config ──────────────────────────────────────────────────*/
    mconf_save(&g_schema, &app->config, &g_flash_io);
    MLOG_INFO("BOOT","%s","All 9 subsystems initialised ✓");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main loop tick
 * ═══════════════════════════════════════════════════════════════════════════ */

static void app_tick(app_t *app)
{
    /* 1. microtimer tick → fires report/watchdog/LED callbacks */
    mtimer_tick(&app->timers);

    /* 2. microbus dispatch → delivers deferred ISR queue events */
    mbus_dispatch(&app->bus);

    /* 3. Drain micoring → encode CBOR → breaker → MQTT */
    sensor_event_t ev;
    while (mring_pop(&g_ring, &ev) == MRING_OK) {

        MLOG_DEBUG("SENSOR","seq=%d  T=%.1f°C  H=%.1f%%",
                   ev.seq, (double)ev.temp, (double)ev.hum);

        size_t cbor_len = encode_telemetry(app, &ev);
        if (!cbor_len) continue;
        app->cbor_total += (uint32_t)cbor_len;
        MLOG_DEBUG("CBOR","Encoded %zu B (vs ~80 JSON)", cbor_len);

        /* Build topic */
        char topic[128];
        snprintf(topic, sizeof(topic), "sensors/%s/telemetry", app->config.device_id);

        /* microres — circuit breaker call via op_fn */
        mqtt_op_ctx_t op_ctx = { .topic = topic, .payload = app->cbor_buf, .len = cbor_len };
        int bres = mres_breaker_call(&app->breaker, mqtt_publish_op, &op_ctx, plat_now_ms);

        if (bres == MRES_ERR_OPEN) {
            MLOG_WARN("MQTT","%s","Breaker OPEN — publish blocked");
            mbus_signal(&app->bus, MBUS_TOPIC_NETWORK);
            mfsm_dispatch(&app->fsm, EV_BREAKER_OPEN);
            return;
        }

        if (bres == MRES_OK) {
            app->publish_ok++;
            MLOG_INFO("MQTT","[%lu] OK  %zu B → %s",
                      (unsigned long)app->publish_ok, cbor_len, topic);
            mbus_signal(&app->bus, MBUS_TOPIC_NETWORK);
            mfsm_dispatch(&app->fsm, EV_PUB_OK);
        } else {
            app->publish_fail++;
            uint32_t delay = mres_delay_calc(&g_retry_policy, (uint8_t)app->retry.attempts, plat_now_ms);
            MLOG_WARN("MQTT","FAIL #%lu — next retry in %lu ms",
                      (unsigned long)app->publish_fail, (unsigned long)delay);
            mbus_signal(&app->bus, MBUS_TOPIC_NETWORK);
            mfsm_dispatch(&app->fsm, EV_PUB_FAIL);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Entry point
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifdef PLATFORM_ESP32

void app_main(void)
{
    nvs_flash_init();
    app_init(&g_app);
    mfsm_dispatch(&g_app.fsm, EV_BOOT_DONE);
    plat_sleep_ms(500); mfsm_dispatch(&g_app.fsm, EV_WIFI_UP);
    plat_sleep_ms(500); mfsm_dispatch(&g_app.fsm, EV_MQTT_UP);
    for (;;) { app_tick(&g_app); plat_sleep_ms(50); }
}

#else /* PLATFORM_LINUX */

int main(void)
{
    srand(42);
    app_init(&g_app);

    mfsm_dispatch(&g_app.fsm, EV_BOOT_DONE);
    plat_sleep_ms(50);
    mfsm_dispatch(&g_app.fsm, EV_WIFI_UP);
    plat_sleep_ms(50);
    mfsm_dispatch(&g_app.fsm, EV_MQTT_UP);

    MLOG_INFO("MAIN","%s","Running 10 ticks...");
    for (int i = 0; i < 10; i++) {
        MLOG_INFO("MAIN","─── Tick %d ───", i + 1);
        app_tick(&g_app);
        if (mfsm_current(&g_app.fsm) == ST_ERROR) {
            plat_sleep_ms(100);
            mres_breaker_reset(&g_app.breaker);
            mfsm_dispatch(&g_app.fsm, EV_RECOVER);
            mfsm_dispatch(&g_app.fsm, EV_MQTT_UP);
        }
        plat_sleep_ms(200);
    }

    /* Shell demo */
    printf("\n");
    MLOG_INFO("SHELL","%s","=== Debug Shell Demo ===");
    const char *demo[] = { "help","status","conf list","conf get device_id","cbor","breaker","bus","timers" };
    for (size_t i = 0; i < sizeof(demo)/sizeof(demo[0]); i++) {
        printf("\n");
        msh_prompt(&g_app.shell);
        printf("%s\n", demo[i]);
        msh_exec(&g_app.shell, demo[i]);
    }

    mconf_save(&g_schema, &g_app.config, &g_flash_io);
    printf("\n");
    MLOG_INFO("MAIN","%s","=== Complete ===");
    MLOG_INFO("MAIN","State    : %s",  mfsm_state_name(&g_app.fsm));
    MLOG_INFO("MAIN","Published: %lu OK / %lu FAIL",
              (unsigned long)g_app.publish_ok, (unsigned long)g_app.publish_fail);
    MLOG_INFO("MAIN","CBOR     : %lu B", (unsigned long)g_app.cbor_total);
    MLOG_INFO("MAIN","Breaker  : %s",  mres_breaker_state_name(&g_app.breaker));
    MLOG_INFO("MAIN","WDG ticks: %lu", (unsigned long)g_app.watchdog_ticks);
    MLOG_INFO("MAIN","LED blinks:%lu", (unsigned long)g_app.led_blinks);
    MLOG_INFO("MAIN","Timer fires:%lu",  (unsigned long)mtimer_total_fires(&g_app.timers));
    MLOG_INFO("MAIN","Bus publishes:%lu",(unsigned long)mbus_publish_count(&g_app.bus));
    return 0;
}

#endif /* PLATFORM_LINUX */
