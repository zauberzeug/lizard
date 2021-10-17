# Examples

Create a new LED “green” at pin 14 and pulse it twice a second:

    new led green 14
    set green.interval=0.5
    green pulse

Create a button “b1” at pin 25 with internal pull-up resistor and read its value:

    new button b1 25
    set b1.pullup=1
    b1 get

Clear the persistent storage, configure a button and an LED, restart the ESP with these two new modules and print the stored configuration:

    esp erase
    +new led green 14
    +new button b1 25
    esp restart
    ?

Create a pulsing LED “red”, a button “b1” with pull-up resistor as well as a condition “c1” that turns off the LED as soon as the button is pressed:

    new led red 14
    red pulse
    new button b1 25
    set b1.pullup=1
    if b1 != 1 red off

Create a “red” LED that shadows a “green” LED, i.e. will receive a copy of each command:

	new led red 13
	new led green 14
	shadow green > red

Create an E-Stop switch at pin “MCP_A3” that is HIGH by default and triggers an esp stop command when LOW to stop all actuators:

	new button estop MCP_A3
	if estop == 0 esp stop
