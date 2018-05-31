	obj-m += dev_driver.o  
#generate the path  
	CURRENT_PATH:=$(shell pwd)  
#the current kernel version number  
	LINUX_KERNEL:=$(shell uname -r)  
#the absolute path  
LINUX_KERNEL_PATH:=/usr/src/linux-headers-4.15.0-20-generic
#complie object  
all:  
	$(MAKE) -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH)  modules
#clean  
clean:  
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.order *.symvers
