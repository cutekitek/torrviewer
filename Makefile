BUILD_DIR ?= build
APP := $(BUILD_DIR)/src/torrview

.PHONY: build run

build:
	@if [ -d "$(BUILD_DIR)" ]; then \
		meson setup "$(BUILD_DIR)" --reconfigure; \
	else \
		meson setup "$(BUILD_DIR)"; \
	fi
	meson compile -C "$(BUILD_DIR)"

run: build
	$(APP)
