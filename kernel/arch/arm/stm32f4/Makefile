ifeq ($(TOPDIR),)
TOPDIR=		../../../../
endif

include /opt/m/crazy/pok/misc/mk/config.mk
-include /opt/m/crazy/pok/misc/mk/common-$(ARCH).mk

LO_TARGET=	stm32f4.lo

LO_OBJS=	bsp.o \
		cons.o \
		timer.o \
		startup.o \
		pm.o

LO_DEPS=

include /opt/m/crazy/pok/misc/mk/objdir.mk

.PHONY: test

# Optional test target for checkmake compliance
test: $(LO_TARGET)
	@echo "STM32F4 BSP build test passed"

include /opt/m/crazy/pok/misc/mk/rules-common.mk
include /opt/m/crazy/pok/misc/mk/rules-kernel.mk
