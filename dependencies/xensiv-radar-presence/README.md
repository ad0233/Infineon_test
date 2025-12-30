# XENSIV™ Radar presence detection

## Overview

The XENSIV™ Radar Presence Detection library detects both macro and micro movements in a configurable range using the data acquired by XENSIV™ FMCW radar sensor. 

This library is ported from official [ModusToolbox™](https://www.infineon.com/cms/en/design-support/tools/sdk/modustoolbox-software/) released package on [Github/Infineon](https://github.com/Infineon/xensiv-radar-presence). It uses the ESP-DSP library that provides signal processing functions required to support the implementation of presence detection algorithm.

The user can configure the operation mode:

* Macro only
* Micro only
* Macro and micro
* Micro after macro

The minimum and maximum range detection can be configured.

The sensitivity level of each mode can be adjusted by changing the thresholds levels. 
   
The library provides the following features:

* Initialization of presence detection algorithm
* Configuration of presence detection algorithm parameters
* Ability to detect micro-movements
* Generation of presence detection events


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
Note that this library is released in **binary** form. For any customization, please reach out to Infineon. 

### Tested Environment
Tested in `esp-idf-v5.0.1` with VSCode on Windows using [ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) by adding library as an extra component for now. 

### Tested Devices 
Tested with XENSIV PAS CO2 Wing on the following devices:
* Adafruit ESP32 Feather V2 (Product ID [5400](https://www.adafruit.com/product/5400))
* Adafruit ESP32-S3 TFT Feather (Product ID [5483](https://www.adafruit.com/product/5483))
* ESP32-DevKitC
* ESP-WROVER-KIT

