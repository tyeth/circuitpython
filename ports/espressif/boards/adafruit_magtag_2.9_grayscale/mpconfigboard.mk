USB_VID = 0x239A
USB_PID = 0x80E6
USB_PRODUCT = "MagTag"
USB_MANUFACTURER = "Adafruit"

IDF_TARGET = esp32s2

CIRCUITPY_ESP_FLASH_MODE = qio
CIRCUITPY_ESP_FLASH_FREQ = 80m
CIRCUITPY_ESP_FLASH_SIZE = 4MB

CIRCUITPY_ESP_PSRAM_SIZE = 2MB
CIRCUITPY_ESP_PSRAM_MODE = qio
CIRCUITPY_ESP_PSRAM_FREQ = 80m

CIRCUITPY_PARALLELDISPLAYBUS = 0

# Include these Python libraries in firmware.
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_ConnectionManager
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_PortalBase
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_FakeRequests
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_Requests
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_NeoPixel
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_Display_Text
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_LIS3DH
