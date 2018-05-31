#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/seqlock.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/signal.h>
#include <linux/random.h>

//新增调试信息使用这个宏
/*
#define RTNIC_DEBUG
#ifdef RTNIC_DEBUG
#  define printk(fmt, args...) printk(fmt, ## args) 
#else
#  define printk(fmt, args...)
#endif
*/

//申请内存的调试信息
#define DO_DEBUG_ALLOC
#ifdef DO_DEBUG_ALLOC
#define DEBUG_ALLOC(val, ptr, size) \
		printk(val " allocated: at %p-%p\n", ptr, ((u8 *)ptr) + size)
#else
#define DEBUG_ALLOC(...)
#endif

//设备相关宏定义
#define RTNIC_VENDER_ID (0x10ee)
#define RTNIC_DEVICE_ID (0x7021)
#define MODULE_NAME "xilinx_701"

#define MAJOR_NUM  250  //设备数
#define MEM_CLEAR 0x00 /*清零全局内存*/

#define BAR_0 (0)			/* MMIO, for 8-byte access */
#define BAR_SIZE_0               (0x08000000)
#define BAR_TYPE_0               (IORESOURCE_MEM)

#define SW_FPGA_RX_BUF_SIZE 131072
#define SW_FPGA_RX_BUF_SIZE_PAD 4096
#define SW_FPGA_TX_BUF_SIZE 8192
#define SW_FPGA_TX_BUF_SIZE_PAD 4096

#define PCI_REG_INTERRUPT_MASK  (0x4000000)

//interrupt type
#define REG_WRITE_READ (0x01)
#define DMA_UPSTREAM_BEGIN (0x02)
#define DMA_UPSTREAM_END (0x04)
#define DMA_DOWNSTREAM_END (0x08)


//用于测试字符设备
struct mycdev 
{
	struct cdev cdev;

	unsigned char buffer[50];

	int len;  //is used?
};

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

//是否成功申请内存
bool req_mem_success; 

//中断掩码
u32 intr_enabled_mask;

//字符设备指针
struct mycdev *global_cdev;
//device number 
static dev_t dev_num = {0};
//
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


//////////////////辅助工具函数//////////////////////
int read_msi_data_config(struct pci_dev *pdev)
{
	int pos;
	unsigned short msgctl;
	unsigned long result;
	unsigned long address;

	pos = pci_find_capability(pdev, PCI_CAP_ID_MSI);

	pci_read_config_word(pdev, pos + PCI_MSI_FLAGS, &msgctl);
	printk("PCI_MSI_FLAGS:0x%0x\n", msgctl);

	pci_read_config_word(pdev, pos + PCI_MSI_ADDRESS_LO, &msgctl);
	printk("PCI_MSI_ADDRESS_LO:0x%0x\n", msgctl);

	address = ioaddr + msgctl;
	result = ioread32(address);
	printk("Read:0x%0x = 0x%0x\n", msgctl, result);

	pci_read_config_word(pdev, pos + PCI_MSI_DATA_32, &msgctl);
	printk("PCI_MSI_DATA_32:0x%0x\n", msgctl);

	return 0;
}

/////////////////PCIe驱动相关/////////////////////

