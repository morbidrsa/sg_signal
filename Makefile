CC=gcc
CFLAGS=-Wall -Wextra -pedantic -O2
LDFLAGS=-pthread
TARGET=sg_signal
SOURCE=$(TARGET).c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
