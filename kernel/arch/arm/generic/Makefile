include $(POK_PATH)/misc/mk/config.mk
-include $(POK_PATH)/misc/mk/common-$(ARCH).mk

LO_TARGET = generic.lo
LO_DEPS = bsp.o cons.o

.PHONY: clean distclean depend all

all: $(LO_TARGET)

clean: common-clean

distclean: clean

SOURCES_TO_DEP := $(wildcard *.c) $(wildcard *.S)
depend:
ifneq ($(strip $(SOURCES_TO_DEP)),)
	$(CC) $(CFLAGS) -MM $(SOURCES_TO_DEP) > .depend.mk
else
	touch .depend.mk
endif

include $(POK_PATH)/misc/mk/rules-common.mk

-include .depend.mk
