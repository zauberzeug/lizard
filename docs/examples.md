# Examples

## Turn on an LED

Create a new LED "green" at pin 14 and turn it on:

```
green = Output(14)
green.on()
```

## Read a button

Create a button "b1" at pin 25 with internal pull-up resistor and read its value:

```
b1 = Input(25)
b1.pullup()
b1.level
```

## Write a persistent startup script

Clear the persistent storage, configure a button and an LED, write the new startup script to the persistent storage, restart the microcontroller with these two new modules and print the stored configuration:

```
!-
!+green = Output(14)
!+b1 = Input(25)
!.
core.restart()
!?
```

## Define a rule

Create an LED "red", a button "b1" with pull-up resistor as well as a condition "c1" that turns off the LED as soon as the button is pressed:

```
red = Output(14)
red.on()
b1 = Input(25)
b1.pullup()
when b1.level == 0 then red.off(); end
```

## Create a shadow module

Create a "green" LED that shadows a "red" LED, i.e. will receive a copy of each command:

```
green = Output(13)
red = Output(14)
red.shadow(green)
```

## Use a port expander

Create a serial connection as well as a port expander with an LED at pin 15 and turn it on:

```
serial = Serial(26, 27, 11500, 1)
expander = Expander(serial, 32, 33)
led = expander.Output(15)
led.on()
```

## Use a wireless console dongle

Two ESP32 run the same Lizard firmware, they only differ in their startup scripts.
The robot's ESP32 joins the ESP-NOW radio as node "robot":

```
!-
!+robot = EspNowBridge("robot")
!.
core.restart()
```

The second ESP32 is the dongle on the host's USB port; it joins as node "dongle" and links to the robot.
Write its startup script before linking, because a linked dongle forwards `!+` and `!.` to the robot:

```
!-
!+usb = EspNowBridge("dongle")
!+usb.link("robot")
!.
core.restart()
```

Both scripts can also be uploaded with `configure.py`, e.g. `./configure.py dongle.liz /dev/ttyUSB1`.
From now on the host talks to the dongle's serial port as if it were the robot's UART0, e.g. with `./monitor.py /dev/ttyUSB1`, `./configure.py`, or RoSys pointed at that device:

```
core.info()
green = Output(14)
green.on()
!+green = Output(14)
!.
```

Every line runs on the robot and the robot's console output appears on the dongle's port.
Only lines starting with the dongle's module name stay on the dongle, e.g. `usb.unlink()` to stop forwarding or `usb.lost` to check the radio.
