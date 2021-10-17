# Getting Started

You can use monitor.py to launch an interactive shell to try out configurations. Normally the ESP will write its output to the console for processing on the Jetson. To stop these messages you can call
    
    esp mute

See the ESP module reference for other commands. Do not forget to unmute the esp afterwards to make sure you can read back the data on the Jetson.
To try out individual modules, you can get their current state or unmute them for continuous output, e.g.:
	
    new button estop MCP_A3
	estop get
	estop unmute
