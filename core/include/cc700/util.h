/* util.h: CRC-32, SHA-256, varint, little-endian helpers, half-up rounding, floor division.
 *
 * Every `//` in the test vectors and ref.py is floor division (brief 5.0). C `/` truncates toward
 * zero, so any division that can see a negative numerator must use cc_floordiv. */
#ifndef CC700_UTIL_H
#define CC700_UTIL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Floor division (Python `//`). b must not be 0. */
static inline int64_t cc_floordiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q -= 1;
    return q;
}

/* Half-up integer rounding of a/d for d > 0: (a*2 + d) // (2*d). */
static inline int64_t cc_div_half_up(int64_t a, int64_t d)
{
    return cc_floordiv(a * 2 + d, 2 * d);
}

/* CRC-32 (IEEE 802.3, reflected, init FFFFFFFF, xorout FFFFFFFF). Incremental: pass 0 to start. */
uint32_t cc_crc32_update(uint32_t crc, const void *data, size_t len);
static inline uint32_t cc_crc32(const void *data, size_t len) { return cc_crc32_update(0, data, len); }

/* SHA-256 */
typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t buf[64];
    size_t n;
} cc_sha256_t;

void cc_sha256_init(cc_sha256_t *s);
void cc_sha256_update(cc_sha256_t *s, const void *data, size_t len);
void cc_sha256_final(cc_sha256_t *s, uint8_t out[32]);
void cc_sha256(const void *data, size_t len, uint8_t out[32]);

/* Little-endian load and store */
static inline uint16_t cc_get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline int16_t cc_get_i16(const uint8_t *p) { return (int16_t)cc_get_u16(p); }
static inline uint32_t cc_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline int32_t cc_get_i32(const uint8_t *p) { return (int32_t)cc_get_u32(p); }
static inline void cc_put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void cc_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Varint (7 bits per byte, low group first) and zigzag. */
size_t cc_varint_put(uint8_t *out, uint64_t v);                                    /* returns bytes written (<= 10) */
size_t cc_varint_get(const uint8_t *in, size_t avail, uint64_t *v);                /* 0 on truncation or overlong */
static inline uint64_t cc_zigzag(int64_t n) { return ((uint64_t)n << 1) ^ (uint64_t)(n >> 63); }
static inline int64_t cc_unzigzag(uint64_t u) { return (int64_t)(u >> 1) ^ -(int64_t)(u & 1); }

#ifdef __cplusplus
}
#endif
#endif
