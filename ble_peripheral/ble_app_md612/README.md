# Invensense Motion Driver 6.12 with BLE to send euler (x, y, z) information..

It uses the S132 soft device found in $(SDK_ROOT)/components/softdevice/s132/hex/s132_nrf52_3.0.0_softdevice.hex

The was taken from ble_app_hids_joystick_md612 and added BLE with custom service and charateristics for the x, y, z values.
The device will show up as Nordic_MD612.  It does have the compability to be bonded but the data is sent even if no encrypted. 

Euler values will be sent every 15 seconds if no motion and every .5 sec (500ms) if motion is detected.
 
BLE has the custom service and the battery information too.
The custom service UUID and charactericts UUIDs need to be changed to something valid in the future for now just used what the examples recommended .

The ble_app_md612/nrf path is for compiling the code for the NRF52 with the pesky MPU9250 attached.
MPU interrupt is set to pin 27 and the TWI interface SCL is pin 3 and SDA is pin 4.  These pins are defined in main.c.
This used the 

The ble_app_md612/pesky path is for compilling the code for the pesky development board.  
MPU interrupt is set to pin 10 (defined in main.c) and the TWI interface SCL is pin 7 and SDA is pin 7.
This uses the projects/common/custom_board.h. 

The Makefiles are different but the rest of the code is the same with ifdefs to distinguish the differences. 
The sdk_config.h should be the same for pesky annd nrf. 
 
make flash_softdevice - will erase all the flash and program the S132
make flash - will update the application code and leave S132.

The button on the NRF52 are used as follows:

Most BLE examples use the following standard button assignments as configured by the BSP BLE Button Module:
During advertising or scanning:
	Button 1: Sleep (if not also in a connection)
	Button 2 long push: Turn off whitelist.
During sleep:
	Button 1: Wake up.
	Button 2: Wake up and delete bond information.
During connection:
	Button 1 long push: Disconnect.
	
 