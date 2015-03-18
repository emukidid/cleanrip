# CleanRip - Wii/GC Disc Ripper Tool - Version 1.0.4 #

### What is it ###
A tool to backup your Gamecube/Wii Discs using IOS58 and no need for cIOS.

## FAQ ##
Q. How do I use the redump.org verification to see if my rips are good?

A. Go to www.redump.org, click on downloads and then download the
> "Nintendo GameCube datfile" and the "Nintendo Wii datfile". Extract the .dat from the
> zip archives and place them on the root of the device you will be dumping to.
> They must be named gc.dat (Gamecube) and wii.dat (Wii), otherwise they will not be found. Until I implement http fetching of the file from within CleanRip, please make sure
> you update the DAT files on your disk regularly.


Q. CleanRip tells me that my dump is not verified, is this a bad rip?

A. It might be. To be sure, go to redump.org and have a look at the game you're trying to rip.
> If it doesn't exist on redump.org, then feel free to sign up to the forum and submit
> your rip checksum. If it does exist, then make sure you've turned off any Gamecube/Wii
> region patching from your modchip and also that the disc is as clean as can be. It might
> help to try ripping it on another wii if possible.


Q. What is "New Device per chunk" I see when entering my Wii settings?

A. This is useful to set to "No" when you're ripping a large game (>4GB) to a large enough
> FAT32 formatted device. This way, it'll automatically split your file based on the chunk
> size and will not prompt you to insert the next storage device for the next piece.

## Features ##
- FAT32 / NTFS file system support

- USB 2.0 support

- Front SD Support

- Gamecube / Wii / Wii Dual layer disc dumping

- BCA ripping to disk

- File Splitting (1,2,3GB (or Maximum file size - only on NTFS))

- MD5 sum is saved to disk

- Redump.org in-program verification for known rips


## Requirements ##
- Wii running the latest HBC (http://bootmii.org)

- Wii or GC Controller

- USB or SD storage device (>1.35GB free space)


## Credits ##
**libNTFS - Tantric/rodries (http://code.google.com/p/wiimc/source/browse/#svn/trunk/libs/libntfs)**

**libOGC/devKitPPC - shagkur / WinterMute**

**libmxml - Michael Sweet**

**Team Twiizers - http://bootmii.org/**

**md5.c - Aladdin Enterprises**

**sha1.c - Paul E. Jones**

**crc32.c - Craig Bruce**

.. and you, the users of course :)