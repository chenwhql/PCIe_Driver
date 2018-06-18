#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "rtnic_kernel.h"

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
	printk("Read:0x%0x = 0x%0lx\n", msgctl, result);

	pci_read_config_word(pdev, pos + PCI_MSI_DATA_32, &msgctl);
	printk("PCI_MSI_DATA_32:0x%0x\n", msgctl);

	return 0;
}

//DMA down stream interface
void dma_downstream(void){

	unsigned long *pLong = NULL;
	int loop_flag = 0;
	int loop;
	int tmp;
	int each_frame = 1512;
	int downstream_value = 0;

	PDEBUG("tx_dma_buf = 0x%x\n", tx_dma_buf);
	memset(tx_dma_buf, 0, SW_FPGA_TX_BUF_SIZE);
	pLong = (unsigned long *)tx_dma_buf;
	for(loop = 0; loop < SW_FPGA_TX_BUF_SIZE; loop += 4) {
		tmp = loop % each_frame;
		if(loop_flag == 0) {
			*pLong = downstream_value;
			downstream_value++;
		} else if(tmp == 4 || tmp == 12) {
			*pLong = 0x0f;
		} else if (tmp == (each_frame - 4) || loop == (SW_FPGA_TX_BUF_SIZE - 4)) {
			*pLong = 0x01;
		} else {
			*pLong = 0;
		}
		loop_flag = (loop_flag == 0) ? 1 : 0;
		pLong++;
	}
	
	//shield all interrupt
	iowrite32(0x00, (void *)(ioaddr + INTR_MASK_REG));
	//tell the down addr of memary
	iowrite32(tx_addr, (void *)(ioaddr + DMA_DOWN_ADDR_REG));
	//tell the data length
	iowrite32(SW_FPGA_TX_BUF_SIZE, (void *)(ioaddr + DMA_DOWN_DATA_LEN_REG));
	//give the read start cmd
	iowrite32(0x02, (void *)(ioaddr + DMA_WR_CTL_REG));
	//open all interrupt
	iowrite32(0xFFFFFFFF, (void *)(ioaddr + INTR_MASK_REG));
	
	sent_packets++;
}