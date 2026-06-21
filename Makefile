.PHONY: start_emulator install_emulator

LOG_FILE := tmp/emulator.log

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