/* 中断处理函数 */
static irqreturn_t rtnic_interrupt(int irq, void *dev_id)
{
	struct pci_dev *pdev = (struct pci_dev *)dev_id;

    //resisiter temp value
    int link_value = 0;
    //interrupt mask
	int interrupt_value = 0xFFFFFFFF;

	//DMA upload assist value
	unsigned long *pLong = NULL;
	int loop_flag = 0;
	int loop = 0;
	int frame_cnt = 0;

    //获取中断掩码
    int intr_mask = ioread32(ioaddr + PCI_REG_INTERRUPT_MASK);

	//判断是不是已经进入中断了，不允许重复进入
	if (re_enter){
		printk("ERR: --re-enter interrupt handler --\n");
	}
	re_enter = 1;

    printk("--interrupt handler--\n");   
	intr_enter_conut++;

    //////////////////中断测试程序/////////////////////
    //写寄存器
    iowrite32(0x00, (void *)(ioaddr + 0x4000004));
    printk("intr mask = 0x%x\n", intr_mask);
    //读寄存器
    if(intr_mask & REG_WRITE_READ) {
		link_value = ioread32((void *)(ioaddr + 0x4000008));
		printk("link_value = 0x%x\n", link_value);
	}
    //DMA数据上传开始
	if(intr_mask & DMA_UPSTREAM_BEGIN){
		//dma upstream begin
		printk("begin dma upstream.\n");

		dma_data_length = ioread32((void *)(ioaddr + 0x4000014));
		dma_data_length = dma_data_length / 8;
		
		iowrite32(tx_addr, (void *)(ioaddr + 0x400000c));
		iowrite32(SW_FPGA_RX_BUF_SIZE, (void *)(ioaddr + 0x4000010));
		iowrite32(0x01, (void *)(ioaddr + 0x4000018));
		
		printk("tx_addr = 0x%x\n", tx_addr);
		printk("dma_buf_len = 0x%x\n", SW_FPGA_RX_BUF_SIZE);
		printk("ioaddr = 0x%x\n", ioaddr);
		printk("length = %d\n", dma_data_length);
		printk("upstream register end.\n");
		
		received_dma_up_begin++;
	}
    //DMA数据上传结束
	if(intr_mask & DMA_UPSTREAM_END) {
		//dma upstream end
	
		printk("begin upstream read.\n");
		printk("upstream: length = %d\n", dma_data_length);
		printk("end upstream read");
	
		pLong = (unsigned long*)rx_dma_buf;
		for(loop = 0; loop < dma_data_length * 8; loop += 4) {
			if(loop_flag == 1){
				//这个跟帧的各市有关？
				if (*pLong == 0x01 || *pLong == 0x02 || *pLong == 0x04 || *pLong == 0x08) {
					frame_cnt ++;
				}
			}
			loop_flag = (loop_flag == 0) ? 1 : 0;
			pLong++;
		}
		
		printk("dma end receive frames cnt: %d\n", frame_cnt);
		
		received_packets_successfully += frame_cnt;
		received_dma_up_end++;
	}
	//DMA数据下发结束
	if(intr_mask & DMA_DOWNSTREAM_END){
		//dma downstream end
		printk("end downstream\n");
		
	}
    
    /* Rewrite the interrupt mask including any changes */
    iowrite32(interrupt_value, (void *)(ioaddr + 0x4000004));
	re_enter = 0;
	return (IRQ_HANDLED);
}

/* pci_driver 的 probe 函数 */
static int __init rtnic_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    int err;

    printk("-------------------------------");
    printk("probing, pdev: %p\n", pdev);

    //启动 PCI 设备
	err = pci_enable_device(pdev);
	if (err) {
		printk("can't enable device %p.\n", pdev);
		goto fail_pci_enable_device;
	}

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
    printk("BAR %d : base : %p, len: %p, flags: %lx\n",
			BAR_0, (void *)(bar_base),
			(void *)(bar_len), bar_flags);
    /*
	if (bar_len != BAR_SIZE_0 ||
			!(bar_flags & BAR_TYPE_0)) {
		printk("BAR %d not match\n", BAR_0);
		err = -EIO;
		goto fail_chk_bar;
	}
	*/

    //关联数据结构
    //pci_set_drvdata(pdev, rtnic_priv);

    // 在内核中访问 I/O 内存之前，需首先使用 ioremap()函数将设备所处的物理地址映射到虚拟地址
    printk(KERN_INFO "rtnic: mapping I/O space\n");
    ioaddr = ioremap(bar_base, bar_len);
	req_mem_success = true;
	if (!(void *)(ioaddr)) {
		req_mem_success = false;
		printk("failed to map MMIO range\n");
		goto fail_ioremap;
	}
	printk("map_ioaddr: %d\n");

    //设置成总线主 DMA 模式
    pci_set_master(pdev);
	if ((err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32)))) {
		printk("no suitable DMA configuration, aborting\n");
		goto fail_no_dma;
	}
	if ((err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)))) {
		printk("no suitable DMA configuration, aborting\n");
		goto fail_no_dma;
	}

    //申请 I/O 资源
    //申请DMA缓存
    //TODO: this pdev maybe error
    /*
    tx_dma_buf = pci_alloc_consistent(pdev,
			SW_FPGA_TX_BUF_SIZE + SW_FPGA_TX_BUF_SIZE_PAD,  //???
			&tx_addr);
	if (!tx_dma_buf) {
		printk("failed to get memory for TX DMA buffer\n");
		err = -ENOMEM;
		goto fail_alloc_tx_dma_buf;
	}
    //申请DMA接收缓存
	rx_dma_buf = pci_alloc_consistent(pdev,
			SW_FPGA_RX_BUF_SIZE + SW_FPGA_RX_BUF_SIZE_PAD,
			&rx_addr);
	if (!rx_dma_buf) {
		printk("failed to get memory for TX DMA buffer\n");
		err = -ENOMEM;
		goto fail_alloc_rx_dma_buf;
	}
	DEBUG_ALLOC("tx_dma_buf", tx_dma_buf, SW_FPGA_TX_BUF_SIZE + SW_FPGA_TX_BUF_SIZE_PAD);
	DEBUG_ALLOC("rx_dma_buf", rx_dma_buf, SW_FPGA_RX_BUF_SIZE + SW_FPGA_RX_BUF_SIZE_PAD);
	*/

    //中断相关
    intr_enabled_mask = ~0;
    //使能MSI中断
    err = pci_enable_msi(pdev);	/* 0 or negative */
	if (err) {
		printk("failed to enable MSI\n");
		goto fail_enable_msi;
	}
    //申请中断，注册中断处理程序
    err = request_irq(pdev->irq, rtnic_interrupt, 
			IRQF_SHARED, MODULE_NAME, pdev); //???
	if (err) {
		printk("failed to request irq\n");
		goto fail_req_irq;
	}

    return 0;

