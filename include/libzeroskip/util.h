/*
 * util.h
 *
 * Utilities
 *
 * This file is part of zeroskip.
 *
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#include <arpa/inet.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>

#ifdef LINUX
#include <endian.h>
#elif MACOSX
#include <machine/endian.h>
#endif

#include <libzeroskip/macros.h>
#include <libzeroskip/strarray.h>

CPP_GUARD_START

/* MAX_BUF_PATH : buffer size used for printing path */
#define MAX_BUF_PATH ((2 * PATH_MAX) + 1)

typedef enum {
        FALSE = 0,
        TRUE
} bool_t;

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
void *xcalloc(size_t nmemb, size_t size);
char *xstrdup(const char *s);
unsigned char *xucharbufdup(const unsigned char *s, size_t len);
#define xfree(ptr) do {                                 \
                if (ptr) { free(ptr); ptr = NULL; }     \
        } while (0)


static inline size_t off_to_size_t(off_t len)
{
        size_t size = (size_t) len;

        if (len != (off_t) size) {
                /* overflow, file size too big - need to abort */
                abort();
        }

        return size;
}


#define ENSURE_NON_NULL(p) p?p:""

/*
 * Endianness utils
 */
static inline uint64_t bswap64(uint64_t val)
{
        return (((val & (uint64_t)0x00000000000000ffULL) << 56) |
                ((val & (uint64_t)0x000000000000ff00ULL) << 40) |
                ((val & (uint64_t)0x0000000000ff0000ULL) << 24) |
                ((val & (uint64_t)0x00000000ff000000ULL) <<  8) |
                ((val & (uint64_t)0x000000ff00000000ULL) >>  8) |
                ((val & (uint64_t)0x0000ff0000000000ULL) >> 24) |
                ((val & (uint64_t)0x00ff000000000000ULL) >> 40) |
                ((val & (uint64_t)0xff00000000000000ULL) >> 56));
}

#define hton8(x)   (x)
#define ntoh8(x)   (x)
#define hton16(x)  htons(x)
#define ntoh16(x)  ntohs(x)
#define hton32(x)  htonl(x)
#define ntoh32(x)  ntohl(x)
#if BYTE_ORDER == LITTLE_ENDIAN
#define hton64(x)  bswap64(x)
#define ntoh64(x)  bswap64(x)
#elif BYTE_ORDER == BIG_ENDIAN
#define hton64(x)  (x)
#define ntoh64(x)  (x)
#endif

/* Wrapper functions for reading and writing data.
   p - pointer to buffer, v - value to be copied into buffer
 */
#define write_be8(p, v)    do { *(uint8_t *)(p) = hton8(v); } while(0)
#define write_be16(p, v)   do { *(uint16_t *)(p) = hton16(v); } while(0)
#define write_be32(p, v)   do { *(uint32_t *)(p) = hton32(v); } while(0)
#define write_be64(p, v)   do { *(uint64_t *)(p) = hton64(v); } while(0)

#define read_be8(p)        ntoh8(*(uint8_t *)(p))
#define read_be16(p)       ntoh16(*(uint16_t *)(p))
#define read_be32(p)       ntoh32(*(uint32_t *)(p))
#define read_be64(p)       ntoh64(*(uint64_t *)(p))

/*
 * ARRAY_SIZE - get the number of elements in a visible array
 *  <at> x: the array whose size you want.
 *
 * This does not work on pointers, or arrays declared as [], or
 * function parameters.  With correct compiler support, such usage
 * will cause a build error (see the build_assert_or_zero macro).
 */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define bitsizeof(x)  (CHAR_BIT * sizeof(x))


/*
 * Macros to guard against integer overflows.
 */
#define maximum_signed_value_of_type(a) \
    (INTMAX_MAX >> (bitsizeof(intmax_t) - bitsizeof(a)))

#define maximum_unsigned_value_of_type(a) \
    (UINTMAX_MAX >> (bitsizeof(uintmax_t) - bitsizeof(a)))

/*
 * Signed integer overflow is undefined in C, so here's a helper macro
 * to detect if the sum of two integers will overflow.
 *
 * Requires: a >= 0, typeof(a) equals typeof(b)
 */
#define signed_add_overflows(a, b) \
    ((b) > maximum_signed_value_of_type(a) - (a))

#define unsigned_add_overflows(a, b) \
    ((b) > maximum_unsigned_value_of_type(a) - (a))

/*
 * Returns true if the multiplication of "a" and "b" will
 * overflow. The types of "a" and "b" must match and must be unsigned.
 * Note that this macro evaluates "a" twice!
 */
#define unsigned_mult_overflows(a, b) \
    ((a) && (b) > maximum_unsigned_value_of_type(a) / (a))

