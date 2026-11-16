#include <pspkernel.h>
#include <pspinit.h>
#include <pspiofilemgr.h>
#include <string.h>

#include "hook.h"
#include "log.h"
#include "overlay.h"
#include "runtime.h"

#define EUR_ID "ULES-01519"
#define USA_ID "ULUS-10563"
#define OBSERVER_INTERVAL_MS 20
#define OVERLAY_POLL_TICKS 5
#define MAINTENANCE_TICKS 10
#define MODULE_POLL_TICKS 25
#ifndef GEB_PROBE_VERSION
#define GEB_PROBE_VERSION "3.7.8"
#endif

PSP_MODULE_INFO("GEBHookProbe",
    PSP_MODULE_KERNEL | PSP_MODULE_NO_STOP | PSP_MODULE_SINGLE_LOAD |
    PSP_MODULE_SINGLE_START, 1, 0);

static volatile int g_running;
static volatile int g_worker_started;
static SceUID g_worker = -1;
static HookModuleStartHandler g_previous_handler;

#if GEB_ENABLE_LOGGING
static int read_game_id(char game_id[11])
{
    SceUID fd = sceIoOpen("disc0:/UMD_DATA.BIN", PSP_O_RDONLY, 0);
    int length;
    if (fd < 0) {
        return (int)fd;
    }
    length = sceIoRead(fd, game_id, 10);
    sceIoClose(fd);
    if (length != 10) {
        return -1;
    }
    game_id[10] = '\0';
    return 0;
}
#endif

static int probe_thread(SceSize args, void *argp)
{
#if GEB_ENABLE_LOGGING
    char game_id[11];
    char *init_file;
    int result;
    int target_active = 0;
    int disc_checked = 0;
#endif
    unsigned int tick = 0;
    int hook_result;
    int overlay_result;
    (void)args;
    (void)argp;
    sceKernelDelayThread(500000);

#if GEB_ENABLE_LOGGING
    result = log_open();
    if (result < 0) {
        return sceKernelExitDeleteThread(0);
    }
    log_printf("=== GEB Enemy HP Probe %s DEBUG ===\n", GEB_PROBE_VERSION);
    log_printf("[POLICY] logging=EVENT_ONLY rendering=ON build=DEBUG maintenance_ms=200\n");
    log_printf("[RUNTIME] platform=%s startup=%s status=PASS\n",
        geb_runtime_is_ppsspp() ? "PPSSPP" : "PSP",
        geb_runtime_is_ppsspp() ? "POLLING" : "ARK_HANDLER");
    if (!geb_runtime_is_ppsspp()) {
        init_file = sceKernelInitFileName();
        log_printf("[GAME-CONTEXT] key_config=%08X api_type=%08X boot_from=%08X init=%s\n",
            (u32)sceKernelInitKeyConfig(), (u32)sceKernelInitApitype(),
            (u32)sceKernelBootFrom(), init_file != 0 ? init_file : "(null)");
    }
#endif
    /* PPSSPP loads plugins before the game starts; module_start must return promptly. */
    if (geb_runtime_is_ppsspp()) {
        unsigned int wait_ticks = 0;
        log_printf("[BOOT] target=main status=WAITING\n");
        while (g_running && !hook_target_module_matches(hook_find_module_by_name("main"))) {
            if (++wait_ticks == 250) {
                log_printf("[BOOT] target=main status=WAITING_FINGERPRINT elapsed_ms=5000\n");
            }
            sceKernelDelayThread(OBSERVER_INTERVAL_MS * 1000);
        }
        if (!g_running) {
            return sceKernelExitDeleteThread(0);
        }
        log_printf("[BOOT] target=main status=READY\n");
    }
    /* Stable starts observation/rendering independently of the log filesystem. */
    hook_probe_start();
    hook_result = hook_enemy_install();
    if (hook_result < 0) {
        log_printf("[HOOK] enemy_capture status=FAIL error=%08X\n", (u32)hook_result);
    }
    overlay_result = overlay_install();
    if (overlay_result < 0) {
        log_printf("[UI] install status=FAIL error=%08X observer=ACTIVE\n", (u32)overlay_result);
    }
    while (g_running) {
#if GEB_ENABLE_LOGGING
        u32 work_started = sceKernelGetSystemTimeLow();
#endif
        if ((tick % MODULE_POLL_TICKS) == 0) {
#if GEB_ENABLE_LOGGING
            if (hook_probe_poll()) {
                target_active = 1;
            }
            if (target_active && !disc_checked) {
                result = read_game_id(game_id);
                if (result == 0) {
                    const char *region = strcmp(game_id, EUR_ID) == 0 ? "EUR" :
                        (strcmp(game_id, USA_ID) == 0 ? "USA" : "UNKNOWN");
                    log_printf("[GAME] source=disc0 id=%s region=%s status=%s expected=%s/%s\n",
                        game_id, region, strcmp(region, "UNKNOWN") != 0 ? "PASS" : "MISMATCH",
                        EUR_ID, USA_ID);
                } else {
                    log_printf("[GAME] source=disc0 status=UNAVAILABLE error=%08X action=continue_passive\n",
                        (u32)result);
                }
                disc_checked = 1;
            }
#else
            hook_probe_poll();
#endif
        }
        if ((tick % OVERLAY_POLL_TICKS) == 0) {
            overlay_poll();
        }
        hook_enemy_poll((tick % MAINTENANCE_TICKS) == 0);
#if GEB_ENABLE_LOGGING
        hook_record_observer_work(sceKernelGetSystemTimeLow() - work_started);
#endif
        ++tick;
        sceKernelDelayThread(OBSERVER_INTERVAL_MS * 1000);
    }
    return sceKernelExitDeleteThread(0);
}

static int start_worker(void)
{
    int result;

    if (g_worker_started) {
        return 0;
    }
    g_worker_started = 1;
    g_worker = sceKernelCreateThread("GEBHookProbe", probe_thread, 0x30, 0x2000, 0, 0);
    if (g_worker < 0) {
        result = (int)g_worker;
        g_worker = -1;
        g_worker_started = 0;
        return result;
    }
    result = sceKernelStartThread(g_worker, 0, 0);
    if (result < 0) {
        sceKernelDeleteThread(g_worker);
        g_worker = -1;
        g_worker_started = 0;
        return result;
    }
    return 0;
}

static int on_module_start(GebModule *module)
{
    if (!g_worker_started && hook_target_module_matches(module)) {
        start_worker();
    }
    if (g_previous_handler != 0) {
        return g_previous_handler(module);
    }
    return 0;
}

int module_start(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    g_running = 1;
    g_worker_started = 0;
    g_worker = -1;
    g_previous_handler = 0;
    if (geb_runtime_detect()) {
        return start_worker();
    }
    return hook_register_start_handler(on_module_start, &g_previous_handler);
}

int module_stop(SceSize args, void *argp)
{
    SceUID worker = g_worker;
    SceUID current = sceKernelGetThreadId();
    (void)args;
    (void)argp;
    g_running = 0;
    if (worker >= 0 && worker != current) {
        SceUInt timeout = 1000000;
        if (sceKernelWaitThreadEnd(worker, &timeout) < 0) {
            sceKernelTerminateDeleteThread(worker);
        }
    }
    g_worker = -1;
    overlay_remove();
    hook_enemy_remove();
    if (log_is_open()) {
        hook_probe_stop();
    }
    log_close();
    return 0;
}
