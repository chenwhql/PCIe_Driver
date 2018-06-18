CC = $(CROSS_COMPILE)gcc

all: ioctl.o pcimem.o
	$(CC) -o ioctl ioctl.o
	$(CC) -o pcimem pcimem.o
ioctl.o: ioctl.c
	$(CC) -c ioctl.c
pcimem.o: pcimem.c
	$(CC) -c pcimem.c

clean:
	rm -rf *.o ioctl pcimem

