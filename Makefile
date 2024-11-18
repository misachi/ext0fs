EXT0_TMP := /tmp
EXT0_PROJECT := $(EXT0_TMP)/ext0fs
SRC := ./src
LOOP_DEV := /dev/loop0
MOUNT_POINT := testdir

obj-m += ext0.o
ext0-objs := $(SRC)/dir.o $(SRC)/file.o $(SRC)/inode.o $(SRC)/super.o

all: 
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

build_temp:
	@mkdir -p $(EXT0_PROJECT)
	@cp -R src $(EXT0_PROJECT)
	@cp Makefile $(EXT0_PROJECT)
	@cd $(EXT0_PROJECT) && make

install: build_temp
	@cd $(EXT0_PROJECT) && insmod ext0.ko

mkfs: install
	@cd $(EXT0_PROJECT) && $(CC) -g -Wall $(SRC)/mkfs.c -o $(SRC)/mkfs.ext0

mount:
	@mkdir -p $(MOUNT_POINT)
	@mount -o loop=$(LOOP_DEV) -t ext0 $(EXT0_TMP)/test.img $(MOUNT_POINT)

unmount:
	@umount -t ext0 $(EXT0_PROJECT)/$(MOUNT_POINT)
	@umount -t ext0 $(MOUNT_POINT)

run: mkfs
	@cd $(EXT0_PROJECT) && $(SRC)/mkfs.ext0 $(EXT0_TMP)/test.img && make mount

uninstall:
	@rmmod ext0

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	@rm -f  $(SRC)/*.ext0 $(SRC)/*.rc $(SRC)/*.o
	@rm -rf $(MOUNT_POINT) $(EXT0_TMP)/ext0fs