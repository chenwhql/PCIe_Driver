#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/time.h>

unsigned long read_once(int fd, int address) {
	unsigned long read_result = address;
	int re = read(fd,&read_result,sizeof(read_result));
	if (re == -1) {
		return -1;
	}
	return read_result;
}

int main(int argc, char **argv) {
	int fd;
	int flag;
	int count, interval;
	unsigned long address;
	unsigned long tmp;
	unsigned long result;
	const int max_offset = 0x8000000;
	float interv;
	fd = open("/dev/dev_driver",O_RDWR);
	if(fd < 0){
		perror("Fail ot open device");
		return -1;
	}
	
	if (argc < 2) {
		printf("Usage: \t %s [read_address]", argv[0]);
		return 0;
	 }
	 
	sscanf(argv[1], "%x", &address);	 
 
	if (address >= 0 && address < max_offset) {
		result = read_once(fd, address);
		printf("read address 0x%0x = 0x%0x\n", address, result);
	}
	
	return result;
}
