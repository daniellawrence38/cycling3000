"""fw/partitions.csv must equal the stock layout from the handover's partition manifest (brief WP7)."""
import csv
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# From GEOID-CC700-Pro-complete-handover.md, "Firmware and partition manifest".
STOCK = [
    ("nvs", "data", "nvs", 0x9000, 16384),
    ("otadata", "data", "ota", 0xD000, 8192),
    ("phy_init", "data", "phy", 0xF000, 4096),
    ("coredump", "data", "coredump", 0x10000, 65536),
    ("ota_0", "app", "ota_0", 0x20000, 8192000),
    ("ota_1", "app", "ota_1", 0x7F0000, 8192000),
]


def rows():
    with open(os.path.join(ROOT, "fw/partitions.csv")) as f:
        for r in csv.reader(f):
            if not r or r[0].lstrip().startswith("#"):
                continue
            name, typ, sub, off, size = [x.strip() for x in r[:5]]
            yield name, typ, sub, int(off, 0), int(size, 0)


def test_partitions_equal_stock_layout():
    assert list(rows()) == STOCK


def test_fits_16mb_and_no_overlap():
    end = 0
    for _, _, _, off, size in rows():
        assert off >= end
        end = off + size
    assert end <= 16 * 1024 * 1024


def test_app_limit_matches_ota1():
    with open(os.path.join(ROOT, "tools/fw_build.sh")) as f:
        assert "LIMIT=8192000" in f.read()


def test_sdkconfig_keeps_usb_and_stock_data():
    with open(os.path.join(ROOT, "fw/sdkconfig.defaults")) as f:
        s = f.read()
    assert "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y" in s
    assert "TINYUSB" not in s.replace("TinyUSB is not", "")
    for line in ("CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=n", "CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y",
                 "CONFIG_BT_NIMBLE_NVS_PERSIST=n", "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y"):
        assert line in s
    for root, _, files in os.walk(os.path.join(ROOT, "fw")):
        for name in files:
            if name.endswith(".c"):
                with open(os.path.join(root, name)) as f:
                    src = f.read()
                assert "nvs_flash_init" not in src.replace("nvs_flash_init (the", "")
                assert "esp_partition_write" not in src and "esp_ota_" not in src and "#include \"tinyusb" not in src and "tusb_" not in src


def test_probe_is_ram_only():
    with open(os.path.join(ROOT, "fw/probe/sdkconfig.defaults")) as f:
        s = f.read()
    assert "CONFIG_APP_BUILD_TYPE_RAM=y" in s and "CONFIG_APP_BUILD_TYPE_PURE_RAM_APP=y" in s
