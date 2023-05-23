# # # This display related code does not yet work!!!! # # #
# circup.exe install adafruit_st7789
# # circup.exe install Adafruit_shtc3 adafruit_vl53l1x adafruit_motorkit adafruit_motor adafruit_vl53l0x
import time
import board
import displayio
from adafruit_st7789 import ST7789

print("starting:")
displayio.release_displays()



# spi = board.SPI()
# while not spi.try_lock():
#     pass
# #spi.configure(baudrate=24000000) # Configure SPI for 24MHz
# spi.unlock()

# tft_cs = board.TFT_CS
# tft_dc = board.TFT_DC

# display_bus = displayio.FourWire(spi, command=tft_dc, chip_select=tft_cs, reset=board.TFT_RESET)

display = board.display() #ST7789(board.display, width=128, height=128, rowstart=40)

# Make the display context
splash = displayio.Group()
display.show(splash)

color_bitmap = displayio.Bitmap(240, 240, 1)
color_palette = displayio.Palette(1)
color_palette[0] = 0xFF0000

bg_sprite = displayio.TileGrid(color_bitmap,
                               pixel_shader=color_palette,
                               x=0, y=0)
splash.append(bg_sprite)

while True:
    print(".")
    time.sleep(1)
    pass