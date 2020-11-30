# camera-control

## Info
Simple command-line application for controlling camera settings.

Based on source code posted by AlexOD42 at [https://github.com/showmewebcam/showmewebcam/issues/56](https://github.com/showmewebcam/showmewebcam/issues/56)


## Dependencies
```sh
sudo apt-get install libncurses5-dev libncursesw5-dev 
```

## Compilation
```
make
```

## How to use
```
Usage: 
Available options are
 -c file     Path to config file
 -h          Print this help screen and exit
 -v device   V4L2 Video Capture device

# default config file - /boot/camera.txt
# default v4l2 device - /dev/video0
```

### Run
```
./camera-ctl
```

### User interface
|keyboard key|action|
|:-----------|:-----|
|Up|Previous item|
|Down|Next item|
|Home|First item|
|End|Last item|
|Left|Decrease value by one step|
|Right|Increase value by one step|
|PgDn|Decrease value by ten steps|
|PgUp|Increase value by ten steps|
|R|Reset all items|
|D|Set default value for current item|
|N|Set minimum value for current item|
|M|Set maximum value for current item|
|L|Load settings from config file|
|S|Save settings to config file|
|Q|Quit application|

