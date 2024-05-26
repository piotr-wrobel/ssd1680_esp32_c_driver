/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <math.h>
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

#include "esp_random.h"

#include "lib_ssd1680.h"
#include "ssd1680_fonts.h"
#include "eye_122_250.h"
#include "c64_122_250.h"
#include "test_122_250.h"
#include "test_ram_122_250.h"

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

#define PIN_NUM_MISO 25		// D6
#define PIN_NUM_MOSI 23		// D5
#define PIN_NUM_CLK  19		// D4
#define PIN_NUM_CS   22 	// D1

#define PIN_NUM_DC   21		// D3
#define PIN_NUM_RST  18		// DO
#define PIN_NUM_BCKL 5  	// D2


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
//	if (disp->orientation == SSD1680_270_DEG || disp->orientation == SSD1680_90_DEG)
//	{
//		res_x = disp->res_y;
//		res_y = disp->res_x;
//	}else
//	{
		res_x = disp->res_x;
		res_y = disp-> res_y;
//	}

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
    memset(area, 0x5A, sizeof(area));
    printf("Area size: %d\r\n", sizeof(area));
//    ssd1680_set_area(disp, (7*8)+1, 0, (8*8), 110, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);

    ssd1680_set_area(disp, 8, 0, 16, 7, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_TRUE, SSD1680_REVERSE_TRUE);
    ssd1680_set_area(disp, 3*8, 0,  (3*8)+8, 8, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_TRUE, SSD1680_REVERSE_TRUE);
    //ssd1680_set_area(disp, 5*8, 0,  (5*8)+7, 15, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
    //ssd1680_set_area(disp, 7*8, 0,  (7*8)+7, 16, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
    //ssd1680_set_area(disp, 9*8, 0,  (9*8)+7, 23, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
    //ssd1680_set_area(disp, 11*8, 0, (11*8)+7, 24, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
//    ssd1680_set_area(disp, (5*8)-5, 0, (7*8), 20, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
//    ssd1680_set_area(disp, (10*8)-7, 0, (11*8)+6, 20, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
//
//    ssd1680_set_area(disp, (8)-2, 30, (3*8)+3, 50, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
//
//    ssd1680_set_area(disp, 8, 60, (2*8)+5, 80, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
//    ssd1680_set_area(disp, (5*8)-7, 60, (5*8)+5, 80, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
//    ssd1680_set_area(disp, (9*8)-5, 60, (9*8)+7, 80, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);
//
//    ssd1680_set_area(disp, (8)-2, 90, (2*8)+2, 110, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_FALSE);


}

void static display_demo_4(ssd1680_t *disp, ssd1680_color_t color)
{
	uint8_t area[] = {0xFF,0xFF,
			  	  	  	0x01,0x80,
						0x11,0xB8,
						0x11,0xA0,
						0x11,0xB8,
						0x11,0x88,
						0x91,0xB8,
						0x81,0x83,
						0xC1,0x81,
						0x1D,0xA9,
						0x11,0xA8,
						0x19,0xB8,
						0x11,0xA0,
						0x1D,0xA0,
						0x01,0x80,
						0xFF,0xFF
			  	  	  };
	ssd1680_fill(disp, SSD1680_WHITE);
	ssd1680_set_area(disp, 0, 0+1, 15, 15+1, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_TRUE, SSD1680_REVERSE_TRUE);
	ssd1680_set_area(disp, 16, 16+4, 16+15, 16+15+4, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_TRUE, SSD1680_REVERSE_TRUE);
	ssd1680_set_area(disp, 32, 32+7, 32+15, 32+15+7, area, sizeof(area), SSD1680_BLACK, SSD1680_REVERSE_TRUE, SSD1680_REVERSE_TRUE);
}

void static display_demo_5(ssd1680_t *disp, ssd1680_color_t color)
{
	ssd1680_fill(disp, SSD1680_WHITE);
	uint8_t counter = 0;
	uint8_t * image_pointer;
	while(1)
	{
		switch (counter++)
		{
			case 0:
				image_pointer = image_test_122_250;
				break;
			case 1:
				image_pointer = image_eye_122_250;
				break;
			case 2:
			default:
				image_pointer = image_c64_122_250;
		}
		if(counter > 2) counter = 0;

		ssd1680_set_area(disp, 0, 0, 121, 249, image_pointer, sizeof(image_eye_122_250), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_TRUE);
		ssd1680_send_framebuffer(disp);
		ssd1680_refresh(disp, FAST_FULL_REFRESH);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}

}

