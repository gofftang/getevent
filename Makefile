PROJECT_DIR = $(shell pwd)
COMPILER_PATH = $(PROJECT_DIR)/../../prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin
#CROSS_COMPILE = $(COMPILER_PATH)/aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

TARGET = getevent

OBJS = getevent.o

all : $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS)
	$(STRIP) -x $(TARGET)

$(OBJS) : %.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) *.o
