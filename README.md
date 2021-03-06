package_bfspi
=============

linux kernel driver for IP0X spi device

created as new package for switchfin base.
the drivers is based on bfsi.c created by David Rowe and all contributors


requiremnet:
1. switchfin build package 
2. avrgcc toolchains
3. olimex avr-jtag-usb

there is two parts :
  1. bfspi spi linux driver module as spi master
  2. bfspi test file.
  3. atmega 128 as spi slave device test file (based on arduino examples files available on the web)

Howto build ip0x staff:
  1. Copy the downloaded git package_bfspi folder into switchfin package directory and renamed as bfspi
  2. Copy the downloaded git source_bfspi into switchfin package/sources directory and renamed as bfspi
  2. to build type in the top switchfin directory: make bfspi
  3. the kernel modules will be copied to kernel modules misc directory
  4. the test software will be build in the build directory at /usr/sbin
  
Howto build atmega 128 staff:
  1. Copy the downloaded avr-spi folder to any directory.
  2. check the Makefile and Makefile.mk 
  3. type in the root avr-spi top folder: make
  4. type in the root avr-spi top folder: make flash

Modules init parameters:
  1. cardno: is the card that wiil be used for this spi modules, please read the bfsi and ip0x docs. for the details.
  2. spibaud: is the frequency  of the spi in khz
  3. spimode: is the mode of the spi device

Howto test:
  1. from the root prompt type: bfspi-test </dev/bfspix.y> <cmd_len> <test_no> <test_parm ...>
   where :

    /dev/bfspix.y: is the char device driver x: is the cardno, y is the port no (1 .. 5);
                   please ceck bfsi docs. for details about port no and card no

    cmd_len number of cmd line

    test_no 1 -> tes the ip0X led 
           2 -> spi read / write test

    test_param  for test_no 1 => led port no
		for test_no 2 => no parameter



