#include "support.h"

#include <ctype.h>

static int nib(int c) { return isdigit(c) ? c - '0' : tolower(c) - 'a' + 10; }

size_t hex_to_bytes(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (*hex && n < cap) {
        if (isspace((unsigned char)*hex)) { hex++; continue; }
        out[n++] = (uint8_t)(nib(hex[0]) << 4 | nib(hex[1]));
        hex += 2;
    }
    return n;
}
