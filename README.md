# PublicPicoW
Tutorials and sample code for Raspberry Pico W.

The samples were compiled in a Windows environment. The Pico SDK v 2.1.1 is used. It's installed in the current user directory. Same is valid to Pico extras API.

If this code is useful to you or saves your time, please send a donation to paypal.me/joerg313/\<amount\>EUR

## MQTT Sample 
Sends a MQTT message to a local MQTT Server.

## OTAUpdateWithWiFiConfig 
The Pico W checks if it has a valid WiFi configuration in its flash memory. If there is none or a reset signal (GPIO4 high) it is waiting in Accesspoint mode for WiFi configuration data. After successful WiFi connection it performs an OTA update. It connects to UPDATE_SERVER and checks if there is a newer version of its firmware. It installs it on flash and reboots.

Add your code at line 448.

## Check Time
Connects to a SNTP Server and prints out time and epoch timestamp. The mktime function does not work as expected. i had to build an own one.
