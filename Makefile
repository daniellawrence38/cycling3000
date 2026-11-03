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

.PHONY: all deps test test-c test-c-plain test-c-asan test-py vectors sim shots android fw-build clean

all: test

test: vectors test-c test-py

test-c: test-c-plain test-c-asan

deps:
	ui/deps.sh

test-c-plain: deps
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

sim: deps
	cmake -S . -B $(BUILD)/host -DCMAKE_BUILD_TYPE=Debug -DCC_SANITIZE=OFF >/dev/null
	cmake --build $(BUILD)/host -j $(JOBS) --target ccsim
	@echo "run: $(BUILD)/host/ui/ccsim   (P power, A bottom left, L bottom right; see ui/README.md)"

# Every scenario of every board from the simulator, the mockups rendered for comparison (needs Node and
# Playwright), and ui/out/contact-sheet.png with each pair side by side. Each step overwrites its output.
shots: sim
	mkdir -p ui/out
	$(BUILD)/host/ui/ccsim --shots ui/out/lvgl
	cd tools/design && CC700_DESIGN_ROOT=$(CURDIR)/ui/out/design $(PYTHON) gen.py >/dev/null
	-cd tools/design && NODE_PATH=`npm root -g` node render_boards.js --fonts $(CURDIR)/build/deps/node/node_modules/@fontsource --gen $(CURDIR)/ui/out/design/canvas/project --out $(CURDIR)/ui/out/mockup >/dev/null
	$(PYTHON) tools/design/contact_sheet.py --lv ui/out/lvgl --ref ui/out/mockup --out ui/out/contact-sheet.png

# Android :core and :testing (JVM). The shared protocol suite also runs against simunit, so build it first.
android: test-c-plain
	cd android && ./gradlew --no-daemon -q :core:test :testing:test

fw-build:
	@echo "make fw-build: not built yet (WP7)"; exit 1

clean:
	rm -rf $(BUILD) tools/reference/cc700-test-vectors.md