/* fail: */
fail_req_irq:
    pci_disable_msi(pdev);
fail_enable_msi:
/*
    dma_free_coherent(&pdev->dev, SW_FPGA_RX_BUF_SIZE + SW_FPGA_RX_BUF_SIZE_PAD,
			rx_dma_buf, rx_addr);
fail_alloc_rx_dma_buf:
    dma_free_coherent(&pdev->dev, SW_FPGA_TX_BUF_SIZE + SW_FPGA_TX_BUF_SIZE_PAD,
			tx_dma_buf, tx_addr);
fail_alloc_tx_dma_buf:
*/
fail_no_dma:
    pci_clear_master(pdev);
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
	if(bar_base != 0x00) {
    	if(req_mem_success) {
    		iounmap(bar_base);
    	}
    	printk("pci rtnic module release\n");
    }
    pci_release_regions(pdev);//释放 I/O 资源
    pci_disable_device(pdev);//禁止 PCI 设备
    return 0;
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
	int link_value = 0;

    switch(cmd)
    {
		case MEM_CLEAR:
            memset(dev->buffer, 0, 50);
			printk(KERN_INFO "rtnic is set to zero\n");
            break;
        case REG_WRITE_READ:
			printk("-- Write and read regisiter --\n");
			iowrite32(arg, (void *)(ioaddr + 0x40000020));
			link_value = ioread32((void *)(ioaddr + 0x4000020));
			printk("Write Value = 0x%x\n", arg);
			printk("Read Value = 0x%x\n", link_value);
			printk("------------------------------\n");
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
	struct mycdev *dev = file->private_data;
	//判断是否初始化过
	if (bar_base != 0x00) {
		//判断是否映射成功
		if(req_mem_success) {
			unsigned long offset = *((unsigned long *)ubuf);
			offset = (offset < 0) ? 0 : offset;
			address = ioaddr + offset;
 			read_result = ioread32(address);
		} else {
			printk("can't use this region \n");
			read_result = 1;
		}
		result = copy_to_user(ubuf, &read_result, sizeof(long));
		if(result != 0){
			printk("dev_fifo_read failed!\n");
			return -EFAULT;
		}
	}
	return sizeof(long);
}

static ssize_t dev_fifo_write(struct file *file, const char __user *ubuf, size_t size, loff_t *ppos)
{
	unsigned long address;
	struct mycdev *dev = file->private_data;
	if (bar_base != 0x0) {
		if (req_mem_success) {
			int value = *((unsigned long *)ubuf);
			unsigned long offset = *((unsigned long *)ubuf + 1);
			offset = (offset < 0) ? 0 : offset;
			address = ioaddr + offset;
			iowrite32(value, address);
		}else{
            printk("this memeory space can't be used ! \n");
		}
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
	ret = register_chrdev_region(dev_num, 1, "dev_driver");
	if(ret < 0){
		ret = alloc_chrdev_region(&dev_num, 0, 1, "dev_driver");
		if(ret < 0){
			printk("Fail to register_chrdev_region\n");
			goto err_register_chrdev_region;
		}
	}
	cls = class_create(THIS_MODULE, "dev_driver");
	if(IS_ERR(cls)){
		ret = PTR_ERR(cls);
		goto err_class_create;
	}
	cdev_init(&global_cdev->cdev, &fifo_operations);
	ret = cdev_add(&global_cdev->cdev,dev_num,1);
	if (ret < 0){
		goto err_cdev_add;
	}
	device = device_create(cls,NULL,dev_num,NULL,"dev_driver");
	if(IS_ERR(device)){
		ret = PTR_ERR(device);
		printk("Fail to device_create\n");
		goto err_device_create;	
	}
	printk("*******************************\n");
	printk("Register dev_fifo to system, ok!\n");
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