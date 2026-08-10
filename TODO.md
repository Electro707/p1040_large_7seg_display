# TODO

This is a TODO list for the next PCB revision

- Fix everything in errata
- Add reset button
- Add jumper or button to be able to jump to download mode
- Add test points
    - Some extra debug pads
    - For VCC and 12V as headers (for easy debugging, especially 5v for powerKit)
- Have it so SMA footprint comes out the back or top of enclosure (preferably the top)
- Shorten antenna routing to ensure less than 0.1 lambda of 2.4Gh
- [OPTIONAL] See if one can improve routing for ethernet portion to allow for 100M connection, right now limited to 10M, without going to 4 layer PCB
- Use the CH340 option with reset broken out to allow USB to reset ESP32, useful for uploading firmware as well
- Reduce resistance of dot segment, currently are too dim
