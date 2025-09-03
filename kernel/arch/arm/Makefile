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

LO_DEPS=

include $(TOPDIR)/misc/mk/objdir.mk
include $(TOPDIR)/misc/mk/rules-common.mk
include $(TOPDIR)/misc/mk/rules-kernel.mk