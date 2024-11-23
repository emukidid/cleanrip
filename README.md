# Introduction
A tool to backup your Gamecube/Wii Discs via IOS58
Create 1:1 backups of your GC/Wii discs for archival purposes without any requirements for custom IOS (cIOS). Supports USB 2.0 / NTFS / exFAT & Front SD.

# Support
If you have any questions about CleanRip?, please make a thread over at http://www.gc-forever.com/

# Features
* exFAT/NTFS
* USB 2.0 support
* Front SD support
* BCA Dumping
* Redump.org Rip Verification (via gc-forever.com) 

# Requirements
* Wii (or GC)
* Wii/GC Controller
* USB or SD storage device (>1.35GB free space)
* HBC 1.0.8 or greater installed 

# Build

1. Install devkitPPC. You can download and install it from the official website: https://devkitpro.org/wiki/Getting_Started

2. Install libogc2 library. libogc2 is a library for Wii and GameCube homebrew development: https://github.com/extremscorner/libogc2

3. Install dependencies: `pacman -S gamecube-tools-git libogc2 libogc2-libdvm libogc2-libntfs ppc-mxml`

4. Build the project: Run `make` in the root directory of the project.

# Device Compatibility
Please note that the Wii can be picky about particular USB drives/storage devices. It's recommended to use a Y cable for hard drives that fail to power up from one USB port alone. If USB flash storage doesn't want to work, try a different brand/size. SD cards on GameCube will potentially have similar issues, it's best to have a few different brands/sizes/types at your disposal.

CleanRip for GC is also compatible with M.2 SSDs via M.2 Loader.
