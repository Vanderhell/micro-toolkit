/*
 * micro-toolkit integration example
 *
 * Linux simulation for the current public headers of the nine core libraries.
 * ESP32 use remains a porting recipe until verified outside this repository.
 *
 * SPDX-License-Identifier: MIT
 */

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#else
#if defined(_WIN32)
#include <windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <unistd.h>
#endif
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbus.h"
#include "mcbor.h"
#include "mconf.h"
#include "mfsm.h"
#include "mlog.h"
#include "mres.h"
#include "mring.h"
#include "msh.h"
#include "mtimer.h"

typedef struct {
    char mqtt_host[32];
    uint32_t mqtt_port;
    char device_id[32];
    uint32_t report_interval_ms;
    int32_t temp_offset_cdeg;
} node_config_t;

typedef struct {
    uint32_t ts_ms;
    int32_t temp_cdeg;
    uint16_t humidity_tenths;
    uint8_t seq;
} sensor_event_t;

typedef struct {
    uint8_t storage[512];
} flash_backend_t;

typedef enum {
    ST_BOOT = 0,
    ST_CONNECTING,
    ST_ONLINE,
    ST_ERROR,
    ST_COUNT
} app_state_t;

typedef enum {
    EV_BOOT_READY = 0,
    EV_NETWORK_READY,
    EV_PUBLISH_OK,
    EV_PUBLISH_FAIL,
    EV_RECOVER
} app_event_t;

typedef struct app_s {
    node_config_t config;
    mconf_t config_ctx;
    mconf_io_t config_io;
    flash_backend_t flash;

    mfsm_t fsm;
    mring_t ring;
    uint8_t ring_storage[8u * sizeof(sensor_event_t)];

    mres_platform_t platform;
    mres_retry_t retry;
    mres_breaker_t breaker;

    msh_t shell;
    mtimer_t timers;
    int timer_report;
    int timer_watchdog;
    int timer_led;

    mbus_t bus;

    uint8_t cbor_buf[96];
    uint32_t publish_ok;
    uint32_t publish_fail;
    uint32_t cbor_bytes;
    uint32_t watchdog_ticks;
    uint32_t led_blinks;
    uint8_t next_seq;
} app_t;

static app_t g_app;

static const char CFG_DEF_MQTT_HOST[] = "broker.local";
static const char CFG_DEF_DEVICE_ID[] = "node-01";
static const uint32_t CFG_DEF_MQTT_PORT = 1883u;
static const uint32_t CFG_DEF_REPORT_INTERVAL_MS = 250u;
static const int32_t CFG_DEF_TEMP_OFFSET_CDEG = 0;

static const mconf_entry_t g_config_entries[] = {
    MCONF_ENTRY_STRING(node_config_t, mqtt_host, CFG_DEF_MQTT_HOST),
    MCONF_ENTRY_SCALAR(node_config_t, mqtt_port, MCONF_TYPE_U32, &CFG_DEF_MQTT_PORT),
    MCONF_ENTRY_STRING(node_config_t, device_id, CFG_DEF_DEVICE_ID),
    MCONF_ENTRY_SCALAR(node_config_t, report_interval_ms, MCONF_TYPE_U32, &CFG_DEF_REPORT_INTERVAL_MS),
    MCONF_ENTRY_SCALAR(node_config_t, temp_offset_cdeg, MCONF_TYPE_I32, &CFG_DEF_TEMP_OFFSET_CDEG),
};

static const mconf_schema_t g_config_schema = {
    .entries = g_config_entries,
    .entry_count = sizeof(g_config_entries) / sizeof(g_config_entries[0]),
    .schema_version = MCONF_SCHEMA_VERSION_CURRENT,
    .data_size = sizeof(node_config_t),
};

static uint32_t plat_now_ms(void)
{
#ifdef ESP_PLATFORM
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
#elif defined(_WIN32)
    return (uint32_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000L));
#endif
}

static uint32_t plat_now_ms_ctx(void *context)
{
    (void)context;
    return plat_now_ms();
}

