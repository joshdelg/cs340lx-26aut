# Builds libbar.so, libfoo.so (needs libbar + libpi), and multi.elf (needs libfoo + libpi).

# Binaries
CC = arm-none-eabi-gcc
LD = arm-none-eabi-ld
OBJDUMP = arm-none-eabi-objdump
READELF = arm-none-eabi-readelf

# Directories
OBJDIR = ./objs
LIBPI = ../0-my-libpi

# Flags
CFLAGS = -D__RPI__ -Og -Wall -nostdlib -nostartfiles -ffreestanding \
         -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -std=gnu99 \
         -ggdb -Wno-pointer-sign -Werror -Wno-unused-function -Wno-unused-variable \
         -mno-unaligned-access -mtp=soft
ASFLAGS = -nostdlib -nostartfiles -ffreestanding \
		  -Wa,--warn -Wa,--fatal-warnings -Wa,-mcpu=arm1176jzf-s -Wa,-march=armv6zk
INCLUDES = -I. \
           -I$(LIBPI)/ \
           -I$(LIBPI)/src \
           -I$(LIBPI)/libc \
           -I$(LIBPI)/include
# -rpath-link lets ld find libbar.so when checking libfoo.so's DT_NEEDED
LIB_LDFLAGS = -shared --strip-debug -T $(LIBPI)/memmap -L. -L$(LIBPI) -rpath-link .
EXE_LDFLAGS = --strip-debug -T memmap -L. -L$(LIBPI) -rpath-link .

all: libbar.so libfoo.so multi.elf

libbar.so: $(OBJDIR)/bar.o
	$(LD) $(LIB_LDFLAGS) $< -lpi -o $@
	$(READELF) -a $@ > $@.readelf
	$(OBJDUMP) -D $@ > $@.list

libfoo.so: $(OBJDIR)/foo.o libbar.so
	$(LD) $(LIB_LDFLAGS) $< -lbar -lpi -o $@
	$(READELF) -a $@ > $@.readelf
	$(OBJDUMP) -D $@ > $@.list

multi.elf: $(OBJDIR)/multi.o $(OBJDIR)/start.o libfoo.so
	$(LD) $(EXE_LDFLAGS) $(OBJDIR)/multi.o $(OBJDIR)/start.o -lfoo -lpi -o $@
	$(READELF) -a $@ > $@.readelf
	$(OBJDUMP) -D $@ > $@.list

# Library objects must be position-independent
$(OBJDIR)/bar.o $(OBJDIR)/foo.o: $(OBJDIR)/%.o: %.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -fPIC $(INCLUDES) -c $< -o $@

$(OBJDIR)/multi.o: multi.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/start.o: start.S
	@mkdir -p $(OBJDIR)
	$(CC) $(ASFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJDIR) *.so *.elf *.list *.readelf
