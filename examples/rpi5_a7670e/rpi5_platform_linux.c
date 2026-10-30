/**
 * @file rpi5_platform_linux.c
 * @brief Raspberry Pi 5 Linux hardware adapter implementing non-blocking termios UART,
 *        Linux GPIO v2 character device ioctl (/dev/gpiochip4 -> /dev/gpiochip0) for BCM GPIO 4,
 *        poll()-paced RX ingestion, and CLOCK_MONOTONIC time source (T007).
 */

#include "rpi5_platform.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

/* ========================================================================== */
/* Internal Helpers                                                           */
/* ========================================================================== */

static uint64_t read_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static speed_t map_baud_to_speed(uint32_t baud_rate)
{
    switch (baud_rate) {
    case 9600U:   return B9600;
    case 19200U:  return B19200;
    case 38400U:  return B38400;
    case 57600U:  return B57600;
    case 115200U: return B115200;
    case 230400U: return B230400;
#ifdef B460800
    case 460800U: return B460800;
#endif
#ifdef B921600
    case 921600U: return B921600;
#endif
    default:      return B115200;
    }
}

static bool open_uart_nonblocking(rpi5_linux_ctx_t * const linux_ctx, const rpi5_example_cfg_t * const cfg)
{
    const int fd = open(cfg->serial_port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        (void)fprintf(stderr,
                      "[ERROR] Cannot open serial port '%s': %s (check raspi-config UART0 and 'dialout' group).\n",
                      cfg->serial_port,
                      strerror(errno));
        return false;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        (void)fprintf(stderr, "[ERROR] tcgetattr failed on '%s': %s\n", cfg->serial_port, strerror(errno));
        (void)close(fd);
        return false;
    }

    /* Raw 8N1 mode, no echo, no flow control */
    cfmakeraw(&tio);
    tio.c_cflag |= (tcflag_t)(CLOCAL | CREAD | CS8);
    tio.c_cflag &= (tcflag_t)~(CSTOPB | PARENB);
#ifdef CRTSCTS
    tio.c_cflag &= (tcflag_t)~CRTSCTS;
#endif
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    const speed_t spd = map_baud_to_speed(cfg->baud_rate);
    (void)cfsetispeed(&tio, spd);
    (void)cfsetospeed(&tio, spd);

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        (void)fprintf(stderr, "[ERROR] tcsetattr failed on '%s': %s\n", cfg->serial_port, strerror(errno));
        (void)close(fd);
        return false;
    }

    (void)tcflush(fd, TCIOFLUSH);
    linux_ctx->uart_fd = (int32_t)fd;
    return true;
}

static bool open_gpio_pwrkey_line(rpi5_linux_ctx_t * const linux_ctx, const rpi5_example_cfg_t * const cfg)
{
    int chip_fd = -1;
    const char *used_chip = cfg->gpio_chip;

    if (used_chip != NULL) {
        chip_fd = open(used_chip, O_RDONLY | O_CLOEXEC);
    } else {
        /* Raspberry Pi 5 RP1 40-pin header is exposed at /dev/gpiochip4 (or /dev/gpiochip0 on newer kernels) */
        used_chip = "/dev/gpiochip4";
        chip_fd = open(used_chip, O_RDONLY | O_CLOEXEC);
        if (chip_fd < 0) {
            used_chip = "/dev/gpiochip0";
            chip_fd = open(used_chip, O_RDONLY | O_CLOEXEC);
        }
    }

    if (chip_fd < 0) {
        (void)fprintf(stderr,
                      "[ERROR] Cannot open GPIO character device '%s': %s (check 'gpio' group permissions).\n",
                      used_chip,
                      strerror(errno));
        return false;
    }

    struct gpio_v2_line_request req;
    (void)memset(&req, 0, sizeof(req));
    req.offsets[0] = cfg->pwrkey_gpio_pin;
    req.num_lines = 1U;
    req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    req.config.num_attrs = 1U;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = 0ULL; /* Start LOW (PWRKEY released) */
    req.config.attrs[0].mask = 1ULL;
    (void)strncpy(req.consumer, "libsimcom-rpi5-a7670e", sizeof(req.consumer) - 1U);

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        (void)fprintf(stderr,
                      "[ERROR] GPIO_V2_GET_LINE_IOCTL failed for BCM GPIO %u on '%s': %s\n",
                      (unsigned int)cfg->pwrkey_gpio_pin,
                      used_chip,
                      strerror(errno));
        (void)close(chip_fd);
        return false;
    }

    linux_ctx->gpio_chip_fd = (int32_t)chip_fd;
    linux_ctx->gpio_line_fd = (int32_t)req.fd;
    return true;
}

