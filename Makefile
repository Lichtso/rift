CFLAGS = -I include -Werror -Wall -Wextra -Wpedantic -Wstrict-aliasing=2 -Wconversion -Wdouble-promotion -Wformat-security -Wimplicit-fallthrough -Winline

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LDLIBS = -framework hypervisor
	SHARED_OBJECT = dylib
	PAYLOAD_LAYOUT = -Wl,-e,_start -Wl,-rename_section,__TEXT,__const,__RODATA,__const -Wl,-merge_zero_fill_sections -Wl,-pagezero_size,0 -Wl,-preload,-segment_order,__TEXT:__RODATA:__DATA -Wl,-segaddr,__TEXT,200000  -Wl,-segaddr,__RODATA,400000 -Wl,-segaddr,__DATA,600000
else
	SHARED_OBJECT = so
	PAYLOAD_LAYOUT = -no-pie -Wl,-T,example/payload.ld
endif

.PHONY: all
all: build/librift.a build/librift.$(SHARED_OBJECT) build/payload build/example

build/payload: example/payload.c
	$(CC) $(CFLAGS) -ffreestanding -nostartfiles -nostdlib -fvisibility=hidden $(PAYLOAD_LAYOUT) -o $@ $^

build/example: example/main.c build/librift.a
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) -o $@ $^
ifeq ($(UNAME_S),Darwin)
	codesign --sign - --force --entitlements example/hvf.entitlements $@
endif

SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst src/%.c, build/%.o, $(SOURCES))

build/librift.a: $(OBJECTS)
	$(AR) rcs $@ $^

build/librift.$(SHARED_OBJECT): $(OBJECTS)
	$(CC) -shared $(LDFLAGS) $(LDLIBS) -o $@ $^

build/%.o: src/%.c build/
	$(CC) $(CFLAGS) -fPIC -O2 -c -o $@ $<

build/:
	mkdir build