void static fonts_demo(ssd1680_t *disp, ssd1680_color_t color)
{
	uint8_t allowed_characters[] = {'1','2','3','4','5','6','7','8','9','0','Q','W','E','R','T','Y','U','I',
									'O','P','A','S','D','F','G','H','J','K','L','Z','X','C','V','B','N','M',
									'q','w','e','r','t','y','u','i','o','p','a','s','d','f','g','h','j','k',
									'l','z','x','c','v','b','n','m','&','*','#','@','?',',','!',127,128,129,
									130,131,132,133,134,135,136,137,138,139,140,141,142,143,144
									};
	uint32_t random_number;
	ssd1680_cursor_t cursor;
	ssd1680_font_t * font;

	uint8_t res_x, res_y;
	switch (disp->orientation)
	{
		case SSD1680_90_DEG: case SSD1680_270_DEG:
			res_x = disp->res_y;
			res_y = disp->res_x;
		break;
		default:
			res_x = disp->res_x;
			res_y = disp->res_y;
	}
	font = &font_terminal_9pt;
	cursor = ssd1680_display_string(disp, font, 20, (res_y / 2) - font->y_size , "Random characters test !", color);

	ssd1680_send_framebuffer(disp);
    ssd1680_refresh(disp, FAST_FULL_REFRESH);
	vTaskDelay(2000 / portTICK_PERIOD_MS);

	while(1)
	{
		cursor.x = 0;
		cursor.y = 0;
		ssd1680_fill(disp, SSD1680_WHITE);
		while(2)
		{
			random_number = esp_random();

			uint8_t font_type = (uint8_t)((random_number & 0xFF) % 5);
			random_number = random_number >> 8;
			switch(font_type)
			{
				case 0:
					font = &font_terminal_9pt;
					break;
				case 1:
					font = &font_terminal_9pt_bold;
					break;
				case 2:
					font = &font_terminal_14pt;
					break;
				case 3:
					font = &font_consolas_16pt_bold;
					break;
				case 4:
					font = &font_consolas_22pt;
					break;
				default:
					font = &font_terminal_9pt;
			}

			if(cursor.y + font->y_size > res_y)
				break; // Exit from while(2)

			cursor.x = (uint8_t)(random_number & 0x07);

			while(cursor.x < res_x - font->x_size)
			{

				random_number = esp_random();
				uint8_t character = (uint8_t)((random_number & 0xFF) % sizeof(allowed_characters));
				cursor.x = ssd1680_display_char(disp, font, cursor.x, cursor.y, allowed_characters[character], color);
			}
			cursor.x = 0 ;
			cursor.y += font->y_size;

		}

		ssd1680_send_framebuffer(disp);
	    ssd1680_refresh(disp, FAST_FULL_REFRESH);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}
}

void static fonts_demo_2(ssd1680_t *disp, ssd1680_color_t color)
{
	//ssd1680_cursor_t cursor;
	ssd1680_font_t * font = &font_consolas_22pt;
	uint8_t x1 = 0; //,x2 = 0;
	for(uint8_t i = 0; i < 12; i++)
	{
		ssd1680_display_char(disp, font, x1, 0, 'A' + i, color);
		ssd1680_display_char(disp, font, x1, 54 + i, 'A' + i, color);
		x1 = ssd1680_display_char(disp, font, x1, 22 - i, 'A' + i, color);
	}
	font = &font_terminal_9pt;
	x1 = 0;

	for(uint8_t i = 0; i < 44; i++)
	{
		x1 = ssd1680_display_char(disp, font, x1, 95 + (int)(sin((double)i / 4) * 15), 'A' + i, color);
	}
	for(uint8_t i = 0; i < 12; i++)
	{
		x1 = ssd1680_display_char(disp, font, 234 + (int)(sin((double)i/1.5) * 10), i * (font->y_size - 1), 'a' + i, color);
	}


	ssd1680_display_string(disp, font, 0, 45, "Characters can be positioned with acc. of 1px", color);

	ssd1680_send_framebuffer(disp);
	ssd1680_refresh(disp, FAST_FULL_REFRESH);
}

void static font_orientation_demo(ssd1680_t *disp, ssd1680_color_t color)
{
	ssd1680_font_t * font = &font_terminal_9pt;

	ssd1680_change_orientation(disp, SSD1680_0_DEG);
	ssd1680_display_string(disp, font, 30, 16, "Test...", color);
	ssd1680_send_framebuffer(disp);
	ssd1680_refresh(disp, FAST_FULL_REFRESH);


	ssd1680_change_orientation(disp, SSD1680_90_DEG);
	ssd1680_read_ram(disp, SSD1680_READ_RAM_BW);
	ssd1680_display_string(disp, font, 30, 16, "Test...", color);
	ssd1680_send_framebuffer(disp);
	ssd1680_refresh(disp, FAST_FULL_REFRESH);


	ssd1680_change_orientation(disp, SSD1680_180_DEG);
	ssd1680_read_ram(disp, SSD1680_READ_RAM_BW);
	ssd1680_display_string(disp, font, 30, 16, "Test...", color);
	ssd1680_send_framebuffer(disp);
	ssd1680_refresh(disp, FAST_FULL_REFRESH);


	ssd1680_change_orientation(disp, SSD1680_270_DEG);
	ssd1680_read_ram(disp, SSD1680_READ_RAM_BW);
	ssd1680_display_string(disp, font, 30, 16, "Test...", color);
	ssd1680_send_framebuffer(disp);
	ssd1680_refresh(disp, FAST_FULL_REFRESH);

}

