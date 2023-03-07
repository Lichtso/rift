CFLAGS = -O2 -I include -Werror -Wall -Wextra -Wpedantic -Wstrict-aliasing=2 -Wconversion -Wdouble-promotion -Wformat-security -Wimplicit-fallthrough -Winline
GUEST_CFLAGS = $(CFLAGS) -g -ffreestanding -fvisibility=hidden

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LDLIBS = -framework hypervisor
	SHARED_OBJECT = dylib
	PAYLOAD_LAYOUT = -u _interrupt_table -bundle -Wl,-dead_strip_dylibs -Wl,-merge_zero_fill_sections -Wl,-pagezero_size,0 -Wl,-segaddr,__TEXT,200000  -Wl,-segaddr,__RODATA,400000 -Wl,-segaddr,__DATA,600000 -Wl,-rename_section,__TEXT,__const,__RODATA,__const
else
	SHARED_OBJECT = so
	PAYLOAD_LAYOUT = -u interrupt_table -nostdlib -static -Wl,-T,example/payload.ld
endif

HEADERS := $(wildcard src/host/*.h include/arch/*.h include/*.h)
HOST_OBJECTS := $(patsubst src/host/%.c, build/host/%.o, $(wildcard src/host/*.c))
GUEST_OBJECTS := $(patsubst src/guest/%.c, build/guest/%.o, $(wildcard src/guest/*.c))
XML_FILES := $(patsubst src/arch/%.xml, build/host/%_xml.h, $(wildcard src/arch/*.xml))

.PHONY: all
all: build/ build/host/librift.a build/host/librift.$(SHARED_OBJECT) build/host/example

build/host/example: example/main.c build/host/librift.a build/guest/payload
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) -o $@ $< build/host/librift.a
ifeq ($(UNAME_S),Darwin)
	codesign --sign - --force --entitlements example/hvf.entitlements $@
endif

build/guest/librift.a: $(GUEST_OBJECTS)
	$(AR) rcs $@ $^

build/host/librift.a: $(HOST_OBJECTS) build/guest/shared.o
	$(AR) rcs $@ $^

build/host/librift.$(SHARED_OBJECT): $(HOST_OBJECTS) build/guest/shared.o
	$(CC) -shared $(LDFLAGS) $(LDLIBS) -o $@ $^

build/host/%_xml.h: src/arch/%.xml
	xxd -i < $< > $@

build/host/%.o: src/host/%.c $(HEADERS) $(XML_FILES)
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

build/guest/%.o: src/guest/%.c $(HEADERS)
	$(CC) $(GUEST_CFLAGS) -c -o $@ $<

build/guest/payload: example/payload.c example/benchmark.h build/guest/librift.a
	$(CC) $(GUEST_CFLAGS) $(PAYLOAD_LAYOUT) -nostartfiles -o $@ $< build/guest/librift.a

build/:
	mkdir build
	mkdir build/host
	mkdir build/guest
