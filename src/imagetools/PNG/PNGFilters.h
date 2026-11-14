// Copyright (C) 2026 DarkMatter Productions
//

#ifndef __PNGFILTERS_H__
#define __PNGFILTERS_H__

#include <stddef.h>
#include <stdint.h>

/*
===============================================================================

	PNG scanline filters (filter method 0: None, Sub, Up, Average, Paeth).

	bytesPerPixel is the filter distance: the pixel size in bytes, rounded up
	to one for sub-byte depths. prior is the previous unfiltered row, or a
	zeroed row for the first row of an image or pass; it is never NULL.

===============================================================================
*/

static const int PNG_FILTER_NONE	= 0;
static const int PNG_FILTER_SUB		= 1;
static const int PNG_FILTER_UP		= 2;
static const int PNG_FILTER_AVERAGE	= 3;
static const int PNG_FILTER_PAETH	= 4;

// Reverses filter on src into dst (dst may equal src). False for an unknown filter type.
bool	PNG_UnfilterRow( uint8_t filter, uint8_t *dst, const uint8_t *src, const uint8_t *prior, size_t rowBytes, uint32_t bytesPerPixel );

// Applies filter to row into dst.
void		PNG_FilterRow( int filter, uint8_t *dst, const uint8_t *row, const uint8_t *prior, size_t rowBytes, uint32_t bytesPerPixel );

// Sum of the filtered bytes read as signed values: the minimum-sum heuristic
// for choosing a row's filter.
uint64_t	PNG_FilterRowSum( const uint8_t *filtered, size_t rowBytes );

#endif /* !__PNGFILTERS_H__ */
