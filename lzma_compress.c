/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
 * Use is subject to license terms.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * moinakg@belenix.org, http://moinakg.wordpress.com/
 *      
 */

#include <sys/types.h>
#include <stdio.h>
#include <strings.h>
#include <LzmaEnc.h>
#include <LzmaDec.h>
#include <utils.h>
#include <pcompress.h>
#include <allocator.h>

#define	SZ_ERROR_DESTLEN	100
#define	LZMA_DEFAULT_DICT	(1 << 24)

CLzmaEncProps *p = NULL;

static ISzAlloc g_Alloc = {
	slab_alloc,
	slab_release,
	NULL
};

void
lzma_stats(int show)
{
}

void
lzma_mt_props(algo_props_t *data, int level, uint64_t chunksize) {
	data->compress_mt_capable = 1;
	data->decompress_mt_capable = 0;
	data->buf_extra = 0;
	data->c_max_threads = 2;
	data->delta2_span = 150;
	if (level < 12)
		data->deltac_min_distance = (EIGHTM * 16);
	else
		data->deltac_min_distance = (EIGHTM * 32);
}

void
lzma_props(algo_props_t *data, int level, uint64_t chunksize) {
	data->compress_mt_capable = 0;
	data->decompress_mt_capable = 0;
	data->buf_extra = 0;
	data->delta2_span = 150;
	if (level < 12)
		data->deltac_min_distance = (EIGHTM * 16);
	else
		data->deltac_min_distance = (EIGHTM * 32);
}

/*
 * The two functions below are not thread-safe, by design.
 */
int
lzma_init(void **data, int *level, int nthreads, uint64_t chunksize,
	  int file_version, compress_op_t op)
{
	if (!p && op == COMPRESS) {
		p = (CLzmaEncProps *)slab_alloc(NULL, sizeof (CLzmaEncProps));
		LzmaEncProps_Init(p);
		/*
		 * Set the dictionary size and fast bytes based on level.
		 */
		if (*level < 8) {
			/*
			 * Choose a dict size with a balance between perf and
			 * compression.
			 */
			p->dictSize = LZMA_DEFAULT_DICT;

		} else {
			/*
			 * Let LZMA determine best dict size.
			 */
			p->dictSize = 0;
		}

		/* Determine the fast bytes value and also adjust dict size further. */
		if (*level < 7) {
			p->fb = 32;

		} else if (*level < 10) {
			p->fb = 64;

		} else if (*level == 11) {
			p->fb = 64;
			p->mc = 128;

		} else if (*level == 12) {
			p->fb = 128;
			p->mc = 256;

		} else if (*level == 13) {
			p->fb = 64;
			p->mc = 128;
			p->dictSize = (1 << 27);

		} else if (*level == 14) {
			p->fb = 128;
			p->mc = 256;
			p->dictSize = (1 << 28);
		}
		if (*level > 9) *level = 9;
		p->level = *level;
		p->numThreads = nthreads;
		LzmaEncProps_Normalize(p);
		slab_cache_add(p->litprob_sz);
	}
	if (*level > 9) *level = 9;
	*data = p;
	return (0);
}

int
lzma_deinit(void **data)
{
	if (p) {
		slab_release(NULL, p);
		p = NULL;
	}
	*data = NULL;
	return (0);
}

static void
lzerr(int err, int cmp)
{
	switch (err) {
	    case SZ_ERROR_MEM:
		log_msg(LOG_ERR, 0, "LZMA: Memory allocation error\n");
		break;
	    case SZ_ERROR_PARAM:
		log_msg(LOG_ERR, 0, "LZMA: Incorrect paramater\n");
		break;
	    case SZ_ERROR_WRITE:
		log_msg(LOG_ERR, 0, "LZMA: Write callback error\n");
		break;
	    case SZ_ERROR_PROGRESS:
		log_msg(LOG_ERR, 0, "LZMA: Progress callback errored\n");
		break;
	    case SZ_ERROR_INPUT_EOF:
		log_msg(LOG_ERR, 0, "LZMA: More compressed input bytes expected\n");
		break;
	    case SZ_ERROR_OUTPUT_EOF:
		/* This error is non-fatal during compression */
		if (!cmp)
			log_msg(LOG_ERR, 0, "LZMA: Output buffer overflow\n");
		break;
	    case SZ_ERROR_UNSUPPORTED:
		log_msg(LOG_ERR, 0, "LZMA: Unsupported properties\n");
		break;
	    case SZ_ERROR_DESTLEN:
		log_msg(LOG_ERR, 0, "LZMA: Output chunk size too small\n");
		break;
	    case SZ_ERROR_DATA:
		log_msg(LOG_ERR, 0, "LZMA: Data Error\n");
		break;
	    default:
		log_msg(LOG_ERR, 0, "LZMA: Unknown error code: %d\n", err);
	}
}

/*
 * LZMA compressed segment format(simplified)
 * ------------------------------------------
 * Offset Size Description
 *  0     1   Special LZMA properties for compressed data
 *  1     4   Dictionary size (little endian)
 * 13         Compressed data
 *
 * Derived from http://docs.bugaco.com/7zip/lzma.txt
 * We do not store the uncompressed chunk size here. It is stored in
 * our chunk header.
 */
int
lzma_compress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data)
{
	SizeT props_len = LZMA_PROPS_SIZE;
	SRes res;
	Byte *_dst;
	CLzmaEncProps *props = (CLzmaEncProps *)data;
	SizeT dlen;

	if (*dstlen < LZMA_PROPS_SIZE) {
		lzerr(SZ_ERROR_DESTLEN, 1);
		return (-1);
	}

	if (PC_SUBTYPE(btype) == TYPE_COMPRESSED_ZPAQ)
		return (-1);
	props->level = level;

	_dst = (Byte *)dst;
	*dstlen -= LZMA_PROPS_SIZE;
	dlen = *dstlen;
	res = LzmaEncode(_dst + LZMA_PROPS_SIZE, &dlen, (const uchar_t *)src, srclen,
	    props, (uchar_t *)_dst, &props_len, 0, NULL, &g_Alloc, &g_Alloc);
	*dstlen = dlen;

	if (res != 0) {
		lzerr(res, 1);
		return (-1);
	}

	*dstlen += LZMA_PROPS_SIZE;
	return (0);
}

int
lzma_decompress(void *src, uint64_t srclen, void *dst,
	uint64_t *dstlen, int level, uchar_t chdr, int btype, void *data)
{
	SizeT _srclen;
	const uchar_t *_src;
	SRes res;
	ELzmaStatus status;
	SizeT dlen;

	_srclen = srclen - LZMA_PROPS_SIZE;
	_src = (uchar_t *)src + LZMA_PROPS_SIZE;
	dlen = *dstlen;

	if ((res = LzmaDecode((uchar_t *)dst, &dlen, _src, &_srclen,
	    (uchar_t *)src, LZMA_PROPS_SIZE, LZMA_FINISH_ANY,
	    &status, &g_Alloc)) != SZ_OK) {
		*dstlen = dlen;
		lzerr(res, 0);
		return (-1);
	}
	*dstlen = dlen;
	return (0);
}

