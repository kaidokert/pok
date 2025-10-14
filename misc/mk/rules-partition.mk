# Set flag to use libpok include order (libpok before kernel)
export POK_BUILD_LIBPOK := 1
# Also ensure it's passed to sub-makes
MAKEFLAGS += POK_BUILD_LIBPOK=1

ifeq ($(TOPDIR),)
CFLAGS += -I$(POK_PATH)/libpok/include -I.
else
CFLAGS += -I$(TOPDIR)/libpok/include -I.
endif

ifneq ($(LUSTRE_DIRECTORY),)
CFLAGS += -I$(LUSTRE_DIRECTORY)
endif

ifneq ($(DEPLOYMENT_HEADER),)
COPTS += -include $(DEPLOYMENT_HEADER)
endif

ifeq ($(TARGET_LIBPOK),) # This variable should identify the path to the copied libpok.a file. Put a dummy value if empty
TARGET_LIBPOK = $(POK_PATH)/libpok/libpok.a
endif

# This target produces libpok and a new library consisting of the contents of libpok and the partition's object files.
libpok: $(TARGET_LIBPOK)

$(TARGET_LIBPOK): $(DEPLOYMENT_HEADER)
	$(CD) $(POK_PATH)/libpok && $(MAKE) all DEPLOYMENT_HEADER=$(DEPLOYMENT_HEADER)
	$(ECHO) $(ECHO_FLAGS) $(ECHO_FLAGS_ONELINE) "[AR] libpart.a "
	$(AR) -x $(TARGET_LIBPOK)
	$(AR) -csr $(TARGET_LIBPOK) *.lo
	if test $$? -eq 0; then $(ECHO) $(ECHO_FLAGS) $(ECHO_GREEN) " OK "; else $(ECHO) $(ECHO_FLAGS) $(ECHO_RED) " KO"; fi
	rm *.lo

$(TARGET): $(OBJS) $(TARGET_LIBPOK)
	$(ECHO) $(ECHO_FLAGS) $(ECHO_FLAGS_ONELINE) "[Assemble partition $@ "
	@if [ -n "$(USER_LDFLAGS)" ] && echo "$(USER_LDFLAGS)" | grep -q "__PARTITION_BASE_ADDR"; then \
		$(LD) $(LDFLAGS) $(LDFLAGS_GC) -T $(POK_PATH)/misc/ldscripts/$(ARCH)/$(BSP)/partition.lds $+ -o $@ -L$(dir $(TARGET_LIBPOK)) -lpok -lgcc $(USER_LDFLAGS) -Wl,-Map,$@.map; \
	elif echo "$(TARGET)" | grep -q "pr1\.elf\|part1\.elf"; then \
		$(LD) $(LDFLAGS) $(LDFLAGS_GC) -T $(POK_PATH)/misc/ldscripts/$(ARCH)/$(BSP)/partition.lds $+ -o $@ -L$(dir $(TARGET_LIBPOK)) -lpok -lgcc -Wl,--defsym,__PARTITION_BASE_ADDR=0x20008000 -Wl,-Map,$@.map; \
	elif echo "$(TARGET)" | grep -q "pr2\.elf\|part2\.elf"; then \
		$(LD) $(LDFLAGS) $(LDFLAGS_GC) -T $(POK_PATH)/misc/ldscripts/$(ARCH)/$(BSP)/partition.lds $+ -o $@ -L$(dir $(TARGET_LIBPOK)) -lpok -lgcc -Wl,--defsym,__PARTITION_BASE_ADDR=0x20010000 -Wl,-Map,$@.map; \
	else \
		$(LD) $(LDFLAGS) $(LDFLAGS_GC) -T $(POK_PATH)/misc/ldscripts/$(ARCH)/$(BSP)/partition.lds $+ -o $@ -L$(dir $(TARGET_LIBPOK)) -lpok -lgcc -Wl,-Map,$@.map; \
	fi
	if test $$? -eq 0; then $(ECHO) $(ECHO_FLAGS) $(ECHO_GREEN) " OK "; else $(ECHO) $(ECHO_FLAGS) $(ECHO_RED) " KO"; fi
	@if command -v $(STRIP) >/dev/null 2>&1; then \
		$(ECHO) $(ECHO_FLAGS) $(ECHO_FLAGS_ONELINE) "[Strip $@ "; \
		cp $@ $@.debug && $(STRIP) -o $@ $@.debug && $(ECHO) $(ECHO_FLAGS) $(ECHO_GREEN) " OK " || $(ECHO) $(ECHO_FLAGS) $(ECHO_RED) " KO"; \
	fi
	@if command -v $(OBJDUMP) >/dev/null 2>&1; then \
		$(ECHO) $(ECHO_FLAGS) $(ECHO_FLAGS_ONELINE) "[Disasm $@ "; \
		$(OBJDUMP) -d $@.debug > $@.asm && $(ECHO) $(ECHO_FLAGS) $(ECHO_GREEN) " OK " || $(ECHO) $(ECHO_FLAGS) $(ECHO_RED) " KO"; \
	fi

libpok-clean:
	$(CD) $(POK_PATH)/libpok && $(MAKE) clean
	$(RM) $(TARGET_LIBPOK) $(shell pwd)/libpart.a
