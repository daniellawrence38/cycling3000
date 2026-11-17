/* dump_fmt.h: the text lines the eMMC read-out sends to the PC (FS_EMMC_MODE 3, hal_fs_esp.c), read
 * by tools/emmc_dump.py. No ESP-IDF dependency; host-tested by core/tests/test_dump_fmt.c, and the
 * same golden line is in tools/tests/test_emmc_dump.py so the two ends cannot drift apart.
 *
 * A data record is one line:
 *
 *     #CC7 D <lba, 8 hex> <sectors, 2 hex> <crc, 8 hex> <base64 of the sectors>\n
 *
 * The CRC is the project's CRC-32 (zlib's) over the four bytes of the LBA, little-endian, followed by
 * the data, so a damaged address is caught as surely as damaged data. Text and not binary because
 * the console turns line feeds into CR LF and other tasks' log lines can land between records; a
 * line that fails its CRC is simply sent again on the next pass. */
#ifndef FW_DUMP_FMT_H
#define FW_DUMP_FMT_H
#include <stddef.h>
#include <stdint.h>

#define FW_DUMP_MAX_SECTORS 8
/* Room for the longest record line: the prefix, 8 sectors in base64, the newline and a NUL. */
#define FW_DUMP_LINE_MAX (32 + ((FW_DUMP_MAX_SECTORS * 512 + 2) / 3) * 4 + 2)

/* Standard base64 with padding. Writes ((n + 2) / 3) * 4 characters, no NUL; returns that count. */
size_t fw_b64_encode(const uint8_t *in, size_t n, char *out);

/* The CRC a record carries. */
uint32_t fw_dump_crc(uint32_t lba, const uint8_t *data, size_t len);

/* Formats one record into out (cap at least FW_DUMP_LINE_MAX). Returns the line's length without the
 * NUL, or 0 if sectors is 0 or more than FW_DUMP_MAX_SECTORS or cap is too small. *crc_out, if not
 * NULL, receives the record's CRC. */
size_t fw_dump_record(char *out, size_t cap, uint32_t lba, uint32_t sectors, const uint8_t *data,
                      uint32_t *crc_out);
#endif
