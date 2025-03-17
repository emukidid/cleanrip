// from title_manager
#include <gccore.h>
#include <ogc/machine/processor.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "patches.h"

#define HW_AHBPROT 0x0d800064
#define MEM2_PROT 0x0d8b420a

#define AHBPROT_DISABLED (read32(HW_AHBPROT) != 0)
#define IOS_MEMORY_START (void *)0x933E0000
#define ES_MEMORY_START (void *)0x939F0000
#define IOS_MEMORY_END (void *)0x94000000

bool checked_dolphin = false;
bool in_dolphin = false;

/*
 * Within Dolphin, we have no IOS to patch.
 * Additionally, many patches can cause Dolphin to fail.
 */
bool is_dolphin() {
    if (!checked_dolphin) {
        int fd = IOS_Open("/dev/dolphin", 0);
        if (fd >= 0) {
            IOS_Close(fd);
            in_dolphin = true;
        } else {
            fd = IOS_Open("/dev/sha", 0);
            if (fd == IPC_ENOENT) { // *
                in_dolphin = true;
            } else if (fd >= 0) {
                IOS_Close(fd);
            }
        }

        checked_dolphin = true;
    }

    return in_dolphin;
}

void disable_memory_protections() { write16(MEM2_PROT, 2); }

bool patch_memory_range(u16 *start, u16 *end, const u16 original_patch[],
                        const u16 new_patch[], u32 patch_size) {
    bool patched = false;

    for (u16 *patchme = start; patchme < end; ++patchme) {
        if (memcmp(patchme, original_patch, patch_size) == 0) {
            // Copy our new patch over the existing, and flush.
            memcpy(patchme, new_patch, patch_size);
            DCFlushRange(patchme, patch_size);

            // While this realistically won't do anything for some parts,
            // it's worth a try...
            // ICInvalidateRange(patchme, patch_size);

            patched = true;
        }
    }

    return patched;
}

bool patch_ios_range(const u16 original_patch[], const u16 new_patch[],
                     u32 patch_size) {
    // Consider our changes successful under Dolphin.
    if (is_dolphin()) {
        return true;
    }

    return patch_memory_range(IOS_MEMORY_START, IOS_MEMORY_END, original_patch,
                              new_patch, patch_size);
}

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

bool do_sha_exploit(void) {
    if (is_dolphin()) // We have no ARM core
        return true;

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

bool patch_ahbprot_reset_for_ver(s32 ios_version) {
    // Under Dolphin, we do not need to disable AHBPROT.
    if (is_dolphin()) {
        return true;
    }

    // This is a really uncanny way to go about using the exploit.
    if (!AHBPROT_DISABLED && !do_sha_exploit()) {
        printf("/dev/sha exploit failed!\n");
        return false;
    }

    // We'll need to disable MEM2 protections in order to write over IOS.
    disable_memory_protections();

    // Attempt to patch IOS.
    bool patched = patch_ios_range(ticket_check_old, ticket_check_patch,
                                   TICKET_CHECK_SIZE);
    if (!patched) {
        printf("unable to find and patch ES memory!\n");
        return false;
    }

    s32 ios_result = IOS_ReloadIOS(ios_version);
    if (ios_result < 0) {
        printf("unable to reload IOS version! (error %d)\n", ios_result);
        return false;
    }

    // Keep memory protections disabled.
    disable_memory_protections();

    if (AHBPROT_DISABLED) {
        return true;
    } else {
        printf("unable to preserve AHBPROT after IOS reload!\n");
        return false;
    }
}

bool patch_ahbprot_reset() {
    s32 current_ios = IOS_GetVersion();
    if (current_ios < 0) {
        printf("unable to get current IOS version! (error %d)\n", current_ios);
        return false;
    }

    return patch_ahbprot_reset_for_ver(current_ios);
}

bool patch_isfs_permissions() {
    return patch_ios_range(isfs_permissions_old, isfs_permissions_patch,
                           ISFS_PERMISSIONS_SIZE);
}

bool patch_es_identify() {
    return patch_ios_range(es_identify_old, es_identify_patch,
                           ES_IDENTIFY_SIZE);
}

bool patch_ios_verify() {
    return patch_ios_range(ios_verify_old, ios_verify_patch, IOS_VERIFY_SIZE);
}

bool patch_es_delete_check() {
    return patch_ios_range(delete_check_old, delete_check_patch, DELETE_CHECK_SIZE);
}

bool apply_patches() {
    bool ahbprot_fix = patch_ahbprot_reset();
    if (!ahbprot_fix) {
        // patch_ahbprot_reset should log its own errors.
        return false;
    }
/*
    if (!patch_isfs_permissions()) {
        printf("unable to find and patch ISFS permissions!\n");
        return false;
    }
*/
    if (!patch_ios_verify()) {
        printf("unable to find and patch IOSC_VerifyPublicKeySign!\n");
        return false;
    }

    if (!patch_es_identify()) {
        printf("unable to find and patch ES_Identify!\n");
        return false;
    }

    if (!patch_es_delete_check()) {
        printf("unable to find & patch ES title delete check!\n");
        return false;
    }
    return true;
}