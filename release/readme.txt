CleanRip - Wii/GC Disc Ripper Tool - Version 2.0.0

Whatsnew in 2.1.0:
- Fix DAT downloading issue (Wii DAT is > 1MB)
- Fix crash on verify and other small issues
- Fix build issues/warnings with latest devKitPPC
- Add Datel disc dumping support
- Add SD2SP2 (GameCube Serial Port 2) support
- Disable DAT downloading from GameCube (no DNS support)

Whatsnew in 2.0.0:
- GameCube version integrated into one codebase
- GX GUI
- Fix disc dumping on Wii U (Wii discs only of course)
- Fix ETA calculation on non GC discs
- Fix fatUnmount
- Stop larger than 4GB chunks on FAT FS
- Fix potential issue when read error occurs

Whatsnew in 1.0.5:
- Built with latest libOGC git (as of 30/06/2012)
- Built with libntfs-2012-1-15 (SVN Rev 13)
- Async read/write support for faster dumps (tueidj)
- Fixed time remaining to be accurate (tueidj)
- Fixed a potential bug when writing next chunk (tueidj)
- Fixed redump.org DAT file downloads (now re-hosted on gc-forever.com)
- Use proper DVD init from PPC and kill the starlet DVD IRQ (tueidj)
- Fixed Motor Off error message at the end of dumps

Whatsnew in 1.0.4:
- Added redump.org DAT file downloading
- Added more info to the dumpinfo.txt
- Fixed rip completion time not showing up
- Fixed NTFS issues (failed to create file/etc)

Whatsnew in 1.0.3:
- Added SHA-1 and CRC32 calculation (all saved to disk)
- Fixed redump.org verification on second and subsequent files
- Compiled with the latest libFAT & libOGC

Whatsnew in 1.0.2:
- Added redump.org in-program verification (see FAQ section)
- Added auto file splitting (i.e. ripping a Wii disc to a large FAT32 hdd)
- Fixed BCA dumping (GC & Wii disc)
- Fixed Multi-Disc rip file names
- Fixed Crashes on disc init

Whatsnew in 1.0.1:
- Added experimental BCA dumping (Wii disc only)
- Added MD5 sum saving
- Fixed Speed/ETA calculation
- Fixed file split issues
- Fixed SD/USB re-init issues
- Hopefully random code dumps with Wiimote are gone


What is it?
A tool to backup your Gamecube/Wii Discs using IOS58 and no need for cIOS.

FAQ:
Q. How do I use the redump.org verification to see if my rips are good?
A. Go to www.redump.org, click on downloads and then download the 
   "Nintendo GameCube datfile" and the "Nintendo Wii datfile". Extract the .dat from the
   zip archives and place them on the root of the device you will be dumping to.
   They must be named gc.dat (Gamecube) and wii.dat (Wii), otherwise they will not be found.
   Until I implement http fetching of the file from within CleanRip, please make sure
   you update the DAT files on your disk regularly.

Q. CleanRip tells me that my dump is not verified, is this a bad rip?
A. It might be. To be sure, go to redump.org and have a look at the game you're trying to rip.
   If it doesn't exist on redump.org, then feel free to sign up to the forum and submit 
   your rip checksum. If it does exist, then make sure you've turned off any Gamecube/Wii
   region patching from your modchip and also that the disc is as clean as can be. It might
   help to try ripping it on another wii if possible.

Q. What is "New Device per chunk" I see when entering my Wii settings?
   This is useful to set to "No" when you're ripping a large game (>4GB) to a large enough
   FAT32 formatted device. This way, it'll automatically split your file based on the chunk
   size and will not prompt you to insert the next storage device for the next piece.

Features:
- FAT32 / NTFS file system support
- USB 2.0 support
- Front SD Support
- Gamecube / Wii / Wii Dual layer disc dumping
- BCA ripping to disk
- File Splitting (1,2,3GB (or Maximum file size - only on NTFS))
- MD5 sum is saved to disk
- Redump.org in-program verification for known rips


Requirements:
- Wii running the latest HBC (http://bootmii.org)
- Wii or GC Controller
- USB or SD storage device (>1.35GB free space)


Credits:
* libNTFS - Tantric/rodries (http://code.google.com/p/wiimc/source/browse/#svn/trunk/libs/libntfs)
* libOGC/devKitPPC - shagkur / WinterMute
* libmxml - Michael Sweet
* Team Twiizers - http://bootmii.org/
* md5.c - Aladdin Enterprises
* sha1.c - Paul E. Jones
* crc32.c - Craig Bruce
.. and you, the users of course :)