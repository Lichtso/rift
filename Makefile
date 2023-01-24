CFLAGS = -I include -fPIC -O2 -Werror -Wall -Wextra -Wpedantic -Wstrict-aliasing=2 -Wconversion -Wdouble-promotion -Wformat-security -Wimplicit-fallthrough -Winline

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LDLIBS = -framework hypervisor
	SHARED_OBJECT = dylib
else
	SHARED_OBJECT = so
endif

.PHONY: all
all: build/librift.a build/librift.$(SHARED_OBJECT) build/example

build/example: example/main.c build/librift.a
	$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) -o $@ $^
ifeq ($(UNAME_S),Darwin)
	codesign --sign - --force --entitlements hvf.entitlements $@
endif

SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst src/%.c, build/%.o, $(SOURCES))

build/librift.a: $(OBJECTS)
	$(AR) rcs $@ $^

build/librift.$(SHARED_OBJECT): $(OBJECTS)
	$(CC) -shared $(LDFLAGS) $(LDLIBS) -o $@ $<

build/%.o: src/%.c build/
	$(CC) $(CFLAGS) -c -o $@ $<

build/:
	mkdir build