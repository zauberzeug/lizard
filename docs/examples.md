# Examples

## Turn on an LED

Create a new LED "green" at pin 14 and turn it on:

    green = Output(14)
    green.on()

## Read a button

Create a button "b1" at pin 25 with internal pull-up resistor and read its value:

    b1 = Input(25)
    b1.pullup()
    b1.level

## Write a persistent startup script

Clear the persistent storage, configure a button and an LED, write the new startup script to the persistent storage, restart the microcontroller with these two new modules and print the stored configuration:

    !-
    !+green = Output(14)
    !+b1 = Input(25)
    !.
    core.restart()
    !?

## Define a rule

Create an LED "red", a button "b1" with pull-up resistor as well as a condition "c1" that turns off the LED as soon as the button is pressed:

    red = Output(14)
    red.on()
    b1 = Input(25)
    b1.pullup()
    when b1.level == 0 then red.off(); end

## Create a shadow module

Create a "green" LED that shadows a "red" LED, i.e. will receive a copy of each command:

    green = Output(13)
    red = Output(14)
    red.shadow(green)

## Use a port expander

Create a serial connection as well as a port expander with an LED at pin 15 and turn it on:

    serial = Serial(26, 27, 11500, 1)
    expander = Expander(serial, 32, 33)
    led = expander.Output(15)
    led.on()
