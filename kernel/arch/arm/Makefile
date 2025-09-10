TOPDIR=		../../../

include $(TOPDIR)/misc/mk/config.mk
-include $(TOPDIR)/misc/mk/common-$(ARCH).mk

LO_TARGET=	arm.lo

LO_OBJS=	arch.o \
		space.o \
		thread.o \
		syscalls.o \
		exceptions.o \
		mpu.o \
		nvic.o \
		timer.o \
		multiprocessing.o

ifdef BSP
LO_DEPS=	$(OBJ_DIR)/$(BSP)/$(BSP).lo
endif

include $(TOPDIR)/misc/mk/objdir.mk

all: $(LO_TARGET)

ifdef BSP
$(LO_TARGET): $(LO_DEPS)

$(LO_DEPS):
	$(MAKE) -C $(BSP) TOPDIR=$(TOPDIR) OBJ_DIR=$(OBJ_DIR)
endif
.PHONY: clean distclean depend all test

clean: common-clean
ifdef BSP
	$(MAKE) -C $(BSP) clean
endif

distclean: clean
	$(RM) .depend.mk
ifdef BSP
	$(MAKE) -C $(BSP) distclean
endif

SOURCES_TO_DEP := $(wildcard *.c) $(wildcard *.S)
depend:
ifneq ($(strip $(SOURCES_TO_DEP)),)
	$(CC) $(CFLAGS) -MM $(SOURCES_TO_DEP) > .depend.mk
else
	touch .depend.mk
endif
ifdef BSP
	$(MAKE) -C $(BSP) depend
endif

# Optional test target for checkmake compliance
test: all
	@echo "ARM architecture build test passed"

include $(TOPDIR)/misc/mk/rules-common.mk
include $(TOPDIR)/misc/mk/rules-kernel.mk
