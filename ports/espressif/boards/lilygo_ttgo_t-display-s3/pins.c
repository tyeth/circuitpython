#include "shared-bindings/board/__init__.h"
#include "shared-module/displayio/__init__.h"

/*
powershell and bash commands to go back and forth between WSL and powershell
## POWERSHELL -- assumes linux and windows path, adjust accordingly...

 $BOARD="lilygo_ttgo_t-display-s3"; wsl --cd ~/dev-proj/python/circuitpython-tyeth/ports/espressif -e /bin/bash -c "export BOARD=$BOARD && git pull && source ./esp-idf/export.sh && make BOARD=$BOARD V=1 -j8 clean &&  make BOARD=$BOARD V=1 -j8 && mkdir -p /mnt/c/dev/python/circuitpython-VIEWonly/build-$BOARD/firmware/ && cp build-$BOARD/firmware.* /mnt/c/dev/python/circuitpython-VIEWonly/build-$BOARD/firmware" ; python $ENV:IDF_PATH\components\esptool_py\esptool\esptool.py --after no_reset erase_flash ; python $ENV:IDF_PATH\components\esptool_py\esptool\esptool.py --before no_reset --after hard_reset write_flash 0x0 C:\dev\python\circuitpython-VIEWonly\build-$BOARD\firmware.bin


Circup powershell commands commands for batch search / optional-install:
 $install=1;$term="motor";circup show | % { if($_.contains($term)){ return $_} else {return $null }} | % { if(-not ($_ -eq $null)){ if($install -eq 1) { circup install $_ } else {write-host $_ }} }

*/

// pins taken from https://github.com/espressif/arduino-esp32/commit/d03217af47a21c13e12eabf148df644d5f811d8e

