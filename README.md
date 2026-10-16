# Introduction
A tool to backup your Gamecube/Wii Discs on a GameCube or Wii
Create 1:1 backups of your GC/Wii discs for archival purposes without any requirements for custom IOS (cIOS). Supports USB 2.0 / NTFS / exFAT & Front SD.

# Support
If you have any questions about CleanRip, please make a new [discussion](https://github.com/emukidid/cleanrip/discussions) or [issue](https://github.com/emukidid/cleanrip/issues/new)

# Features
* exFAT/NTFS
* USB 2.0 support (Wii)
* Front SD support (Wii)
* BCA Dumping (GameCube on stock drives, Wii)
* Redump.info Rip Verification (via gc-forever.com copies and embedded CRC lists)
* Dump NR discs on GameCube

# Requirements
* Wii (or GC)
* Wii/GC Controller
* USB or SD storage device (>1.35GB free space)
* A method of booting homebrew (Homebrew Channel for Wii, Swiss for GameCube)

# Build

1. Install devkitPPC. You can download and install it from the official website: https://devkitpro.org/wiki/Getting_Started

2. Install libogc2 library. libogc2 is a library for Wii and GameCube homebrew development: https://github.com/extremscorner/libogc2

3. Install dependencies: `pacman -S gamecube-tools-git libogc2 libogc2-libdvm libogc2-libntfs ppc-mxml`

4. Build the project: Run `make` in the root directory of the project.

# Device Compatibility
Please note that the Wii can be picky about particular USB drives/storage devices. It's recommended to use a Y cable for hard drives that fail to power up from one USB port alone. If USB flash storage doesn't want to work, try a different brand/size. SD cards on GameCube will potentially have similar issues, it's best to have a few different brands/sizes/types at your disposal.

CleanRip for GC is also compatible with M.2 SSDs via M.2 Loader.

# NR Disc dumping
This is only available on GameCube consoles (and the Q, but that's untested). Only use it if you don't have other better means of dumping NR discs, it's purely there in the hope to make NR disc dumping more easily accessible to anyone with a GameCube they can run homebrew on.

The GameCube version of CleanRip can now (as of 2.4.1) read and dump NR discs via a patch that's uploaded to the drive at runtime. It leverages the fact that the disc drive has a known backdoor allowing us to upload patches to alter its functionality (as we always have since 2004 when Anaconda04 was released by disassembling/reversing the Cobra BIOS and its disc patching routines to allow DVD-R's). The [patch itself](https://github.com/emukidid/cleanrip/blob/master/drivecode/drivepatch.S) that CleanRip uses is ultimately an extension of various later revisions of said patch with an extension/mode I've added to allow it to send CleanRip the data from the NR discs which are closer to DVD-R's than they are to pressed GC discs. For those who care, an LLM was used to assist me by writing various tools to help me identify/streamline things I was looking for to implement said functionality, because without it this functionality would've taken me more time than I have to invest in this project. If you're completely anti-LLM then don't dump NR discs on a GameCube with CleanRip, that's the only time it'll use code that was written with LLM assistance (and said code was still checked/rewritten/tested to a standard I'm happy with by me).
