# XENSIV™ BGT60TRxx Radar Sensor

## Introduction 
This library provides functions for interfacing with the XENSIV™ BGT60TRxx 60 GHz FMCW Radar Sensors. 

This library is ported from official [ModusToolbox™](https://www.infineon.com/cms/en/design-support/tools/sdk/modustoolbox-software/) released package on [Github/Infineon](https://github.com/Infineon/sensor-xensiv-bgt60trxx). The software structure follows the offical porting guide by providing platform-specific implementations of functions declared in *xensiv_bgt60trxx_platform.h* in *xensiv_bgt60trxx_esp.c*, while keeping platform independent files: 
- *xensiv_bgt60trxx.c*
- *xensiv_bgt60trxx.h*
- *xensiv_bgt60trxx_platform.h*
- *xensiv_bgt60trxx_regs.h*.

## Versioning 
This library adopts the following versioning convention:  `a.b.c+esp32-#`, where 
- `a.b.c` refers to the upstream official version it's ported from 
- `esp32` refers to the MCU platform 
- `#` refers to the build ID 


## How-to
This library is designed to be an ESP-IDF extra component. 

To include the component in the project, add the following line to the root `CMakeLists.txt` 

```
set(EXTRA_COMPONENT_DIRS path/to/extra/components/dir)
```

To use this library, simply include this header to project:
```
include "xensiv_bgt60trxx_esp.h"
```

### Driver Behavior
**IMPORTANT NOTE:** SPI Bus was not optimized to run at frequency higher than 20MHz. 

### Tested Environment
Tested in `esp-idf-v5.0.1` with VSCode on Windows using [ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) by adding library as an extra component for now. 

### Tested Devices 
Tested with XENSIV PAS CO2 Wing on the following devices:
* Adafruit ESP32 Feather V2 (Product ID [5400](https://www.adafruit.com/product/5400))
* Adafruit ESP32-S3 TFT Feather (Product ID [5483](https://www.adafruit.com/product/5483))
* ESP32-DevKitC
* ESP-WROVER-KIT
