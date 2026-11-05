*****************************************************************************
** ChibiOS/RT port for Atmel AVR ATmega328p.                               **
*****************************************************************************

** TARGET **

The demo runs on an Arduino Uno board.

** The Demo **

The demo exchanges COBS framed packets on USART0, available on the board USB
connector, using the SIO driver at 115200 baud 8N1.

Packet layout before stuffing: type, payload, CRC-16 high, CRC-16 low. The
CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, not reflected) computed
with the avr-libc _crc_xmodem_update(). Each stuffed packet is terminated by
a 0x00 delimiter.

Receive path: the SIO callback runs in ISR context outside the kernel lock,
it drains the USART, feeds an incremental COBS decoder and checks the CRC.
Valid packets are posted to the main thread through a mailbox, the buffers
are recycled through a second mailbox. Malformed, oversized or bad CRC
frames are counted and discarded. The LED on PB5 (pin 13 on Arduino IDE)
toggles on each accepted packet.

Device to host packets:
- 0x01 telemetry, every 100ms: seq (u8), uptime ms (u32), good, bad and
  dropped packet counters (u16), little endian.
- 0x82 echo reply, the payload of the request.
- 0x83 period reply, the telemetry period in ms (u16).
- 0xFF NAK, the rejected packet type.

Host to device packets:
- 0x02 echo request, up to 39 payload bytes.
- 0x03 set telemetry period, ms (u16), 10..10000.

** Build Procedure **

The demo was built using the GCC AVR toolchain.
