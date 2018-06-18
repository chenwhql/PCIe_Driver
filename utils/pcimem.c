#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

void clean_stdin(void) {
    int c;
    do {
        c = getchar();
    } while (c != '\n' && c != EOF);
}

unsigned long read_once(int fd, int address) {
	unsigned long read_result = address;
	int re = read(fd,&read_result,sizeof(read_result));
	if (re == -1) {
		return -1;
	}
	return read_result;
}

void read_loop(int fd, int address, int count, int interval) {
	int value[10000] = {0};
	int i;
	for (i = 0; i < count; ++i) {
		unsigned long result = read_once(fd, address);
		if (result < 10000) {
			value[result]++;
		}
		sleep(interval);
	}
	printf("Address 0x%0x = \n", address);
	for (i = 0; i < 10000; ++i){
		if (value[i] > 0) {
			printf("0x%0x = %d\n", i, value[i]);
		}
	}
}

void write_once(int fd, int address, int writeval) {
	unsigned long offset[2];
	offset[0] = writeval;
	offset[1] = address;
	write(fd,offset,sizeof(offset));
}

void write_loop(int fd, int address, int writeval, int count, int interval) {
	int i;
	unsigned long offset[2];
	offset[0] = writeval;
	offset[1] = address;
	for (i = 0; i < count; ++i) {
		write(fd, offset, sizeof(offset));
		sleep(interval);
	}
}

void read_after_write_once(int fd, int address, int writeval) {
	write_once(fd, address, writeval);
	int read_result = read_once(fd, address);
	if (read_result == writeval) {
		printf("Read and write data equal.\n");
	} else {
		printf("Read and write data not equal: written 0x%0x, readback 0x%0x\n", writeval, read_result);
	}
}

void read_after_write_loop(int fd, int address, int writeval, int count, int interval) {
	int success = 0, fail = 0, i;
	for (i = 0; i < count; ++i) {
		write_once(fd, address, writeval);
		int read_result = read_once(fd, address);
		if (read_result == writeval) {
			success++;
		} else {
			fail++;
		}
		sleep(interval);
	}
	printf("Read and write data equal count: %d\n", success);
	printf("Read and write data not equal count: %d\n", fail);
}

int main(int argc, char **argv) {
	int fd;
	int flag;
	int address, count, interval;
	int writeval;
	const int max_offset = 0x8000000;
	float interv;
	fd = open("/dev/rtnic",O_RDWR);
	if(fd < 0){
		perror("Fail ot open device");
		return -1;
	}
	while ( 1 ) {
		printf( "-----------------------------\n"
				"1) read once.\n"
				"2) read loop.\n"
				"3) write once.\n"
				"4) write loop.\n"
				"5) read after write once.\n"
				"6) read after write loop.\n"
				"7) exit.\n"
				"-----------------------------\n"
				"Please enter a number (1-7): "
		);
choice:
		scanf("%d", &flag);
		clean_stdin();
		switch(flag) {
			case 1:
case1:
				printf("Input address: ");
				scanf("%x", &address);
				clean_stdin();
				printf("address = 0x%0x\n", address);
				if (address >= 0 && address < max_offset) {
					unsigned long result = read_once(fd, address);
					printf("Address 0x%0x = 0x%lx\n", address, result);
				} else {
					printf("Invalid input\n");
					goto case1;
				}
				break;
			case 2:
case2:
				printf("Input address, count, interval: ");
				scanf("%x %d %d", &address, &count, &interval);
				clean_stdin();
				printf("address = 0x%0x, count = %d, interv = %f\n", address, count, interv);
				if (address >= 0 && address < max_offset && count > 0 && interval >= 0) {
					interv = (float)interval / 10e6;
					read_loop(fd, address, count, interv);
				} else {
					printf("Invalid input\n");
					goto case2;
				}
				break;
			case 3:
case3:
				printf("Input address and data: ");
				scanf("%x %x", &address, &writeval);
				clean_stdin();
				printf("address = 0x%0x, writeval = 0x%0x\n", address, writeval);
				if (address >= 0 && address < max_offset && writeval >= 0) {
					write_once(fd, address, writeval);
				} else {
					printf("Invalid input\n");
					goto case3;
				}
				break;
			case 4:
case4:
				printf("Input address, data, count and interval: ");
				scanf("%x %x %d %d", &address, &writeval, &count, &interval);
				interv = (float)interval / 10e6;
				printf("address = 0x%0x, writeval = 0x%0x, count = %d, interv = %f\n", address, writeval, count, interv);
				clean_stdin();
				if (address >= 0 && address < max_offset && writeval >= 0 && count > 0 && interval >= 0) {
					write_loop(fd, address, writeval, count, interv);
				} else {
					printf("Invalid input\n");
					goto case4;
				}
				break;
			case 5:
case5:
				printf("Input address and data: ");
				scanf("%x %x", &address, &writeval);
				printf("address = 0x%0x, writeval = 0x%0x,\n", address, writeval);
				clean_stdin();
				if (address >= 0 && address < max_offset && writeval >= 0) {
					read_after_write_once(fd, address, writeval);
				} else {
					printf("Invalid input\n");
					goto case5;
				}
				break;
			case 6:
case6:
				printf("Input address, data, count and interval: ");
				scanf("%x %x %d %d", &address, &writeval, &count, &interval);
				interv = (float)interval / 10e6;
				printf("address = 0x%0x, writeval = 0x%0x, count = %d, interv = %f\n", address, writeval, count, interv);
				clean_stdin();
				if (address >= 0 && address < max_offset && writeval >= 0 && count > 0 && interval >= 0) {
					read_after_write_loop(fd, address, writeval, count, interv);
				} else {
					printf("Invalid input\n");
					goto case6;
				}
				break;
			case 7:
				exit(0);
			default:
				printf("Please enter a number (1-7): ");
				goto choice;
				break;
		}
	}
	return 0;
}
