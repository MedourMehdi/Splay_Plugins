# Splay_Plugins
A list of sound plugins for [Splay](http://ol.lutece.net/telechargement/opensource/splay.zip) from Olivier Landemarre. His The concept is to have one interface and many sound players fully independent and corresponding via Unix signals.
More information [here](https://www.atari-forum.com/viewtopic.php?p=445499&hilit=splay)...

This repository contains many plugins using different libraries for playing the same format.
The purpose is to establish a base code in order to benchmark these sound libraries.
It's adapted for Atari platforms but giving a few efforts it should work on any platform.

## Supported formats & Libraries

* MP4:
    * fdkaac
    * faad

* MP3:
    * libMad
    * mpg123
    * minimp3