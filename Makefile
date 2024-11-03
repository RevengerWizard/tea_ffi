CC = gcc
INCLUDE = -Ilib
# -fPIC?
CFLAGS = -Wall -O0 -g $(INCLUDE) -DTEA_BUILD_AS_DLL
LDFLAGS = -shared #-g
LIBS = -lffi -ltea00

SRC = $(wildcard src/*.c)
OBJS = $(SRC:.c=.o)

TARGET = ffi.dll
STRIP = strip --strip-unneeded

Q = @
E = @echo

all: $(TARGET)

$(TARGET): $(OBJS)
	$(E) "LINK         $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	$(E) "STRIP        $@"
	$(Q)$(STRIP) $@

src/%.o: src/%.c
	$(E) "CC           $@"
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<