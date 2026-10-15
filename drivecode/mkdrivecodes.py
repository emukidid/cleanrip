#!/usr/bin/env python3
"""
Assemble drivepatch.S for each supported GameCube drive fw and write
source/drivecodes.h.

Needs mn10200-elf binutils. Only binutils 2.15 works to produce the same as the old patches
devkitPPC binutils 2.24 throws "fixup not contained within frag" on jsr/jmp to link-time symbols. Override the location with MN10200_BIN.

  python3 mkdrivecodes.py              NR-capable patches (default)
  python3 mkdrivecodes.py --original   original libogc/Swiss patches
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, 'drivepatch.S')
OUTPUT = os.path.join(HERE, '..', 'source', 'drivecodes.h')
BIN = os.environ.get('MN10200_BIN', os.path.join(HERE, '..', 'mn10200', 'bin'))
LOAD_ADDR = 0x40d000

# RAM variables: assembly-time constants so the assembler can pick the short
# 16-bit encodings for them.
RAM = ['FwBSS_Size', 'FwDriveState', 'FwTimer2', 'FwIdleTimeout', 'FwCDB0',
       'FwCmdState1', 'FwCmdState2', 'FwAuthList', 'FwErrByte',
       'FwStartFlags', 'FwAcceptCopyMask']

# name, inquiry date, firmware addresses. ROM code addresses are resolved at
# link time so pc-relative jsr/jmp get the right offstes.
DRIVES = [
    ('Drive04', 0x20020402, dict(
        FwInitDrive=0x808aa, FwData1_Source=0x8ea2a, FwMemcpy=0x82f27, FwBSS_Size=0x1b8,
        FwMemset=0x82f49, FwBP1Addr=0x89cd6, FwBP0Addr=0x8ae28, FwReset=0x8d47e,
        FwDriveState=0x40ecf9, FwTimer2=0x8084, FwIdleTimeout=0x819c, FwCDB0=0x80b4,
        FwCmdState1=0x81b0, FwCmdState2=0x8194, FwOrgDiskCheckAddr=0x885c1,
        FwBP0Exit=0x8ae33, FwBP1Target=0x89d4e, FwIntHandler=0x80a74,
        FwParamBPAddr=0x8b1b3, FwParamBPExit=0x8b1b5,
        FwAuthList=0x40ece6, FwErrByte=0x819a, FwStartFlags=0x40ecf8, FwAcceptCopyMask=1,
        FwDiscAuth=0x8a788, FwSetAuthOk=0x883a1)),
    ('Drive06', 0x20010608, dict(
        FwInitDrive=0x808aa, FwData1_Source=0x8e9c8, FwMemcpy=0x82f2e, FwBSS_Size=0x1ac,
        FwMemset=0x82f50, FwBP1Addr=0x89d42, FwBP0Addr=0x8b145, FwReset=0x8d41e,
        FwDriveState=0x40ed02, FwTimer2=0x8078, FwIdleTimeout=0x8190, FwCDB0=0x80a8,
        FwCmdState1=0x81a4, FwCmdState2=0x8188, FwOrgDiskCheckAddr=0x885b9,
        FwBP0Exit=0x8b150, FwBP1Target=0x89dd0, FwIntHandler=0x80a74,
        FwParamBPAddr=0x8b4d0, FwParamBPExit=0x8b4d2,
        FwAuthList=0x40ece6, FwErrByte=0x818e, FwStartFlags=0x40ed01, FwAcceptCopyMask=2,
        FwDiscAuth=0x8aa9d, FwSetAuthOk=0x88399)),
    ('Drive08', 0x20020823, dict(
        FwInitDrive=0x808aa, FwData1_Source=0x8ea7c, FwMemcpy=0x82f27, FwBSS_Size=0x1b6,
        FwMemset=0x82f49, FwBP1Addr=0x89d32, FwBP0Addr=0x8ae75, FwReset=0x8d4d9,
        FwDriveState=0x40ecf5, FwTimer2=0x8080, FwIdleTimeout=0x8198, FwCDB0=0x80b0,
        FwCmdState1=0x81ac, FwCmdState2=0x8190, FwOrgDiskCheckAddr=0x885c1,
        FwBP0Exit=0x8ae80, FwBP1Target=0x89da7, FwIntHandler=0x80a74,
        FwParamBPAddr=0x8b200, FwParamBPExit=0x8b202,
        FwAuthList=0x40ece6, FwErrByte=0x8196, FwStartFlags=0x40ecf4, FwAcceptCopyMask=1,
        FwDiscAuth=0x8a7d5, FwSetAuthOk=0x883a1)),
    ('DriveQ', 0x20010831, dict(
        FwInitDrive=0x808aa, FwData1_Source=0x8ebf6, FwMemcpy=0x82f7a, FwBSS_Size=0x1ae,
        FwMemset=0x82f9c, FwBP1Addr=0x89e39, FwBP0Addr=0x8b302, FwReset=0x8d640,
        FwDriveState=0x40ed02, FwTimer2=0x8078, FwIdleTimeout=0x8192, FwCDB0=0x80aa,
        FwCmdState1=0x81a6, FwCmdState2=0x818a, FwOrgDiskCheckAddr=0x8867f,
        FwBP0Exit=0x8b30d, FwBP1Target=0x89f84, FwIntHandler=0x80aa4,
        FwParamBPAddr=0x8b68d, FwParamBPExit=0x8b68f,
        FwAuthList=0x40ece6, FwErrByte=0x8190, FwStartFlags=0x40ed01, FwAcceptCopyMask=2,
        FwDiscAuth=0x8ac4e, FwSetAuthOk=0x8845f)),
]


def tool(name):
    for prefix in ('mn10200-elf-', ''):
        path = os.path.join(BIN, prefix + name)
        if os.path.exists(path) or os.path.exists(path + '.exe'):
            return path
    sys.exit('cannot find %s in %s (set MN10200_BIN)' % (name, BIN))


def build(syms, nr_support, tmp):
    inc = os.path.join(tmp, 'ram.inc')
    with open(inc, 'w') as f:
        for name in RAM:
            f.write('\t.set\t%s, 0x%x\n' % (name, syms[name]))
    obj, elf, out = (os.path.join(tmp, n) for n in ('p.o', 'p.elf', 'p.bin'))
    asflags = ['--defsym', 'NR_SUPPORT=1'] if nr_support else []
    subprocess.check_call([tool('as')] + asflags + ['-o', obj, inc, SOURCE])
    defsyms = []
    for name, addr in syms.items():
        if name not in RAM:
            defsyms += ['--defsym', '%s=0x%x' % (name, addr)]
    subprocess.check_call([tool('ld')] + defsyms + ['-Ttext', '0x%x' % LOAD_ADDR, '-o', elf, obj])
    subprocess.check_call([tool('objcopy'), '-O', 'binary', '-j', '.text', elf, out])
    nm = subprocess.check_output([tool('nm'), elf], text=True)
    addrs = {l.split()[2]: int(l.split()[0], 16) - LOAD_ADDR for l in nm.splitlines()}
    offsets = (addrs['patch_mode'], addrs.get('nr_servo'))
    with open(out, 'rb') as f:
        return f.read(), offsets


def main():
    nr_support = '--original' not in sys.argv[1:]
    blobs = []
    with tempfile.TemporaryDirectory() as tmp:
        for name, date, syms in DRIVES:
            blob, mode_offset = build(syms, nr_support, tmp)
            blobs.append((name, date, blob, mode_offset))
    if len(set(b[3] for b in blobs)) != 1:
        sys.exit('patch_mode offset differs between drives')
    mode_offset, servo_offset = blobs[0][3]
    size = max(len(b[2]) for b in blobs)
    if size % 4:
        sys.exit('patch size %d is not a multiple of 4' % size)

    lines = [
        '// GameCube DVD drive patches, generated by drivecode/mkdrivecodes.py',
        '// from drivecode/drivepatch.S. Do not edit by hand.',
        '//',
        '// Based on the drive code from linux cactus/shagkur/libogc, added to',
        '// GCoS and Swiss by emu_kidid. Adapted again for NR disc dumping in CleanRip',
        '// Supports:',
        '//          Offset patching //0x32',
        '//          AudioFix',
        '//          DRE Retry',
        '//          Originals (if disabled)',
        '//          DVD +/-R Media (patch mode 1)',
    ]
    if nr_support:
        lines.append('//          NR (development) discs (patch mode 2)')
    lines += [
        '',
        '#ifndef _DRIVECODES_H_',
        '#define _DRIVECODES_H_',
        '',
        '#define DRIVE_PATCH_ADDR 0x40d000',
        '#define DRIVE_PATCH_SIZE 0x%x' % size,
        '#define DRIVE_PATCH_MODE_OFFSET 0x%x' % mode_offset,
    ]
    if servo_offset is not None:
        lines.append('#define DRIVE_PATCH_SERVO_OFFSET 0x%x' % servo_offset)
    lines += [
        '',
    ]
    for name, date, blob, _ in blobs:
        blob = blob.ljust(size, b'\0')
        lines.append('// drive date %08x' % date)
        lines.append('static char %s[] __attribute__((aligned(32), unused)) =' % name)
        lines.append('{')
        for i in range(0, len(blob), 12):
            row = blob[i:i + 12]
            sep = ',' if i + 12 < len(blob) else ''
            lines.append('\t' + ','.join('0x%02x' % b for b in row) + sep)
        lines.append('};')
        lines.append('')
    lines.append('#endif')
    with open(OUTPUT, 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('wrote %s (%d bytes per drive, NR support %s)' %
          (os.path.normpath(OUTPUT), size, 'on' if nr_support else 'off'))


if __name__ == '__main__':
    main()
