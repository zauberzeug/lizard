# Telemetry Frames

Lizard streams module properties as text with `core.output`.
A telemetry frame carries the same properties in binary form, next to the text lines on the same console.
Frames cost a fraction of the bytes, carry a CRC-16 and the sender's clock, and a serial bus peer can send them to its coordinator, which passes them through to the console without parsing them.

## Defining a frame

```
core.frame(1, "motor.position:i3 motor.enabled input.level", 100)
```

`core.frame(id, format, interval)` sends the listed properties every `interval` milliseconds as frame `id`.
The `format` string lists space-separated elements of the pattern `<module>.<property>[:<type>[<digits>]]` or `<variable>[:<type>[<digits>]]`.
The type is one of `?` (bool), `b`/`B` (signed/unsigned 8-bit integer), `h`/`H` (16-bit), `i`/`I` (32-bit) and `f` (32-bit float).
Without a type, booleans become `?`, integers `i` and numbers `f`.
The optional `digits` scales the value by 10^`digits` before the integer cast, so `motor.position:i3` carries three decimals in an int32, the same decimals `core.output` prints with `:3`.
Calling `core.frame` again with the same `id` replaces the frame.
`core.frame_add(id, format)` appends fields to a frame whose field list does not fit one line, e.g. a serial bus startup line, which travels inside one bus message.
`core.frame_clear()` removes all frames.

## Payload

Numeric fields are written little-endian in the order listed.
All `?` fields follow them, packed as bits, least significant first.
The example above yields 4 + 1 bytes: the position as int32 and two bits in one byte.

## Body

```
0x00 | src | id | seq | millis[4] | len | payload[len] | crc16[2]
```

`src` is 0 for the console host and the node id of a serial bus peer.
`seq` counts the frames of an `id`, so a reader sees gaps.
`millis` is the sender's `core.millis` when the frame was built, little-endian.
`len` is the payload length, at most 200 bytes.
`crc16` is CRC-16/CCITT-FALSE over everything before it.

## On the console

The body travels as `0x00 | COBS(body) | 0x00`.
COBS removes every zero byte from the body, so the two zeros are unambiguous frame delimiters between text lines.
A reader keeps the rule: after a `0x00`, a `0x01` opens a frame and the next `0x00` closes it; anything else is text again.
`monitor.py` prints each frame as `[frame src=… id=… seq=… millis=… payload=…]` and a frame that fails its length or CRC check as `[corrupt frame: …]`.

## Over the serial bus

A peer remembers the node that polls it and sends its frames there instead of to its console.
Bus messages are text lines, so the body is byte-stuffed: marker `0x01`, then every `00 09 0a 0d 20 7d` byte becomes `0x7d` followed by the byte XOR `0x50`.
The coordinator unstuffs the message, checks length, CRC and that `src` matches the sender, and writes the body to its console as a COBS frame.
Frames a peer cannot hand over, e.g. before its first poll, are counted in `core.frame_drops`; malformed frames at the coordinator are counted and reported at most once per second.

## From an expander

With `expander.frames = true` (see [Expander](module_reference.md#expander)), the core defines frame 255 on the other microcontroller with the properties of all its proxies, in the order the proxies are created and each proxy's properties in alphabetical order.
Booleans become `?`, integers `i` and numbers `f`.
A proxy with a property of another type, or one that would push the payload beyond 200 bytes, keeps its text broadcast.
The other microcontroller sends the frame once per step with `core.frame_lines = true`, i.e. as a line on its console, which is the core's serial link: marker `0x01`, the body without its leading `0x00`, the bytes `0a 0d 7d` escaped as `0x7d` followed by the byte XOR `0x50`, and `\n`.
Zero bytes stay unescaped because the link is read as raw lines, not as C strings; the escaping decodes like the bus stuffing.
The core checks length and CRC, counts gaps in `seq` as `frame_gaps` and failed checks as `frame_errors`, and writes the values straight into the proxies.
Frames with another id, i.e. frames the other microcontroller defines itself, are passed through to the core's console.
