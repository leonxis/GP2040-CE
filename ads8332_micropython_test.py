from machine import SPI, Pin
import time

# Wiring (user provided)
SPI_ID = 0
PIN_SCK = 2
PIN_MISO = 0  # ADS8332 SDO
PIN_MOSI = 3  # ADS8332 SDI
PIN_CS = 1
PIN_CONVST = 4

SPI_BAUD = 10_000_000
SPI_POLARITY = 1  # MODE2
SPI_PHASE = 0     # MODE2

CHANNELS = (0, 1, 7)
READS_PER_CHANNEL = 3
SETTLE_US_AFTER_CHANNEL_SWITCH = 2
SETTLE_US_AFTER_SAMPLE = 2

CMD_WRITE_CFR = 0xE000
CMD_MANUAL_CHANNEL = 0x8000

# CFR target (based on current project plan)
# D11=0, D10=1, D9=1, D4=1, D3=1, D2=1, D1=0, D0=0.
CFR_VALUE = (1 << 10) | (1 << 9) | (1 << 4) | (1 << 3) | (1 << 2)


spi = SPI(
    SPI_ID,
    baudrate=SPI_BAUD,
    polarity=SPI_POLARITY,
    phase=SPI_PHASE,
    bits=8,
    firstbit=SPI.MSB,
    sck=Pin(PIN_SCK),
    mosi=Pin(PIN_MOSI),
    miso=Pin(PIN_MISO),
)
cs = Pin(PIN_CS, Pin.OUT, value=1)
convst = Pin(PIN_CONVST, Pin.OUT, value=1)

tx = bytearray(2)
rx = bytearray(2)


def transfer16(word):
    tx[0] = (word >> 8) & 0xFF
    tx[1] = word & 0xFF
    cs.value(0)
    spi.write_readinto(tx, rx)
    cs.value(1)
    return (rx[0] << 8) | rx[1]


def write_cfr():
    transfer16(CMD_WRITE_CFR | (CFR_VALUE & 0x0FFF))
    time.sleep_us(SETTLE_US_AFTER_SAMPLE)


def do_conversion_frame():
    convst.value(0)
    convst.value(1)
    time.sleep_us(SETTLE_US_AFTER_SAMPLE)
    value = transfer16(0x0000)
    time.sleep_us(SETTLE_US_AFTER_SAMPLE)
    return value


def read_channel_once(channel):
    transfer16(CMD_MANUAL_CHANNEL | ((channel & 0x07) << 8))
    time.sleep_us(SETTLE_US_AFTER_CHANNEL_SWITCH)

    # First full conversion after channel switch is discarded to flush pipeline.
    _ = do_conversion_frame()
    # Second full conversion/read is used as valid sample.
    return do_conversion_frame()


def main():
    write_cfr()
    print("ADS8332 test start: mode2, 10MHz, channels IN0/IN1/IN7, 3 reads/channel, 1s period")
    while True:
        row = []
        for ch in CHANNELS:
            samples = [read_channel_once(ch) for _ in range(READS_PER_CHANNEL)]
            row.append("CH{}={}/{}/{}".format(ch, samples[0], samples[1], samples[2]))
        print(" | ".join(row))
        time.sleep(1)


main()
