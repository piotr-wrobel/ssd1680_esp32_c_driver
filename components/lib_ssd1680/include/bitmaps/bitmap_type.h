/*
 * bitmap_type.h
 *
 *  Created on: 10.06.2024
 *      Author: pvg
 */

#ifndef COMPONENTS_LIB_SSD1680_INCLUDE_BITMAPS_BITMAP_TYPE_H_
#define COMPONENTS_LIB_SSD1680_INCLUDE_BITMAPS_BITMAP_TYPE_H_

typedef struct {
	const uint16_t width;
	const uint16_t height;
	const uint16_t data_size;
	const unsigned char data[];
} ssd1680_bitmap_t;

#endif /* COMPONENTS_LIB_SSD1680_INCLUDE_BITMAPS_BITMAP_TYPE_H_ */
