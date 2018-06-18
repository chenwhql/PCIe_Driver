#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include <linux/moduleparam.h>
#include <linux/interrupt.h>
//#include <asm/io.h>
//#include <linux/ioport.h>
//#include <linux/sched.h>
//#include <linux/string.h>
//#include <linux/delay.h>
//#include <linux/time.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/seqlock.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
//#include <linux/mm.h>
//#include <linux/list.h>
//#include <linux/signal.h>
//#include <linux/random.h>

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/fcntl.h>

#include <linux/device.h>
//#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>

#include "rtnic_kernel.h"


//////////全局变量//////////////
//PCI设备
struct pci_dev *global_pdev;

/* Address in memory of board */
void *ioaddr;

//DMA缓存指针
void *tx_dma_buf, *rx_dma_buf;
//DMA缓存地址
dma_addr_t tx_addr, rx_addr;

//PCI设备信息
unsigned long bar_base;
unsigned long bar_len;
unsigned long bar_flags;

//字符设备指针
struct mycdev *global_cdev;
//device number 
static dev_t dev_num = {0};
//class
struct class *cls;

//进入中断的次数统计
int intr_enter_conut = 0;

//中断互斥性检查
int re_enter = 0;

//收发帧次数统计
long sent_packets = 0;
long received_packets_successfully = 0;
long sent_packets_successfully = 0;


////////DMA相关全局变量/////////

//DMA传输数据长度
int dma_data_length = 0;

// DMA数据次数统计
long received_dma_up_begin = 0;
long received_dma_up_end = 0;

