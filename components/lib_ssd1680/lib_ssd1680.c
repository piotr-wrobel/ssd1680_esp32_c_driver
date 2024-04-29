#include "lib_ssd1680.h"
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

static void ssd1680_write(ssd1680_t *disp, ssd1680_regmap_t cmd, void *data, size_t data_size)
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

//I have a problem with this function, it stopped working, the controller hangs when receiving data via SPI. I'm looking for a solution.....

static void ssd1680_read(ssd1680_t *disp, ssd1680_regmap_t cmd, void *data, size_t data_size)
{
    static spi_transaction_t trs;

    printf("\r\ndata_size: %d\r\n", data_size);
    trs.length = 8 + (data_size * 8);
    trs.rxlength = data_size * 8;
    trs.tx_buffer = &cmd;
    trs.rx_buffer = data;
    gpio_set_level(disp->pinmap.dc, 0);
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
    ssd1680_write(disp, SSD1680_SW_RESET, NULL, 0);
    ssd1680_wait_busy(disp);
    vTaskDelay(100);
}

static void ssd1680_set_x_window(ssd1680_t *disp, uint16_t x_start, uint16_t x_stop)
{
    uint8_t wnd[2] = {
        x_start & 0x1F,
        x_stop & 0x1F,
    };
    ssd1680_write(disp, SSD1680_SET_RAM_X_ADDR, &wnd, sizeof(ssd1680_x_window_t));
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
    ssd1680_write(disp, SSD1680_SET_RAM_Y_ADDR, &wnd, sizeof(wnd));
    ssd1680_wait_busy(disp);
}

static void ssd1680_set_ram_pos(ssd1680_t *disp, uint16_t x_pos, uint16_t y_pos)
{
    disp->pos_x = x_pos & 0x1F;
    disp->pos_y = y_pos & 0x1FF;
    ssd1680_write(disp, SSD1680_SET_RAM_X_ADDR_CNT, &disp->pos_x, sizeof(uint8_t));
    ssd1680_wait_busy(disp);
    ssd1680_write(disp, SSD1680_SET_RAM_Y_ADDR_CNT, &disp->pos_y, sizeof(uint16_t));
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
    ssd1680_write(disp, SSD1680_DRIVER_OUTPUT_CTRL, &gate, sizeof(ssd1680_gate_t));
    ssd1680_wait_busy(disp);
}

static void ssd1680_setup_border(ssd1680_t *disp)
{
    uint8_t b = 0b00000101;
    ssd1680_write(disp, SSD1680_BORDER_WAVEFORM_CTRL, &b, sizeof(uint8_t));
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
    ssd1680_write(disp, SSD1680_BOOSTER_SOFT_START_CTRL, &booster, sizeof(booster));
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
    ssd1680_write(disp, SSD1680_DATA_ENTRY_MODE, &b, sizeof(uint8_t));
    ssd1680_wait_busy(disp);

    // Set draw window
    ssd1680_set_x_window(disp, xwnd_start, xwnd_stop);
    ssd1680_set_y_window(disp, ywnd_start, ywnd_stop);

    // Setup border waveform
    ssd1680_setup_border(disp);

    // Setup update control
    uint8_t ctrl_1[2] = {0, 0x80};
    ssd1680_write(disp, SSD1680_DISP_UPDATE_CTRL_1, &ctrl_1, sizeof(ctrl_1));
    ssd1680_wait_busy(disp);
    uint8_t ctrl_2 = FULL_REFRESH;
    ssd1680_write(disp, SSD1680_DISP_UPDATE_CTRL_2, &ctrl_2, sizeof(ctrl_2));
    ssd1680_wait_busy(disp);
}
static void ssd1680_temp_sensor_ctrl(ssd1680_t *disp, uint8_t tmp_sensor)
{
	ssd1680_write(disp, SSD1680_TEMP_SENS_CTRL, &tmp_sensor, sizeof(uint8_t));
	ssd1680_wait_busy(disp);
}

