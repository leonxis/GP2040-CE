from machine import SPI, Pin
import time

# ==================== 硬件引脚配置 ====================
PIN_SCK    = 2
PIN_MISO   = 0  # SDO
PIN_MOSI   = 3  # SDI
PIN_CS     = 1
PIN_CONVST = 4

# ==================== SPI 驱动初始化 ====================
# 根据手册: CPOL=1, CPHA=0 (Mode 2)
# 频率设置为 1MHz 保证长线传输稳定
spi = SPI(0, baudrate=1_000_000, polarity=1, phase=0, 
          sck=Pin(PIN_SCK), mosi=Pin(PIN_MOSI), miso=Pin(PIN_MISO))

cs = Pin(PIN_CS, Pin.OUT, value=1)
convst = Pin(PIN_CONVST, Pin.OUT, value=1)

# ==================== 底层通信函数 ====================
def ads8332_transfer(cmd_word):
    """
    发送16位指令并同步读回16位数据
    cmd_word: 16位整数 (0x0000 - 0xFFFF)
    """
    tx = bytearray([(cmd_word >> 8) & 0xFF, cmd_word & 0xFF])
    rx = bytearray(2)
    
    cs.value(0)
    spi.write_readinto(tx, rx)
    cs.value(1)
    
    return (rx[0] << 8) | rx[1]

# ==================== 核心读取函数 ====================
def read_adc_raw(channel):
    """
    严格按照 CMR 手册时序读取指定通道
    1. 发送 Select Channel 指令 (CMR)
    2. 触发 CONVST 转换
    3. 发送 Read Data 指令 (0xD000) 取回结果
    """
    # --- 步骤 1: 选择通道 (CMR: 0x0000 - 0x7000) ---
    # 根据 Table 4, D[15:12] 为通道选择位
    select_cmd = (channel & 0x07) << 12
    ads8332_transfer(select_cmd)
    
    # --- 步骤 2: 触发转换 ---
    # 给 MUX 时间稳定并启动转换
    convst.value(0)
    time.sleep_us(2) 
    convst.value(1)
    
    # 等待转换完成 (Tconv 典型值 1.2us)
    time.sleep_us(2)
    
    # --- 步骤 3: 读回数据 ---
    # 发送 0xD000 (Read Data)，此时 SDO 输出的是步骤 2 转换出的数据
    val = ads8332_transfer(0xD000)
    
    return val

# ==================== 初始化 ADS8332 ====================
def init_ads8332():
    print("正在初始化 ADS8332...")
    # 写入 CFR (Write CFR: 0xE000 | 值)
    # CFR = 0x6FD: 内部时钟(Bit10=1), 手动模式(Bit11=0), 参考电压正常
    cfr_val = 0xE000 | 0b011011111101 
    ads8332_transfer(cfr_val)
    time.sleep_ms(20)
    print("✅ 初始化完成：手动通道选择模式，内部时钟。")

# ==================== 主程序 ====================
def main():
    init_ads8332()
    
    # 目标通道：IN0, IN1, IN2, IN7
    target_channels = [0, 1, 2, 7]
    VREF = 3.3
    
    while True:
        output_logs = []
        
        for ch in target_channels:
            samples = []
            # 每个通道连续读取 3 次
            for _ in range(3):
                raw_value = read_adc_raw(ch)
                samples.append(raw_value)
                time.sleep_us(50) # 采样间隔，防止电源扰动
            
            # 计算电压值（取第三次采样的电压作为参考展示）
            volts = (samples[2] * VREF) / 65535
            output_logs.append(f"CH{ch}: {samples} ({volts:.3f}V)")
        
        # 打印当前轮次所有通道的结果
        print(" | ".join(output_logs))
        
        # 每一轮检测之间停顿
        time.sleep_ms(1000)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n程序已停止")