/* 指明该驱动程序适用于哪一些 PCI 设备 */
static const struct pci_device_id rtnic_pci_tbl [] __initdata = {
	{ RTNIC_VENDER_ID, RTNIC_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{},
};
MODULE_DEVICE_TABLE(pci, rtnic_pci_tbl);


/////////////////PCIe驱动相关/////////////////////

/* 中断处理函数 */
static irqreturn_t rtnic_interrupt(int irq, void *dev_id)
{
	struct pci_dev *pdev = (struct pci_dev *)dev_id;

    //resisiter temp value
    int link_value = 0;

	//DMA upload assist value
	unsigned long *pLong = NULL;
	int loop_flag = 0;
	int loop = 0;
	int frame_cnt = 0;

    //获取中断掩码, read the interrupt status now
    int intr_mask = ioread32(ioaddr + IRQ_TYPE_REG);

	//判断是不是已经进入中断了，不允许重复进入
	if (re_enter){
		printk("ERR: --re-enter interrupt handler --\n");
	}
	re_enter = 1;
  
    PDEBUG("------ interrupt handler ------\n");       
	intr_enter_conut++;

    //////////////////中断测试程序/////////////////////
    //shield all interrupt
    iowrite32(0x00, (void *)(ioaddr + INTR_MASK_REG));
    PDEBUG("rtnic intr: intr mask = 0x%x\n", intr_mask);
    //port status change intr
    if(intr_mask & INTR_PORT_STATUS_CHG) {
    	//the status of ports changed, 
    	// so this branch will read the new status of ports
		link_value = ioread32((void *)(ioaddr + STATUS_CHG_REG));
		PDEBUG("rtnic intr: port status = 0x%x\n", link_value);
	}
    //DMA数据上传开始
	if(intr_mask & INTR_DMA_UP_STRAT){
		//dma upstream begin
		PDEBUG("rtnic intr: begin dma upstream.\n");

		//get the data length will be writed to memory 
		dma_data_length = ioread32((void *)(ioaddr + DMA_UP_DATA_LEN_REG));
		//the last 2 bit is not usefull info
		dma_data_length = dma_data_length / 8;
		
		//tell fpga the start memory addr to write
		iowrite32(rx_addr, (void *)(ioaddr + DMA_UP_ADDR_REG));
		//tell the length of memory
		iowrite32(SW_FPGA_RX_BUF_SIZE, (void *)(ioaddr + DMA_UP_MEM_SIZE_REG));
		//give the write start cmd
		iowrite32(0x01, (void *)(ioaddr + DMA_WR_CTL_REG));
		
		//debug info 
		PDEBUG("rtnic intr: tx_addr = 0x%x\n", rx_addr);
		PDEBUG("rtnic intr: dma_buf_len = 0x%x\n", SW_FPGA_RX_BUF_SIZE);
		PDEBUG("rtnic intr: virtual ioaddr = 0x%x\n", ioaddr);
		PDEBUG("rtnic intr: data length = %d\n", dma_data_length);
		PDEBUG("rtnic intr: upstream register end.\n");
		
		received_dma_up_begin++;
	}
    //DMA数据上传结束
	if(intr_mask & INTR_DMA_UP_END) {
		//dma upstream end
		PDEBUG("rtnic intr: begin upstream read.\n");
		PDEBUG("rtnic intr: upstream: length = %d\n", dma_data_length);
		PDEBUG("rtnic intr: end upstream read");
	
		pLong = (unsigned long*)rx_dma_buf;
		for(loop = 0; loop < dma_data_length * 8; loop += 4) {
			if(loop_flag == 1){
				//???
				if (*pLong == 0x01 || *pLong == 0x02 || *pLong == 0x04 || *pLong == 0x08) {
					frame_cnt++;
				}
			}
			loop_flag = (loop_flag == 0) ? 1 : 0;
			pLong++;
		}
		
		PDEBUG("rtnic intr: dma end receive frames cnt: %d\n", frame_cnt);
		
		received_packets_successfully += frame_cnt;
		received_dma_up_end++;
	}
	//DMA数据下发结束
	if(intr_mask & INTR_DMA_DOWN_END){
		//dma downstream end
		PDEBUG("rtnic intr: end downstream\n");
	}
    
    /* Rewrite the interrupt mask including any changes */
    iowrite32(0xFFFFFFFF, (void *)(ioaddr + INTR_MASK_REG));
	re_enter = 0;
	return (IRQ_HANDLED);
}

/* pci_driver 的 probe 函数 */
static int __init rtnic_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    int err;

    PDEBUG("-------------------------------");
    PDEBUG("rtnic: loading driver: %p\n", pdev);

    //启动 PCI 设备
	err = pci_enable_device(pdev);
	if (err) {
		printk("rtnic: can't enable device %p.\n", pdev);
		goto fail_pci_enable_device;
	}

	PDEBUG("**************************************\n");
    PDEBUG("**************************************\n");
    PDEBUG("rtnic: Vendor:%0x\n", pdev->vendor);
    PDEBUG("rtnic: Device:%0x\n", pdev->device);
    PDEBUG("rtnic: Subvendor:%0x\n", pdev->subsystem_vendor);
    PDEBUG("rtnic: Subdevice:%0x\n", pdev->subsystem_device);
    PDEBUG("rtnic: Class:%0x\n", pdev->class);
    PDEBUG("rtnic: Revision:%0x\n", pdev->revision);
    
    PDEBUG("rtnic: Rom_base_reg:%0x\n", pdev->rom_base_reg);
    PDEBUG("rtnic: Pin:%0x\n", pdev->pin);
    PDEBUG("rtnic: Irq:%0x\n", pdev->irq);
    PDEBUG("**************************************\n");

    //申请私有数据结构
    /*
    rtnic_priv =  kmalloc(sizeof(struct rtnic_pci_priv), GFP_KERNEL);
	if (!rtnic_priv) {
		printk("can't alloc private data %p.\n", pdev);
		err = -ENOMEM;
		goto fail_alloc_priv;
	}
    DEBUG_ALLOC("rtnic_priv", rtnic_priv, sizeof(*rtnic_priv));
    */

    //私有数据结构初始化
    global_pdev = pdev;
	//需要的变量在这里初始化
	bar_base = 0;
	bar_len = 0;
	bar_flags = 0;

    /* 读取 PCI 配置信息 */
	bar_base = pci_resource_start(pdev, BAR_0);
	bar_len = pci_resource_len(pdev, BAR_0);
	bar_flags = pci_resource_flags(pdev, BAR_0);
    PDEBUG("rtnic: BAR %d : base : 0x%x\n", BAR_0, bar_base);
    PDEBUG("rtnic: BAR %d : len : 0x%x\n", BAR_0, bar_len);
    PDEBUG("rtnic: BAR %d : flags : 0x%x\n", BAR_0, bar_flags);

    /*
	if (bar_len != BAR_SIZE_0 ||
			!(bar_flags & BAR_TYPE_0)) {
		printk("BAR %d not match\n", BAR_0);
		err = -EIO;
		goto fail_chk_bar;
	}
	*/

    // 关联数据结构
    //pci_set_drvdata(pdev, rtnic_priv);

    // 设置成总线主 DMA 模式
    PDEBUG("rtnic: set pci master\n");
    pci_set_master(pdev);
    // set mask, 函数返回成功，则可以在mask指定的地址范围内进行DMA操作
	if ((err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32)))) 
	{
		printk("rtnic: no suitable DMA configuration, aborting\n");
		goto fail_no_dma;
	}
	if ((err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)))) 
	{
		printk("rtnic: no suitable DMA configuration, aborting\n");
		goto fail_no_dma;
	}

	/* Request the memory region corresponding to the card */
	PDEBUG("rtnic: requesting memory region for NetFPGA-1G\n");
	// 标记这片内存区的拥有者是rtnic
	if (!request_mem_region(bar_base, bar_len, "rtnic")) 
	{
		printk(KERN_ERR "rtnic: cannot reserve MMIO region\n");
		goto fail_req_mem;
	}

    // 在内核中访问 I/O 内存之前，需首先使用 ioremap()函数将设备所处的物理地址映射到虚拟地址
    PDEBUG("rtnic: mapping I/O space\n");
    ioaddr = ioremap(bar_base, bar_len);
	if (!(void *)(ioaddr)) 
	{
		printk("rtnic: failed to map MMIO range\n");
		goto fail_ioremap;
	}
	PDEBUG("rtnic: map_ioaddr: 0x%x\n", ioaddr);

    //申请 I/O 资源
    //申请DMA缓存
    //TODO: this pdev maybe error
    PDEBUG("rtnic: alloc dma buffer\n");
	tx_dma_buf = dma_alloc_coherent(&pdev->dev,
			SW_FPGA_TX_BUF_SIZE/* + SW_FPGA_TX_BUF_SIZE_PAD*/,
			&tx_addr, GFP_KERNEL);
	if (!tx_dma_buf) {
		printk("rtnic: failed to get memory for TX DMA buffer\n");
		err = -ENOMEM;
		goto fail_alloc_tx_dma_buf;
	}
	rx_dma_buf = dma_alloc_coherent(&pdev->dev,
			SW_FPGA_RX_BUF_SIZE/* + SW_FPGA_RX_BUF_SIZE_PAD*/,
			&rx_addr, GFP_KERNEL);
	if (!rx_dma_buf) {
		printk("rtnic: failed to get memory for TX DMA buffer\n");
		err = -ENOMEM;
		goto fail_alloc_rx_dma_buf;
	}
	DEBUG_ALLOC("tx_dma_buf", tx_dma_buf, SW_FPGA_TX_BUF_SIZE/* + SW_FPGA_TX_BUF_SIZE_PAD*/);
	DEBUG_ALLOC("rx_dma_buf", rx_dma_buf, SW_FPGA_RX_BUF_SIZE/* + SW_FPGA_RX_BUF_SIZE_PAD*/);

    //interrupt enable 
    iowrite32(0xFFFFFFFF, (void *)(ioaddr + INTR_MASK_REG));
    //使能MSI中断
    PDEBUG("rtnic: enable msi interrupt\n");
    err = pci_enable_msi(pdev);	/* 0 or negative */
	if (err) {
		printk("rtnic: failed to enable MSI\n");
		goto fail_enable_msi;
	}
    //申请中断，注册中断处理程序
    PDEBUG("rtnic: regisiter interrupt\n");
    err = request_irq(pdev->irq, rtnic_interrupt, 
			IRQF_SHARED, MODULE_NAME, pdev); //???
	if (err) {
		printk("rtnic: failed to request irq\n");
		goto fail_req_irq;
	}

    return 0;