static void ssd1680_init_sequence(ssd1680_t *disp)
{
    ssd1680_setup_gate_driver(disp);
    ssd1680_setup_booster(disp);
    ssd1680_setup_ram(disp);
    ssd1680_temp_sensor_ctrl(disp, TMP_INTERNAL_SENSOR);
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

	disp->clmn_cnt = (res_x + 7) / 8;
	disp->rows_cnt = res_y;

#ifdef DEBUG
    printf("clmns: %d, rows: %d\r\n", disp->clmn_cnt, disp->rows_cnt);
#endif

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
        //.clock_speed_hz = SPI_MASTER_FREQ_20M,
		.clock_speed_hz = SPI_MASTER_FREQ_8M,
        .spics_io_num = -1,
        .queue_size = 1,
        //.flags = SPI_DEVICE_HALFDUPLEX,
    };

    esp_err_t err = spi_bus_add_device(spi_host, &devcfg, &disp->spi_device);
    if (err != ESP_OK)
        goto error;

    ssd1680_hw_reset(disp);
    ssd1680_sw_reset(disp);

    ssd1680_init_sequence(disp);
    ssd1680_fill(disp, SSD1680_WHITE);
    ssd1680_refresh(disp, FULL_REFRESH);

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
    ssd1680_write(disp, SSD1680_DEEP_SLEEP_MODE, &mode, 1);
}

void ssd1680_wakeup(ssd1680_t *disp)
{
    ssd1680_hw_reset(disp);
    ssd1680_init_sequence(disp);
    ssd1680_send_framebuffer(disp);
}
void ssd1680_change_orientation(ssd1680_t *disp, ssd1680_orientation_t orientation)
{
	disp->orientation = orientation;
	ssd1680_setup_ram(disp);
}

void ssd1680_read_ram(ssd1680_t *disp, ssd1680_orientation_t orientation, ssd1680_read_ram_opt_t read_ram_opt)
{
	uint8_t * framebuffer;
	ssd1680_orientation_t orientation_orig = disp->orientation;

	ssd1680_change_orientation(disp, orientation);

	switch(read_ram_opt)
	{
		case SSD1680_READ_RAM_BW:
			framebuffer = disp->framebuffer_bw;
			break;
		case SSD1680_READ_RAM_RED:
		default:
			framebuffer = disp->framebuffer_red;
			break;
	}
	memset(framebuffer, (SSD1680_WHITE & 0x1) * 0xFF, disp->framebuffer_size); // For tests
	printf("\r\nRAM read, phaze 1");
	ssd1680_write(disp, SSD1680_READ_RAM_OPT, &read_ram_opt, 8);
	printf("\r\nRAM read, phaze 2");
	ssd1680_wait_busy(disp);
	printf("\r\nRAM read, phaze 3");
	ssd1680_read(disp, SSD1680_READ_RAM, framebuffer, disp->framebuffer_size);
	printf("\r\nRAM read, phaze 4");
	ssd1680_wait_busy(disp);
	printf("\r\nRAM read, phaze 5");
	ssd1680_change_orientation(disp, orientation_orig);
}

void ssd1680_set_pixel(ssd1680_t *disp, uint16_t x, uint16_t y, ssd1680_color_t color)
{
    int idx, offset;
    if (disp->orientation == SSD1680_90_DEG)
    	y += 8 - (disp->res_x % 8);
    if (disp->orientation == SSD1680_180_DEG)
    	x += 8 - (disp->res_x % 8);
    switch (disp->orientation)
    {
		case SSD1680_90_DEG: case SSD1680_270_DEG:
			idx = x + ((y >> 3) * disp->rows_cnt);
			offset = 7 - (y % 8);
			break;
		default: // SSD1680_NORMAL || SSD1680_180_DEG
			idx = (x >> 3) + (y * disp->clmn_cnt);
			offset = 7 - (x % 8);
			break;
    }
    disp->framebuffer_bw[idx] &= ~(1 << offset);
    disp->framebuffer_bw[idx] |= (color & 0x1) << offset;
    disp->framebuffer_red[idx] &= ~(1 << offset);
    disp->framebuffer_red[idx] |= ((color >> 1) & 0x1) << offset;
}

