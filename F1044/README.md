# F1044 Firmware

## Building
This firmware uses PlatformIO for it's build system.

You should be able to build either in VSCode, or in the CLI by running
```bash
pio run     # to build
pio run --target upload     # to upload
```

## Uploading
During the initial build, one must upload the firmware over the programming interface
on the E1041 PCB, J23 (next to the MCU). The pogo pins is for a [Tag-Connect](https://www.tag-connect.com/product/tc2030-idc-nl)
connector. The programming pinout is available on the schematic.

I personally used a generic ESP programmer dev board to upload the firmware, the one below is what I have.
![Image of programmer](https://m.media-amazon.com/images/I/61H0RezJ6QL._SL1050_.jpg)

On that particular programmer, I connected the yellow header to the tag-connect it as follows:
|---------------|-----------------|
|ESP Programmer | Tag Connect Pin |
|---------------|-----------------|
|RX             |1                |
|TX             |2                |
|3V3            |3                |
|RST            |4                |
|IO0            |5                |
|GND            |6                |
|---------------|-----------------|

Then you can use esptool.py to upload the firmware.

After the initial firmware upload, while you can still use the tag-connect, the firmware also accepts new firmware
over the serial line. I build a python script [`tools/upload.py`](tools/upload.py) to accomplish this.

The first argument is the firmware bin file, the second is either an IP address or the serial port to upload over.

The uploading in PIO is setup to use that script. You can change the port used in [platformio.ini](platformio.ini).

> TODO: Have the config use the port given in VSCode.
> TODO: Have a seperate config that uses esptool to upload, same build settings

## Documentation
The documentation for this project is written in AsciiDoc, and uses Asciidoctor to generate a PDF or HTML page.
The docs can be built with:
```bash
asciidoctor "doc/F1044 Doc.adoc"        # for HTML
asciidoctor-pdf "doc/F1044 Doc.adoc"    # for PDF
```