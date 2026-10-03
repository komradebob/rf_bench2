ARDUINO_CLI ?= arduino-cli

# Flashing does not go through arduino-cli: it needs esptool's
# --before no-reset, which arduino-cli has no way to pass. uv puts its
# tools in ~/.local/bin, which is not always on PATH.
ESPTOOL ?= $(if $(wildcard $(HOME)/.local/bin/esptool),$(HOME)/.local/bin/esptool,esptool)

FQBN ?= esp8266:esp8266:oak
PORT ?= /dev/tty.usbserial-2
BAUD ?= 115200
UPLOAD_SPEED ?= 460800

SKETCH_DIR := .
SKETCH_NAME := $(notdir $(abspath $(SKETCH_DIR)))

.PHONY: all compile upload monitor board-list clean help version bump-minor

TMPDIR ?= /tmp/arduino-tmp
export TMPDIR

BUILD_DIR = ./build
BOARD_DIR = $(BUILD_DIR)/$(subst :,.,$(FQBN))
BINARY = $(BOARD_DIR)/$(SKETCH_NAME).ino.bin
VERSION_HEADER := version.h

# Everything that ends up in the binary. secrets.h is picked up by the
# wildcard even though it is not in version control, so editing Wi-Fi
# credentials still triggers a rebuild.
SOURCES := $(wildcard $(SKETCH_DIR)/*.ino) \
           $(wildcard $(SKETCH_DIR)/*.cpp) \
           $(wildcard $(SKETCH_DIR)/*.h)

all: compile

# Read the current packed version, e.g. "1.0.0"
version:
	@awk '/^#define FIRMWARE_VERSION_MAJOR/ {ma=$$3} \
	     /^#define FIRMWARE_VERSION_MINOR/ {mi=$$3} \
	     /^#define FIRMWARE_VERSION_PATCH/ {pa=$$3} \
	     END {printf "%d.%d.%d\n", ma, mi, pa}' $(VERSION_HEADER)

# Increment the minor number so every recompile has a distinct version.
# Called from the build recipe, so it only runs when there is something
# new to compile: the bump happens first and the compile second, which
# leaves the binary newer than the header it was built from.
bump-minor:
	@awk '/^#define FIRMWARE_VERSION_MINOR/ { \
	        printf "#define FIRMWARE_VERSION_MINOR %d\n", $$3 + 1; \
	        next } \
	      { print }' $(VERSION_HEADER) > $(VERSION_HEADER).tmp
	@mv $(VERSION_HEADER).tmp $(VERSION_HEADER)
	@echo "Firmware version now $$(make -s version)"

# A real target, not a phony one, so make compares timestamps and does
# nothing when the sources are unchanged. --build-path is what makes that
# possible: without it arduino-cli compiles into a temporary directory and
# throws the binary away, leaving nothing to compare against.
$(BINARY): $(SOURCES)
	@$(MAKE) -s bump-minor
	$(ARDUINO_CLI) compile \
		--fqbn $(FQBN) \
		--build-path $(BOARD_DIR) \
		$(SKETCH_DIR)

compile: $(BINARY)

# The auto-reset network on this bench does not assert EN and GPIO0, so
# esptool cannot strap the chip into the bootloader on its own. GPIO0 is
# held low by hand and the board power-cycled instead, which leaves the
# chip sitting in the ROM bootloader. --before no-reset then attaches to
# it without touching DTR or RTS at all, so which adapter line reaches
# which pin stops mattering.
#
#   1. Disconnect RTS and DTR from the FTDI.
#   2. Jumper P2 (GPIO0) to ground.
#   3. Power-cycle the Oak.
#   4. make upload
#   5. Remove the jumper and power-cycle again to run the sketch.
#
# --after no-reset leaves the chip in the bootloader rather than toggling
# RTS, which step 5 undoes instead.
upload: $(BINARY)
	$(ESPTOOL) --chip esp8266 \
		--port $(PORT) \
		--baud $(UPLOAD_SPEED) \
		--before no-reset \
		--after no-reset \
		write-flash 0x0 $(BINARY)

monitor:
	$(ARDUINO_CLI) monitor \
		--port $(PORT) \
		--config baudrate=$(BAUD)

board-list:
	$(ARDUINO_CLI) board list

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  make compile       Compile the sketch if sources changed"
	@echo "  make upload        Compile if needed, then flash"
	@echo "  make monitor       Open serial monitor"
	@echo "  make board-list    List connected boards"
	@echo "  make clean         Remove build files"
	@echo "  make version       Show the current firmware version"
	@echo ""
	@echo "compile and upload are timestamp driven: both compile only when"
	@echo "a source file is newer than $(BINARY)."
	@echo ""
	@echo "upload needs the chip held in its bootloader by hand, because the"
	@echo "EN/GPIO0 network does not reset it: jumper P2 to ground, power-cycle"
	@echo "the Oak, then run make upload. See the recipe in this file."
	@echo ""
	@echo "Variables:"
	@echo "  FQBN=$(FQBN)"
	@echo "  PORT=$(PORT)"
	@echo "  BAUD=$(BAUD)"
	@echo "  UPLOAD_SPEED=$(UPLOAD_SPEED)"
	@echo "  ESPTOOL=$(ESPTOOL)"