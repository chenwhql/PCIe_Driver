#Comment/uncomment the following line to disable/enable debugging
#DEBUG = y

# Add your debugging flag (or not) to EXTRA_CFLAGS
# ifeq ($(DEBUG),y)
#  DEBFLAGS = -O -g -RTNIC_DEBUG # "-O" is needed to expand inlines
# else
#  DEBFLAGS = -O2
# endif

# EXTRA_CFLAGS += $(DEBFLAGS)
# EXTRA_CFLAGS += -I$(LDDINC)

# call from kernel build system
rtnic-objs := rtnic_main.o rtnic_util.o
obj-m += rtnic.o  

#generate the path  
CURRENT_PATH:=$(shell pwd)  
#the current kernel version number  
LINUX_KERNEL:=$(shell uname -r)  
#the absolute path  
LINUX_KERNEL_PATH:=/usr/src/linux-headers-4.15.0-22-generic
#complie object  
all:  
	$(MAKE) -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) modules
#clean  
clean:  
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module.symvers modules.order *.ur-safe
