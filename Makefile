.PHONY: build start_emulator install_emulator

LOG_FILE := tmp/emulator.log

build:
	pebble build || pebble build

start_emulator:
	mkdir -p tmp
	-pebble kill
	pebble wipe
	pebble logs --emulator=emery > $(LOG_FILE) 2>&1 &
	@echo "Emulator starting, logs -> $(LOG_FILE)"
	@sleep 3 && osascript \
		-e 'tell application "System Events" to set frontmost of (first process whose name contains "qemu") to true' &

install_emulator:
	pebble install --emulator=emery

install_cloudpebble:
	pebble install --cloudpebble
