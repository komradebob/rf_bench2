
ARDUINO_CLI ?= arduino-cli

FQBN ?= esp8266:esp8266:oak
PORT ?= /dev/tty.usbserial-1
BAUD ?= 115200

SKETCH_DIR := .

.PHONY: all compile upload monitor board-list clean help

TMPDIR ?= /tmp/arduino-tmp
export TMPDIR

BUILD_DIR = ./build

all: compile

compile:
	$(ARDUINO_CLI) compile \
		--fqbn $(FQBN) \
		$(SKETCH_DIR)

upload: compile
	$(ARDUINO_CLI) upload \
		--fqbn $(FQBN) \
		--port $(PORT) \
		$(SKETCH_DIR)

monitor:
	$(ARDUINO_CLI) monitor \
		--port $(PORT) \
		--config baudrate=$(BAUD)

board-list:
	$(ARDUINO_CLI) board list

clean:
	rm -rf build

help:
	@echo "Targets:"
	@echo "  make compile       Compile the sketch"
	@echo "  make upload        Compile and upload"
	@echo "  make monitor       Open serial monitor"
	@echo "  make board-list    List connected boards"
	@echo "  make clean         Remove build files"
	@echo ""
	@echo "Variables:"
	@echo "  FQBN=$(FQBN)"
	@echo "  PORT=$(PORT)"
	@echo "  BAUD=$(BAUD)"

