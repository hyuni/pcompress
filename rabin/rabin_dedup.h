/*
 * rabin_polynomial_constants.h
 * 
 * Created by Joel Lawrence Tucci on 09-May-2011.
 * 
 * Copyright (c) 2011 Joel Lawrence Tucci
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the project's author nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

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

#ifndef _RABIN_POLY_H_
#define _RABIN_POLY_H_

#include <utils.h>
#include <index.h>
#include <crypto_utils.h>
#include <pthread.h>
#include <semaphore.h>

//List of constants, mostly constraints and defaults for various parameters
//to the Rabin Fingerprinting algorithm
//Use prime constant from Bulat Ziganshin's REP. Seems to work best across wide range of data.
#define	RAB_POLYNOMIAL_CONST 153191
#define	POLY_MASK (0xffffffffffULL)
#define	RAB_BLK_DEFAULT 1
#define	RAB_BLK_MIN_BITS 11
#define LZMA_WINDOW_MAX (128L * 1024L * 1024L)
#define	RAB_POLYNOMIAL_WIN_SIZE 16
#define	RAB_POLYNOMIAL_MIN_WIN_SIZE 8
#define	RAB_POLYNOMIAL_MAX_WIN_SIZE 64
#define	RAB_POLYNOMIAL_MAX_BLOCK_SIZE (128 * 1024)
#define	RAB_POLY_MAX_BLOCK_SIZE_GLOBAL (64 * 1024)
#define	RAB_BLK_MASK (((1 << RAB_BLK_MIN_BITS) - 1) >> 1)
#define	RAB_BLK_AVG_SZ(x) (1 << ((x) + RAB_BLK_MIN_BITS))

// The sliding window starts at min window size - this offset. It is needless
// to slide the window over every byte in the chunk.
#define	RAB_WINDOW_SLIDE_OFFSET	(64)

// Minimum practical chunk sizes when doing dedup
#define	RAB_MIN_CHUNK_SIZE (1048576L)
#define	RAB_MIN_CHUNK_SIZE_GLOBAL (2097152L)

// An entry in the Rabin block array in the chunk.
// It is either a length value <= RABIN_MAX_BLOCK_SIZE or an index value with
// which this block is a duplicate/similar. The entries are variable sized.
// Offset can be dynamically calculated.
//
#define	RABIN_ENTRY_SIZE (sizeof (unsigned int))

// Header for a chunk deduped using Rabin
// Number of rabin blocks, size of original data chunk, size of compressed index,
// size of deduped data, size of compressed data
#define	RABIN_HDR_SIZE (sizeof (unsigned int) + sizeof (uint64_t) + sizeof (uint64_t) + sizeof (uint64_t) + sizeof (uint64_t))

// Maximum number of dedup blocks supported (2^30 - 1)
#define	RABIN_MAX_BLOCKS (0x3FFFFFFFUL)

// Maximum possible block size for a single rabin block. This is a hard limit much
// larger than RAB_POLYNOMIAL_MAX_BLOCK_SIZE. Useful when merging non-duplicate blocks.
// This is also 2^31 - 1.
#define	RABIN_MAX_BLOCK_SIZE (RABIN_MAX_BLOCKS)

// Masks to determine whether Rabin index entry is a length value, duplicate index value
// or similar index value.
// MSB = 1 : Index
// MSB = 0 : Length
// MSB-1 = 1: Similarity Index
// MSB-1 = 0: Exact Duplicate Index
#define	RABIN_INDEX_FLAG (0x80000000UL)
#define	SET_SIMILARITY_FLAG (0x40000000UL)
#define	GET_SIMILARITY_FLAG SET_SIMILARITY_FLAG
#define	CLEAR_SIMILARITY_FLAG (0xBFFFFFFFUL)
#define	GLOBAL_FLAG RABIN_INDEX_FLAG
#define	CLEAR_GLOBAL_FLAG (0x7fffffffUL)

#define	RABIN_DEDUPE_SEGMENTED	0
#define	RABIN_DEDUPE_FIXED	1
#define	RABIN_DEDUPE_FILE_GLOBAL	2

// Mask to extract value from a rabin index entry
#define	RABIN_INDEX_VALUE (0x3FFFFFFFUL)

/*
 * Types of block similarity.
 */
