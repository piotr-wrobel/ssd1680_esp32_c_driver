/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "lib_ssd1680.h"
#include "ssd1680_fonts.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif


#define EPAPER_HOST    SPI2_HOST
#define EPAPER_RES_X	122
#define EPAPER_RES_Y	250

#define PIN_NUM_MISO 25
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  19
#define PIN_NUM_CS   22

#define PIN_NUM_DC   21
#define PIN_NUM_RST  18
#define PIN_NUM_BCKL 5


/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;



static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void static display_demo_1(ssd1680_t *disp, ssd1680_color_t color)
{
	 uint16_t res_x, res_y;
	 if (disp->orientation == SSD1680_270_DEG || disp->orientation == SSD1680_90_DEG)
	 {
		 res_x = disp->res_y;
		 res_y = disp->res_x;
	 }else
	 {
		 res_x = disp->res_x;
		 res_y = disp-> res_y;
	 }
	 ssd1680_draw_line(disp, 0, 0, res_x-1, res_y-1, color);
	 ssd1680_draw_line(disp, res_x-1, 0, 0, res_y-1, color);
	 ssd1680_draw_line(disp, (res_x-1)/2, 0, 0, (res_y-1)/2, color);
	 ssd1680_draw_line(disp, (res_x-1)/2, 0, res_x-1, (res_y-1)/2, color);
	 ssd1680_draw_line(disp, (res_x-1)/2, res_y-1, 0, (res_y-1)/2, color);
	 ssd1680_draw_line(disp, (res_x-1)/2, res_y-1, res_x-1, (res_y-1)/2, color);

	 ssd1680_draw_line(disp, 0, 0, res_x-1, 0 , color);
	 ssd1680_draw_line(disp, 10, 10, res_x-1 - 10, 10, color);
	 ssd1680_draw_line(disp, 20, 20, res_x-1 - 20, 20, color);
	 ssd1680_draw_line(disp, 30, 30, res_x-1 - 30, 30, color);

	 ssd1680_draw_line(disp, 0, res_y-1, res_x-1, res_y-1, color);
	 ssd1680_draw_line(disp, 10, res_y-1 - 10, res_x-1 - 10, res_y-1 - 10, color);
	 ssd1680_draw_line(disp, 20, res_y-1 - 20, res_x-1 - 20, res_y-1 - 20, color);
	 ssd1680_draw_line(disp, 30, res_y-1 - 30, res_x-1 - 30, res_y-1 - 30, color);

	 ssd1680_draw_line(disp, 0, 0, 0, res_y-1, color);
	 ssd1680_draw_line(disp, 10, 10, 10, res_y-1 - 10, color);
	 ssd1680_draw_line(disp, 20, 20, 20, res_y-1 - 20, color);
	 ssd1680_draw_line(disp, 30, 30, 30, res_y-1 - 30, color);

	 ssd1680_draw_line(disp, res_x-1, 0, res_x-1, res_y-1, color);
	 ssd1680_draw_line(disp, res_x-1 - 10, 10, res_x-1 - 10, res_y-1 - 10, color);
	 ssd1680_draw_line(disp, res_x-1 - 20, 20, res_x-1 - 20, res_y-1 - 20, color);
	 ssd1680_draw_line(disp, res_x-1 - 30, 30, res_x-1 - 30, res_y-1 - 30, color);
}

void static display_demo_2(ssd1680_t *disp, ssd1680_color_t color)
{
	uint16_t res_x, res_y;
	res_x = disp->res_x;
	res_y = disp-> res_y;

	for( uint8_t x = 0; x < res_x; x += 8)
	{
		for(uint16_t y=0; y < res_y; y += 6)
		{
			if(y+2 < res_y)
				ssd1680_draw_line(disp, x, y, x, y+2, color);
			if( x+7 < res_x && y+4 < res_y)
				ssd1680_draw_line(disp, x+7, y+4, x+7, y+4, color);
		}
	}
}

