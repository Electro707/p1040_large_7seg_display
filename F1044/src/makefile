compile:
	arduino-cli compile -v \
		--fqbn esp32:esp32:pico32 \
		F1044.ino

upload:
	arduino-cli upload -v \
		--fqbn esp32:esp32:pico32 \
		-p /dev/ttyUSB0 \
		F1044.ino 

doc: doc/F1044\ Doc.html
	asciidoctor "doc/F1044 Doc.adoc"