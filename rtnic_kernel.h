#ifndef _RTNIC_KERNEL_H_
#define _RTNIC_KERNEL_H_

#include <linux/cdev.h>

//新增调试信息使用这个宏
#define RTNIC_DEBUG
#ifdef RTNIC_DEBUG
#  define PDEBUG(fmt, args...) printk(fmt, ##args) 
#else
#  define PDEBUG(fmt, args...)
#endif


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
#define MODULE_NAME "xilinx_7021"

#define MAJOR_NUM  250  //设备数

#define BAR_0 (0)			/* MMIO, for 8-byte access */
#define BAR_SIZE_0          (0x08000000)

#define SW_FPGA_RX_BUF_SIZE 128 //131072
#define SW_FPGA_RX_BUF_SIZE_PAD 4096
#define SW_FPGA_TX_BUF_SIZE 256 //8192
#define SW_FPGA_TX_BUF_SIZE_PAD 4096

//ioctl cmd
#define MEM_CLEAR (0x00) /*清零全局内存*/

#define REG_WRITE_READ (0x01)
#define REG_ADDR_WRITE_READ (0x02)
#define SET_DMA_REG (0x03)
#define DMA_UPSTREAM (0x04)
#define DMA_DOWNSTREAM (0x05)

#define GET_IO_ADDR (0x06)
#define GET_IRQ_TYPE_REG (0x07)

//interrupt type
#define INTR_PORT_STATUS_CHG (0x01)
#define INTR_DMA_UP_STRAT (0x02)
#define INTR_DMA_UP_END (0x04)
#define INTR_DMA_DOWN_END (0x08)
#define INTR_SYNC_STATIS_CHG (0x10)

//registers define
#define IRQ_TYPE_REG			(0x4000000)
#define INTR_MASK_REG			(0x4000004)
#define STATUS_CHG_REG			(0x4000008)
#define DMA_UP_ADDR_REG			(0x400000c)
#define DMA_UP_MEM_SIZE_REG		(0x4000010)
#define DMA_UP_DATA_LEN_REG		(0x4000014)
#define	DMA_WR_CTL_REG			(0x4000018) 
#define DMA_DOWN_ADDR_REG		(0x400001c)
#define DMA_DOWN_DATA_LEN_REG	(0x4000020)
#define DS_READY				(0x4000024)

//global variable declare
//PCI设备
extern struct pci_dev *global_pdev;
//Address in memory of board
extern void *ioaddr;
//DMA缓存指针
extern void *tx_dma_buf, *rx_dma_buf;
//DMA缓存地址
extern dma_addr_t tx_addr, rx_addr;
//PCI设备信息
extern unsigned long bar_base;
extern unsigned long bar_len;
extern unsigned long bar_flags;
//收发帧次数统计
extern long sent_packets;
extern long received_packets_successfully;
extern long sent_packets_successfully;

//用于测试字符设备
struct mycdev 
{
	struct cdev cdev;

	unsigned char buffer[50];

	int len;  //is used?
};

//interface
void dma_downstream(void);

#endif