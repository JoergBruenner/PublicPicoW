# PublicPicoW
Tutorials and sample code for Raspberry Pico W.

The samples were compiled in a Windows environment. The Pico SDK v 2.1.1 is used. It's installed in the current user directory. Same is valid to Pico extras API.

## MQTT Sample 
Sends a MQTT message to a local MQTT Server.

## Check Time
Connects to a SNTP Server and prints out time and epoch timestamp. The mktime function does not work as expected. i had to build an own one.
