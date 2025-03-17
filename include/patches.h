// from title_manager
#include <ogcsys.h>

// This patch allows us to read tickets/TMDs/so forth.
static const u16 isfs_permissions_old[] = {0x428B, 0xD001, 0x2566};
static const u16 isfs_permissions_patch[] = {0x428B, 0xE001, 0x2566};

// This patch is used to allow us to identify regardless of our UID.
// We patch the start of this in order to be IOS-agnostic, as immediately
// following is a branch whose address is not guaranteed to always be the same.
// For this reason, we may also patch ES_DiVerifyWithTicketView's condition
// within the main ES Ioctlv handler.
// (No issue there. It cannot hurt anything.)
static const u16 es_identify_old[] = {
    0x68cc, // ldr r4, [r1, #0xc]
    0x69a6, // ldr r6, [r4, #0x18]
    0x6868, // ldr r0, [r5, #0x4] ; context->UID, 3 seems to be DI
    0x2803  // cmp r0, #0x3
};
static const u16 es_identify_patch[] = {
    0x68cc, // ldr r4, [r1, #0xc]
    0x69a6, // ldr r6, [r4, #0x18]
    0x2003, // mov r0, #0x3 ; if you can't beat them, set yourself to them(?)
    0x2803  // cmp r0, #0x3
};

// This patch allows us to gain access to the AHBPROT register.
static const u16 ticket_check_old[] = {
    0x685B,         // ldr r3,[r3,#4] ; get TMD pointer
    0x22EC, 0x0052, // movls r2, 0x1D8
    0x189B,         // adds r3, r3, r2; add offset of access rights field in TMD
    0x681B,         // ldr r3, [r3]   ; load access rights (haxxme!)
    0x4698,         // mov r8, r3  ; store it for the DVD video bitcheck later
    0x07DB          // lsls r3, r3, #31; check AHBPROT bit
};
static const u16 ticket_check_patch[] = {
    0x685B,         // ldr r3,[r3,#4] ; get TMD pointer
    0x22EC, 0x0052, // movls r2, 0x1D8
    0x189B,         // adds r3, r3, r2; add offset of access rights field in TMD
    0x23FF,         // li r3, 0xFF  ; <--- 0xFF gives us all access bits
    0x4698,         // mov r8, r3  ; store it for the DVD video bitcheck later
    0x07DB          // lsls r3, r3, #31; check AHBPROT bit
};

// This patch returns success to all signatures.
static const u16 ios_verify_old[] = {
    0xb5f0, // push { r4, r5, r6, r7, lr }
    0x4657, // mov r7, r10
    0x464e, // mov r6, r9
    0x4645, // mov r5, r8
    0xb4e0, // push { r5, r6, r7 }
    0xb083, // sub sp, #0xc
    0x2400  // mov r4, #0x0
};
static const u16 ios_verify_patch[] = {
    0x2000, // mov r0, #0x0
    0x4770, // bx lr
    0xb000, // nop
    0xb000, // nop
    0xb000, // nop
    0xb000, // nop
    0xb000  // nop
};

static const u16   delete_check_old[] = { 0xD800, 0x4A04 };
static const u16 delete_check_patch[] = { 0xE000, 0x4A04 };

// If a new IOS patch is added, please update accordingly.
#define ISFS_PERMISSIONS_SIZE sizeof(isfs_permissions_patch)
#define ES_IDENTIFY_SIZE sizeof(es_identify_patch)
#define IOS_VERIFY_SIZE sizeof(ios_verify_patch)
#define TICKET_CHECK_SIZE sizeof(ticket_check_patch)
#define DELETE_CHECK_SIZE sizeof(delete_check_patch)