void ssd1680_draw_line(ssd1680_t *disp, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, ssd1680_color_t color) //Bresenham’s Line Drawing Algorithm
{
	 uint8_t steep = 0;
	    if (abs(x1-x2)<abs(y1-y2)) {
	    	uint16_t tmp;
	    	tmp = x1; x1 = y1; y1 = tmp; // swap x1, y1
	    	tmp = x2; x2 = y2; y2 = tmp; // swap x2, y2
	        steep = 1;
	    }
	    if (x1>x2) {
	    	uint16_t tmp;
	    	tmp = x1; x1 = x2; x2 = tmp; // swap x1, x2
	    	tmp = y1; y1 = y2; y2 = tmp; //swap y1,y2
	    }
	    int dx = x2-x1;
	    int dy = y2-y1;
	    int derror2 = abs(dy)*2;
	    int error2 = 0;
	    int y = y1;
	    for (int x=x1; x<=x2; x++) {
	        if (steep) {
	        	ssd1680_set_pixel(disp, y, x, color);
	        } else {
	        	ssd1680_set_pixel(disp, x, y, color);
	        }
	        error2 += derror2;
	        if (error2 > dx) {
	            y += (y2>y1?1:-1);
	            error2 -= dx*2;
	        }
	    }
}
//static uint8_t return_byte(uint8_t * byte, ssd1680_reverse_t reverse_bits_values, ssd1680_reverse_t reverse_bits_order)
//{
//	uint8_t tmp = *byte;
//
//	if(reverse_bits_order == SSD1680_REVERSE_TRUE)
//		tmp = ((tmp * 0x0802LU & 0x22110LU) | (tmp * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16;
//
//	if(reverse_bits_values == SSD1680_REVERSE_TRUE)
//		return ~tmp;
//	else
//		return tmp;
//}

