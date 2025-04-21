#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

// libogc files
#include <ogc/machine/processor.h>
#include <ogc/ipc.h>
#include <ogc/cache.h>

static const u32 stage0[] = {
    0x4903468D,	/* ldr r1, =0x10100000; mov sp, r1; */
    0x49034788,	/* ldr r1, =entrypoint; blx r1; */
    /* Overwrite reserved handler to loop infinitely */
    0x49036209, /* ldr r1, =0xFFFF0014; str r1, [r1, #0x20]; */
    0x47080000,	/* bx r1 */
    0x10100000,	/* temporary stack */
    0x00000000, /* entrypoint */
    0xFFFF0014,	/* reserved handler */
};

static const u32 stage1[] = {
    0xE3A01536, // mov r1, #0x0D800000
    0xE5910064, // ldr r0, [r1, #0x64]
    0xE380013A, // orr r0, #0x8000000E
    0xE3800EDF, // orr r0, #0x00000DF0
    0xE5810064, // str r0, [r1, #0x64]
    0xE12FFF1E, // bx  lr
};

#define DISABLE_AHBPROT_PAYLOAD_SIZE (sizeof(disable_ahbprot_payload) / sizeof(disable_ahbprot_payload[0]))
#define AHBPROT_DISABLED			(*(vu32*)0xcd800064 == 0xFFFFFFFF)

bool is_dolphin()
{
    // /dev/dolphin will never exist in an official IOS
    s32 fd = IOS_Open("/dev/dolphin", 0);
    if (fd >= 0)
    {
        IOS_Close(fd);
        return true;
    }
    return false;
}

// time to exploit /dev/sha!
bool disable_ahbprot()
{
    if (AHBPROT_DISABLED || is_dolphin()) {
        return true; // AHBPROT is already disabled, likely via launching through HBC or the user is using Dolphin. Dolphin always has it disabled however :)
    }

    u32 *const mem1 = (u32 *)0x80000000;

    __attribute__((__aligned__(32)))
    ioctlv vectors[3] = {
        [1] = {
            .data = (void *)0xFFFE0028,
            .len  = 0,
        },

        [2] = {
            .data = mem1,
            .len  = 0x20,
        }
    };

    memcpy(mem1, stage0, sizeof(stage0));
    mem1[5] = (((u32)stage1) & ~0xC0000000);

    int ret = IOS_Ioctlv(0x10001, 0, 1, 2, vectors);
    if (ret < 0)
        return false;

    int tries = 1000;
    while (!AHBPROT_DISABLED) {
        usleep(1000);
        if (!tries--)
            return false;
    }

    return true;
}