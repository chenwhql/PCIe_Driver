#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

int main (int argc, char* argv[])
{
	if (argc < 3)
	{
		printf ("Usage:\t%s [cmd] [loop]", argv[0]);
		return 0;
	}
	int fd;

	fd = open("/dev/dev_driver",O_RDWR);
	if(fd < 0)
	{
		printf("open error.\n");
		return -1;
	}

	unsigned long cmd = strtoul(argv[1], NULL, 0);
	unsigned long loop = strtoul(argv[2], NULL, 0);


	long res = ioctl(fd, cmd, loop);
	
	printf("cmd = %d, len = %d\n", cmd, loop);
	printf("res = %ld\n", res);
	close(fd);
	return 0;
}
