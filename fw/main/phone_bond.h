/* phone_bond.h: the bonded phone's pairing keys on the eMMC (Q43). No ESP-IDF or NimBLE dependency;
 * host-tested by core/tests/test_fw_phone_bond.c.
 *
 * WHY. NimBLE normally persists bonds in the NVS partition, which belongs to the stock firmware and is
 * never initialised here (Q29). link_gatt.c keeps NimBLE's RAM store as the live store and mirrors the
 * phone's records into CC700/config/phone.a and phone.b: two slots, each with a sequence number and a
 * CRC-32, written alternately and fsynced and never renamed (protocol 7, brief 5.9), so a power cut at
 * any byte leaves the previous record loadable.
 *
 * WHAT. Only the phone's bond is kept: a peer security record that is *authenticated* (the passkey was
 * typed), our own security record for the same peer, that peer's CCC descriptors, and the unit's
 * local IRK (so the identity the phone stored stays valid). A sensor that bonded through Just Works is
 * never authenticated, so it is neither written nor counted (Q42 point 5). The keys are not logged. */
#ifndef PHONE_BOND_H
#define PHONE_BOND_H
#include <stddef.h>
#include <stdint.h>

#include "hal_fs.h"

#define FW_BOND_A "config/phone.a"
#define FW_BOND_B "config/phone.b"
#define FW_BOND_MAX_SEC 8   /* NimBLE MAX_BONDS (4) peer records plus as many of our own */
#define FW_BOND_MAX_CCCD 8  /* NimBLE MAX_CCCDS default */
#define FW_BOND_REC_MAX 768 /* any record: 14 + 8 * 72 + 8 * 12 + 23 = 709 bytes; one phone is 286 */

enum { FW_BOND_OUR = 1, FW_BOND_PEER = 2 };

typedef struct {
    uint8_t type; /* BLE address type (0 public, 1 random) */
    uint8_t val[6];
} fw_bond_addr_t;

typedef struct {
    uint8_t kind; /* FW_BOND_OUR or FW_BOND_PEER */
    fw_bond_addr_t peer;
    uint8_t key_size;
    uint16_t ediv;
    uint64_t rand_num;
    uint8_t ltk[16], irk[16], csrk[16];
    uint8_t ltk_present, irk_present, csrk_present;
    uint32_t sign_counter;
    uint8_t authenticated, sc;
} fw_bond_sec_t;

typedef struct {
    fw_bond_addr_t peer;
    uint16_t chr_val_handle;
    uint16_t flags;
    uint8_t value_changed;
} fw_bond_cccd_t;

typedef struct {
    uint8_t nsec, ncccd, has_local_irk;
    fw_bond_sec_t sec[FW_BOND_MAX_SEC];
    fw_bond_cccd_t cccd[FW_BOND_MAX_CCCD];
    fw_bond_addr_t local_addr;
    uint8_t local_irk[16];
} fw_bond_t;

typedef struct {
    int newest; /* 0 slot a, 1 slot b, -1 none valid */
    uint32_t seq;
} fw_bond_slots_t;

/* Keeps from `all` only what belongs to an authenticated (passkey) peer: its peer and our security
 * records, its CCCDs, and the local IRK if any such peer exists. Returns the number of phones kept. */
int fw_bond_filter(const fw_bond_t *all, fw_bond_t *out);
/* Authenticated peer records in b: the "a phone is bonded" count. Unauthenticated ones never count. */
int fw_bond_phone_count(const fw_bond_t *b);

/* Returns the encoded length, or 0 if b does not fit in cap. */
size_t fw_bond_encode(const fw_bond_t *b, uint32_t seq, uint8_t *out, size_t cap);
/* 1 if buf holds a whole, valid record (magic, version, counts, exact length, CRC). */
int fw_bond_decode(const uint8_t *buf, size_t len, fw_bond_t *b, uint32_t *seq);

/* Loads the newest valid slot. Returns 1 if found, 0 if none (b is then empty). */
int fw_bond_load(hal_fs_t *fs, fw_bond_t *b, fw_bond_slots_t *slots);
/* Writes the older slot and fsyncs it. 0 on success. */
int fw_bond_save(hal_fs_t *fs, const fw_bond_t *b, fw_bond_slots_t *slots);
/* Removes both slots (forget phone): no key material is left behind. 0 if neither remains. */
int fw_bond_erase(hal_fs_t *fs, fw_bond_slots_t *slots);
#endif
