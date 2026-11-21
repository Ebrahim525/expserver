CC = gcc

ifeq ($(m),d)
	CFLAGS = -g -Wall
	BUILD_DIR = ./build/debug
	TARGET = xps-debug
else
	CFLAGS = -O2
	BUILD_DIR = ./build
	TARGET = xps
endif

SUBDIR = lib/vec lib/parson network utils core disk http config

LIBS = -lz

HEADERS = $(wildcard *.h $(foreach fd, $(SUBDIR), $(fd)/*.h))
SRCS = $(wildcard main.c $(foreach fd, $(SUBDIR), $(fd)/*.c))
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(BUILD_DIR)/%.o: %.c $(HEADERS)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -rf build xps xps-debug
