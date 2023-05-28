# # # This display related code does not yet work!!!! # # #
# circup.exe install adafruit_st7789
# # circup.exe install Adafruit_shtc3 adafruit_vl53l1x adafruit_motorkit adafruit_motor adafruit_vl53l0x
import time
# import board
# import displayio
# from adafruit_st7789 import ST7789
# import gc9a01


import board
import displayio
import gc9a01
import busio
# Raspberry Pi Pico pinout, one possibility, at "southwest" of board
tft_clk = board.LCD_CLK # must be a SPI CLK
tft_mosi= board.LCD_MOSI # must be a SPI TX
tft_rst = board.TFT_RESET
tft_dc  = board.TFT_DC
tft_cs  = board.TFT_CS
tft_bl  = board.TFT_BACKLIGHT
spi = busio.SPI(clock=tft_clk, MOSI=tft_mosi)
display_bus = displayio.FourWire(spi, command=tft_dc, chip_select=tft_cs, reset=tft_rst)
display = gc9a01.GC9A01(display_bus, width=128, height=128, backlight_pin=tft_bl)

###################
#displayio.release_displays()

# spi = board.SPI()
# while not spi.try_lock():
#     pass
# #spi.configure(baudrate=24000000) # Configure SPI for 24MHz
# spi.unlock()

# tft_cs = board.TFT_CS
# tft_dc = board.TFT_DC

# display_bus = displayio.FourWire(spi, command=tft_dc, chip_select=tft_cs, reset=board.TFT_RESET)

#display = board.DISPLAY #ST7789(board.display, width=128, height=128, rowstart=40)

##############################################################

# # Release any resources currently in use for the displays
# displayio.release_displays()

# # Define the SPI bus and pins
# spi = board.SPI()
# tft_cs = board.D9
# tft_dc = board.D10

# # Define the display bus
# display_bus = displayio.FourWire(spi, command=tft_dc, chip_select=tft_cs, reset=board.TFT_RESET)

# # Define the display
# display = adafruit_gc9a01.GC9A01(display_bus, width=128, height=128)

#####################################################################################

display.show(displayio.CIRCUITPYTHON_TERMINAL)
display.root_group = displayio.CIRCUITPYTHON_TERMINAL # Set to terminal

#################################################################################

# # Make the display context
# splash = displayio.Group()
# display.root_group=splash

# color_bitmap = displayio.Bitmap(240, 240, 1)
# color_palette = displayio.Palette(1)
# color_palette[0] = 0xFF0000

# bg_sprite = displayio.TileGrid(color_bitmap,
#                                pixel_shader=color_palette,
#                                x=0, y=0)
# splash.append(bg_sprite)

print("starting:")
while True:
    print(".")
    display.refresh()
    time.sleep(1)
    pass