void static print_buffor(char * text, uint8_t * buff, uint8_t lenght)
{
	printf("\r\n%s:", text);
	for(uint8_t i = 0; i < lenght; i++)
	{
		printf("0x%.2x, ",buff[i]);
	}
}

void static partial_demo(ssd1680_t *disp, ssd1680_color_t color)
{
	//vTaskDelay(3000 / portTICK_PERIOD_MS);
	ssd1680_fill(disp, SSD1680_WHITE);
	ssd1680_set_area(disp, 0, 0, 121, 249, image_c64_122_250, sizeof(image_c64_122_250), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_TRUE);
	//ssd1680_set_refresh_window(disp, 0, 0, 121, 249);
	ssd1680_send_framebuffer(disp);
	ssd1680_refresh(disp, FULL_REFRESH);


	//vTaskDelay(3000 / portTICK_PERIOD_MS);
	ssd1680_fill(disp, SSD1680_WHITE);
	ssd1680_set_area(disp, 0, 0, 121, 249, image_eye_122_250, sizeof(image_eye_122_250), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_TRUE);
	ssd1680_send_framebuffer(disp);
	//ssd1680_set_refresh_window(disp, 0, 0, 121, 125);
	ssd1680_refresh(disp, FAST_FULL_REFRESH);
}

void static ram_demo_1(ssd1680_t *disp, ssd1680_color_t color)
{
	ssd1680_fill(disp, SSD1680_WHITE);

	ssd1680_set_area(disp, 0, 0, 121, 249, image_c64_122_250, sizeof(image_c64_122_250), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_TRUE);
	//ssd1680_set_area(disp, 0, 0, 121, 249, image_test_ram_122_250, sizeof(image_test_ram_122_250), SSD1680_BLACK, SSD1680_REVERSE_FALSE, SSD1680_REVERSE_TRUE);

	print_buffor("Before send", disp->framebuffer_bw, 20);
	ssd1680_display_string(disp, &font_terminal_14pt, 0, 0, "Some text...", color);
	ssd1680_send_framebuffer(disp);


	ssd1680_refresh(disp, FAST_FULL_REFRESH);

	memset(disp->framebuffer_bw, (SSD1680_WHITE & 0x1) * 0xFF, disp->framebuffer_size );
	print_buffor("Filled again by white", disp->framebuffer_bw, 20);
	ssd1680_change_orientation(disp, SSD1680_270_DEG);
	ssd1680_read_ram(disp, SSD1680_READ_RAM_BW);
	print_buffor("Readed from RAM", disp->framebuffer_bw, 20);


	ssd1680_display_string(disp, &font_terminal_14pt, 0, 0, "Image from RAM...", color);
	ssd1680_display_string(disp, &font_terminal_14pt, 60, 105, "Inscription added", color);
	ssd1680_send_framebuffer(disp);
	ssd1680_refresh(disp, FAST_FULL_REFRESH);
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
		.flags = SPICOMMON_BUSFLAG_MASTER,
    };

    //Initialize the SPI bus
    ret = spi_bus_initialize(spi_host, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    uint8_t ssd1680_orientation = SSD1680_NORMAL;
    //uint8_t ssd1680_orientation = SSD1680_90_DEG;
    //uint8_t ssd1680_orientation = SSD1680_180_DEG;
    //uint8_t ssd1680_orientation = SSD1680_270_DEG;
    ssd1680_disp = ssd1680_init(spi_host, ssd1680_pinmap, EPAPER_RES_X, EPAPER_RES_Y, ssd1680_orientation);
    //ssd1680_disp = ssd1680_init_partial(spi_host, ssd1680_pinmap, EPAPER_RES_X, EPAPER_RES_Y, ssd1680_orientation);


    //display_demo_1(ssd1680_disp, SSD1680_BLACK);
    //display_demo_2(ssd1680_disp, SSD1680_BLACK);
    //display_demo_3(ssd1680_disp, SSD1680_BLACK);
    //display_demo_4(ssd1680_disp, SSD1680_BLACK);
    //display_demo_5(ssd1680_disp, SSD1680_BLACK);
    //ram_demo_1(ssd1680_disp, SSD1680_BLACK);
    //fonts_demo(ssd1680_disp, SSD1680_BLACK);
    //fonts_demo_2(ssd1680_disp, SSD1680_BLACK);
    //font_orientation_demo(ssd1680_disp, SSD1680_BLACK);
    partial_demo(ssd1680_disp, SSD1680_BLACK);
    //ssd1680_send_framebuffer(ssd1680_disp);
    //ssd1680_refresh(ssd1680_disp, FAST_FULL_REFRESH);

    //ssd1680_send_framebuffer(ssd1680_disp);
    //ssd1680_set_refresh_window(ssd1680_disp, 0, 180, 90, ssd1680_disp->res_y-1);
    //ssd1680_refresh(ssd1680_disp, FAST_PARTIAL_REFRESH);
    ssd1680_sleep(ssd1680_disp);
}
