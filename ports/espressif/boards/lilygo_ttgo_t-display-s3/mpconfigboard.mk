USB_VID = 0x303a
USB_PID = 0x813F
USB_PRODUCT = "T-Display-S3"
USB_MANUFACTURER = "LILYGO"

IDF_TARGET = esp32s3

CIRCUITPY_ESP_FLASH_MODE = dio
CIRCUITPY_ESP_FLASH_FREQ = 80m
CIRCUITPY_ESP_FLASH_SIZE = 16MB

CIRCUITPY_DISPLAYIO = 1
CIRCUITPY_PARALLELDISPLAY = 1

OPTIMIZATION_FLAGS = -Os
CIRCUITPY_ESPCAMERA = 0

#include these libs in firmware
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_Requests
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_ESP32SPI
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_NeoPixel
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_Display_Text
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_FakeRequests
