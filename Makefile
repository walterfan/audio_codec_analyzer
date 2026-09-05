# Thin wrapper over CMake so the common flows are one command.
# `make verify` is the single entrypoint that must pass before committing.

BUILD      ?= build
BUILD_ASAN ?= build-asan
BUILD_TSAN ?= build-tsan
JOBS       ?= 8
ASSETS     ?= assets
OUT        ?= out

# Prefer Homebrew LLVM's clang-tidy when present (Apple CLT has none).
CLANG_TIDY ?= $(shell \
  command -v clang-tidy 2>/dev/null || \
  ls /opt/homebrew/opt/llvm/bin/clang-tidy 2>/dev/null || \
  ls /usr/local/opt/llvm/bin/clang-tidy 2>/dev/null)
CPPCHECK   ?= cppcheck

SRC_CXX := $(shell find src tools -name '*.cpp' | sort)
TEST_CXX := $(shell find tests -name '*.cpp' | sort)
ALL_CXX  := $(SRC_CXX) $(TEST_CXX)

.PHONY: all configure build test verify fixtures demo demo-live asan tsan \
        fetch-real tidy cppcheck lint clean fmt help

all: help

configure:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=RelWithDebInfo

build: configure
	cmake --build $(BUILD) -j$(JOBS)

test: build
	cd $(BUILD) && ctest --output-on-failure

# Build + full test suite + a real end-to-end run over generated fixtures.
verify: test fixtures demo
	@echo "verify: OK"

fixtures: build
	@mkdir -p $(ASSETS)
	./$(BUILD)/gen_fixtures $(ASSETS)

# Tiny public real-speech kit (Microsoft AEC-Challenge synthetic clip + local
# noisy/quiet derivatives). Written to assets/real/ (gitignored). Needs curl+sox.
fetch-real:
	./tools/fetch_real_assets.sh

# Exercises every stage on the synthetic fixtures. No audio device needed.
demo: build fixtures
	@mkdir -p $(OUT)
	./$(BUILD)/aca encode $(ASSETS)/speech.wav -o $(OUT)/speech.opus-frames --bitrate 24000
	./$(BUILD)/aca decode $(OUT)/speech.opus-frames -o $(OUT)/speech-decoded.wav
	./$(BUILD)/aca decode $(OUT)/speech.opus-frames -o $(OUT)/speech-lossy.wav --drop-every-nth 5
	./$(BUILD)/aca aec --mic $(ASSETS)/mic_echo.wav --ref $(ASSETS)/far_end.wav -o $(OUT)/aec.wav
	./$(BUILD)/aca ans $(ASSETS)/noisy.wav -o $(OUT)/ans.wav
	./$(BUILD)/aca agc $(ASSETS)/quiet.wav -o $(OUT)/agc.wav
	./$(BUILD)/aca pipeline --mic $(ASSETS)/mic_echo.wav --ref $(ASSETS)/far_end.wav \
		-o $(OUT)/pipeline.wav --aec --codec

# Needs a real mic; macOS will prompt for microphone permission on first run.
demo-live: build
	./$(BUILD)/aca devices
	./$(BUILD)/aca live --seconds 5 --aec --record $(OUT)/live.wav

# AddressSanitizer + UBSan. Live audio is off: PortAudio's CoreAudio backend
# produces unrelated sanitizer noise.
#
# detect_container_overflow=0 is required, not optional: Homebrew's libgtest is
# built WITHOUT ASan, and mixing instrumented and non-instrumented std::vector
# code produces a documented false positive inside GTestIsInitialized(). See
# https://github.com/google/sanitizers/wiki/AddressSanitizerContainerOverflow
#
# detect_stack_use_after_return=1 is off by default in ASan; turn it on so
# stack-use-after-return bugs are actually caught.
ASAN_OPTIONS_VAL ?= detect_container_overflow=0:detect_stack_use_after_return=1