/* fail: */
fail_req_irq:
    pci_disable_msi(pdev);
fail_enable_msi:
    dma_free_coherent(&pdev->dev, SW_FPGA_RX_BUF_SIZE + SW_FPGA_RX_BUF_SIZE_PAD,
			rx_dma_buf, rx_addr);
fail_alloc_rx_dma_buf:
    dma_free_coherent(&pdev->dev, SW_FPGA_TX_BUF_SIZE + SW_FPGA_TX_BUF_SIZE_PAD,
			tx_dma_buf, tx_addr);
fail_alloc_tx_dma_buf:
fail_no_dma:
    pci_clear_master(pdev);
fail_req_mem:
fail_ioremap:
/*
    pci_release_region(pdev, BAR_0);
fail_chk_bar:
*/
	pci_disable_device(pdev);
fail_pci_enable_device:
	return err;
}

static void rtnic_release(struct pci_dev *pdev)
{
	PDEBUG("rtnic: unloading driver\n");

	if(bar_base != 0x00) {
		PDEBUG("rtnic: unmaping driver\n");
    	iounmap(bar_base);
    }

    PDEBUG("rtnic: releasing mem region\n");
	release_mem_region(bar_base, bar_len);

	PDEBUG("rtnic: disabling device\n");
    pci_disable_device(pdev);//禁止 PCI 设备

    PDEBUG("rtnic: finished removing\n");
}

