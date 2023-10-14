# Splay_Plugins
A list of sound plugins for [Splay](http://ol.lutece.net/telechargement/opensource/splay.zip) from Olivier Landemarre. His The concept is to have one interface and many sound players fully independent and corresponding via Unix signals.
More information [here](https://www.atari-forum.com/viewtopic.php?p=445499&hilit=splay)...

This repository contains many plugins using different libraries for playing the same format.
The purpose is to establish a base code in order to benchmark these sound libraries.
It's adapted for Atari platforms but giving a few efforts it should work on any platform.

## Supported formats & Libraries

* MP4/AAC:
    * fdkaac
    * faad

* MP3:
    * libMad
    * mpg123
    * minimp3

## How it works

Pass the sound file as argument to splay: it will search in his "./plugins" directory for an executable named play"sound extension".prg.

For testing or using these plugins you should rename them as playmp4.prg, playm4u.prg, playmp3.prg ... and place them in the plugin directory.

## Benchmark

These results corresponds to the time needed for for decoding 1000ms of sound at 49170Hz * 16bits * 2 channels. They were produced with Aranym hosted on a Raspberry Pi400.

NB: note that unlike libmad and minimp3, mpg123 resamples samples in real time from 44,1khz to 49,17khz.

* MP4/AAC ([sample mp3 used](https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3))

| Codec library | Best time(in ms) | Worst time (in ms) |
| ------------- | ---------------- | ------------------ |
| fdkaac        | 1090             | 2660               | 
| faad          | 540              | 1370               | 

* MP3 ([sample mp3 used](https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp4))

| Codec library | Best time(in ms) | Worst time (in ms) |
| ------------- | ---------------- | ------------------ |
| minimp3       | 0                | 900                | 
| mpg123        | 0                | 780                | 
| mad           | 140              | 350                | 

## Build

In each directory you'll find a makefile. Just use make after having grabbed the right library.

* MP3 libraries

- [mpg123](https://www.mpg123.de/index.shtml) and [libmad](http://m.baert.free.fr/contrib/docs/libmad/doxy/html/index.html) were grabbed from [Otto's website](https://tho-otto.de/crossmint.php)
- [minimp3](https://github.com/lieff/minimp3) library is already embedded in the code

* MP4/AAC libraries

- [mp4v2](https://mp4v2.org/) is used to parse the mp4 container
- [fdkaac](https://github.com/mstorsjo/fdk-aac) and mp4v2 was grabbed from [Otto's website](https://tho-otto.de/crossmint.php)
- [faad2](https://github.com/knik0/faad2) was built from source with the following commands (you should adapt them to your environment):

```console
cd faad2; mkdir build; cd build;
export CMAKE_PREFIX_PATH="/opt/cross-mint/m68k-atari-mint/lib";
export CMAKE_INCLUDE_PATH=/opt/cross-mint/m68k-atari-mint/include/;
export CMAKE_LIBRARY_PATH=/opt/cross-mint/m68k-atari-mint/lib/;
cmake \
-DCMAKE_SYSTEM_NAME=UNIX \
-DBUILD_SHARED_LIBS=OFF \
-DCMAKE_C_FLAGS=" -m68020-60 -fomit-frame-pointer -fno-strict-aliasing -O2 " \
-DCMAKE_CXX_FLAGS=" -m68020-60 -fomit-frame-pointer -fno-strict-aliasing -O2 -Wno-multichar" \
-DCMAKE_CXX_COMPILER=m68k-atari-mint-g++ \
-DCMAKE_C_COMPILER=m68k-atari-mint-gcc \
-DCMAKE_AR=/opt/cross-mint/bin/m68k-atari-mint-ar \
-DCMAKE_RANLIB=/opt/cross-mint/bin/m68k-atari-mint-ranlib \
-DCMAKE_PREFIX_PATH=/opt/cross-mint/m68k-atari-mint/lib \
../
```