STATIC const mp_rom_map_elem_t board_module_globals_table[] = {
    CIRCUITPYTHON_BOARD_DICT_STANDARD_ITEMS

    {MP_ROM_QSTR(MP_QSTR_IO0), MP_ROM_PTR(&pin_GPIO0)},
    {MP_ROM_QSTR(MP_QSTR_IO1), MP_ROM_PTR(&pin_GPIO1)},
    {MP_ROM_QSTR(MP_QSTR_IO2), MP_ROM_PTR(&pin_GPIO2)},
    {MP_ROM_QSTR(MP_QSTR_IO3), MP_ROM_PTR(&pin_GPIO3)},
    {MP_ROM_QSTR(MP_QSTR_IO4), MP_ROM_PTR(&pin_GPIO4)},
    {MP_ROM_QSTR(MP_QSTR_IO5), MP_ROM_PTR(&pin_GPIO5)},
    {MP_ROM_QSTR(MP_QSTR_IO6), MP_ROM_PTR(&pin_GPIO6)},
    {MP_ROM_QSTR(MP_QSTR_IO7), MP_ROM_PTR(&pin_GPIO7)},
    {MP_ROM_QSTR(MP_QSTR_IO8), MP_ROM_PTR(&pin_GPIO8)},
    {MP_ROM_QSTR(MP_QSTR_IO9), MP_ROM_PTR(&pin_GPIO9)},

    {MP_ROM_QSTR(MP_QSTR_IO10), MP_ROM_PTR(&pin_GPIO10)},
    {MP_ROM_QSTR(MP_QSTR_IO11), MP_ROM_PTR(&pin_GPIO11)},
    {MP_ROM_QSTR(MP_QSTR_IO12), MP_ROM_PTR(&pin_GPIO12)},
    {MP_ROM_QSTR(MP_QSTR_IO13), MP_ROM_PTR(&pin_GPIO13)},
    {MP_ROM_QSTR(MP_QSTR_IO14), MP_ROM_PTR(&pin_GPIO14)},
    {MP_ROM_QSTR(MP_QSTR_IO15), MP_ROM_PTR(&pin_GPIO15)},
    {MP_ROM_QSTR(MP_QSTR_IO16), MP_ROM_PTR(&pin_GPIO16)},
    {MP_ROM_QSTR(MP_QSTR_IO17), MP_ROM_PTR(&pin_GPIO17)},
    {MP_ROM_QSTR(MP_QSTR_IO18), MP_ROM_PTR(&pin_GPIO18)},
    {MP_ROM_QSTR(MP_QSTR_IO19), MP_ROM_PTR(&pin_GPIO19)},

    {MP_ROM_QSTR(MP_QSTR_IO20), MP_ROM_PTR(&pin_GPIO20)},
    {MP_ROM_QSTR(MP_QSTR_IO21), MP_ROM_PTR(&pin_GPIO21)},

    {MP_ROM_QSTR(MP_QSTR_IO39), MP_ROM_PTR(&pin_GPIO39)},
    {MP_ROM_QSTR(MP_QSTR_IO40), MP_ROM_PTR(&pin_GPIO40)},
    {MP_ROM_QSTR(MP_QSTR_IO41), MP_ROM_PTR(&pin_GPIO41)},
    {MP_ROM_QSTR(MP_QSTR_IO42), MP_ROM_PTR(&pin_GPIO42)},
    {MP_ROM_QSTR(MP_QSTR_IO43), MP_ROM_PTR(&pin_GPIO43)},
    {MP_ROM_QSTR(MP_QSTR_IO44), MP_ROM_PTR(&pin_GPIO44)},
    {MP_ROM_QSTR(MP_QSTR_IO45), MP_ROM_PTR(&pin_GPIO45)},
    {MP_ROM_QSTR(MP_QSTR_IO46), MP_ROM_PTR(&pin_GPIO46)},

    {MP_ROM_QSTR(MP_QSTR_TX), MP_ROM_PTR(&pin_GPIO43)},
    {MP_ROM_QSTR(MP_QSTR_RX), MP_ROM_PTR(&pin_GPIO44)},
    {MP_ROM_QSTR(MP_QSTR_TX1), MP_ROM_PTR(&pin_GPIO17)},
    {MP_ROM_QSTR(MP_QSTR_RX1), MP_ROM_PTR(&pin_GPIO18)},

    // // SD Card
    // { MP_ROM_QSTR(MP_QSTR_SD_MISO), MP_ROM_PTR(&pin_GPIO13) },
    // { MP_ROM_QSTR(MP_QSTR_SD_MOSI), MP_ROM_PTR(&pin_GPIO11) },
    // { MP_ROM_QSTR(MP_QSTR_SD_CLK), MP_ROM_PTR(&pin_GPIO12) },
    // { MP_ROM_QSTR(MP_QSTR_SD_CS), MP_ROM_PTR(&pin_GPIO10) },

    // // 1.14 inch LCD ST7789
    // { MP_ROM_QSTR(MP_QSTR_LCD_MOSI), MP_ROM_PTR(&pin_GPIO35) },
    // { MP_ROM_QSTR(MP_QSTR_LCD_CLK), MP_ROM_PTR(&pin_GPIO36) },
    // { MP_ROM_QSTR(MP_QSTR_LCD_CS), MP_ROM_PTR(&pin_GPIO34) },
    // { MP_ROM_QSTR(MP_QSTR_LCD_RST), MP_ROM_PTR(&pin_GPIO38) },
    // { MP_ROM_QSTR(MP_QSTR_LCD_BCKL), MP_ROM_PTR(&pin_GPIO33) },
    // { MP_ROM_QSTR(MP_QSTR_LCD_D_C), MP_ROM_PTR(&pin_GPIO37) },
    // { MP_ROM_QSTR(MP_QSTR_DISPLAY), MP_ROM_PTR(&displays[0].display) },

    // // LCD pins
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TFT_RESET), MP_ROM_PTR(&pin_PA00) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TFT_RD), MP_ROM_PTR(&pin_PB04) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TFT_RS), MP_ROM_PTR(&pin_PB05) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TFT_CS), MP_ROM_PTR(&pin_PB06) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TFT_TE), MP_ROM_PTR(&pin_PB07) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TFT_WR), MP_ROM_PTR(&pin_PB09) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TFT_DC), MP_ROM_PTR(&pin_PB09) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TFT_BACKLIGHT), MP_ROM_PTR(&pin_PB31) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA0), MP_ROM_PTR(&pin_PA16) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA1), MP_ROM_PTR(&pin_PA17) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA2), MP_ROM_PTR(&pin_PA18) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA3), MP_ROM_PTR(&pin_PA19) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA4), MP_ROM_PTR(&pin_PA20) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA5), MP_ROM_PTR(&pin_PA21) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA6), MP_ROM_PTR(&pin_PA22) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA7), MP_ROM_PTR(&pin_PA23) },

    // // Peripheral Power control
    // { MP_ROM_QSTR(MP_QSTR_PE_POWER), MP_ROM_PTR(&pin_GPIO14) },

    // // Battery Sense
    // { MP_ROM_QSTR(MP_QSTR_BATTERY), MP_ROM_PTR(&pin_GPIO9) },

    /* ------------===========--------------============------------- */
    /* ------------===========--------------============------------- */
    /* ------------===========--------------============------------- */
    /* ------------===========--------------============------------- */
   
    {MP_OBJ_NEW_QSTR(MP_QSTR_D0), MP_ROM_PTR(&pin_GPIO0)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D1), MP_ROM_PTR(&pin_GPIO1)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D2), MP_ROM_PTR(&pin_GPIO2)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D3), MP_ROM_PTR(&pin_GPIO3)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D4), MP_ROM_PTR(&pin_GPIO4)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D5), MP_ROM_PTR(&pin_GPIO5)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D6), MP_ROM_PTR(&pin_GPIO6)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D7), MP_ROM_PTR(&pin_GPIO7)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D8), MP_ROM_PTR(&pin_GPIO8)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D9), MP_ROM_PTR(&pin_GPIO9)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D10), MP_ROM_PTR(&pin_GPIO10)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D11), MP_ROM_PTR(&pin_GPIO11)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D12), MP_ROM_PTR(&pin_GPIO12)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D13), MP_ROM_PTR(&pin_GPIO13)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D14), MP_ROM_PTR(&pin_GPIO14)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D15), MP_ROM_PTR(&pin_GPIO15)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D16), MP_ROM_PTR(&pin_GPIO16)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D17), MP_ROM_PTR(&pin_GPIO17)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D18), MP_ROM_PTR(&pin_GPIO18)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D19), MP_ROM_PTR(&pin_GPIO19)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D20), MP_ROM_PTR(&pin_GPIO20)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D21), MP_ROM_PTR(&pin_GPIO21)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D39), MP_ROM_PTR(&pin_GPIO39)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D40), MP_ROM_PTR(&pin_GPIO40)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D41), MP_ROM_PTR(&pin_GPIO41)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D42), MP_ROM_PTR(&pin_GPIO42)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D43), MP_ROM_PTR(&pin_GPIO43)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D44), MP_ROM_PTR(&pin_GPIO44)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D45), MP_ROM_PTR(&pin_GPIO45)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_D46), MP_ROM_PTR(&pin_GPIO46)},
   
    /* ------------===========--------------============------------- */
    /* ------------===========--------------============------------- */
    /* ------------===========--------------============------------- */
    /* ------------===========--------------============------------- */

    {MP_OBJ_NEW_QSTR(MP_QSTR_BUTTON_1), MP_ROM_PTR(&pin_GPIO0)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_BUTTON_2), MP_ROM_PTR(&pin_GPIO14)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_BAT_VOLT), MP_ROM_PTR(&pin_GPIO4)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_BATTERY), MP_ROM_PTR(&pin_GPIO4)},

    // TX/RX 0+1 defined above
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TX), MP_ROM_PTR(&pin_GPIO43) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_RX), MP_ROM_PTR(&pin_GPIO44) },

    {MP_OBJ_NEW_QSTR(MP_QSTR_SDA), MP_ROM_PTR(&pin_GPIO18)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_SCL), MP_ROM_PTR(&pin_GPIO17)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_SDA1), MP_ROM_PTR(&pin_GPIO43)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_SCL1), MP_ROM_PTR(&pin_GPIO44)},

    {MP_OBJ_NEW_QSTR(MP_QSTR_SS), MP_ROM_PTR(&pin_GPIO10)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_MOSI), MP_ROM_PTR(&pin_GPIO11)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_MISO), MP_ROM_PTR(&pin_GPIO13)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_SCK), MP_ROM_PTR(&pin_GPIO12)},

    {MP_OBJ_NEW_QSTR(MP_QSTR_TP_RESET), MP_ROM_PTR(&pin_GPIO21)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_TP_INIT), MP_ROM_PTR(&pin_GPIO16)},

    // // LCD pins
    {MP_OBJ_NEW_QSTR(MP_QSTR_TFT_BACKLIGHT), MP_ROM_PTR(&pin_GPIO38)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_BACKLIGHT), MP_ROM_PTR(&pin_GPIO38)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_BL), MP_ROM_PTR(&pin_GPIO38)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_D0), MP_ROM_PTR(&pin_GPIO39)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_D1), MP_ROM_PTR(&pin_GPIO40)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_D2), MP_ROM_PTR(&pin_GPIO41)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_D3), MP_ROM_PTR(&pin_GPIO42)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_D4), MP_ROM_PTR(&pin_GPIO45)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_D5), MP_ROM_PTR(&pin_GPIO46)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_D6), MP_ROM_PTR(&pin_GPIO47)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_D7), MP_ROM_PTR(&pin_GPIO48)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA0), MP_ROM_PTR(&pin_GPIO39)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA1), MP_ROM_PTR(&pin_GPIO40)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA2), MP_ROM_PTR(&pin_GPIO41)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA3), MP_ROM_PTR(&pin_GPIO42)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA4), MP_ROM_PTR(&pin_GPIO45)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA5), MP_ROM_PTR(&pin_GPIO46)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA6), MP_ROM_PTR(&pin_GPIO47)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DATA7), MP_ROM_PTR(&pin_GPIO48)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_WR), MP_ROM_PTR(&pin_GPIO8)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_RD), MP_ROM_PTR(&pin_GPIO9)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_DC), MP_ROM_PTR(&pin_GPIO7)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_CS), MP_ROM_PTR(&pin_GPIO6)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_RES), MP_ROM_PTR(&pin_GPIO5)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_LCD_POWER_ON), MP_ROM_PTR(&pin_GPIO15)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_TFT_WR), MP_ROM_PTR(&pin_GPIO8)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_TFT_RD), MP_ROM_PTR(&pin_GPIO9)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_TFT_DC), MP_ROM_PTR(&pin_GPIO7)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_TFT_CS), MP_ROM_PTR(&pin_GPIO6)},
    {MP_OBJ_NEW_QSTR(MP_QSTR_TFT_RESET), MP_ROM_PTR(&pin_GPIO5)},
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TFT_RS), MP_ROM_PTR(&pin_PB05) },
    // { MP_OBJ_NEW_QSTR(MP_QSTR_TFT_TE), MP_ROM_PTR(&pin_PB07) },

    {MP_ROM_QSTR(MP_QSTR_I2C), MP_ROM_PTR(&board_i2c_obj)},
    {MP_ROM_QSTR(MP_QSTR_STEMMA_I2C), MP_ROM_PTR(&board_i2c_obj)},
    {MP_ROM_QSTR(MP_QSTR_SPI), MP_ROM_PTR(&board_spi_obj)},
    {MP_ROM_QSTR(MP_QSTR_UART), MP_ROM_PTR(&board_uart_obj)},

#ifdef CIRCUITPYTHON_PARALLELDISPLAY
    {MP_ROM_QSTR(MP_QSTR_DISPLAY), MP_ROM_PTR(&displays[0].display)}
#endif

};
MP_DEFINE_CONST_DICT(board_module_globals, board_module_globals_table);
