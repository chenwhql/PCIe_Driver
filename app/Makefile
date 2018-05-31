CC = $(CROSS_COMPILE)gcc

all: ioctl.o pcimem_read.o pcimem_write.o
	$(CC) -o ioctl ioctl.o
	$(CC) -o pcimem_read pcimem_read.o
	$(CC) -o pcimem_write pcimem_write.o
ioctl.o: ioctl.c
	$(CC) -c ioctl.c
pcimem_read.o: pcimem_read.c
	$(CC) -c pcimem_read.c
pcimem_write.o: pcimem_write.c
	$(CC) -c pcimem_write.c

clean:
	rm -rf *.o ioctl pcimem_read pcimem_write

