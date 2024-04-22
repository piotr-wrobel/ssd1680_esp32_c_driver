/*
 * font_type.h
 *
 *  Created on: 21 kwi 2024
 *      Author: pvg
 */

#ifndef COMPONENTS_LIB_SSD1680_INCLUDE_FONTS_FONT_TYPE_H_
#define COMPONENTS_LIB_SSD1680_INCLUDE_FONTS_FONT_TYPE_H_

typedef struct {
	const uint8_t x_size;
	const uint8_t y_size;
	const uint8_t bytes_per_row;
	const uint8_t bytes_per_char;
	const unsigned char data[];
} ssd1680_font_t;

#endif /* COMPONENTS_LIB_SSD1680_INCLUDE_FONTS_FONT_TYPE_H_ */
