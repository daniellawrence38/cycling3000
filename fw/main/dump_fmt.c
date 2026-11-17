/* dump_fmt.c: see dump_fmt.h. */
#include "dump_fmt.h"

#include "cc700/util.h"

size_t fw_b64_encode(const uint8_t *in, size_t n, char *out)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t k = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n)
            v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n)
            v |= in[i + 2];
        out[k++] = T[(v >> 18) & 63];
        out[k++] = T[(v >> 12) & 63];
        out[k++] = i + 1 < n ? T[(v >> 6) & 63] : '=';
        out[k++] = i + 2 < n ? T[v & 63] : '=';
    }
    return k;
}

uint32_t fw_dump_crc(uint32_t lba, const uint8_t *data, size_t len)
{
    uint8_t a[4] = {(uint8_t)lba, (uint8_t)(lba >> 8), (uint8_t)(lba >> 16), (uint8_t)(lba >> 24)};
    return cc_crc32_update(cc_crc32(a, 4), data, len);
}

static size_t put_hex(char *out, uint32_t v, int digits)
{
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < digits; i++)
        out[i] = H[(v >> (4 * (digits - 1 - i))) & 15];
    return (size_t)digits;
}

size_t fw_dump_record(char *out, size_t cap, uint32_t lba, uint32_t sectors, const uint8_t *data,
                      uint32_t *crc_out)
{
    if (sectors == 0 || sectors > FW_DUMP_MAX_SECTORS)
        return 0;
    size_t len = (size_t)sectors * 512;
    size_t need = 7 + 8 + 1 + 2 + 1 + 8 + 1 + ((len + 2) / 3) * 4 + 1 + 1;
    if (cap < need)
        return 0;
    uint32_t crc = fw_dump_crc(lba, data, len);
    if (crc_out)
        *crc_out = crc;
    size_t k = 0;
    const char pre[] = "#CC7 D ";
    for (size_t i = 0; i < sizeof pre - 1; i++)
        out[k++] = pre[i];
    k += put_hex(out + k, lba, 8);
    out[k++] = ' ';
    k += put_hex(out + k, sectors, 2);
    out[k++] = ' ';
    k += put_hex(out + k, crc, 8);
    out[k++] = ' ';
    k += fw_b64_encode(data, len, out + k);
    out[k++] = '\n';
    out[k] = '\0';
    return k;
}
