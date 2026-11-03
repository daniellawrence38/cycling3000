# CC700 build entry points. See docs/PROGRESS.md for what each target covers today.
#   make test      host C tests (plain and ASan+UBSan) and Python tool tests
#   make vectors   regenerate the test vectors from tools/reference and fail on any difference
#   make sim       build the LVGL simulator (WP4)
#   make shots     render every board scenario and the contact sheet (WP4)
#   make android   Gradle build and unit tests (WP5, WP6)
#   make fw-build  ESP-IDF build of fw/ (WP7). Never flashes anything.

PYTHON  ?= python3
JOBS    ?= $(shell nproc 2>/dev/null || echo 4)
BUILD   := build

.PHONY: all test test-c test-c-plain test-c-asan test-py vectors sim shots android fw-build clean

all: test

test: vectors test-c test-py

test-c: test-c-plain test-c-asan

test-c-plain:
	cmake -S . -B $(BUILD)/host -DCMAKE_BUILD_TYPE=Debug -DCC_SANITIZE=OFF >/dev/null
	cmake --build $(BUILD)/host -j $(JOBS)
	ctest --test-dir $(BUILD)/host --output-on-failure -j $(JOBS)

test-c-asan:
	cmake -S . -B $(BUILD)/asan -DCMAKE_BUILD_TYPE=Debug -DCC_SANITIZE=ON >/dev/null
	cmake --build $(BUILD)/asan -j $(JOBS)
	ctest --test-dir $(BUILD)/asan --output-on-failure -j $(JOBS)

test-py:
	$(PYTHON) -m pytest -q tools

vectors:
	cd tools/reference && $(PYTHON) vectors.py
	diff -u claude/cc700-test-vectors.md tools/reference/cc700-test-vectors.md
	@echo "vectors: identical to claude/cc700-test-vectors.md"

sim:
	@echo "make sim: not built yet (WP4)"; exit 1

shots:
	@echo "make shots: not built yet (WP4)"; exit 1

android:
	@echo "make android: not built yet (WP5)"; exit 1

fw-build:
	@echo "make fw-build: not built yet (WP7)"; exit 1

clean:
	rm -rf $(BUILD) tools/reference/cc700-test-vectors.md
