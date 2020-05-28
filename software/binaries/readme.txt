For ATtiny85:
-------------
avrdude -c usbtiny -p t85 -U lfuse:w:0xe2:m -U hfuse:w:0xdf:m -U flash:w:USB_Tester_t85_v1.0.hex

For ATtiny45:
-------------
avrdude -c usbtiny -p t45 -U lfuse:w:0xe2:m -U hfuse:w:0xdf:m -U flash:w:USB_Tester_t45_v1.0.hex
