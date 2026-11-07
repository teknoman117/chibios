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
it drains the USART and feeds an incremental COBS decoder that writes the
decoded bytes directly in a 256 bytes ring buffer and keeps a running CRC
from the start of the frame, the cost per byte does not depend on the
frame size. Three indices are kept: head, the next byte of the frame being
decoded; mark, the start of that frame and the end of the last complete
packet; tail, the oldest packet not yet read by the application. On a
valid CRC the packet length is written in a slot reserved at mark, the
CRC is dropped, mark moves to head and a semaphore is signaled. Frames
with errors are discarded moving head back to mark. Malformed, oversized
or bad CRC frames are counted as bad, frames not fitting the ring are
counted as dropped. The LED on PB5 (pin 13 on Arduino IDE) toggles on each
accepted packet.

Flow control: the device grants credits to the host as an absolute limit,
the count of the bytes received so far plus the free ring space. The host
may send while its count of sent bytes is lower than the last limit
received. A received byte takes at most one ring byte so a host honoring
the credits cannot overflow the ring, lost credit packets, noise and
discarded frames do not affect the accounting. The limit is sent at
start, after the application consumes packets, in batches of 64 bytes
while more packets are pending, and with every telemetry packet.

Device to host packets:
- 0x01 telemetry, every 100ms: seq (u8), uptime ms (u32), good, bad and
  dropped packet counters (u16), little endian.
- 0x04 credit, the credit limit in received bytes (u16, wrapping).
- 0x82 echo reply, the payload of the request.
- 0x83 period reply, the telemetry period in ms (u16).
- 0x85 stall reply, the stall time in ms (u16).
- 0xFF NAK, the rejected packet type.

Host to device packets:
- 0x02 echo request, up to 39 payload bytes.
- 0x03 set telemetry period, ms (u16), 10..10000.
- 0x05 stall, ms (u16), 0..1000, the application stops processing packets
  for the given time, the ring fills meanwhile.

** Build Procedure **

The demo was built using the GCC AVR toolchain.