#ifdef __GNUC__
#define TYPEOF(x) (__typeof__(x))
#else
#define TYPEOF(x)
#endif

#define MSB(x, bits) ((x) & TYPEOF(x)(~0ULL << (bitsizeof(x) - (bits))))
#define HAS_MULTI_BITS(i)  ((i) & ((i) - 1))  /* checks if an integer has more than 1 bit set */

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

#define ALLOC_ARRAY(x, alloc) (x) = xmalloc(st_mult(sizeof(*(x)), (alloc)))
#define REALLOC_ARRAY(x, alloc) (x) = xrealloc((x), st_mult(sizeof(*(x)), (alloc)))

#define alloc_nr(x) (((x)+16)*3/2)

/*
 * Realloc the buffer pointed at by variable 'x' so that it can hold
 * at least 'nr' entries; the number of entries currently allocated
 * is 'alloc', using the standard growing factor alloc_nr() macro.
 *
 * DO NOT USE any expression with side-effect for 'x', 'nr', or 'alloc'.
 */
#define ALLOC_GROW(x, nr, alloc) \
        do { \
                if ((nr) > alloc) { \
                        if (alloc_nr(alloc) < (nr)) \
                                alloc = (nr); \
                        else \
                                alloc = alloc_nr(alloc); \
                        REALLOC_ARRAY(x, alloc); \
                } \
        } while (0)



static inline size_t st_mult(size_t a, size_t b)
{
        if (unsigned_mult_overflows(a, b)) {
                fprintf(stderr, "size_t overflow: %zx * %zx",
                        (uintmax_t)a, (uintmax_t)b);
                exit(EXIT_FAILURE);
        }

        return a * b;
}


int file_change_mode_rw(const char *path);
bool_t file_exists(const char *file);

int xrename(const char *oldpath, const char *newpath);
int xmkdir(const char *path, mode_t mode);
int xunlink(const char *path);

int get_filenames_with_matching_prefix(char *const path[], const char *prefix,
                                       struct str_array *arr, int full_path);

/* Return filenames with absolute paths */
static inline int get_filenames_with_matching_prefix_abs(char *const path[],
                                                         const char *prefix,
                                                         struct str_array *arr)
{
        return get_filenames_with_matching_prefix(path, prefix,
                                                  arr, 1);

}

/* Return filenames with relative paths */
static inline int get_filenames_with_matching_prefix_rel(char *const path[],
                                                         const char *prefix,
                                                         struct str_array *arr)
{
        return get_filenames_with_matching_prefix(path, prefix,
                                                  arr, 0);

}

/* recursive_rm():
 * Recursively remove contents of a directory.
 */
int recursive_rm(const char *path);

static inline int is_dotdir(const char *name)
{
        return (name[0] == '.' &&
                (name[1] == '\0' ||
                 (name[1] == '.' && name[2] == '\0')));
}


/* Time functions */
long long time_in_us(void);
long long time_in_ms(void);

void sleep_ms(uint32_t milliseconds);

/* Round up `n` to the nearest multiple of `m` */
static inline size_t round_up(size_t n, size_t m)
{
        return ((n + m - 1) / m) * m;
}

static inline size_t round_up_bits(size_t n, size_t m)
{
        return ((((n * 8) + m - 1) / m) * m) / 8;
}

#define roundup64(x)      round_up((x), 64)
#define roundup32(x)      round_up((x), 32)
#define roundup64bits(x)  round_up_bits((x), 64)
/*
  File Locking
 */
struct flockctx;

int file_lock(int fd, struct flockctx **ctx);
int file_unlock(int fd, struct flockctx **ctx);


/* Comparison functions */
int natural_strcasecmp(const char *s1, const char *s2);

static inline int memcmp_raw(const void *s1, size_t l1,
                             const void *s2, size_t l2)
{
        int ret;

        ret = memcmp(s1, s2, l1 < l2 ? l1 : l2);

        if (ret == 0) {
                if (l1 > l2)
                        ret = 1;
                else if (l2 > l1)
                        ret = -1;
        }

        return ret;
}

static inline int memcmp_mboxlist(const void *s1, size_t l1,
                                  const void *s2, size_t l2)
{
        int ret;

        ret = memcmp(s1, s2, l1 < l2 ? l1 : l2);

        if (ret < 0) {
                if (l1 > l2)
                        ret = 1;
                else if (l2 > l1)
                        ret = -1;
                else
                        ret = 0;
        }

        return ret;
}

static inline int memcmp_natural(const void *s1, size_t l1,
                                 const void *s2, size_t l2)
{
        return memcmp(s1, s2, l1 < l2 ? l1 : l2);
}

CPP_GUARD_END

#endif  /* _UTIL_H_ */
