obj-m	+= hid-logitech-hidpp.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
build:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean

update:
	@echo "load built module into kernel"
	-rmmod hid_logitech_hidpp
	insmod hid-logitech-hidpp.ko

revert:
	@echo "revert module to original"
	-rmmod hid_logitech_hidpp
	modprobe hid-logitech-hidpp

install:
	@echo "install module into system"
	$(eval DDIR := /lib/modules/$(shell uname -r)/kernel/drivers/hid)
	mv $(DDIR)/hid-logitech-hidpp.ko $(DDIR)/hid-logitech-hidpp.ko.bak
	cp $(PWD)/hid-logitech-hidpp.ko $(DDIR)/
	-rmmod hid_logitech_hidpp
	modprobe hid-logitech-hidpp
