KDIR ?= /lib/modules/$(shell uname -r)/build
TEXT_MODE ?= 0

.PHONY: all kernel user clean load unload demo

all: kernel user

kernel:
	$(MAKE) -C kernel KDIR="$(KDIR)" TEXT_MODE="$(TEXT_MODE)"

user:
	$(MAKE) -C user

clean:
	$(MAKE) -C kernel KDIR="$(KDIR)" TEXT_MODE="$(TEXT_MODE)" clean
	$(MAKE) -C user clean

load: all
	bash ./scripts/load.sh

unload:
	bash ./scripts/unload.sh

demo:
	bash ./scripts/demo.sh
