# clicker_firmware
The idea is to have a standard firmware running on your clicker, and provide an API to your creator board application to easily send commands to any clicker.


## Context

This work is based on [Creator Ci40 kit](https://docs.creatordev.io/ci40/iotkit/iot-kit-summary/), and this repository is for the PIC based clicker that are part of this kit.

When the code of this repository is programed onto a clicker, the clicker is going to connect to a TCP server running on the ci40 board. Then the ci40 will be able to access the IOs and the various buses available on the PIC and arround the click socket present on the clicker.


## How to pull the project

This is the recomanded directory strucutre :

```
work
 +- contiki
 +- LetMeCreateIoT
 +- dev
     +- clicker_firmware
```

You can pull the project and it's dependancies as follow :

```
git clone https://github.com/CreatorDev/contiki.git
git clone https://github.com/mtusnio/LetMeCreateIoT.git
cd LetMeCreateIoT
./install.sh -p ../contiki
cd ..
mkdir dev
cd dev
git clone https://github.com/jordanpenard/clicker_firmware.git
```

To build for avrdude bootloader : `make`

To program via avrdude : `make main.u`


## TCP spec for 6lowpan messages

The messages sent over 6lowpan are strings separated by '/'.

The strings received are passed to `process_request()` in order to split them into multiple strings chained to each others. The function is returning the first element of this chained list of strings. 

### Server -> Client messages

Standard formating :
  "bus/command/address/data/data"

- bus : Can be I2C, SPI, GPIO, UART or CORE
- command : Can be INIT, RELEASE, WRITE or READ
- address
  - For READ and WRITE of I2C, SPI and GPIO : base 10 address
  - For WRITE of CORE : can only be DEVICE_NAME
- data
  - For I2C and SPI : 
    - Base 10
    - Only required for READ and WRITE
    - For WRITE : Can be one or more occurence (burst)
    - For READ : One occurence, number of byte to be read
  - For GPIO : 
    - Base 2
    - One occurence
    - Only required for WRITE
  - For UART : 
    - String
    - One occurence
    - Only required for WRITE

Comments :
- For bus CORE, command can only be READ or WRITE

To be clarified :
- UART READ : How is this suposed to work ?
- GPIO address : Format ?

Limitations : 
- The max length of the TCP request is defined by INPUTBUFSIZE

Exceptions :
- At boot-up of a 6lowpan client, there is a hand-check process to make sure the connection is up. The client send "HELLO/device_name" (with device_name being the name saved in the flash). And then the server reply "HELLO", which would be an invalid request if this wasn't at boot-up.

### Client -> Server messages
					//
Standard formating :
  "command/data"
		            //
- command : Can be HELLO or REPLY
- data : 
  - For HELLO : Device name of the clicker
  - For RELPY : tbd

