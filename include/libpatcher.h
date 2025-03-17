// from title_manager
#include <gccore.h>

bool patch_memory_range(u32 *start, u32 *end, const u16 original_patch[],
                        const u16 new_patch[], u32 patch_size);

bool patch_ios_range(const u16 original_patch[], const u16 new_patch[],
                     u32 patch_size);

// Applies specific patches.
bool patch_ahbprot_reset_for_ver(s32 ios_version);
bool patch_ahbprot_reset();

// These functions expect AHBPROT has already been disabled via the above.
bool patch_isfs_permissions();
bool patch_es_identify();
bool patch_ios_verify();

// Applies all patches.
bool apply_patches();

bool is_dolphin();