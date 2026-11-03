# Builds the LED plugins (yellow.so, blue.so, both.so), the libdl.so link-time stub,
# and leds.elf (needs only libdl + libpi; it dlopens the plugins at runtime).
# Copy libdl.so, yellow.so, blue.so, and leds.elf to the SD card. Add both.so later.

# Binaries
CC = arm-none-eabi-gcc
LD = arm-none-eabi-ld
OBJDUMP = arm-none-eabi-objdump
READELF = arm-none-eabi-readelf

# Directories
OBJDIR = ./objs
LIBPI = ../0-my-libpi
LOADER = ../4-on-demand

# Flags
CFLAGS = -D__RPI__ -Og -Wall -nostdlib -nostartfiles -ffreestanding \
         -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -std=gnu99 \
         -ggdb -Wno-pointer-sign -Werror -Wno-unused-function -Wno-unused-variable \
         -mno-unaligned-access -mtp=soft
ASFLAGS = -nostdlib -nostartfiles -ffreestanding \
		  -Wa,--warn -Wa,--fatal-warnings -Wa,-mcpu=arm1176jzf-s -Wa,-march=armv6zk
# $(LOADER) last: only for dlfcn.h
INCLUDES = -I. \
           -I$(LIBPI)/ \
           -I$(LIBPI)/src \
           -I$(LIBPI)/libc \
           -I$(LIBPI)/include \
           -I$(LOADER)
LIB_LDFLAGS = -shared --strip-debug -T $(LIBPI)/memmap -L. -L$(LIBPI)
EXE_LDFLAGS = --strip-debug -T memmap -L. -L$(LIBPI)

PLUGINS = yellow.so blue.so both.so

all: libdl.so $(PLUGINS) leds.elf

libdl.so: $(OBJDIR)/dl-stub.o
	$(LD) $(LIB_LDFLAGS) $< -o $@
	$(READELF) -a $@ > $@.readelf

# Each plugin only needs libpi
%.so: $(OBJDIR)/%.o
	$(LD) $(LIB_LDFLAGS) $< -lpi -o $@
	$(READELF) -a $@ > $@.readelf
	$(OBJDUMP) -D $@ > $@.list

leds.elf: $(OBJDIR)/leds.o $(OBJDIR)/start.o libdl.so
	$(LD) $(EXE_LDFLAGS) $(OBJDIR)/leds.o $(OBJDIR)/start.o -ldl -lpi -o $@
	$(READELF) -a $@ > $@.readelf
	$(OBJDUMP) -D $@ > $@.list

# Library objects must be position-independent
$(OBJDIR)/dl-stub.o $(OBJDIR)/yellow.o $(OBJDIR)/blue.o $(OBJDIR)/both.o: $(OBJDIR)/%.o: %.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -fPIC $(INCLUDES) -c $< -o $@

$(OBJDIR)/leds.o: leds.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/start.o: start.S
	@mkdir -p $(OBJDIR)
	$(CC) $(ASFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJDIR) *.so *.elf *.list *.readelf
