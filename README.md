# RTNIC

Real time network interface card

### ioctl测试说明

#### ioctl DMA
##### 设置接收DMA数据（上行）：sudo ./ioctl 3 0
- write：告知DMA数据接收内存地址
- write：告知DMA数据接收内存长度
- write：屏蔽所有中断
- write：发送写开始命令

##### 读取DMA上行数据：sudo ./ioctl 4 0
- 将DMA上传后，存储在虚拟内存的数据，依次打印出来

##### 下发DMA数据：sudo ./ioctl 5 0

- 将下发数据在tx_dma_buf中准备好
- write：屏蔽所有中断
- write：告知DMA数据发送内存地址
- write：告知DMA数据发送内存长度
- write：发送读开始命令
- write：恢复所有中断

#### 中断说明

##### 端口状态变化中断 0x01
- 读取新的端口状态寄存器

##### DMA数据上传开始中断 0x02
- 读取上传数据的长度
- 告知fpga上位机接收数据的内存起始地址
- 告知fpga上位机接收数据的内存长度
- 发送写开始命令

##### DMA数据上传结束中断 0x04
- 将DMA上传后，存储在虚拟内存的数据，依次打印出来

##### DMA数据下发结束 0x08
- 无操作