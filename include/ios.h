#ifndef _IOS_H_
#define _IOS_H_

#define AHBPROT_DISABLED			(*(vu32*)0xcd800064 == 0xFFFFFFFF)
bool disable_ahbprot();
bool is_dolphin();
#endif
