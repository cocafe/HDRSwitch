# HDRSwitch

CLI program to toggle HDR on/off on Windows.

Inspired by [AutoActions](https://github.com/Codectory/AutoActions), but with some fixes, including reloading ICC profile on toggle.



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

# toggle hdr state on a monitor
./HDRSwitch.exe -t -m 0

# turn off hdr on a monitor
./HDRSwitch.exe --hdr_off -m 0

# turn on hdr on ALL monitors
./HDRSwitch.exe --hdr_on --all
```



#### How to compile

Compile with MSYS2 mingw64 and cmake.

