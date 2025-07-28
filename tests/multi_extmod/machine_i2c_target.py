# Test I2CTarget, using hardware I2C controller
#
# Requires:
# - instance0 to be a PYBx, with I2C connections to the Y I2C ports
# - instance1 to be a RPI_PICOx with I2C connections to pins 9/8

import time
from machine import I2C, I2CTarget

ADDR = 67


# I2C controller
def instance0():
    i2c = I2C("Y")

    multitest.next()
    multitest.wait("target stage 1")

    i2c.writeto_mem(ADDR, 0, "abcdefgh")
    multitest.broadcast("controller stage 2")
    multitest.wait("target stage 3")
    print(i2c.readfrom_mem(ADDR, 0, 8))
    multitest.broadcast("controller stage 4")


def irq(i2c_target):
    flags = i2c_target.irq().flags()
    if flags & I2CTarget.IRQ_ADDR_MATCH_READ:
        print("IRQ_ADDR_MATCH_READ")
    if flags & I2CTarget.IRQ_ADDR_MATCH_WRITE:
        print("IRQ_ADDR_MATCH_WRITE")
    time.sleep_us(100)


# I2C target
def instance1():
    buf = bytearray(8)
    i2c_target = I2CTarget(0, ADDR, scl=9, sda=8, mem=buf)
    i2c_target.irq(irq, I2CTarget.IRQ_ADDR_MATCH_READ | I2CTarget.IRQ_ADDR_MATCH_WRITE, hard=True)

    multitest.next()
    multitest.broadcast("target stage 1")
    multitest.wait("controller stage 2")
    print(buf)
    multitest.broadcast("target stage 3")
    multitest.wait("controller stage 4")
    print("done")