static uint8_t modify_byte(uint8_t * byte, ssd1680_reverse_t rbv_condition, ssd1680_reverse_t rbo_condition, int8_t shift_value, ssd1680_order_t modify_order)
{
	uint8_t tmp = *byte;
	for(uint8_t i = 1;  i < 4; i++)
	{
		uint8_t order = modify_order;
		for(uint8_t j = 1; j < 4; j++)
		{
			if((order && 0x03) == i)
			{
				switch(j)
				{
					case 0x01:
						if(rbv_condition == SSD1680_REVERSE_TRUE)
							tmp = ~tmp;
					break;
					case 0x02:
						if(rbo_condition == SSD1680_REVERSE_TRUE)
							tmp = ((tmp * 0x0802LU & 0x22110LU) | (tmp * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16;
					break;
					case 0x03:
						if(shift_value > 0 && shift_value < 8)
							tmp = (tmp >> shift_value);
						else if (shift_value < 0 && shift_value > -8)
							tmp = (tmp << abs(shift_value));
					break;
				}
			}
			order = order >> 2;
		}
	}
	return tmp;
}

void ssd1680_clmns_rows_rotate(ssd1680_t *disp, uint16_t x, uint16_t y, uint8_t* area, uint16_t area_size, uint8_t* rotated_area, uint16_t rotated_area_size)
{
	memset(rotated_area, 0x00, rotated_area_size);
	uint16_t bytes_per_row = (x + 7) / 8;
	uint16_t rows = y;
	uint8_t bytes_per_column = (rows + 7) / 8;

	for(uint8_t rbyte = 0; rbyte < bytes_per_column; rbyte++ )
	{
		for(uint8_t c = 0; c < x; c++)
		{

			uint8_t remain_rows = rows - (rbyte * 8);
			if(remain_rows >= 8)
				remain_rows = 8;

			for(int16_t r = remain_rows - 1; r >= 0; r--)
			{

				uint8_t ida = (bytes_per_row * (r + (rbyte * 8))) + (c / 8);
				uint8_t idra = (rbyte * x) + c;
				if(idra < rotated_area_size && ida < area_size)
				{
					rotated_area[idra] |= (area[ida] >> ((c % 8))) & 0x01;
					if(r > 0)
						rotated_area[idra] = rotated_area[idra] << 1;
				}
			}
		}
	}
}
void ssd1680_set_area(ssd1680_t *disp, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint8_t* area, uint16_t area_size, ssd1680_color_t color, ssd1680_reverse_t reverse_bits_values, ssd1680_reverse_t reverse_bits_order)
{

	static const uint8_t BitsSetTable[8] = { 0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F }, BitsSetTableRev[8] = { 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x80 };
	int clmn_start, clmn_stop, idx; //offset;
	uint16_t ycurr, xcurr;
	uint8_t rotated_area[((y2 - y1 + 7) / 8) * (x2 - x1 + 1)];

    if (disp->orientation == SSD1680_90_DEG)
    {
    	y1 += 8 - (disp->res_x % 8);
    	y2 += 8 - (disp->res_x % 8);
    }
    if (disp->orientation == SSD1680_180_DEG)
    {
    	x1 += 8 - (disp->res_x % 8);
    	x2 += 8 - (disp->res_x % 8);
    }

	switch (disp->orientation)
	{
		case SSD1680_90_DEG: case SSD1680_270_DEG:
			uint8_t area_size_x = x2 - x1 + 1;
			uint8_t area_size_y = y2 - y1 + 1;
			ssd1680_clmns_rows_rotate(disp,area_size_x, area_size_y, area, area_size,rotated_area,sizeof(rotated_area));
			area = rotated_area; area_size = sizeof(rotated_area);
			uint8_t y1bits, y2bits;
			y1bits = (8 - (y1 % 8)) % 8;
			y2bits = (y2 % 8) + 1;
			clmn_start = y1 >> 3;
			clmn_stop = y2 >> 3;

#ifdef DEBUG
			printf("\r\ny1bits: %d, y2bits: %d, clmn_start: %d, clmn_stop: %d\r\n",y1bits,y2bits,clmn_start,clmn_stop);
#endif

			for ( ycurr = clmn_start; ycurr <= clmn_stop; ycurr++)
			{
#ifdef DEBUG
				uint16_t x1_cond = x1 + 3;
#endif
				for ( xcurr = x1; xcurr <= x2; xcurr++ )
				{
					idx = xcurr + ( ycurr * disp->rows_cnt );
					if( ycurr == clmn_start && y1bits > 0 ) // CONDITION 1 (90,270)
					{
						uint8_t mb = modify_byte(area, reverse_bits_values, reverse_bits_order, (8 - y1bits), SSD1680_ORDER_231);

						disp->framebuffer_bw[idx] =
								( disp->framebuffer_bw[idx] & BitsSetTableRev[y1bits])
								| (
									mb
									& BitsSetTable[y1bits]
								);
#ifdef DEBUG
						if(xcurr < x1_cond)
						{
							printf("idx: %X y1: %X, bytea: %X, byte: %X, modify byte: %X\r\n", idx, y1bits, (uint)area, *(area),mb);
							printf("Section 1\r\n");
						}
#endif
						area++;
					} else if (ycurr == clmn_stop && y2bits > 0 && y1bits > 0) // CONDITION 2 (90,270)
					{
						if(clmn_stop - clmn_start > 1)
						{
							if( y1bits + y2bits > 8)
							{
								uint8_t mb = modify_byte(area - area_size_x, reverse_bits_values, reverse_bits_order, -y1bits, SSD1680_ORDER_123)
											 | modify_byte(area, reverse_bits_values, reverse_bits_order, 8 - y1bits, SSD1680_ORDER_123);

								disp->framebuffer_bw[idx] =
										( disp->framebuffer_bw[idx] & BitsSetTable[8 - y2bits] )
										| (
											mb
											& BitsSetTableRev[8 - y2bits]
										);

#ifdef DEBUG
								if(xcurr < x1_cond)
								{
									printf("idx: %X y1: %X, bytea: %X, byte: %X, byte2a: %X, byte2: %X, modify byte: %X\r\n", idx, y1bits, (uint)(area - area_size_x), *(area - area_size_x), (uint)area, *area, mb);
									printf("Section 2.1.1\r\n");
								}
#endif
								area++;
							} else
							{
								uint8_t mb = modify_byte(area - area_size_x, reverse_bits_values, reverse_bits_order, -y1bits, SSD1680_ORDER_321);

								disp->framebuffer_bw[idx] =
										( disp->framebuffer_bw[idx] & BitsSetTable[8 - y2bits] )
										| (
											mb
											& BitsSetTableRev[8 - y2bits]
										);

#ifdef DEBUG
								if(xcurr < x1_cond)
								{
									printf("idx: %X y1: %X, bytea: %X, byte: %X, modify byte: %X\r\n", idx, y1bits, (uint)(area - area_size_x), *(area - area_size_x), mb);
									printf("Section 2.1.2\r\n");
								}
#endif
								area++;
							}

						} else
						{
							if( y1bits + y2bits > 8)
							{
								uint8_t mb = modify_byte(area - area_size_x, reverse_bits_values, reverse_bits_order, -y1bits, SSD1680_ORDER_123)
											 | modify_byte(area, reverse_bits_values, reverse_bits_order, 8 - y1bits, SSD1680_ORDER_123);

								disp->framebuffer_bw[idx] =
										( disp->framebuffer_bw[idx] & BitsSetTable[8 - y2bits] )
										| (
											mb
											& BitsSetTableRev[8 - y2bits]
										);

#ifdef DEBUG
								if(xcurr < x1_cond)
								{
									printf("idx: %X y1: %X, bytea: %X, byte: %X, byte2a: %X, byte2: %X, modify byte: %X\r\n", idx, y1bits, (uint)(area - area_size_x), *(area - area_size_x), (uint)area, *area, mb);
									printf("Section 2.2.1\r\n");
								}
#endif
								area++;
							}else
							{
								uint8_t mb = modify_byte(area - area_size_x, reverse_bits_values, reverse_bits_order, -y1bits, SSD1680_ORDER_231);

								disp->framebuffer_bw[idx] =
										( disp->framebuffer_bw[idx] & BitsSetTable[8 - y2bits])
										| (
											mb
											& BitsSetTableRev[8 - y2bits]
										);
#ifdef DEBUG
								if(xcurr < x1_cond)
									{
										printf("idx: %X y1: %X, bytea: %X, byte: %X, modify byte: %X\r\n", idx, y1bits, (uint)(area - area_size_x), *(area - area_size_x), mb);
										printf("Section 2.2.2\r\n");
									}
#endif
							}
						}

					} else if ( clmn_start != clmn_stop && ycurr == clmn_stop && y2bits > 0) // CONDITION 3 (90,270)
					{
						uint8_t mb = modify_byte(area, reverse_bits_values, reverse_bits_order, 0, SSD1680_ORDER_12);
						disp->framebuffer_bw[idx] =
								( disp->framebuffer_bw[idx] & BitsSetTable[8 - y2bits] )
								| (
										mb
										& BitsSetTableRev[8 - y2bits]
								);
#ifdef DEBUG
						if(xcurr < x1_cond)
						{
							printf("idx: %X y1: %X, bytea: %X, byte: %X, modify byte: %X\r\n", idx, y1bits, (uint)(area), *(area), mb);
							printf("Section 3\r\n");
						}
#endif
						area++;
					} else // CONDITION 4 (90,270)
					{
						if( y1bits > 0 )
						{
							uint8_t mb = modify_byte(area - area_size_x, reverse_bits_values, reverse_bits_order, -y1bits, SSD1680_ORDER_123)
										 | modify_byte(area, reverse_bits_values, reverse_bits_order, 8 - y1bits, SSD1680_ORDER_123);

							disp->framebuffer_bw[idx] = mb;
#ifdef DEBUG
							if(xcurr < x1_cond)
							{
								printf("idx: %X y1: %X, bytea: %X, byte: %X, byte2a: %X, byte2: %X, modify byte: %X\r\n", idx, y1bits, (uint)(area - area_size_x), *(area - area_size_x), (uint)area, *area, mb);
								printf("Section 4.1\r\n");
							}
#endif
						} else
						{
							uint8_t mb = modify_byte(area, reverse_bits_values, reverse_bits_order, 0, SSD1680_ORDER_12);
							disp->framebuffer_bw[idx] = mb;
#ifdef DEBUG
							if(xcurr < x1_cond)
							{
								printf("idx: %X y1: %X, bytea: %X, byte: %X, modify byte: %X\r\n", idx, y1bits, (uint)(area), *(area), mb);
								printf("Section 4.2\r\n");
							}
#endif
						}
						area++;
					}
				}
			}


			//			for ( ycurr = y1; ycurr <= y2; ycurr++)
			//			{
			//
			//				for ( xcurr = clmn_start; xcurr <= clmn_stop; xcurr++ )
			//				{
			//					idx = xcurr + ( ycurr * disp->clmn_cnt );
			//					if( xcurr == clmn_start && x1bits > 0 )
			//					{
			//
			//						disp->framebuffer_bw[idx] = ( disp->framebuffer_bw[idx] & ~(BitsSetTable[x1bits]) ) | (return_byte(area, reverse_bits_values, reverse_bits_order) >> (8 - x1bits));
			//
			//					} else if ( xcurr == clmn_stop && x2bits > 0 && x1bits > 0)
			//					{
			//
			//						disp->framebuffer_bw[idx] = (return_byte(area - 1, reverse_bits_values, reverse_bits_order) << x1bits) | (return_byte(area, reverse_bits_values, reverse_bits_order) & ~(BitsSetTable[8 - x2bits])) >> x1bits;
			//
			//					} else if ( xcurr == clmn_stop && x2bits > 0)
			//					{
			//
			//						disp->framebuffer_bw[idx] = ( disp->framebuffer_bw[idx] & BitsSetTable[8 - x2bits] ) | (return_byte(area, reverse_bits_values, reverse_bits_order) & ~(BitsSetTable[8 - x2bits]));
			//						area++;
			//
			//					} else
			//					{
			//						if( x1bits > 0 )
			//							disp->framebuffer_bw[idx] = (return_byte(area, reverse_bits_values, reverse_bits_order) << x1bits) | (return_byte(area + 1, reverse_bits_values, reverse_bits_order) >> (8 - x1bits));
			//						elseidx
			//							disp->framebuffer_bw[idx] = return_byte(area, reverse_bits_values, reverse_bits_order);
			//						area++;
			//					}
			//				}
			//			}
		break;
		default: // SSD1680_NORMAL || SSD1680_180_DEG
			uint8_t x1bits, x2bits;
			x1bits = (8 - (x1 % 8)) % 8;
			x2bits = (x2 % 8) + 1;
			clmn_start = x1 >> 3;
			clmn_stop = x2 >> 3;
#ifdef DEBUG
			printf("\r\nx1bits: %d, x2bits: %d, clmn_start: %d, clmn_stop: %d\r\n",x1bits,x2bits,clmn_start,clmn_stop);
#endif

			for ( ycurr = y1; ycurr <= y2; ycurr++)
			{
#ifdef DEBUG
				uint16_t y1_cond = y1 + 5;
#endif
				for ( xcurr = clmn_start; xcurr <= clmn_stop; xcurr++ ) // CONDITION 1 (0,180)
				{
					idx = xcurr + ( ycurr * disp->clmn_cnt );
					if( xcurr == clmn_start && x1bits > 0 )
					{
						uint8_t mb = modify_byte(area, reverse_bits_values, reverse_bits_order, (8 - x1bits), SSD1680_ORDER_231);

						disp->framebuffer_bw[idx] =
								( disp->framebuffer_bw[idx] & BitsSetTableRev[x1bits])
								| (
									mb
									& BitsSetTable[x1bits]
								);
#ifdef DEBUG
						if(ycurr < y1_cond)
						{
							printf("idx: %X x1: %X, bytea: %X, byte: %X, modify byte: %X\r\n", idx, x1bits, (uint)area, *(area),mb);
							printf("Section 1\r\n");
						}
#endif
						area++;
					} else if (xcurr == clmn_stop && x2bits > 0 && x1bits > 0) // CONDITION 2 (0,180)
					{
						if(clmn_stop - clmn_start > 1)
						{
							if( x1bits + x2bits > 8)
							{
								uint8_t mb = modify_byte(area - 1, reverse_bits_values, reverse_bits_order, -x1bits, SSD1680_ORDER_123)
											 | modify_byte(area, reverse_bits_values, reverse_bits_order, 8 - x1bits, SSD1680_ORDER_123);

								disp->framebuffer_bw[idx] =
										( disp->framebuffer_bw[idx] & BitsSetTable[8 - x2bits] )
										| (
											mb
											& BitsSetTableRev[8 - x2bits]
										);

#ifdef DEBUG
								if(ycurr < y1_cond)
								{
									printf("idx: %X x1: %X, bytea: %X, byte: %X, byte2a: %X, byte2: %X, modify byte: %X\r\n", idx, x1bits, (uint)(area-1), *(area-1), (uint)area, *area, mb);
									printf("Section 2.1.1\r\n");
								}
#endif
								area++;
							} else
							{
								uint8_t mb = modify_byte(area - 1, reverse_bits_values, reverse_bits_order, -x1bits, SSD1680_ORDER_231);

								disp->framebuffer_bw[idx] =
										( disp->framebuffer_bw[idx] & BitsSetTable[8 - x2bits] )
										| (
											mb
											& BitsSetTableRev[8 - x2bits]
										);

#ifdef DEBUG
								if(ycurr < y1_cond)
								{
									printf("idx: %X x1: %X, bytea: %X, byte: %X, modify byte: %X\r\n", idx, x1bits, (uint)(area-1), *(area -1), mb);
									printf("Section 2.1.2\r\n");
								}
#endif
								//area++;
							}

						} else
						{
							if( x1bits + x2bits > 8)
							{
								uint8_t mb = modify_byte(area - 1, reverse_bits_values, reverse_bits_order, -x1bits, SSD1680_ORDER_123)
											 | modify_byte(area, reverse_bits_values, reverse_bits_order, 8 - x1bits, SSD1680_ORDER_123);

								disp->framebuffer_bw[idx] =
										( disp->framebuffer_bw[idx] & BitsSetTable[8 - x2bits] )
										| (
											mb
											& BitsSetTableRev[8 - x2bits]
										);

#ifdef DEBUG
								if(ycurr < y1_cond)
								{
									printf("idx: %X x1: %X, bytea: %X, byte: %X, byte2a: %X, byte2: %X, modify byte: %X\r\n", idx, x1bits, (uint)(area-1), *(area-1), (uint)area, *area, mb);
									printf("Section 2.2.1\r\n");
								}
#endif
								area++;
							}else
							{
								uint8_t mb = modify_byte(area - 1, reverse_bits_values, reverse_bits_order, -x1bits, SSD1680_ORDER_231);

								disp->framebuffer_bw[idx] =
										( disp->framebuffer_bw[idx] & BitsSetTable[8 - x2bits])
										| (
											mb
											& BitsSetTableRev[8 - x2bits]
										);
#ifdef DEBUG
								if(ycurr < y1_cond)
									{
										printf("idx: %X x1: %X, bytea: %X, byte: %X, modify byte: %X\r\n", idx, x1bits, (uint)(area-1), *(area -1), mb);
										printf("Section 2.2.2\r\n");
									}
#endif
							}
						}

					} else if ( clmn_start != clmn_stop && xcurr == clmn_stop && x2bits > 0) // CONDITION 3 (0,180)
					{
						uint8_t mb = modify_byte(area, reverse_bits_values, reverse_bits_order, 0, SSD1680_ORDER_12);
						disp->framebuffer_bw[idx] =
								( disp->framebuffer_bw[idx] & BitsSetTable[8 - x2bits] )
								| (
										mb
										& BitsSetTableRev[8 - x2bits]
								);
#ifdef DEBUG
						if(ycurr < y1_cond)
						{
							printf("idx: %X x1: %X, bytea: %X, byte: %X, modify byte: %X\r\n", idx, x1bits, (uint)(area), *(area), mb);
							printf("Section 3\r\n");
						}
#endif
						area++;
					} else // CONDITION 4 (0,180)
					{
						if( x1bits > 0 )
						{
							uint8_t mb = modify_byte(area - 1, reverse_bits_values, reverse_bits_order, -x1bits, SSD1680_ORDER_123)
										 | modify_byte(area, reverse_bits_values, reverse_bits_order, 8 - x1bits, SSD1680_ORDER_123);

							disp->framebuffer_bw[idx] = mb;
#ifdef DEBUG
							if(ycurr < y1_cond)
							{
								printf("idx: %X x1: %X, bytea: %X, byte: %X, byte2a: %X, byte2: %X, modify byte: %X\r\n", idx, x1bits, (uint)(area-1), *(area-1), (uint)area, *area, mb);
								printf("Section 4.1\r\n");
							}
#endif
						} else
						{
							uint8_t mb = modify_byte(area, reverse_bits_values, reverse_bits_order, 0, SSD1680_ORDER_12);
							disp->framebuffer_bw[idx] = mb;
#ifdef DEBUG
							if(ycurr < y1_cond)
							{
								printf("idx: %X x1: %X, bytea: %X, byte: %X, modify byte: %X\r\n", idx, x1bits, (uint)(area), *(area), mb);
								printf("Section 4.2\r\n");
							}
#endif
						}
						area++;
					}
				}
			}
		break;
	}
}

uint16_t ssd1680_display_char(ssd1680_t *disp, ssd1680_font_t * font, uint16_t x, uint16_t y, uint8_t character, ssd1680_color_t color)
{
	static uint8_t unicode_prefix;

	if( character > 190)
		unicode_prefix = character;

	if (character < 190)
	{
		switch (character)
		{
			case 134:
				character = 128;
				break;
			case 152:
				character = 129;
				break;
			case 129:
				character = 130;
				break;
			case 131:
				character = 131;
				break;
			case 147:
				character = 132;
				break;
			case 154:
				character = 133;
				break;
			case 187:
				character = 134;
				break;
			case 185:
				character = 135;
				break;
			case 133:
				character = 136;
				break;
			case 135:
				character = 137;
				break;
			case 153:
				character = 138;
				break;
			case 130:
				character = 139;
				break;
			case 132:
				if(unicode_prefix == 196)
					character = 127;
				else
					character = 140;
				break;
			case 179:
				character = 141;
				break;
			case 155:
				character = 142;
				break;
			case 188:
				character = 143;
				break;
			case 186:
				character = 144;
				break;
		}

		ssd1680_set_area(	disp, x, y,
							 x + font->x_size -1,
							 y + font->y_size -1,
							 (uint8_t *)font->data + ((character - ' ') * font->bytes_per_char),
							 font->bytes_per_char,
							 SSD1680_BLACK, SSD1680_REVERSE_TRUE, SSD1680_REVERSE_TRUE
						 );
			return x + font->x_size;
	}
	return x;
}

ssd1680_cursor_t ssd1680_display_string(ssd1680_t *disp, ssd1680_font_t * font, uint16_t x, uint16_t y, char * string, ssd1680_color_t color)
{
	ssd1680_cursor_t cursor;
	cursor.x = x;
	cursor.y = y;
	uint8_t res_x; //res_y;
	switch (disp->orientation)
	{
		case SSD1680_90_DEG: case SSD1680_270_DEG:
			res_x = disp->res_y;
			//res_y = disp->res_x;
		break;
		default:
			res_x = disp->res_x;
			//res_y = disp->res_y;
	}


	while(*string)
	{
		if (cursor.x > res_x - font->x_size)
		{
			cursor.x = 0;
			cursor.y += font->y_size;
		}
		cursor.x = ssd1680_display_char(disp, font, cursor.x, cursor.y, *string, color);
#ifdef DEBUG
		printf("display: %c -> %d\r\n--------\r\n", *string, *string);
#endif
		string++;
	}
	return cursor;
}

void ssd1680_fill(ssd1680_t *disp, ssd1680_color_t color)
{
    memset(disp->framebuffer_bw, (color & 0x1) * 0xFF, disp->framebuffer_size);
    memset(disp->framebuffer_red, ((color >> 1) & 0x1) * 0xFF, disp->framebuffer_size);
    ssd1680_send_framebuffer(disp);
}

void ssd1680_send_framebuffer(ssd1680_t *disp)
{
#ifdef DEBUG
	int64_t t = get_time();
#endif
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
    ssd1680_write(disp, SSD1680_WRITE_RAM_BW, disp->framebuffer_bw, disp->framebuffer_size);
    ssd1680_wait_busy(disp);
    ssd1680_write(disp, SSD1680_WRITE_RAM_RED, disp->framebuffer_red, disp->framebuffer_size);
    ssd1680_wait_busy(disp);

#ifdef DEBUG
    t = get_time() - t;
    printf("send framebuffer time: %lld\r\n", t);
#endif
}

void ssd1680_set_refresh_window(ssd1680_t *disp, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    switch (disp->orientation)
    {
    case SSD1680_90_DEG:
        ssd1680_set_x_window(disp, y2 >> 3, y1 >> 3);
        ssd1680_set_y_window(disp, x1, x2);
        //ssd1680_set_ram_pos(disp, y2 >> 3, x1);
        break;
    case SSD1680_180_DEG:
        ssd1680_set_x_window(disp, x2 >> 3, x1 >> 3);
        ssd1680_set_y_window(disp, y2, y1);
        //ssd1680_set_ram_pos(disp, x2 >> 3, y2);
        break;
    case SSD1680_270_DEG:
        ssd1680_set_x_window(disp, y1 >> 3, y2 >> 3);
        ssd1680_set_y_window(disp, x2, x1);
        //ssd1680_set_ram_pos(disp, y1 >> 3, x2);
        break;
    default: // SSD1680_NORMAL
        ssd1680_set_x_window(disp, x1 >> 3, x2 >> 3);
        ssd1680_set_y_window(disp, y1, y2);
        //ssd1680_set_ram_pos(disp, x1 >> 3, y1);
        break;
    }
}

void ssd1680_refresh(ssd1680_t *disp, uint8_t mode)
{
#ifdef DEBUG
	int64_t t = get_time();
#endif
    ssd1680_write(disp, SSD1680_DISP_UPDATE_CTRL_2, &mode, sizeof(mode));
    ssd1680_wait_busy(disp);
    ssd1680_write(disp, SSD1680_MASTER_ACTIVATION, NULL, 0);
    ssd1680_wait_busy(disp);

#ifdef DEBUG
    t = get_time() - t;
    printf("refresh time: %lld\r\n", t);
#endif
}