#define	SIMILAR_EXACT 1
#define	SIMILAR_PARTIAL 2
#define	SIMILAR_REF 3

/*
 * TYpes of delta operations.
 * DELTA_NORMAL = Check for at least 60% similarity
 * DELTA_EXTRA  = Check for at least 40% similarity
 */
#define	DELTA_NORMAL	1
#define	DELTA_EXTRA	2

/*
 * Irreducible polynomial for Rabin modulus. This value is from the
 * Low Bandwidth Filesystem.
 */
#define	FP_POLY  0xbfe6b8a5bf378d83ULL

typedef struct rab_blockentry {
	uint64_t offset;
	uint32_t similarity_hash;
	uint32_t hash;
	uint32_t index;
	uint32_t length;
	unsigned char similar;
	struct rab_blockentry *other;
	struct rab_blockentry *next;
} rabin_blockentry_t;

typedef struct {
	unsigned char *current_window_data;
	rabin_blockentry_t **blocks;
	global_blockentry_t *g_blocks;
	uint32_t blknum;
	unsigned char *cbuf;
	uint32_t rabin_poly_max_block_size;
	uint32_t rabin_poly_min_block_size;
	uint32_t rabin_poly_avg_block_size;
	uint32_t rabin_avg_block_mask;
	uint32_t rabin_break_patt;
	uint64_t real_chunksize;
	short valid;
	void *lzma_data;
	int level, delta_flag, dedupe_flag, deltac_min_distance;
	uint64_t file_offset; // For global dedupe
	archive_config_t *arc;
	Sem_t *index_sem;
	Sem_t *index_sem_next;
	uchar_t *similarity_cksums;
	uint32_t pagesize;
	int out_fd;
	int id;
	int show_chunks; // Debug display of chunks (offset, length)
} dedupe_context_t;

extern dedupe_context_t *create_dedupe_context(uint64_t chunksize, uint64_t real_chunksize, 
	int rab_blk_sz, const char *algo, const algo_props_t *props, int delta_flag, int dedupe_flag,
	int file_version, compress_op_t op, uint64_t file_size, char *tmppath, int pipe_mode, int nthreads);
extern void destroy_dedupe_context(dedupe_context_t *ctx);
extern unsigned int dedupe_compress(dedupe_context_t *ctx, unsigned char *buf, 
	uint64_t *size, uint64_t offset, uint64_t *rabin_pos, int mt);
extern void dedupe_decompress(dedupe_context_t *ctx, uchar_t *buf, uint64_t *size);
extern void parse_dedupe_hdr(uchar_t *buf, unsigned int *blknum, uint64_t *dedupe_index_sz,
	uint64_t *dedupe_data_sz, uint64_t *rabin_index_sz_cmp,
	uint64_t *dedupe_data_sz_cmp, uint64_t *deduped_size);
extern void update_dedupe_hdr(uchar_t *buf, uint64_t dedupe_index_sz_cmp,
	uint64_t dedupe_data_sz_cmp);
extern void reset_dedupe_context(dedupe_context_t *ctx);
extern uint32_t dedupe_buf_extra(uint64_t chunksize, int rab_blk_sz, const char *algo,
	int delta_flag);
extern int global_dedupe_bufadjust(uint32_t rab_blk_sz, uint64_t *user_chunk_sz, int pct_interval,
		 const char *algo, cksum_t ck, cksum_t ck_sim, size_t file_sz,
		 size_t memlimit, int nthreads, int pipe_mode);

#endif /* _RABIN_POLY_H_ */