asan:
	cmake -S . -B $(BUILD_ASAN) -DCMAKE_BUILD_TYPE=Debug -DACA_ENABLE_ASAN=ON -DACA_ENABLE_LIVE=OFF
	ASAN_OPTIONS=$(ASAN_OPTIONS_VAL) cmake --build $(BUILD_ASAN) -j$(JOBS)
	ASAN_OPTIONS=$(ASAN_OPTIONS_VAL) ./$(BUILD_ASAN)/aca_tests

# ThreadSanitizer. Separate from ASan — the two runtimes cannot coexist.
# Live audio stays off for the same reason as the asan target.
tsan:
	cmake -S . -B $(BUILD_TSAN) -DCMAKE_BUILD_TYPE=Debug -DACA_ENABLE_TSAN=ON -DACA_ENABLE_LIVE=OFF
	cmake --build $(BUILD_TSAN) -j$(JOBS)
	./$(BUILD_TSAN)/aca_tests

# clang-tidy needs compile_commands.json from a configured build tree.
tidy: build
	@test -n "$(CLANG_TIDY)" || { \
	  echo "clang-tidy not found. Install: brew install llvm  # or apt install clang-tidy"; \
	  exit 1; }
	@test -f $(BUILD)/compile_commands.json || { \
	  echo "missing $(BUILD)/compile_commands.json (rebuild with CMAKE_EXPORT_COMPILE_COMMANDS)"; \
	  exit 1; }
	$(CLANG_TIDY) -p $(BUILD) --quiet $(ALL_CXX)

# cppcheck is compile-database aware when available; falls back to -I include.
cppcheck:
	@command -v $(CPPCHECK) >/dev/null || { \
	  echo "cppcheck not found. Install: brew install cppcheck  # or apt install cppcheck"; \
	  exit 1; }
	@if [ -f $(BUILD)/compile_commands.json ]; then \
	  $(CPPCHECK) --project=$(BUILD)/compile_commands.json \
	    --enable=warning,style,performance,portability \
	    --std=c++20 --error-exitcode=1 --inline-suppr \
	    --suppressions-list=.cppcheck-suppressions \
	    --suppress=missingIncludeSystem \
	    -j$(JOBS); \
	else \
	  $(CPPCHECK) --enable=warning,style,performance,portability \
	    --std=c++20 --error-exitcode=1 --inline-suppr \
	    --suppressions-list=.cppcheck-suppressions \
	    --suppress=missingIncludeSystem \
	    -I include -j$(JOBS) $(ALL_CXX); \
	fi

lint: tidy cppcheck

fmt:
	@command -v clang-format >/dev/null || { echo "clang-format not installed"; exit 1; }
	clang-format -i $$(find src include tests tools -name '*.cpp' -o -name '*.h')

clean:
	rm -rf $(BUILD) $(BUILD_ASAN) $(BUILD_TSAN) $(OUT) $(ASSETS)/*.wav

help:
	@echo "audio_codec_analyzer (aca) — available make targets:"
	@echo ""
	@echo "  make build       configure + compile into build/"
	@echo "  make test        build + ctest (unit tests)"
	@echo "  make verify      test + fixtures + end-to-end demo  <- run before commit"
	@echo "  make fixtures    regenerate synthetic assets/*.wav"
	@echo "  make fetch-real  download tiny real-speech kit -> assets/real/"
	@echo "  make demo        run every stage on the synthetic fixtures"
	@echo "  make demo-live   mic/speaker demo (needs a device + mic permission)"
	@echo "  make asan        ASan/UBSan build and test run"
	@echo "  make tsan        ThreadSanitizer build and test run"
	@echo "  make tidy        clang-tidy over src/tests/tools"
	@echo "  make cppcheck    cppcheck static analysis"
	@echo "  make lint        tidy + cppcheck"
	@echo "  make fmt         clang-format -i on src/include/tests/tools"
	@echo "  make clean       remove build trees, out/, and assets/*.wav"
	@echo "  make help        show this list"

