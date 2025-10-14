-include .depend.mk

LDOPTS=-T $(POK_PATH)/misc/ldscripts/$(ARCH)/$(BSP)/kernel.lds -o $@ $(TARGET_KERNEL)

assemble-partitions:
	$(ECHO) $(ECHO_FLAGS) $(ECHO_FLAGS_ONELINE) "[BIN] partitions.bin"
# padding to get aligned file size (needed for SPARC)
	for v in $(PARTITIONS); do \
		SIZE=`ls -l $$v | awk '{print $$5}'`; \
		PADDING=`echo "$$SIZE % 4" | bc`; \
		if [ $$PADDING -ne 0 ]; then \
			BYTES=`echo "4 - $$PADDING" | bc`; \
			dd if=/dev/zero bs=1 count=$$BYTES >> $$v 2> /dev/null; \
		fi; \
	done
	cat $(PARTITIONS) > partitions.bin
	if test $$? -eq 0; then $(ECHO) $(ECHO_FLAGS) $(ECHO_GREEN) " OK "; else $(ECHO) $(ECHO_FLAGS) $(ECHO_RED) " KO"; fi


$(TARGET): assemble-partitions
#	$(RM) -f cpio.c
#	$(TOUCH) cpio.c
#	$(CC) $(CONFIG_CFLAGS) -c cpio.c -o cpio.o
#	$(RM) -f cpio.c
#	$(OBJCOPY) --add-section .archive=$(ARCHIVE) cpio.o
	$(RM) -f sizes.c
	$(TOUCH) sizes.c
	$(ECHO) "#include <stdint.h>" >> sizes.c
	$(ECHO) "#include <types.h>" >> sizes.c
	grep pok_ports_names kernel/deployment.c | \
	cut -d'{' -f2 | tr -d '};'               | \
	awk '{n=split($$0,a,","); m=0; for (i in a) { l = length(a[i])-2; if (m<l) m=l;} print "uint32_t pok_ports_names_max_len = "m";" }' >> sizes.c
	$(ECHO) "uint32_t part_sizes[] = {" >> sizes.c
	N=1 ; for v in $(PARTITIONS); do \
		if test $$N -eq 0; then $(ECHO) "," >> sizes.c ; fi ; N=0 ;\
		ls -l $$v|awk '{print $$5}' >> sizes.c ; \
	done
	$(ECHO) "};" >> sizes.c
	$(CC) $(CONFIG_CFLAGS) -I $(POK_PATH)/kernel/include -c sizes.c -o sizes.o
	$(OBJCOPY) -I binary -O elf32-littlearm -B arm --rename-section .data=.archive2 partitions.bin partitions.o
	$(OBJCOPY) --set-section-flags .archive2=alloc,load,readonly,data partitions.o
	$(OBJCOPY) --redefine-sym _binary_partitions_bin_start=__archive2_begin --redefine-sym _binary_partitions_bin_end=__archive2_end partitions.o
	$(ECHO) $(ECHO_FLAGS) $(ECHO_FLAGS_ONELINE) "[LD] $@"
	$(LD) $(LDFLAGS) $(LDFLAGS_GC) -T $(POK_PATH)/misc/ldscripts/$(ARCH)/$(BSP)/kernel.lds -o $@ $(KERNEL) $(OBJS) sizes.o partitions.o `$(CC) $(CFLAGS) -print-libgcc-file-name` -Wl,-Map,$@.map
	if test $$? -eq 0; then $(ECHO) $(ECHO_FLAGS) $(ECHO_GREEN) " OK "; else $(ECHO) $(ECHO_FLAGS) $(ECHO_RED) " KO"; fi

plop: assemble-partitions
	$(RM) -f sizes.c
	$(TOUCH) sizes.c
	$(ECHO) "#include <stdint.h>" >> sizes.c
	$(ECHO) "#include <types.h>" >> sizes.c
	grep pok_ports_names kernel/deployment.c | \
	cut -d'{' -f2 | tr -d '};'               | \
	awk '{n=split($$0,a,","); m=0; for (i in a) { l = length(a[i])-2; if (m<l) m=l;} print "uint32_t pok_ports_names_max_len = "m";" }' >> sizes.c
	$(ECHO) "uint32_t part_sizes[] = {" >> sizes.c
	N=1 ; for v in $(PARTITIONS); do \
		if test $$N -eq 0; then $(ECHO) "," >> sizes.c ; fi ; N=0 ;\
		ls -l $$v|awk '{print $$5}' >> sizes.c ; \
	done
	$(ECHO) "};" >> sizes.c
	$(CC) $(CONFIG_CFLAGS) -I $(POK_PATH)/kernel/include -c sizes.c -o sizes.o
	$(OBJCOPY) --add-section .archive2=partitions.bin sizes.o
	$(ECHO) $(ECHO_FLAGS) $(ECHO_FLAGS_ONELINE) "[LD] $@"
	$(LD) $(LDFLAGS) $(LDFLAGS_GC) -T $(POK_PATH)/misc/ldscripts/$(ARCH)/$(BSP)/kernel.lds -o pok.elf $(KERNEL) $(OBJS) sizes.o `$(CC) $(CFLAGS) -print-libgcc-file-name` -Wl,-Map,$@.map
	if test $$? -eq 0; then $(ECHO) $(ECHO_FLAGS) $(ECHO_GREEN) " OK "; else $(ECHO) $(ECHO_FLAGS) $(ECHO_RED) " KO"; fi
