CC = gcc
STRIP = strip

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
