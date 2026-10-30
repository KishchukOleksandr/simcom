/**
 * @file main.c
 * @brief Raspberry Pi 5 & Waveshare A7670E Cat-1/GNSS HAT reference application entry point (T012).
 */

#include "rpi5_app.h"
#include "rpi5_platform.h"

#include <signal.h>
#include <stdio.h>

static rpi5_app_t s_app;
static rpi5_linux_ctx_t s_linux_ctx;
static volatile sig_atomic_t s_stop_requested = 0;

static void handle_termination_signal(int sig)
{
    (void)sig;
    s_stop_requested = 1;
}

int main(int argc, char *argv[])
{
    rpi5_platform_if_t platform_if;
    rpi5_example_cfg_t cfg;

    Rpi5Platform_BindCallbacks(&s_linux_ctx, &platform_if);

    if (Rpi5App_ParseArgs((int32_t)argc, (const char * const *)argv, &cfg) != SIMCOM_STATUS_OK) {
        (void)fprintf(stderr, "[ERROR] Invalid command-line arguments. Use --help for usage.\n");
        Rpi5App_PrintHelp((argc > 0) ? argv[0] : NULL, &platform_if);
        return 1;
    }

    if (cfg.show_help) {
        Rpi5App_PrintHelp((argc > 0) ? argv[0] : NULL, &platform_if);
        return 0;
    }

    struct sigaction sa;
    sa.sa_handler = handle_termination_signal;
    (void)sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);

    if (!Rpi5Platform_Open(&s_linux_ctx, &cfg)) {
        return 1;
    }

    if (Rpi5App_Init(&s_app, &cfg, &platform_if) != SIMCOM_STATUS_OK) {
        (void)fprintf(stderr, "[ERROR] Rpi5App_Init failed.\n");
        Rpi5Platform_Close(&s_linux_ctx);
        return 1;
    }

    if (Rpi5App_Start(&s_app) != SIMCOM_STATUS_OK) {
        Rpi5Platform_Close(&s_linux_ctx);
        return (int)s_app.exit_code;
    }

    while (!Rpi5App_IsFinished(&s_app)) {
        if (s_stop_requested != 0) {
            Rpi5App_RequestStop(&s_app);
            break;
        }
        Rpi5Platform_PollRx(&s_linux_ctx, &s_app, 5);
        Rpi5App_Poll(&s_app);
    }

    Rpi5Platform_Close(&s_linux_ctx);
    return (int)s_app.exit_code;
}