/* 设备模块信息 */
static struct pci_driver rtnic_pci_driver = {
    name:       MODULE_NAME,    
    id_table:   rtnic_pci_tbl,   
    probe:      rtnic_probe,
    remove:     rtnic_release,
};

/////////////////字符设备相关//////////////////////

/* 字符设备 file_operations open 成员函数 */
static int dev_fifo_open(struct inode *inode, struct file *file) 
{	
	file->private_data = global_cdev;
	return 0;
}

/* 字符设备 file_operations ioctl 成员函数 */
static long dev_fifo_ioctl(struct file *file,
                       unsigned int cmd, unsigned long arg)
{
	struct mycdev *dev = file->private_data;
	int loop = 0;
	char *data_ptr = NULL;
	unsigned long link_value = 0;
	unsigned long *pLong = NULL;
	int loop_flag = 0;
	int data_lentgh = 0;

	printk("rtnic cdev: arg value: %ld\n", arg);
    switch(cmd)
    {
		case MEM_CLEAR:
            memset(dev->buffer, 0, 50);
			printk("rtnic cdev: rtnic is set to zero\n");
            break;
        case REG_WRITE_READ:  //base test, fixed addr
			printk("rtnic cdev: Write and read regisiter\n");
			iowrite32(arg, (void *)(ioaddr + 0x10));
			link_value = ioread32((void *)(ioaddr + 0x10));
			printk("rtnic cdev: Write Value = 0x%lx\n", arg);
			printk("rtnic cdev: Read Value = 0x%lx\n", link_value);
			printk("------------------------------\n");
        	break;
        case REG_ADDR_WRITE_READ: //base test, fixed val
        	printk("rtnic cdev: Write and read regisiter addr\n");
			iowrite32(0xFF, (void *)(ioaddr + arg));
			link_value = ioread32((void *)(ioaddr + arg));
			printk("rtnic cdev: Write Value = 0x%lx\n", arg);
			printk("rtnic cdev: Read Value = 0x%lx\n", link_value);
			printk("------------------------------\n");
        	break;
        /////////////DMA Test Section//////////////
        case SET_DMA_REG:
        	printk("-- set register for DMA upstream --\n");
        	iowrite32(rx_addr, (void *)(ioaddr + DMA_UP_ADDR_REG));
			iowrite32(SW_FPGA_RX_BUF_SIZE, (void *)(ioaddr + DMA_UP_MEM_SIZE_REG));
			iowrite32(0x00, (void *)(ioaddr + INTR_MASK_REG));
			iowrite32(0x01, (void *)(ioaddr + DMA_WR_CTL_REG));  //0x01: DMA write
			printk("rtnic cdev: rx_addr = 0x%llx\n", rx_addr);
			printk("rtnic cdev: rx_buf_len = 0x%x\n", SW_FPGA_RX_BUF_SIZE);
			printk("-----------------------------------\n");
			break;
		case DMA_UPSTREAM:
			printk("----------- DMA upstream ----------\n");
			printk("rtnic cdev: rx_dma_buf_addr: 0x%x\n", rx_dma_buf);
			data_ptr = rx_dma_buf;
			data_lentgh = SW_FPGA_RX_BUF_SIZE;
			for(loop = 0; loop < data_lentgh; loop += 4)
			{
				printk ("rtnic cdev: rx_dma_buf[%d] = 0x%x\n", loop, *(int*)data_ptr);
				data_ptr += 4;
			}
			printk("-----------------------------------\n");
			break;
		case DMA_DOWNSTREAM:
			printk("--------- DMA downstream -------\n");
			pLong = (unsigned long *)tx_dma_buf;
			link_value = 1;
			data_lentgh = SW_FPGA_TX_BUF_SIZE;
			//prepare the download data
			for(loop = 0; loop < data_lentgh - 16; loop+=4)
			{
				if(loop_flag == 0){
					if(loop == 0) {
						*pLong = 0; //目的MAC高4B
					} else if (loop == 8) {
						*pLong = 0;
					} else if (loop == 16) {
						*pLong = 0; //源MAC的低4B
					} else {
						*pLong = link_value;
						link_value++;
					} 
				} else if (loop == 4 || loop == 12) {
					*pLong = 0;
				} else if (loop == data_lentgh - 20) {
					*pLong = 0x01;  //帧尾？
				} else {
					*pLong = 0; //边带信息
				}
				loop_flag = (loop_flag == 0) ? 1 : 0;
				pLong++;
			}
			/*
			for(loop = 0; loop < data_lentgh; loop += 4)
			{
				*pLong = link_value;
				link_value++;
				pLong++;
			}
			*/
			iowrite32(0x00, (void *)(ioaddr + INTR_MASK_REG));
			
			iowrite32(tx_addr, (void *)(ioaddr + DMA_DOWN_ADDR_REG));
			iowrite32(data_lentgh, (void *)(ioaddr + DMA_DOWN_DATA_LEN_REG));
			iowrite32(0x02, (void *)(ioaddr + DMA_WR_CTL_REG)); //0x02: Read start
			
			iowrite32(0xFFFFFFFF, (void *)(ioaddr + INTR_MASK_REG));
			printk("rtnic cdev: tx_addr = 0x%llx\n", tx_addr);
			printk("rtnic cdev: tx_buf_len = 0x%x\n", data_lentgh);
			printk("-----------------------------------\n");
			break;
		/////////////Base Control Section//////////////
		case GET_IO_ADDR:
			printk("-------- print base address -------\n");
			printk("rtnic cdev: base virt io addr = 0x%x\n", ioaddr);
			printk("-----------------------------------\n");
			break;
		case GET_IRQ_TYPE_REG:
			//获取中断掩码, read the interrupt status now
			printk("-------- get intr reg data  -------\n");
			link_value = ioread32(ioaddr + IRQ_TYPE_REG);
			printk("rtnic intr: intr mask = 0x%x\n", link_value);
			printk("-----------------------------------\n");
			break;
		default:
			return -EINVAL;
    }
	return 0;
}

