CC = gcc
STRIP = strip

TARGET = getenv

OBJS = getenv.o

all : $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS)
	$(STRIP) -x $(TARGET)

$(OBJS) : %.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) *.o
