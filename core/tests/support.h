/* Shared helpers for the host tests. */
#ifndef CC_TEST_SUPPORT_H
#define CC_TEST_SUPPORT_H
#include <stddef.h>
#include <stdint.h>

/* Parse a hex string with optional spaces and newlines into out. Returns byte count. */
size_t hex_to_bytes(const char *hex, uint8_t *out, size_t cap);
#endif
