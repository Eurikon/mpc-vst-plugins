MPC_VST ?= ../toolchain/upstream
PINNED_MPC_VST := $(abspath ../toolchain/upstream)
MPC_VST_COMMIT := fc1ea20ff83779d3f47fb43177f5284f59e57a99
TOOLS := $(MPC_VST)/tools
ifneq ($(filter default undefined,$(origin CXX)),)
CXX := $(if $(filter Darwin,$(shell uname -s)),clang++,g++)
endif
TEST_BIN := build/tests-$(shell uname -s)-$(shell uname -m)/engine-test
CPPFLAGS := -Ibuild -I$(MPC_VST)/wrapper
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test host-test skin arm clean prepare-tools
build/layout.conf: skin.py layout.template.conf $(wildcard assets/*) skin.css
	python3 skin.py

build/params.h: vst.json params.json build/layout.conf
	python3 $(TOOLS)/gen_vst.py vst.json --params-h

$(TEST_BIN): src/engine.cpp src/dsp/SynthDSP.hpp tests/engine_test.cpp build/params.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/engine.cpp tests/engine_test.cpp -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

host-test:
	bash $(TOOLS)/test_port.sh vst.json

ART_PYTHON ?= python3.11
ART_VENV ?= $(if $(wildcard ../toolchain/build/art-venv/bin/python),../toolchain/build/art-venv,build/art-venv)
ART_BROWSER ?= $(if $(wildcard ../toolchain/build/browser),../toolchain/build/browser,build/browser)
ART_REQUIREMENTS ?= requirements-art.txt

$(ART_VENV)/bin/python:
	$(ART_PYTHON) -m venv $(ART_VENV)

$(ART_VENV)/.dependencies: $(ART_REQUIREMENTS) $(ART_VENV)/bin/python
	@$(ART_VENV)/bin/python -c 'import importlib.metadata as m, pathlib, sys; pairs = [line.split("==") for line in pathlib.Path(sys.argv[1]).read_text().splitlines() if line.strip()]; assert all(m.version(name) == version for name, version in pairs)' "$(ART_REQUIREMENTS)" >/dev/null 2>&1 || $(ART_VENV)/bin/python -m pip install -r "$(ART_REQUIREMENTS)"
	touch $@

$(ART_VENV)/.browser: $(ART_VENV)/.dependencies
	@PLAYWRIGHT_BROWSERS_PATH="$(abspath $(ART_BROWSER))" $(ART_VENV)/bin/python -c 'from pathlib import Path; from playwright.sync_api import sync_playwright; p = sync_playwright().start(); executable = p.chromium.executable_path; p.stop(); assert Path(executable).is_file()' >/dev/null 2>&1 || PLAYWRIGHT_BROWSERS_PATH="$(abspath $(ART_BROWSER))" $(ART_VENV)/bin/python -m playwright install chromium
	touch $@

prepare-tools:
	@set -eu; \
	if [ "$(abspath $(MPC_VST))" != "$(PINNED_MPC_VST)" ]; then \
		[ "$$(git -C "$(MPC_VST)" rev-parse HEAD)" = "$(MPC_VST_COMMIT)" ] || { echo "MPC_VST must use upstream commit $(MPC_VST_COMMIT)" >&2; exit 1; }; \
	fi; \
	for patch in skin-tools.patch release-tools.patch; do \
		if [ "$(abspath $(MPC_VST))" = "$(PINNED_MPC_VST)" ]; then \
			git -C "$(MPC_VST)" apply --reverse --check "$(CURDIR)/$$patch"; \
		else \
			git -C "$(MPC_VST)" apply --reverse --check "$(CURDIR)/$$patch" 2>/dev/null || git -C "$(MPC_VST)" apply "$(CURDIR)/$$patch"; \
		fi; \
	done

skin: prepare-tools $(ART_VENV)/.browser build/layout.conf
	PATH=$(abspath $(ART_VENV))/bin:$(PATH) PLAYWRIGHT_BROWSERS_PATH=$(abspath $(ART_BROWSER)) $(ART_VENV)/bin/python $(TOOLS)/gen_vst.py vst.json
	$(ART_VENV)/bin/python $(TOOLS)/studio.py preview "build/skin/Twisted Cable - VST - MonoSlide/Plugin Skins" -o "build/preview_%d.png"

arm:
	MPC_VST="$(abspath $(MPC_VST))" ./build.sh

clean:
	rm -rf build