void static display_demo_3(ssd1680_t *disp, ssd1680_color_t color)
{
    uint8_t area[255];
    memset(area, 0xA5, sizeof(area));
    printf("Area size: %d\r\n", sizeof(area));
    ssd1680_set_area(disp, 57, 0, 64, 254, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);

    ssd1680_set_area(disp, 8, 0, (3*8)+5, 50, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
    ssd1680_set_area(disp, (5*8)-5, 0, (7*8), 50, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
    ssd1680_set_area(disp, (10*8)-7, 0, (11*8)+6, 50, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);

    ssd1680_set_area(disp, (8)-2, 60, (3*8)+3, 110, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);

    ssd1680_set_area(disp, 8, 120, (2*8)+5, 170, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
    ssd1680_set_area(disp, (5*8)-7, 120, (5*8)+5, 170, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);// !!
    ssd1680_set_area(disp, (9*8)-5, 120, (9*8)+7, 170, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE); //!!

    ssd1680_set_area(disp, (8)-2, 180, (2*8)+2, 230, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
}

void app_main(void)
{
	ssd1680_t * ssd1680_disp;
	spi_host_device_t spi_host = EPAPER_HOST;

	ssd1680_pinmap_t ssd1680_pinmap = {
		    .busy = PIN_NUM_BCKL,
		    .reset = PIN_NUM_RST,
		    .dc = PIN_NUM_DC,
		    .cs = PIN_NUM_CS
	};

	//Initialize NVSPIN_NUM_BCKL
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    //esp_err_t ret;
    //spi_device_handle_t spi;
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        //.max_transfer_sz = PARALLEL_LINES * 320 * 2 + 8
    };

    //Initialize the SPI bus
    ret = spi_bus_initialize(spi_host, &buscfg, SPI_DMA_CH1);
    ESP_ERROR_CHECK(ret);


    uint8_t ssd1680_orientation = SSD1680_NORMAL;
    ssd1680_disp = ssd1680_init(spi_host, ssd1680_pinmap, EPAPER_RES_X, EPAPER_RES_Y, ssd1680_orientation);

    //display_demo_1(ssd1680_disp, SSD1680_BLACK);
    //display_demo_2(ssd1680_disp, SSD1680_BLACK);
    //display_demo_3(ssd1680_disp, SSD1680_BLACK);

    ssd1680_cursor_t ssd1680_cursor;
    char test_string[] = "ABCDEF";
    //ssd1680_cursor = ssd1680_display_string(ssd1680_disp, &font_terminal_14pt, 0, 0, "Test!:) Zobaczmy czy sie bedzie wyswietlac nizej ĄĆĘŁŃÓŚŻŹąćęłńóśżź Grzegżółka sączyła ćmi sok z źbła :)", SSD1680_BLACK);
    ssd1680_cursor = ssd1680_display_string(ssd1680_disp, &font_terminal_14pt, 0, 0, test_string, SSD1680_BLACK);
    ssd1680_cursor = ssd1680_display_string(ssd1680_disp, &font_terminal_9pt, ssd1680_cursor.x, ssd1680_cursor.y, test_string, SSD1680_BLACK);
    ssd1680_cursor = ssd1680_display_string(ssd1680_disp, &font_terminal_9pt_bold, ssd1680_cursor.x, ssd1680_cursor.y, test_string, SSD1680_BLACK);

    //ssd1680_cursor = ssd1680_display_string(ssd1680_disp, &font_terminal_9pt, 0, 0, "Test!:) Zobaczmy czy sie bedzie wyswietlac nizej ĄĆĘŁŃÓŚŻŹąćęłńóśżź Grzegżółka sączyła ćmi sok z źbła :)", SSD1680_BLACK);
    //ssd1680_cursor = ssd1680_display_string(ssd1680_disp, &font_terminal_9pt_bold, ssd1680_cursor.x, ssd1680_cursor.y, "Test!:) Zobaczmy czy sie bedzie wyswietlac nizej ĄĆĘŁŃÓŚŻŹąćęłńóśżź Grzegżółka sączyła ćmi sok z źbła :)", SSD1680_BLACK);

    ssd1680_send_framebuffer(ssd1680_disp);
    //ssd1680_set_refresh_window(ssd1680_disp, 0, 180, ssd1680_disp->res_x-1, ssd1680_disp->res_y-1);
    ssd1680_refresh(ssd1680_disp, FAST_FULL_REFRESH);



    //ssd1680_send_framebuffer(ssd1680_disp);
    //ssd1680_set_refresh_window(ssd1680_disp, 0, 180, 90, ssd1680_disp->res_y-1);
    //ssd1680_refresh(ssd1680_disp, FAST_PARTIAL_REFRESH);
    ssd1680_sleep(ssd1680_disp);
}
