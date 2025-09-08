TOPDIR=		../../

include $(TOPDIR)/misc/mk/config.mk
-include $(TOPDIR)/misc/mk/common-$(ARCH).mk

ifdef ARCH
SUBDIRS:=$(ARCH)
LO_DEPS=	$(OBJ_DIR)/$(ARCH)/$(ARCH).lo
else
SUBDIRS:=arm ppc sparc x86
LO_DEPS=
endif

LO_TARGET=	arch.lo

LO_OBJS=

include $(TOPDIR)/misc/mk/objdir.mk

all: $(LO_TARGET)

$(OBJ_DIR)/$(ARCH)/$(ARCH).lo::
	$(CD) $(ARCH) && $(MAKE)

.PHONY: clean distclean depend all

clean: common-clean
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir clean; done

distclean: clean
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir distclean; done

depend:
ifdef ARCH
	test -d $(ARCH) && $(CD) $(ARCH) && $(MAKE) depend || true
endif

include $(TOPDIR)/misc/mk/rules-common.mk
include $(TOPDIR)/misc/mk/rules-kernel.mk
