TOPDIR=		../../../../

include $(TOPDIR)/misc/mk/config.mk
-include $(TOPDIR)/misc/mk/common-$(ARCH).mk

LO_TARGET=	stm32f4.lo

LO_OBJS=	bsp.o \
		cons.o \
		timer.o \
		startup.o

LO_DEPS=

include $(TOPDIR)/misc/mk/objdir.mk
include $(TOPDIR)/misc/mk/rules-common.mk
include $(TOPDIR)/misc/mk/rules-kernel.mk