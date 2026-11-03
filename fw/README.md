# fw/

ESP-IDF project for the CC700 (brief WP7), pinned to the version in `IDF_VERSION`. Build with `make fw-build` from the repository root after `. $IDF_PATH/export.sh`; it builds `app.bin` (application only) and the RAM probe in `probe/`, and never flashes. Bringing it up on the unit is Daniel's, step by step, in `docs/BRINGUP.md`. `partitions.csv` describes the stock layout and is never flashed.
