PATH=$PATH:/usr/local/angstrom/armv7linaro/bin/
BIN_PATH = /home/ubobrov/develop/projects/intercom/rootfs/root

CROSS_COMPILE = arm-linux-gnueabihf-

CC = $(CROSS_COMPILE)gcc

CP = /usr/bin/sudo /bin/cp
DEL = /bin/rm -f

TARGET = h264enc

SRC = main.c \
	  h264enc.c \
	  video_device.c \
	  ve.c \
	  csc.c


CFLAGS = -Wall -O3 -I .

LIBDIR=
LDFLAGS = -lpthread -lrt
LIBS =

MAKEFLAGS += -rR --no-print-directory

DEP_CFLAGS = -MD -MP -MQ $@
DEFS = -DCPU_HAS_NEON

OBJ = $(addsuffix .o,$(basename $(SRC)))
DEP = $(addsuffix .d,$(basename $(SRC)))

.PHONY: clean all

all: $(TARGET)
$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@
	-$(CP) $(TARGET) $(BIN_PATH)

clean:
	$(DEL) $(OBJ)
	$(DEL) $(DEP)
	$(DEL) $(TARGET)

%.o: %.c
	$(CC) $(DEP_CFLAGS) $(DEFS) $(CFLAGS) -c $< -o $@

%.o: %.S $(MKFILE) $(LDSCRIPT)
	$(CC) -c $< -o $@	

include $(wildcard $(DEP))
