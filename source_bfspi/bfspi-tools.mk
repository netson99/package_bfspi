
bfspi-test: bfspi-test.c
	$(CC) $(CFLAGS) bfspi-test.c -o bfspi-test

spidev_test: spidev_test.c
	$(CC) $(CFLAGS) spidev_test.c -o spidev_test


all: bfspi-test spidev_test
