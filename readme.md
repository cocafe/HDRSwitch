# HDRSwitch

CLI program to toggle HDR on/off on Windows.

Inspired by [AutoActions](https://github.com/Codectory/AutoActions), but with some fixes, including reloading ICC profile on toggle.

Tested on Windows 10 22H2 and Windows 11 22H2.

#### How to use

```shell
    --hdr_on                 Turn on HDR on a monitor
    --hdr_off                Turn off HDR on a monitor
    -t --toggle              Toggle HDR state on a monitor
    -v --verbose             Verbose output
    -l --list                List monitors
    -m --monitor <..>        Monitor index
    -a --all                 Apply to all monitors
    -h --help                This help message
```

```shell
# list all monitors
./HDRSwitch.exe -l

# toggle hdr state on monitor 0
./HDRSwitch.exe -t -m 0

# turn off hdr on monitor 0
./HDRSwitch.exe --hdr_off -m 0

# turn on hdr on ALL monitors
./HDRSwitch.exe --hdr_on --all
```

You can create a desktop shortcut and assign with a shortcut hotkey to toggle HDR by keyboard. 

#### How to compile

Compile with MSYS2 mingw64 and cmake.

