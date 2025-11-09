#define AHBPROT_DISABLED			(*(vu32*)0xcd800064 == 0xFFFFFFFF)

bool disable_ahbprot();