static ssize_t dev_fifo_read(struct file *file, char __user *ubuf, size_t size, loff_t *ppos) 
{
	int result;
	unsigned long read_result;
	unsigned long address;
	unsigned long offset = 0;
	struct mycdev *dev = file->private_data;
	PDEBUG("rtnic cdev: read data from device\n");
	//判断是否初始化过
	PDEBUG("rtnic cdev: bar_base: 0x%x\n", bar_base);
	if (bar_base != 0x00) {
		offset = (int)*((char *)ubuf);
		offset = (offset < 0) ? 0 : offset;
		PDEBUG("rtnic cdev: offset: %d\n", offset);
		address = ioaddr + offset;
		read_result = ioread32(address);
		result = copy_to_user(ubuf, &read_result, sizeof(long));
		if(result != 0){
			printk("rtnic cdev: dev_fifo_read failed!\n");
			return -EFAULT;
		}
	}
	return sizeof(long);
}

static ssize_t dev_fifo_write(struct file *file, const char __user *ubuf, size_t size, loff_t *ppos)
{
	int value;
	unsigned long address;
	unsigned long offset;
	struct mycdev *dev = file->private_data;
	PDEBUG("rtnic cdev: write data to device\n");
	PDEBUG("rtnic cdev: bar_base: 0x%x\n", bar_base);
	if (bar_base != 0x0) {
		value = (int)*((char *)ubuf);
		offset = *((unsigned long *)ubuf + 1);
		offset = (offset < 0) ? 0 : offset;
		PDEBUG("rtnic cdev: value: %d\n", value);
		PDEBUG("rtnic cdev: offset: %d\n", offset);
		address = ioaddr + offset;
		iowrite32(value, address);
	}
	return sizeof(long);
}

