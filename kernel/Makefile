obj-m += kbmonitor.o
kbmonitor-y := kbmonitor_main.o kbmonitor_log.o

ifeq ($(TEXT_MODE),1)
ccflags-y += -DENABLE_TEXT_MODE
endif

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: all clean

all:
	$(MAKE) -C "$(KDIR)" M="$(PWD)" modules

clean:
	$(MAKE) -C "$(KDIR)" M="$(PWD)" clean
