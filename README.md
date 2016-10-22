# clicker_firmware
The idea is to have a standard firmware running on your clicker, and provide an API to your creator board application to easily send commands to any clicker.

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
