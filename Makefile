LOG_FILE := tmp/emulator.log

# Inject --vnc if PEBBLE_VNC is set
ifdef PEBBLE_VNC
VNC := --vnc
else
VNC :=
endif

# Build the app, both C & PKJS
.PHONY: build
build:
	pebble build || pebble build

.PHONY: shutdown_emulator
shutdown_emulator:
	-pebble kill
	pebble wipe

.PHONY: start_emulator
start_emulator: shutdown_emulator
	mkdir -p tmp
	pebble logs --emulator=emery $(VNC) > $(LOG_FILE) 2>&1 &
	@echo "Emulator starting, logs -> $(LOG_FILE)"
ifndef PEBBLE_VNC
	@sleep 3 && osascript \
		-e 'tell application "System Events" to set frontmost of (first process whose name contains "qemu") to true' &
endif

# Install the app on the emulator
.PHONY: install_emulator
install_emulator:
	pebble install --emulator=emery $(VNC)

# Install the app on your watch
.PHONY: install_cloudpebble
install_cloudpebble:
	pebble install --cloudpebble

# Clean build artifacts
.PHONY: clean
clean:
	pebble clean

# Take screenshots and generate GIFs for the app store page
.PHONY: create_screenshots
create_screenshots:
	bash scripts/create_screenshots.sh

# Generate app icon (resources/launcher_icon.png)
.PHONY: create_icon
create_icon:
	python3 scripts/generate_icon.py