/* 字符设备 file_operations read、 write、 mmap 等成员函数 */
static const struct file_operations fifo_operations = {
	.owner = THIS_MODULE,
	.open  = dev_fifo_open,
	.read  = dev_fifo_read,
	.write = dev_fifo_write,
	.unlocked_ioctl   = dev_fifo_ioctl,
};

int __init dev_fifo_init(void)
{
	int ret;
	struct device *device;

	//注册PCI驱动
	pci_register_driver(&rtnic_pci_driver);
	
	global_cdev = kzalloc(sizeof(struct mycdev), GFP_KERNEL);
	if(!global_cdev){
		return -ENOMEM;
	}
	//generate device number by MKDEV(major dev num, minor dev num)
	dev_num = MKDEV(MAJOR_NUM, 0);
	//alloc device number
	ret = register_chrdev_region(dev_num, 1, "rtnic");
	if(ret < 0){
		ret = alloc_chrdev_region(&dev_num, 0, 1, "rtnic");
		if(ret < 0){
			printk("rtnic cdevFail to register_chrdev_region\n");
			goto err_register_chrdev_region;
		}
	}
	cls = class_create(THIS_MODULE, "rtnic");
	if(IS_ERR(cls)){
		ret = PTR_ERR(cls);
		goto err_class_create;
	}
	cdev_init(&global_cdev->cdev, &fifo_operations);
	ret = cdev_add(&global_cdev->cdev,dev_num,1);
	if (ret < 0){
		goto err_cdev_add;
	}
	device = device_create(cls,NULL,dev_num,NULL,"rtnic");
	if(IS_ERR(device)){
		ret = PTR_ERR(device);
		printk("rtnic cdevFail to device_create\n");
		goto err_device_create;	
	}
	printk("*******************************\n");
	printk("rtnic cdev: Register dev_fifo to system, ok!\n");
	return 0;

err_device_create:
	cdev_del(&global_cdev->cdev);

err_cdev_add:
	class_destroy(cls);

err_class_create:
	unregister_chrdev_region(dev_num, 1);

err_register_chrdev_region:
	return ret;

}

void __exit dev_fifo_exit(void)
{
	pci_unregister_driver(&rtnic_pci_driver);
	device_destroy(cls, dev_num);	
	class_destroy(cls);
	cdev_del(&global_cdev->cdev);
	unregister_chrdev_region(dev_num, 1);
	printk("unregister this fifo module \n");
	return;
}

module_init(dev_fifo_init);
module_exit(dev_fifo_exit);
MODULE_LICENSE("GPL");