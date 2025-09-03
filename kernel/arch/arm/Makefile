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
		nvic.o

LO_DEPS=	$(BSP)/$(BSP).lo

include $(TOPDIR)/misc/mk/objdir.mk

all: $(LO_TARGET)

$(LO_TARGET): $(OBJ_DIR)/$(BSP)/$(BSP).lo

$(OBJ_DIR)/$(BSP)/$(BSP).lo::
	$(CD) $(BSP) && $(MAKE)
.PHONY: clean distclean depend all

clean: common-clean
ifdef BSP
	$(CD) $(BSP) && $(MAKE) clean
endif

distclean: clean
	$(RM) .depend.mk
ifdef BSP
	$(CD) $(BSP) && $(MAKE) distclean
endif

depend:
	$(if $(LO_OBJS), $(CC) $(CFLAGS) -MM $(wildcard *.c) $(wildcard *.S) > .depend.mk,)
ifdef BSP
	$(CD) $(BSP) && $(MAKE) depend
endif

include $(TOPDIR)/misc/mk/rules-common.mk
include $(TOPDIR)/misc/mk/rules-kernel.mk
