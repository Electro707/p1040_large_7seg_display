# P1040 - Large 7-Segment Display Driver
\> PCBs (both), Rev 1
\> Firmware, WIP
\> Enclosure, WIP

This project is a driver for some large 4in 7-segment displays

IMAGE COMING SOON!

# Directory Structure
- `CAD`: All FreeCAD enclosure related stuff
    - C1045: Display Holder
    - C1046: Driver Board Holder, spans 2 displays
    - C1047: Single Display Backside
    - C1048: Dowel
- E1041: PCB, Driver board, KiCAD
- E1042: PCB, Pin to Connector Adapter, KiCAD
- F1043: A demo quick firmware for testing
- F1044: Main Firmware
- `misc`: Miscellaneous files. Right now only the spreadsheet I used for some calculations exists
- `release`: Latest exported build files, includes Schematic, BOM, Gerber, and STL files

# Known Issues
Known issues, and any work-arounds, are documented in [ERRATA.md](ERRATA.md)

# License
This project is licensed under [GPLv3](LICENSE.md)**

** This excludes the files in `E1041 - PCB, Driver Board/ExternalStep`, as they came from the manufacturer.
** Also excludes the files `zones.c` and `zones.h` in `F1044`, as those are modified from the [micro_tz_d library](https://github.com/jdlambert/micro_tz_db/tree/master) which is under an MIT
license