static void plat_sleep_ms(uint32_t delay_ms)
{
#ifdef ESP_PLATFORM
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
#elif defined(_WIN32)
    Sleep(delay_ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(delay_ms / 1000u);
    ts.tv_nsec = (long)(delay_ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static int plat_wait_ms_ctx(void *context, uint32_t delay_ms)
{
    (void)context;
    plat_sleep_ms(delay_ms);
    return 0;
}

static void shell_print(const char *text, void *context)
{
    (void)context;
    fputs(text, stdout);
}

static void log_stdout_write(const char *buf, uint16_t len, mlog_level_t level, void *context)
{
    (void)len;
    (void)level;
    (void)context;
    fputs(buf, stdout);
}

static int flash_read(void *callback_ctx, size_t offset, void *buffer, size_t size)
{
    flash_backend_t *flash = (flash_backend_t *)callback_ctx;
    if ((offset + size) > sizeof(flash->storage)) {
        return -1;
    }
    memcpy(buffer, flash->storage + offset, size);
    return 0;
}

static int flash_write(void *callback_ctx, size_t offset, const void *buffer, size_t size)
{
    flash_backend_t *flash = (flash_backend_t *)callback_ctx;
    if ((offset + size) > sizeof(flash->storage)) {
        return -1;
    }
    memcpy(flash->storage + offset, buffer, size);
    return 0;
}

static int flash_erase(void *callback_ctx, size_t offset, size_t size)
{
    flash_backend_t *flash = (flash_backend_t *)callback_ctx;
    if ((offset + size) > sizeof(flash->storage)) {
        return -1;
    }
    memset(flash->storage + offset, 0xFF, size);
    return 0;
}

static void fsm_trace(const mfsm_trace_record_t *record, void *user_data)
{
    app_t *app = (app_t *)user_data;
    const char *name = NULL;
    if ((record == NULL) || (app == NULL)) {
        return;
    }
    if (mfsm_state_name(&app->fsm, &name) == MFSM_OK && name != NULL) {
        MLOG_DEBUG("FSM", "trace kind=%u from=%u event=%u to=%u current=%s",
            (unsigned)record->kind,
            (unsigned)record->from,
            (unsigned)record->event,
            (unsigned)record->to,
            name);
    }
}

static void on_enter_boot(void *user_data)
{
    (void)user_data;
    MLOG_INFO("FSM", "%s", "BOOT");
}

static void on_enter_connecting(void *user_data)
{
    (void)user_data;
    MLOG_INFO("FSM", "%s", "CONNECTING");
}

static void on_enter_online(void *user_data)
{
    (void)user_data;
    MLOG_INFO("FSM", "%s", "ONLINE");
}

static void on_enter_error(void *user_data)
{
    (void)user_data;
    MLOG_WARN("FSM", "%s", "ERROR");
}

static const mfsm_state_t g_states[ST_COUNT] = {
    [ST_BOOT] = MFSM_STATE(on_enter_boot, NULL, "BOOT"),
    [ST_CONNECTING] = MFSM_STATE(on_enter_connecting, NULL, "CONNECTING"),
    [ST_ONLINE] = MFSM_STATE(on_enter_online, NULL, "ONLINE"),
    [ST_ERROR] = MFSM_STATE(on_enter_error, NULL, "ERROR"),
};

static const mfsm_transition_t g_transitions[] = {
    MFSM_TRANSITION(ST_BOOT, EV_BOOT_READY, ST_CONNECTING, NULL, NULL),
    MFSM_TRANSITION(ST_CONNECTING, EV_NETWORK_READY, ST_ONLINE, NULL, NULL),
    MFSM_TRANSITION(ST_ONLINE, EV_PUBLISH_FAIL, ST_ERROR, NULL, NULL),
    MFSM_TRANSITION(ST_ERROR, EV_RECOVER, ST_CONNECTING, NULL, NULL),
    MFSM_TRANSITION(ST_ONLINE, EV_PUBLISH_OK, ST_ONLINE, NULL, NULL),
};

static const mfsm_def_t g_fsm_def = MFSM_DEF(
    g_states,
    g_transitions,
    (uint16_t)ST_COUNT,
    (uint16_t)(sizeof(g_transitions) / sizeof(g_transitions[0])),
    ST_BOOT);

static void on_sensor_signal(const mbus_event_t *event, void *context)
{
    (void)context;
    if (event != NULL) {
        MLOG_DEBUG("BUS", "sensor signal topic=%u", (unsigned)event->topic);
    }
}

static void on_network_signal(const mbus_event_t *event, void *context)
{
    (void)context;
    if (event != NULL) {
        MLOG_DEBUG("BUS", "network signal topic=%u", (unsigned)event->topic);
    }
}

static void timer_report_cb(uint8_t timer_id, void *context)
{
    app_t *app = (app_t *)context;
    sensor_event_t event;
    mring_err_t ring_status;

    (void)timer_id;
    if (app == NULL) {
        return;
    }

    event.ts_ms = plat_now_ms();
    event.temp_cdeg = 2250 + (int32_t)(rand() % 300) + app->config.temp_offset_cdeg;
    event.humidity_tenths = (uint16_t)(500 + (rand() % 150));
    event.seq = app->next_seq++;

    ring_status = mring_push(&app->ring, &event);
    if (ring_status != MRING_OK) {
        MLOG_WARN("RING", "push failed: %s", mring_err_str(ring_status));
        return;
    }

#if MBUS_ENABLE_QUEUE
    if (mbus_queue_signal(&app->bus, MBUS_TOPIC_SENSOR) < 0) {
        MLOG_WARN("BUS", "%s", "queue_signal failed");
    }
#endif
}

static void timer_watchdog_cb(uint8_t timer_id, void *context)
{
    app_t *app = (app_t *)context;
    int status;

    (void)timer_id;
    if (app == NULL) {
        return;
    }

    app->watchdog_ticks++;
    status = mbus_publish(&app->bus, MBUS_TOPIC_HEALTH_ALERT, &app->watchdog_ticks, sizeof(app->watchdog_ticks));
    if (status < 0) {
        MLOG_WARN("BUS", "watchdog publish failed: %d", status);
    }
}

static void timer_led_cb(uint8_t timer_id, void *context)
{
    app_t *app = (app_t *)context;
    uint8_t state;

    (void)timer_id;
    if (app == NULL) {
        return;
    }

    app->led_blinks++;
    state = (uint8_t)(app->led_blinks & 1u);
#if MBUS_ENABLE_QUEUE
    if (mbus_queue(&app->bus, MBUS_TOPIC_USER, &state, sizeof(state)) < 0) {
        MLOG_WARN("BUS", "%s", "queue failed");
    }
#endif
}

static bool log_status(const char *label, int status)
{
    if (status < 0) {
        MLOG_ERROR("APP", "%s failed: %d", label, status);
        return false;
    }
    return true;
}

static bool encode_telemetry(app_t *app, const sensor_event_t *event, size_t *payload_len)
{
    mcbor_enc_t enc;
    mcbor_err_t cb_status;
    size_t size_out = 0u;
    bool overflow = false;

    cb_status = mcbor_enc_init(&enc, app->cbor_buf, sizeof(app->cbor_buf));
    if (cb_status != MCBOR_OK) {
        MLOG_ERROR("CBOR", "enc_init failed: %s", mcbor_err_str(cb_status));
        return false;
    }

    cb_status = mcbor_enc_map(&enc, 4u);
    if (cb_status != MCBOR_OK) {
        MLOG_ERROR("CBOR", "enc_map failed: %s", mcbor_err_str(cb_status));
        return false;
    }

    cb_status = mcbor_enc_str(&enc, "seq");
    if (cb_status != MCBOR_OK) {
        return false;
    }
    cb_status = mcbor_enc_uint(&enc, event->seq);
    if (cb_status != MCBOR_OK) {
        return false;
    }

    cb_status = mcbor_enc_str(&enc, "temp");
    if (cb_status != MCBOR_OK) {
        return false;
    }
#if MCBOR_ENABLE_FLOAT32
    cb_status = mcbor_enc_float(&enc, (float)event->temp_cdeg / 100.0f);
#else
    cb_status = mcbor_enc_int(&enc, event->temp_cdeg);
#endif
    if (cb_status != MCBOR_OK) {
        return false;
    }

    cb_status = mcbor_enc_str(&enc, "hum");
    if (cb_status != MCBOR_OK) {
        return false;
    }
    cb_status = mcbor_enc_uint(&enc, event->humidity_tenths);
    if (cb_status != MCBOR_OK) {
        return false;
    }

    cb_status = mcbor_enc_str(&enc, "ts");
    if (cb_status != MCBOR_OK) {
        return false;
    }
    cb_status = mcbor_enc_uint(&enc, event->ts_ms);
    if (cb_status != MCBOR_OK) {
        return false;
    }

    cb_status = mcbor_enc_size(&enc, &size_out);
    if (cb_status != MCBOR_OK) {
        MLOG_ERROR("CBOR", "enc_size failed: %s", mcbor_err_str(cb_status));
        return false;
    }

    cb_status = mcbor_enc_overflow(&enc, &overflow);
    if (cb_status != MCBOR_OK || overflow) {
        MLOG_ERROR("CBOR", "%s", "encoder overflow");
        return false;
    }

    cb_status = mcbor_validate_one(app->cbor_buf, size_out);
    if (cb_status != MCBOR_OK) {
        MLOG_ERROR("CBOR", "validate_one failed: %s", mcbor_err_str(cb_status));
        return false;
    }

    *payload_len = size_out;
    return true;
}

typedef struct {
    app_t *app;
    const char *topic;
    const uint8_t *payload;
    size_t payload_len;
} publish_ctx_t;

static int mqtt_publish_once(void *context)
{
    publish_ctx_t *publish = (publish_ctx_t *)context;
    bool ok;

    if ((publish == NULL) || (publish->topic == NULL) || (publish->payload == NULL)) {
        return -1;
    }

    ok = (rand() % 5) != 0;
    MLOG_DEBUG("MQTT", "publish %s len=%lu %s",
        publish->topic,
        (unsigned long)publish->payload_len,
        ok ? "OK" : "FAIL");
    return ok ? 0 : -1;
}

static int mqtt_publish_with_retry(void *context)
{
    publish_ctx_t *publish = (publish_ctx_t *)context;
    int op_result = -1;
    mres_err_t status;

    if ((publish == NULL) || (publish->app == NULL)) {
        return -1;
    }

    status = mres_retry_exec(
        &publish->app->retry,
        mqtt_publish_once,
        publish,
        &publish->app->platform,
        &op_result);
    if ((status == MRES_OK) && (op_result == 0)) {
        return 0;
    }

    MLOG_WARN("MQTT", "retry_exec status=%s op_result=%d", mres_err_str(status), op_result);
    return -1;
}

static int cmd_status(int argc, const char *const *argv, void *context)
{
    app_t *app = (app_t *)context;
    const char *state_name = NULL;
    uint32_t total_fires = 0u;

    (void)argc;
    (void)argv;

    if ((app == NULL) || (mfsm_state_name(&app->fsm, &state_name) != MFSM_OK)) {
        return -1;
    }
    if (mtimer_get_total_fires(&app->timers, &total_fires) != MTIMER_OK) {
        total_fires = 0u;
    }

    printf("State     : %s\n", state_name);
    printf("Published : %lu OK / %lu FAIL\n", (unsigned long)app->publish_ok, (unsigned long)app->publish_fail);
    printf("CBOR      : %lu bytes\n", (unsigned long)app->cbor_bytes);
    printf("Watchdog  : %lu\n", (unsigned long)app->watchdog_ticks);
    printf("LED       : %lu\n", (unsigned long)app->led_blinks);
    printf("Timers    : %lu fires\n", (unsigned long)total_fires);
    return 0;
}

static int cmd_conf(int argc, const char *const *argv, void *context)
{
    app_t *app = (app_t *)context;
    size_t index = 0u;

    if (app == NULL) {
        return -1;
    }

    if (argc == 1) {
        printf("mqtt_host=%s\n", app->config.mqtt_host);
        printf("mqtt_port=%lu\n", (unsigned long)app->config.mqtt_port);
        printf("device_id=%s\n", app->config.device_id);
        printf("report_interval_ms=%lu\n", (unsigned long)app->config.report_interval_ms);
        printf("temp_offset_cdeg=%ld\n", (long)app->config.temp_offset_cdeg);
        return 0;
    }

    if (mconf_find(&app->config_ctx, argv[1], &index) != MCONF_OK) {
        printf("unknown key: %s\n", argv[1]);
        return -1;
    }

    if (strcmp(argv[1], "mqtt_host") == 0 || strcmp(argv[1], "device_id") == 0) {
        char buffer[32];
        size_t required = 0u;
        if (mconf_get_string(&app->config_ctx, index, buffer, sizeof(buffer), &required) != MCONF_OK) {
            return -1;
        }
        printf("%s=%s\n", argv[1], buffer);
        return 0;
    }

    if (strcmp(argv[1], "temp_offset_cdeg") == 0) {
        int32_t value = 0;
        if (mconf_get_i32(&app->config_ctx, index, &value) != MCONF_OK) {
            return -1;
        }
        printf("%s=%ld\n", argv[1], (long)value);
        return 0;
    }

    {
        uint32_t value = 0u;
        if (mconf_get_u32(&app->config_ctx, index, &value) != MCONF_OK) {
            return -1;
        }
        printf("%s=%lu\n", argv[1], (unsigned long)value);
    }
    return 0;
}

static int cmd_cbor(int argc, const char *const *argv, void *context)
{
    app_t *app = (app_t *)context;
    sensor_event_t event;
    size_t payload_len = 0u;
    size_t i;

    (void)argc;
    (void)argv;

    if (app == NULL) {
        return -1;
    }

    event.ts_ms = plat_now_ms();
    event.temp_cdeg = 2345;
    event.humidity_tenths = 612;
    event.seq = 0xAAu;

    if (!encode_telemetry(app, &event, &payload_len)) {
        return -1;
    }

    printf("CBOR payload (%zu bytes):", payload_len);
    for (i = 0u; i < payload_len; ++i) {
        printf(" %02X", app->cbor_buf[i]);
    }
    printf("\n");
    return 0;
}

static int cmd_breaker(int argc, const char *const *argv, void *context)
{
    app_t *app = (app_t *)context;
    const char *name = NULL;
    uint32_t remaining = 0u;
    bool is_open = false;

    (void)argc;
    (void)argv;

    if (app == NULL) {
        return -1;
    }

    if (mres_breaker_state_name(&app->breaker, &name) != MRES_OK) {
        return -1;
    }
    if (mres_breaker_remaining_ms(&app->breaker, &app->platform, &remaining, &is_open) != MRES_OK) {
        return -1;
    }

    printf("Breaker   : %s\n", name);
    printf("Open      : %s\n", is_open ? "yes" : "no");
    printf("Remaining : %lu ms\n", (unsigned long)remaining);
    return 0;
}

static int cmd_bus(int argc, const char *const *argv, void *context)
{
    app_t *app = (app_t *)context;

    (void)argc;
    (void)argv;

    if (app == NULL) {
        return -1;
    }

    printf("Subscribers: %u\n", (unsigned)mbus_subscriber_count(&app->bus));
    printf("Publishes  : %lu\n", (unsigned long)mbus_publish_count(&app->bus));
    printf("Deliveries : %lu\n", (unsigned long)mbus_deliver_count(&app->bus));
    printf("Dropped    : %lu\n", (unsigned long)mbus_drop_count(&app->bus));
#if MBUS_ENABLE_QUEUE
    printf("Queued     : %lu\n", (unsigned long)mbus_queue_count(&app->bus));
#endif
    return 0;
}

static int cmd_timers(int argc, const char *const *argv, void *context)
{
    app_t *app = (app_t *)context;
    const char *names[3] = { "report", "watchdog", "led" };
    int ids[3];
    int i;

    (void)argc;
    (void)argv;

    if (app == NULL) {
        return -1;
    }

    ids[0] = app->timer_report;
    ids[1] = app->timer_watchdog;
    ids[2] = app->timer_led;

    for (i = 0; i < 3; ++i) {
        mtimer_state_t state;
        uint32_t fire_count = 0u;
        if ((ids[i] < 0) ||
            (mtimer_get_state(&app->timers, (uint8_t)ids[i], &state) != MTIMER_OK) ||
            (mtimer_get_fire_count(&app->timers, (uint8_t)ids[i], &fire_count) != MTIMER_OK)) {
            printf("%s: unavailable\n", names[i]);
            continue;
        }
        printf("%s: %s fires=%lu\n", names[i], mtimer_state_str(state), (unsigned long)fire_count);
    }
    return 0;
}

static bool app_init_logging(void)
{
    mlog_backend_t backend;
    mlog_t *global = mlog_global();

    mlog_init(global);
    mlog_set_level(global, MLOG_DEBUG);
    mlog_set_clock(global, plat_now_ms);

    backend.write = log_stdout_write;
    backend.ctx = NULL;
    backend.level = MLOG_DEBUG;
#if MLOG_ENABLE_COLOR
    backend.color = true;
#else
    backend.color = false;
#endif

    return log_status("mlog_add_backend", mlog_add_backend(global, &backend));
}

static bool app_init_config(app_t *app)
{
    mconf_err_t status;

    memset(app->flash.storage, 0xFF, sizeof(app->flash.storage));

    status = mconf_init(&app->config_ctx, sizeof(app->config_ctx), &g_config_schema, &app->config, sizeof(app->config));
    if (status != MCONF_OK) {
        MLOG_ERROR("CONF", "init failed: %s", mconf_err_str(status));
        return false;
    }

    app->config_io.callback_ctx = &app->flash;
    app->config_io.storage_size = sizeof(app->flash.storage);
    app->config_io.slot_size = sizeof(app->flash.storage) / 2u;
    app->config_io.read = flash_read;
    app->config_io.write = flash_write;
    app->config_io.erase = flash_erase;

    status = mconf_load(&app->config_ctx, &app->config_io);
    if (status != MCONF_OK) {
        if ((status != MCONF_ERR_MAGIC) && (status != MCONF_ERR_CRC)) {
            MLOG_WARN("CONF", "load returned %s, falling back to defaults", mconf_err_str(status));
        }
        status = mconf_load_defaults(&app->config_ctx);
        if (status != MCONF_OK) {
            MLOG_ERROR("CONF", "defaults failed: %s", mconf_err_str(status));
            return false;
        }
        status = mconf_save(&app->config_ctx, &app->config_io);
        if (status != MCONF_OK) {
            MLOG_ERROR("CONF", "save failed: %s", mconf_err_str(status));
            return false;
        }
    }

    MLOG_INFO("CONF", "%s %s:%lu interval=%lu",
        app->config.device_id,
        app->config.mqtt_host,
        (unsigned long)app->config.mqtt_port,
        (unsigned long)app->config.report_interval_ms);
    return true;
}

static bool app_init_fsm(app_t *app)
{
    if (mfsm_validate(&g_fsm_def) != MFSM_OK) {
        MLOG_ERROR("FSM", "%s", "definition validation failed");
        return false;
    }
    if (mfsm_init(&app->fsm, &g_fsm_def, app) != MFSM_OK) {
        MLOG_ERROR("FSM", "%s", "init failed");
        return false;
    }
    if (mfsm_set_trace(&app->fsm, fsm_trace) != MFSM_OK) {
        MLOG_WARN("FSM", "%s", "trace disabled");
    }
    return true;
}

static bool app_init_ring(app_t *app)
{
    mring_err_t status;
    uint32_t capacity = 0u;

    status = mring_init(
        &app->ring,
        app->ring_storage,
        sizeof(app->ring_storage),
        8u,
        sizeof(sensor_event_t));
    if (status != MRING_OK) {
        MLOG_ERROR("RING", "init failed: %s", mring_err_str(status));
        return false;
    }
    if (mring_capacity(&app->ring, &capacity) != MRING_OK) {
        return false;
    }
    MLOG_INFO("RING", "capacity=%lu mode=%d", (unsigned long)capacity, (int)MRING_CONCURRENCY_MODE);
    return true;
}

static bool app_init_resilience(app_t *app)
{
    static const mres_retry_policy_t retry_policy = {
        .max_attempts = 3u,
        .strategy = MRES_BACKOFF_EXPONENTIAL,
        .jitter = 1u,
        .reserved0 = 0u,
        .base_delay_ms = 25u,
        .max_delay_ms = 200u,
    };
    static const mres_breaker_policy_t breaker_policy = {
        .failure_threshold = 3u,
        .half_open_max_calls = 1u,
        .reserved0 = 0u,
        .reserved1 = 0u,
        .recovery_timeout_ms = 500u,
    };

    app->platform.context = app;
    app->platform.clock = plat_now_ms_ctx;
    app->platform.wait = plat_wait_ms_ctx;

    if (mres_retry_init(&app->retry, &retry_policy) != MRES_OK) {
        return false;
    }
    if (mres_retry_seed(&app->retry, 0x12345678u) != MRES_OK) {
        return false;
    }
    if (mres_breaker_init(&app->breaker, &breaker_policy) != MRES_OK) {
        return false;
    }
    return true;
}

static bool app_init_bus(app_t *app)
{
    if (mbus_init(&app->bus, plat_now_ms) != MBUS_OK) {
        return false;
    }
    if (mbus_subscribe(&app->bus, MBUS_TOPIC_SENSOR, on_sensor_signal, app) < 0) {
        return false;
    }
    if (mbus_subscribe(&app->bus, MBUS_TOPIC_NETWORK, on_network_signal, app) < 0) {
        return false;
    }
    return true;
}

static bool app_init_timers(app_t *app)
{
    if (mtimer_init(&app->timers, plat_now_ms) != MTIMER_OK) {
        return false;
    }

    app->timer_report = mtimer_create(
        &app->timers,
        "report",
        app->config.report_interval_ms,
        MTIMER_PERIODIC,
        timer_report_cb,
        app);
    app->timer_watchdog = mtimer_create(
        &app->timers,
        "watchdog",
        1000u,
        MTIMER_PERIODIC,
        timer_watchdog_cb,
        app);
    app->timer_led = mtimer_create(
        &app->timers,
        "led",
        500u,
        MTIMER_PERIODIC,
        timer_led_cb,
        app);

    if (!log_status("mtimer_create(report)", app->timer_report) ||
        !log_status("mtimer_create(watchdog)", app->timer_watchdog) ||
        !log_status("mtimer_create(led)", app->timer_led)) {
        return false;
    }

    if ((mtimer_start(&app->timers, (uint8_t)app->timer_report) != MTIMER_OK) ||
        (mtimer_start(&app->timers, (uint8_t)app->timer_watchdog) != MTIMER_OK) ||
        (mtimer_start(&app->timers, (uint8_t)app->timer_led) != MTIMER_OK)) {
        return false;
    }
    return true;
}

static bool app_init_shell(app_t *app)
{
    if (msh_init(&app->shell, shell_print, app) != MSH_OK) {
        return false;
    }
    msh_set_prompt(&app->shell, "sensor> ");
    if ((msh_register(&app->shell, "status", "show counters", cmd_status) != MSH_OK) ||
        (msh_register(&app->shell, "conf", "show config or conf <key>", cmd_conf) != MSH_OK) ||
        (msh_register(&app->shell, "cbor", "encode sample payload", cmd_cbor) != MSH_OK) ||
        (msh_register(&app->shell, "breaker", "show breaker status", cmd_breaker) != MSH_OK) ||
        (msh_register(&app->shell, "bus", "show bus counters", cmd_bus) != MSH_OK) ||
        (msh_register(&app->shell, "timers", "show timer state", cmd_timers) != MSH_OK)) {
        return false;
    }
    return true;
}

static bool app_init(app_t *app)
{
    memset(app, 0, sizeof(*app));

    if (!app_init_logging()) {
        return false;
    }
    if (!app_init_config(app)) {
        return false;
    }
    if (!app_init_fsm(app)) {
        return false;
    }
    if (!app_init_ring(app)) {
        return false;
    }
    if (!app_init_resilience(app)) {
        return false;
    }
    if (!app_init_bus(app)) {
        return false;
    }
    if (!app_init_timers(app)) {
        return false;
    }
    if (!app_init_shell(app)) {
        return false;
    }
    return true;
}

static void app_process_sensor_queue(app_t *app)
{
    sensor_event_t event;
    mring_err_t ring_status;

    for (;;) {
        size_t payload_len = 0u;
        char topic[96];
        publish_ctx_t publish_ctx;
        int operation_result = -1;
        mres_err_t breaker_status;

        ring_status = mring_pop(&app->ring, &event);
        if (ring_status == MRING_ERR_EMPTY) {
            break;
        }
        if (ring_status != MRING_OK) {
            MLOG_ERROR("RING", "pop failed: %s", mring_err_str(ring_status));
            break;
        }

        if (!encode_telemetry(app, &event, &payload_len)) {
            app->publish_fail++;
            (void)mfsm_dispatch(&app->fsm, EV_PUBLISH_FAIL);
            continue;
        }

        app->cbor_bytes += (uint32_t)payload_len;
        snprintf(topic, sizeof(topic), "sensors/%s/telemetry", app->config.device_id);

        publish_ctx.app = app;
        publish_ctx.topic = topic;
        publish_ctx.payload = app->cbor_buf;
        publish_ctx.payload_len = payload_len;

        breaker_status = mres_breaker_call(
            &app->breaker,
            mqtt_publish_with_retry,
            &publish_ctx,
            &app->platform,
            &operation_result);

        if ((breaker_status == MRES_OK) && (operation_result == 0)) {
            app->publish_ok++;
            (void)mres_retry_reset(&app->retry);
            if (mbus_signal(&app->bus, MBUS_TOPIC_NETWORK) < 0) {
                MLOG_WARN("BUS", "%s", "signal failed");
            }
            (void)mfsm_dispatch(&app->fsm, EV_PUBLISH_OK);
            continue;
        }

        app->publish_fail++;
        MLOG_WARN("MQTT", "breaker_call status=%s op_result=%d",
            mres_err_str(breaker_status), operation_result);
        (void)mfsm_dispatch(&app->fsm, EV_PUBLISH_FAIL);
    }
}

static bool app_tick(app_t *app)
{
    int tick_status;

    tick_status = mtimer_tick(&app->timers);
    if (tick_status < 0) {
        MLOG_ERROR("TMR", "tick failed: %d", tick_status);
        return false;
    }

#if MBUS_ENABLE_QUEUE
    tick_status = mbus_dispatch(&app->bus);
    if (tick_status < 0) {
        MLOG_ERROR("BUS", "dispatch failed: %d", tick_status);
        return false;
    }
#endif

    app_process_sensor_queue(app);
    return true;
}

static void app_demo_shell(app_t *app)
{
    static const char *commands[] = {
        "help",
        "status",
        "conf",
        "conf mqtt_host",
        "cbor",
        "breaker",
        "bus",
        "timers",
    };
    size_t i;

    for (i = 0u; i < (sizeof(commands) / sizeof(commands[0])); ++i) {
        int status;
        msh_prompt(&app->shell);
        printf("%s\n", commands[i]);
        status = msh_exec(&app->shell, commands[i]);
        if (status < 0) {
            MLOG_WARN("SHELL", "command failed: %s => %d", commands[i], status);
        }
    }
}

#ifdef ESP_PLATFORM
void app_main(void)
{
    if (!app_init(&g_app)) {
        return;
    }
    (void)mfsm_dispatch(&g_app.fsm, EV_BOOT_READY);
    (void)mfsm_dispatch(&g_app.fsm, EV_NETWORK_READY);
    for (;;) {
        if (!app_tick(&g_app)) {
            plat_sleep_ms(100u);
        }
        plat_sleep_ms(50u);
    }
}
#else
int main(void)
{
    size_t i;

    srand(42);
    if (!app_init(&g_app)) {
        return 1;
    }

    (void)mfsm_dispatch(&g_app.fsm, EV_BOOT_READY);
    (void)mfsm_dispatch(&g_app.fsm, EV_NETWORK_READY);

    for (i = 0u; i < 12u; ++i) {
        if (!app_tick(&g_app)) {
            return 1;
        }
        plat_sleep_ms(100u);
        if (i == 8u) {
            (void)mres_breaker_reset(&g_app.breaker);
            (void)mfsm_dispatch(&g_app.fsm, EV_RECOVER);
            (void)mfsm_dispatch(&g_app.fsm, EV_NETWORK_READY);
        }
    }

    app_demo_shell(&g_app);
    return 0;
}
#endif
