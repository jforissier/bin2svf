# bin2svf - BIOS binary to SVF conversion tool for Hisilicon D02

## Introduction - purpose

This file explains how to flash the BIOS of the D02 board using a JTAG
adapter and OpenOCD.

The D02 board has two SPI flash chips per CPU. One is called the *user BIOS*,
the other one is the *golden BIOS*. Which one is used to boot is defined by
the position of switches S3-3 (CPU0_SPI_SEL) and S3-4 (CPU1_SPI_SEL). The
user BIOS is normally used, i.e., the switches are in position 0. Hereafter
we consider single CPU boards only (CPU1 not installed), so only the flash
chips of CPU0 need to be considered. See the D02 Manual [[1]](#D02man). 

The BIOS can be updated from the BIOS itself, in the EBL menu, with the
commands: `provision` and `spiwfmem`. If for some reason you update to a
non-functional BIOS and the board won't boot, you can enable the golden
(backup) BIOS, boot the board into the EBL menu, switch back to the user
BIOS, and perform the `provision`/`spiwfmem` commands.

But, if you forget to switch back to the user BIOS, before entering the
`spiwfmem` command, you will overwrite your golden BIOS copy with a
non-bootable binary! Then, the only way to restore the board to a useable
state  is to use the *SPI flash program* interface (J59 on the D02 PCB) to
program the SPI flash memory. It is similar to a JTAG interface, except that
it expects a special protocol that is converted to SPI commands by the CPLD.
The file that you want to program has to be converted into the .SVF format
[[2]](#svf), useable by most JTAG programmers. It is achieved by the `bin2svf`
tool. Note that this conversion is specific to the D02 board and the logic
in the CPLD.

The whole procedure is described below.

## Prerequisites

You need a JTAG adapter, capable of connecting to a 10-pin header (J59 on the
D02 PCB). The instructions below apply to the C232HM-DDHSL-0 MPSSE cable from
FTDI [[3]](#c232hm).

You also need some software capable of driving the JTAG adapter, and executing
command files in SVF format such as OpenOCD. Assuming you have a Ubuntu
computer, it is available via "sudo apt-get install openocd".

## JTAG connection

Connect the C232HM cable to header J59 (SPI flash program) as follows:

    Orange  [TCK]   1 . . 2  [GND]   Black
    Green   [TDO]   3 . . 4  [3.3V]
    Brown   [TMS]   5 . . 6  [N/C]
    Purple  [TRSTN] 7 . . 8  [N/C]
    Yellow  [TDI]   9 . . 10 [GND]   Black    


Set the SPI_CPU_SEL switch (S3-1) to 0 (*choose SPI flash of CPU 0 to :w
program*).
Set the CPU0_SPI_SEL switch (S3-2) to 0 (*user BIOS) or 1 (golden BIOS),
depending on which one you want to program.

## Download valid BIOS and DTB files

Links to D02 binaries may be found on http://open-estuary.org/. You need the
UEFI binary (UEFI_D02.fd) and the device tree blob (hip05-d02.dtb).
The CPU will execute the firmware from address 0x800000 in the flash memory.
Then the DTB is expected 3 MB above, at 0xB00000. Since the size of the UEFI
binary is 3MB, it is possible to append the DTB to the UEFI and flash both
files in one step. Here is how:

```
$ cd ~/Downloads
$ wget http://download.open-estuary.org/AllDownloads/DownloadsEstuary/releases/2.2/linux/Common/D02/UEFI_D02.fd
$ wget http://download.open-estuary.org/AllDownloads/DownloadsEstuary/releases/2.2/linux/Common/D02/hip05-d02.dtb
$ cat UEFI_D02.fd hip05-d02.dtb >UEFI_DTB_D02.bin
```

## Convert the BIOS binary to .SVF

Build the `bin2svf` tool and run it as follows:

```
$ make
$ ./bin2svf UEFI_DTB_D02.bin >UEFI_DTB_D02.svf
```

## Flash the BIOS

Install OpenOCD with `sudo apt-get install openocd` if you're using Ubuntu.

OpenOCD lacks a definition file for the C232HM cable. One is provided here
(c232hm-ddhsl-0.cfg). A basic board configuration file is also provided
(d02.cfg).

To flash the BIOS:

```
$ openocd -f c232hm-ddhsl-0.cfg -f d02.cfg
Open On-Chip Debugger 0.9.0 (2015-09-02-10:42)
Licensed under GNU GPL v2
For bug reports, read
	http://openocd.org/doc/doxygen/bugs.html
adapter speed: 5000 kHz
jtag
Info : clock speed 5000 kHz
Warn : There are no enabled taps.  AUTO PROBING MIGHT NOT WORK!!
Error: JTAG scan chain interrogation failed: all ones
Error: Check JTAG interface, timings, target power, etc.
Error: Trying to use configured scan chain anyway...
Warn : Bypassing JTAG setup events due to errors
Warn : gdb services need one or more targets defined
```

In another terminal:

```
$ telnet localhost 4444
> ftdi_set_signal nTRST 1
> svf /path/to/UEFI_DTB_D02.svf
[...]
Time used: 4m31s672ms 
svf file programmed successfully for 28285 commands with 0 errors
> ftdi_set_signal nTRST 0
```

Reboot the board and you're done!

Note: the board won't boot if nTRST is 1 ; it should be so only during BIOS
update.

## References

<a name="D02man"></a>[1] D02 Board User Manual 
    https://github.com/hisilicon/boards/blob/master/D02/Linaro%20D02%20Board%20User%20Manual.doc
<a name="svf"></a>[2] Serial Vector Format Specification:
    http://www.jtagtest.com/pdf/svf_specification.pdf
<a name="c232hm"></a>[3] FTDI C232HM-DDHSL-0 USB to MPSSE cable:
    http://www.ftdichip.com/Products/Cables/USBMPSSE.htm
