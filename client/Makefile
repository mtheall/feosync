CFLAGS  := -g -Wall -iquote ../include
LDFLAGS := $(CFLAGS) -lcrypto -lz

CFILES := $(wildcard *.c)
OFILES := $(CFILES:.c=.o)

ifeq ($(findstring MINGW,$(shell uname -s)),)
TARGET := feosync
else
TARGET  := feosync.exe
LDFLAGS += -lws2_32
endif

all: $(TARGET)

$(TARGET): $(OFILES)
	gcc -o $@ $^ $(LDFLAGS)

$(OFILES): %.o : %.c
	gcc -o $@ -c $< $(CFLAGS)

install: all

clean:
	@rm -f $(TARGET) $(OFILES)
