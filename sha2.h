/*
 * FIPS 180-2 SHA-224/256/384/512 implementation
 * Last update: 02/02/2007
 * Issue date:  04/30/2005
 *
 * Copyright (C) 2005, 2007 Olivier Gay <olivier.gay@a3.epfl.ch>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 *
 * 07/12/2007 C++ binding is added by Masahiro Kasahara.
 *            The added part also follows the above conditions.
 */

#ifndef SHA2_H
#define SHA2_H

#include <vector>

#define SHA224_DIGEST_SIZE ( 224 / 8)
#define SHA256_DIGEST_SIZE ( 256 / 8)
#define SHA384_DIGEST_SIZE ( 384 / 8)
#define SHA512_DIGEST_SIZE ( 512 / 8)

#define SHA256_BLOCK_SIZE  ( 512 / 8)
#define SHA512_BLOCK_SIZE  (1024 / 8)
#define SHA384_BLOCK_SIZE  SHA512_BLOCK_SIZE
#define SHA224_BLOCK_SIZE  SHA256_BLOCK_SIZE

#ifndef SHA2_TYPES
#define SHA2_TYPES
typedef unsigned char uint8;
typedef unsigned int  uint32;
typedef unsigned long long uint64;
#endif

typedef struct {
    unsigned int tot_len;
    unsigned int len;
    unsigned char block[2 * SHA256_BLOCK_SIZE];
    uint32 h[8];
} sha256_ctx;

typedef struct {
    unsigned int tot_len;
    unsigned int len;
    unsigned char block[2 * SHA512_BLOCK_SIZE];
    uint64 h[8];
} sha512_ctx;

typedef sha512_ctx sha384_ctx;
typedef sha256_ctx sha224_ctx;

void sha224_init(sha224_ctx *ctx);
void sha224_update(sha224_ctx *ctx, const unsigned char *message,
                   unsigned int len);
void sha224_final(sha224_ctx *ctx, unsigned char *digest);
void sha224(const unsigned char *message, unsigned int len,
            unsigned char *digest);

class SHA224 {
	sha224_ctx ctx;
public:
	inline SHA224() { init(); }
	inline void init() { sha224_init(&ctx); }
	inline void update(const unsigned char *message, unsigned int len) { sha224_update(&ctx, message, len); }
	inline void final(std::vector<unsigned char>& digest) { digest.resize(SHA224_DIGEST_SIZE); sha224_final(&ctx, &*digest.begin()); }
	inline void doAll(const unsigned char *message, unsigned int len, std::vector<unsigned char>& digest) { update(message, len); final(digest); }
};

void sha256_init(sha256_ctx * ctx);
void sha256_update(sha256_ctx *ctx, const unsigned char *message,
                   unsigned int len);
void sha256_final(sha256_ctx *ctx, unsigned char *digest);
void sha256(const unsigned char *message, unsigned int len,
            unsigned char *digest);

class SHA256 {
	sha256_ctx ctx;
public:
	inline SHA256() { init(); }
	inline void init() { sha256_init(&ctx); }
	inline void update(const unsigned char *message, unsigned int len) { sha256_update(&ctx, message, len); }
	inline void final(std::vector<unsigned char>& digest) { digest.resize(SHA256_DIGEST_SIZE); sha256_final(&ctx, &*digest.begin()); }
	inline void doAll(const unsigned char *message, unsigned int len, std::vector<unsigned char>& digest) { update(message, len); final(digest); }
};

void sha384_init(sha384_ctx *ctx);
void sha384_update(sha384_ctx *ctx, const unsigned char *message,
                   unsigned int len);
void sha384_final(sha384_ctx *ctx, unsigned char *digest);
void sha384(const unsigned char *message, unsigned int len,
            unsigned char *digest);

class SHA384 {
	sha384_ctx ctx;
public:
	inline SHA384() { init(); }
	inline void init() { sha384_init(&ctx); }
	inline void update(const unsigned char *message, unsigned int len) { sha384_update(&ctx, message, len); }
	inline void final(std::vector<unsigned char>& digest) { digest.resize(SHA384_DIGEST_SIZE); sha384_final(&ctx, &*digest.begin()); }
	inline void doAll(const unsigned char *message, unsigned int len, std::vector<unsigned char>& digest) { update(message, len); final(digest); }
};

void sha512_init(sha512_ctx *ctx);
void sha512_update(sha512_ctx *ctx, const unsigned char *message,
                   unsigned int len);
void sha512_final(sha512_ctx *ctx, unsigned char *digest);
void sha512(const unsigned char *message, unsigned int len,
            unsigned char *digest);

class SHA512 {
	sha512_ctx ctx;
public:
	inline SHA512() { init(); }
	inline void init() { sha512_init(&ctx); }
	inline void update(const unsigned char *message, unsigned int len) { sha512_update(&ctx, message, len); }
	inline void final(std::vector<unsigned char>& digest) { digest.resize(SHA512_DIGEST_SIZE); sha512_final(&ctx, &*digest.begin()); }
	inline void doAll(const unsigned char *message, unsigned int len, std::vector<unsigned char>& digest) { update(message, len); final(digest); }
};

#endif /* !SHA2_H */

