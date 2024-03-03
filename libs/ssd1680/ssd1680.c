#include "ssd1680.h"
#include "ssd1680_regmap.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unistd.h>

#include <string.h>
#include <sys/time.h>

static struct timeval tv_now;

typedef struct
{
    uint16_t mux : 9;
    uint16_t : 7;
    uint8_t gd : 1;
    uint8_t sm : 1;
    uint8_t tb : 1;
    uint8_t : 5;
} ssd1680_gate_t;

typedef struct
{
    uint8_t start;
    uint8_t stop;
} ssd1680_x_window_t;

typedef struct
{
    uint16_t start;
    uint16_t stop;
} ssd1680_y_window_t;

static inline int64_t get_time(void)
{
    gettimeofday(&tv_now, NULL);
    return (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
}

static void ssd1680_wait_busy(ssd1680_t *disp)
{
    vTaskDelay(1);
    // LOW: idle, HIGH: busy
    while (gpio_get_level(disp->pinmap.busy) == 1)
    {
        vTaskDelay(1);
    }
}

static void ssd1680_write(ssd1680_t *disp, ssd1360_regmap_t cmd, void *data, size_t data_size)
{
    static spi_transaction_t trs;
    
    trs.length = 8;
    trs.tx_buffer = &cmd;
    gpio_set_level(disp->pinmap.dc, 0);
    gpio_set_level(disp->pinmap.cs, 0);
    spi_device_polling_transmit(disp->spi_device, &trs);
    gpio_set_level(disp->pinmap.cs, 1);

    if (data == NULL || data_size == 0) return;

    trs.length = data_size * 8;
    trs.tx_buffer = data;
    gpio_set_level(disp->pinmap.dc, 1);
    gpio_set_level(disp->pinmap.cs, 0);
    spi_device_polling_transmit(disp->spi_device, &trs);
    gpio_set_level(disp->pinmap.cs, 1);
}

static void ssd1680_hw_reset(ssd1680_t *disp)
{
    gpio_set_level(disp->pinmap.reset, 1);
    usleep(50);
    gpio_set_level(disp->pinmap.reset, 0);
    usleep(200);
    gpio_set_level(disp->pinmap.reset, 1);
    ssd1680_wait_busy(disp);
    vTaskDelay(10);
}

static void ssd1680_sw_reset(ssd1680_t *disp)
{
    ssd1680_write(disp, SSD1360_SW_RESET, NULL, 0);
    ssd1680_wait_busy(disp);
    vTaskDelay(100);
}

static void ssd1680_set_x_window(ssd1680_t *disp, uint16_t x_start, uint16_t x_stop)
{
    uint8_t wnd[2] = {
        x_start & 0x1F,
        x_stop & 0x1F,
    };
    ssd1680_write(disp, SSD1360_SET_RAM_X_ADDR, &wnd, sizeof(ssd1680_x_window_t));
    ssd1680_wait_busy(disp);
}

static void ssd1680_set_y_window(ssd1680_t *disp, uint16_t y_start, uint16_t y_stop)
{
    uint8_t wnd[4] = {
        y_start & 0xFF,
        (y_start >> 8) & 0x01,
        y_stop & 0xFF,
        (y_stop >> 8) & 0x01,
    };
    ssd1680_write(disp, SSD1360_SET_RAM_Y_ADDR, &wnd, sizeof(wnd));
    ssd1680_wait_busy(disp);
}

static void ssd1680_set_ram_pos(ssd1680_t *disp, uint16_t x_pos, uint16_t y_pos)
{
    disp->pos_x = x_pos & 0x1F;
    disp->pos_y = y_pos & 0x1FF;
    ssd1680_write(disp, SSD1360_SET_RAM_X_ADDR_CNT, &disp->pos_x, sizeof(uint8_t));
    ssd1680_wait_busy(disp);
    ssd1680_write(disp, SSD1360_SET_RAM_Y_ADDR_CNT, &disp->pos_y, sizeof(uint16_t));
    ssd1680_wait_busy(disp);
}

static void ssd1680_setup_gate_driver(ssd1680_t *disp)
{
    ssd1680_gate_t gate = {
        .mux = disp->rows_cnt - 1,
        .gd = 0,
        .sm = 0,
        .tb = 0,
    };
    ssd1680_write(disp, SSD1360_DRIVER_OUTPUT_CTRL, &gate, sizeof(ssd1680_gate_t));
    ssd1680_wait_busy(disp);
}

static void ssd1680_setup_border(ssd1680_t *disp)
{
    uint8_t b = 0b00000101;
    ssd1680_write(disp, SSD1360_BORDER_WAVEFORM_CTRL, &b, sizeof(uint8_t));
    ssd1680_wait_busy(disp);
}

static void ssd1680_setup_booster(ssd1680_t *disp)
{
    const uint8_t driver_strength = 0b010;
    const uint8_t min_off_time = 0b0100;

    uint8_t booster[4] = {
        (1 << 7) | (driver_strength << 4) | (min_off_time),
        (1 << 7) | (driver_strength << 4) | (min_off_time),
        (1 << 7) | (driver_strength << 4) | (min_off_time),
        0b00000101,
    };
    ssd1680_write(disp, SSD1360_BOOSTER_SOFT_START_CTRL, &booster, sizeof(booster));
    ssd1680_wait_busy(disp);
}

static void ssd1680_setup_ram(ssd1680_t *disp)
{
    // Set data entry mode
    uint8_t b;
    uint16_t xwnd_start, xwnd_stop;
    uint16_t ywnd_start, ywnd_stop;
    switch (disp->orientation)
    {
    case SSD1680_90_DEG:
        b = 0b110;
        xwnd_start = disp->clmn_cnt - 1;
        xwnd_stop = 0;
        ywnd_start = 0;
        ywnd_stop = disp->rows_cnt - 1;
        break;
    case SSD1680_180_DEG:
        b = 0b000;
        xwnd_start = disp->clmn_cnt - 1;
        xwnd_stop = 0;
        ywnd_start = disp->rows_cnt - 1;
        ywnd_stop = 0;
        break;
    case SSD1680_270_DEG:
        b = 0b101;
        xwnd_start = 0;
        xwnd_stop = disp->clmn_cnt - 1;
        ywnd_start = disp->rows_cnt - 1;
        ywnd_stop = 0;
        break;
    default: // SSD1680_NORMAL
        b = 0b011;
        xwnd_start = 0;
        xwnd_stop = disp->clmn_cnt - 1;
        ywnd_start = 0;
        ywnd_stop = disp->rows_cnt - 1;
        break;
    }
    ssd1680_write(disp, SSD1360_DATA_ENTRY_MODE, &b, sizeof(uint8_t));
    ssd1680_wait_busy(disp);

    // Set draw window
    ssd1680_set_x_window(disp, xwnd_start, xwnd_stop);
    ssd1680_set_y_window(disp, ywnd_start, ywnd_stop);

    // Setup border waveform
    ssd1680_setup_border(disp);

    // Setup update control
    uint8_t ctrl_1[2] = {0, 0x80};
    ssd1680_write(disp, SSD1360_DISP_UPDATE_CTRL_1, &ctrl_1, sizeof(ctrl_1));
    ssd1680_wait_busy(disp);
    uint8_t ctrl_2 = 0xF7;
    ssd1680_write(disp, SSD1360_DISP_UPDATE_CTRL_2, &ctrl_2, sizeof(ctrl_2));
    ssd1680_wait_busy(disp);
}

static void ssd1680_init_sequence(ssd1680_t *disp)
{
    ssd1680_setup_gate_driver(disp);
    ssd1680_setup_booster(disp);
    ssd1680_setup_ram(disp);
}

ssd1680_t *ssd1680_init(spi_host_device_t spi_host, ssd1680_pinmap_t pinmap, uint16_t res_x, uint16_t res_y, ssd1680_orientation_t orientation)
{
    ssd1680_t *disp = malloc(sizeof(ssd1680_t));
    if (disp == NULL)
        return NULL;

    disp->spi_host = spi_host;
    disp->pinmap = pinmap;

    disp->orientation = orientation;
    disp->res_x = res_x;
    disp->res_y = res_y;

    switch (disp->orientation)
    {
    case SSD1680_90_DEG: case SSD1680_270_DEG:
        disp->clmn_cnt = (res_y + 7) / 8;
        disp->rows_cnt = res_x;
        break;
    default: // SSD1680_NORMAL || SSD1680_180_DEG
        disp->clmn_cnt = (res_x + 7) / 8;
        disp->rows_cnt = res_y;
        break;
    }

    printf("clmns: %d, rows: %d\r\n", disp->clmn_cnt, disp->rows_cnt);

    disp->framebuffer_size = disp->clmn_cnt * disp->rows_cnt;

    disp->framebuffer_bw = malloc(disp->framebuffer_size);
    if (disp->framebuffer_bw == NULL)
        goto error;

    disp->framebuffer_red = malloc(disp->framebuffer_size);
    if (disp->framebuffer_red == NULL)
        goto error;

    gpio_set_direction(disp->pinmap.busy, GPIO_MODE_INPUT);
    gpio_set_pull_mode(disp->pinmap.busy, GPIO_PULLUP_ONLY);
    gpio_set_direction(disp->pinmap.reset, GPIO_MODE_OUTPUT);
    gpio_set_direction(disp->pinmap.dc, GPIO_MODE_OUTPUT);
    gpio_set_direction(disp->pinmap.cs, GPIO_MODE_OUTPUT);
    gpio_set_level(disp->pinmap.reset, 1);
    gpio_set_level(disp->pinmap.dc, 1);
    gpio_set_level(disp->pinmap.cs, 1);

    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = SPI_MASTER_FREQ_20M,
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    esp_err_t err = spi_bus_add_device(spi_host, &devcfg, &disp->spi_device);
    if (err != ESP_OK)
        goto error;

    ssd1680_hw_reset(disp);
    ssd1680_sw_reset(disp);

    ssd1680_init_sequence(disp);
    ssd1680_fill(disp, SSD1680_WHITE);
    ssd1680_refresh(disp);

    return disp;

error:
    free(disp->framebuffer_bw);
    free(disp->framebuffer_red);
    free(disp);
    return NULL;
}

void ssd1680_deinit(ssd1680_t *disp)
{
    ssd1680_sleep(disp);
    spi_bus_remove_device(disp->spi_device);
    free(disp->framebuffer_bw);
    free(disp->framebuffer_red);
    free(disp);
}

void ssd1680_sleep(ssd1680_t *disp)
{
    uint8_t mode = 0b01;
    ssd1680_write(disp, SSD1360_DEEP_SLEEP_MODE, &mode, 1);
}

void ssd1680_wakeup(ssd1680_t *disp)
{
    ssd1680_hw_reset(disp);
    ssd1680_init_sequence(disp);
    ssd1680_send_framebuffer(disp);
}

void ssd1680_set_pixel(ssd1680_t *disp, uint16_t x, uint16_t y, ssd1680_color_t color)
{
    int idx, offset;

    switch (disp->orientation)
    {
    case SSD1680_90_DEG: case SSD1680_270_DEG:
        idx = (y >> 3);
        offset = 7 - (y - (idx << 3));
        idx = idx * disp->rows_cnt + x;
        break;
    default: // SSD1680_NORMAL || SSD1680_180_DEG
        idx = (x >> 3) + y * disp->clmn_cnt;
        offset = 7 - (x % 8);
        break;
    }

    disp->framebuffer_bw[idx] &= ~(1 << offset);
    disp->framebuffer_bw[idx] |= (color & 0x1) << offset;
    disp->framebuffer_red[idx] &= ~(1 << offset);
    disp->framebuffer_red[idx] |= ((color >> 1) & 0x1) << offset;
}

void ssd1680_fill(ssd1680_t *disp, ssd1680_color_t color)
{
    memset(disp->framebuffer_bw, (color & 0x1) * 0xFF, disp->framebuffer_size);
    memset(disp->framebuffer_red, ((color >> 1) & 0x1) * 0xFF, disp->framebuffer_size);
    ssd1680_send_framebuffer(disp);
}

void ssd1680_send_framebuffer(ssd1680_t *disp)
{
    switch (disp->orientation)
    {
    case SSD1680_90_DEG:
        ssd1680_set_ram_pos(disp, disp->clmn_cnt - 1, 0);
        break;
    case SSD1680_180_DEG:
        ssd1680_set_ram_pos(disp, disp->clmn_cnt - 1, disp->rows_cnt - 1);
        break;
    case SSD1680_270_DEG:
        ssd1680_set_ram_pos(disp, 0, disp->rows_cnt - 1);
        break;
    default: // SSD1680_NORMAL
        ssd1680_set_ram_pos(disp, 0, 0);
        break;
    }
    ssd1680_write(disp, SSD1360_WRITE_RAM_BW, disp->framebuffer_bw, disp->framebuffer_size);
    ssd1680_wait_busy(disp);
    ssd1680_write(disp, SSD1360_WRITE_RAM_RED, disp->framebuffer_red, disp->framebuffer_size);
    ssd1680_wait_busy(disp);
}

void ssd1680_set_refresh_window(ssd1680_t *disp, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    switch (disp->orientation)
    {
    case SSD1680_90_DEG:
        ssd1680_set_x_window(disp, y2 >> 3, y1 >> 3);
        ssd1680_set_y_window(disp, x1, x2);
        break;
    case SSD1680_180_DEG:
        ssd1680_set_x_window(disp, x2 >> 3, x1 >> 3);
        ssd1680_set_y_window(disp, y2, y1);
        break;
    case SSD1680_270_DEG:
        ssd1680_set_x_window(disp, y1 >> 3, y2 >> 3);
        ssd1680_set_y_window(disp, x2, x1);
        break;
    default: // SSD1680_NORMAL
        ssd1680_set_x_window(disp, x1 >> 3, x2 >> 3);
        ssd1680_set_y_window(disp, y1, y2);
        break;
    }
}

void ssd1680_refresh(ssd1680_t *disp)
{
    int64_t t = get_time();
    ssd1680_write(disp, SSD1360_MASTER_ACTIVATION, NULL, 0);
    ssd1680_wait_busy(disp);
    t = get_time() - t;
    printf("refresh time: %lld\r\n", t);
}