/* ========================================================================== */
/* Platform Interface Callbacks                                               */
/* ========================================================================== */

static size_t linux_uart_tx(const uint8_t *data, size_t len, void *ctx)
{
    rpi5_linux_ctx_t * const linux_ctx = (rpi5_linux_ctx_t *)ctx;
    if ((linux_ctx == NULL) || (linux_ctx->uart_fd < 0) || (data == NULL) || (len == 0U)) {
        return 0U;
    }

    const ssize_t written = write(linux_ctx->uart_fd, data, len);
    if (written <= 0) {
        return 0U;
    }
    return (size_t)written;
}

static uint32_t linux_get_tick_ms(void *ctx)
{
    rpi5_linux_ctx_t * const linux_ctx = (rpi5_linux_ctx_t *)ctx;
    const uint64_t now_ms = read_monotonic_ms();
    if (linux_ctx == NULL) {
        return (uint32_t)now_ms;
    }
    return (uint32_t)(now_ms - linux_ctx->boot_epoch_ms);
}

static bool linux_gpio_set_pwrkey(bool high, void *ctx)
{
    rpi5_linux_ctx_t * const linux_ctx = (rpi5_linux_ctx_t *)ctx;
    if ((linux_ctx == NULL) || (linux_ctx->gpio_line_fd < 0)) {
        return false;
    }

    struct gpio_v2_line_values vals;
    (void)memset(&vals, 0, sizeof(vals));
    vals.bits = high ? 1ULL : 0ULL;
    vals.mask = 1ULL;

    return (ioctl(linux_ctx->gpio_line_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) == 0);
}

static void linux_console_print(const char *text, void *ctx)
{
    (void)ctx;
    if (text != NULL) {
        (void)fputs(text, stdout);
        (void)fflush(stdout);
    }
}

/* ========================================================================== */
/* Public Linux Platform Functions                                            */
/* ========================================================================== */

void Rpi5Platform_BindCallbacks(
    rpi5_linux_ctx_t * const linux_ctx,
    rpi5_platform_if_t * const out_if)
{
    if (linux_ctx != NULL) {
        linux_ctx->uart_fd = -1;
        linux_ctx->gpio_chip_fd = -1;
        linux_ctx->gpio_line_fd = -1;
        linux_ctx->boot_epoch_ms = read_monotonic_ms();
    }
    if (out_if != NULL) {
        out_if->uart_tx = linux_uart_tx;
        out_if->get_tick_ms = linux_get_tick_ms;
        out_if->gpio_set_pwrkey = linux_gpio_set_pwrkey;
        out_if->console_print = linux_console_print;
        out_if->ctx = linux_ctx;
    }
}

bool Rpi5Platform_Open(
    rpi5_linux_ctx_t * const linux_ctx,
    const rpi5_example_cfg_t * const cfg)
{
    if ((linux_ctx == NULL) || (cfg == NULL)) {
        return false;
    }

    if (!open_gpio_pwrkey_line(linux_ctx, cfg)) {
        return false;
    }
    if (!open_uart_nonblocking(linux_ctx, cfg)) {
        Rpi5Platform_Close(linux_ctx);
        return false;
    }
    return true;
}

void Rpi5Platform_PollRx(
    rpi5_linux_ctx_t * const linux_ctx,
    rpi5_app_t * const app,
    int32_t timeout_ms)
{
    if ((linux_ctx == NULL) || (linux_ctx->uart_fd < 0) || (app == NULL)) {
        return;
    }

    struct pollfd pfd;
    pfd.fd = linux_ctx->uart_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    const int ready = poll(&pfd, 1U, timeout_ms);
    if ((ready > 0) && ((pfd.revents & POLLIN) != 0)) {
        uint8_t rx_chunk[128];
        const ssize_t n = read(linux_ctx->uart_fd, rx_chunk, sizeof(rx_chunk));
        if (n > 0) {
            (void)Rpi5App_FeedBytes(app, rx_chunk, (size_t)n);
        }
    }
}

void Rpi5Platform_Close(rpi5_linux_ctx_t * const linux_ctx)
{
    if (linux_ctx == NULL) {
        return;
    }
    if (linux_ctx->gpio_line_fd >= 0) {
        (void)linux_gpio_set_pwrkey(false, linux_ctx);
        (void)close(linux_ctx->gpio_line_fd);
        linux_ctx->gpio_line_fd = -1;
    }
    if (linux_ctx->gpio_chip_fd >= 0) {
        (void)close(linux_ctx->gpio_chip_fd);
        linux_ctx->gpio_chip_fd = -1;
    }
    if (linux_ctx->uart_fd >= 0) {
        (void)close(linux_ctx->uart_fd);
        linux_ctx->uart_fd = -1;
    }
}
