/*
 * GameCube disc drive patch, the source of source/drivecodes.h.
 *
 * Loaded at 0x40d000 and entered once through the fw IRQ vector, it
 * re-inits the stock fw with its own IRQ handler installed. Two address
 * breakpoints are used: ADB0 on the LL command sender (descrambler setup and
 * sector offset) and ADB1 to skip a spin-up check that DVD-R media fails or,
 * in NR mode with nr_servo set, to apply the NR reader's servo parameters (which seem to be 
 * required on one of my drives..)
 *
 * NR mode forces "accept copy" (no lead-in/BCA reads), replaces the auth
 * step and keeps the copy protection flags clear. NR discs
 * use a descrambler key the retail hardware can't produce, so the drive is
 * kept on the standard key and CleanRip swaps the keys. CleanRip sets
 * patch_mode (DRIVE_PATCH_MODE_OFFSET) to 2 and nr_servo
 * (DRIVE_PATCH_SERVO_OFFSET) before uploading.
 *
 * Host commands handled by the patch:
 *   55 mm 00 00   set patch mode: 0 off, 1 DVD+-R backup, 2 NR disc
 *   EE s1 s2 00   set firmware command state and descrambler mode
 *                 (06 03 = disc ID needed, descrambler mode 3)
 *   32 00 00 00 oooooooo   multi-disc LBA offset
 *
 * Built by mkdrivecodes.py, which supplies the per-drive firmware addresses.
 */
	.text
	.global _start

_start:
	jsr	FwInitDrive
	mov	0x8000, a0				/* re-init firmware data segment */
	mov	FwData1_Source, d1
	mov	0x6e, d0
	jsr	FwMemcpy
	mov	0x806e, a0				/* clear firmware bss */
	sub	d0, d0
	mov	FwBSS_Size, d1
	jsr	FwMemset
	mov	irq_handler, a0			/* hook the IRQ vector */
	mov	a0, (0x804c)
	mov	FwBP1Addr, a0
	mov	a0, (0xfcd6)			/* ADB1 */
	mov	FwBP0Addr, a0
	mov	a0, (0xfcd2)			/* ADB0 */
	mov	4, d0
	movb	d0, (0xfcda)		/* only ADB0 enabled */
.ifdef NR_SUPPORT
	movbu	(patch_mode), d0	/* NR preset by the host: arm it */
	cmp	2, d0					/* before the firmware re-inits */
	bcs	1f
	jsr	set_mode
1:
.endif
	mov	FwReset, a0
	jmp	(a0)

irq_handler:
	mov	(0xfcda), d0
	and	1, d0
	beq	1f
	jmp	bp0_handler
1:	sub	d0, d0
	mov	0x40ec09, a0			/* DRE retry fix */
	movb	d0, (a0)
	mov	(0xfcda), d0
	and	2, d0
	beq	1f
	jmp	bp1_handler
1:	mov	FwDriveState, a0		/* lead-in found */
	mov	2, d0
	bset	d0, (a0)
.ifdef NR_SUPPORT
	movbu	(patch_mode), d0	/* NR: own disc ID step list */
	cmp	2, d0
	bcs	1f
	mov	nr_auth_list, a0
	mov	a0, (FwAuthList)
	mov	0x0c, d0				/* protection flags as XenoGC does */
	mov	d0, (FwErrByte)			/* them: authenticated, no failure */
								/* bits, no periodic re-check */
	mov	FwStartFlags, a0		/* "accept copy" for every start, */
	mov	FwAcceptCopyMask, d0	/* including the fw's own */
	bset	d0, (a0)			/* after reset: skips lead-in/BCA */
1:
.endif
	mov	(FwTimer2), d0			/* no idle spin-down */
	mov	d0, (FwIdleTimeout)
	mov	FwCDB0, a0				/* tag CDB to detect new commands */
	movbu	(0, a0), d0
	movb	d0, (cdb_backup)
	mov	0xaa, d0
	movb	d0, (a0)
	mov	(orig_handler), a0
	jsr	(a0)
	mov	FwCDB0, a0
	movbu	(0, a0), d0
	cmp	0xaa, d0
	bne	new_cmd
	movb	(cdb_backup), d0
	movb	d0, (a0)
	rts

new_cmd:
	cmp	0x32, d0
	beq	cmd_offset
	cmp	0xa8, d0
	beq	cmd_read
	cmp	0xab, d0
	beq	cmd_read
	cmp	0xe1, d0
	beq	cmd_read
	cmp	0xee, d0
	beq	cmd_status
	cmp	0x55, d0
	beq	cmd_mode
	rts

cmd_offset:					/* 32 xx xx xx oooooooo */
	mov	(4, a0), a1
	jsr	swap24
	mov	a1, (lba_offset)
	sub	d0, d0
	movb	d0, (0, a0)
	rts

cmd_read:					/* add multi-disc offset to reads */
	mov	a2, d1
	mov	(lba_offset), a2
	mov	(4, a0), a1
	jsr	swap24
	add	a2, a1
	jsr	swap24
	mov	a1, (4, a0)
	mov	d1, a2
	rts

swap24:
	mov	a1, (0xffbc)
	mov	(0xffbc), a1
	rts

cmd_mode:					/* 55 mm: set patch mode */
	movbu	(FwCDB0+1), d0
	jsr	set_mode
	bra	cmd_done

cmd_status:					/* EE s1 s2: set command state */
	movbu	(FwCDB0+1), d0
	movb	d0, (FwCmdState1)
	movbu	(FwCDB0+2), d0
	movb	d0, (FwCmdState2)
cmd_done:					/* turn it into a harmless E0 */
	mov	FwCDB0, a0
	mov	0xe0, d0
	movb	d0, (a0)
	sub	d0, d0
	movb	d0, (1, a0)
	movb	d0, (2, a0)
	movb	d0, (3, a0)
	rts

bp0_handler:
	movbu	(0xfcda), d0
	and	0xfffe, d0
	movb	d0, (0xfcda)
	movbu	(0xfc44), d0
	and	0xfffe, d0
	movb	d0, (0xfc44)
	mov	a3, a0
	add	4, a0
	movbu	(0x805b), d0
	cmp	1, d0
	bne	1f
	mov	(4, a3), a0
1:	mov	bp0_code, a1
	mov	a1, (32, a0)
	mov	(52, a0), a1
	cmp	FwOrgDiskCheckAddr, a1		/* BCA read: real disc, unpatch */
	bne	2f
	mov	0, d0
set_mode:
.ifdef NR_SUPPORT
	/* NR mode with nr_servo set uses ADB1 for the servo parameters instead
	   of the spin-up skip */
	mov	FwBP1Addr, a1
	cmp	2, d0
	bcs	1f
	movbu	(nr_servo), d1
	cmp	0, d1
	beq	1f
	mov	FwParamBPAddr, a1
1:	mov	a1, (0xfcd6)
.endif
	movbu	(0xfcda), d1
	and	0xfff7, d1
	cmp	0, d0
	beq	1f
	or	8, d1
1:	movb	d1, (0xfcda)
	mov	patch_mode, a1
	movb	d0, (a1)
2:	rts
	mov	1, d0
	bra	set_mode

.ifdef NR_SUPPORT
/*
 * Runs in place of the IRQ-disable call at the top of the LL command
 * sender; a0 = LL command buffer, d0/d1 are free.
 *  mode 1 (DVD+-R): standard DVD descrambling, +6 sector transfer offset
 *  mode 2 (NR):     standard DVD descrambling, GC sector layout so no offset
 *                   change; the host swaps in the NR keys
 */
bp0_code:					/* replaces jsr at FwBP0Addr */
	and	0xf7ff, psw
	mov	(a0), d1
	cmp	0x0608, d1			/* audio fix */
	bne	1f
	mov	2, d1
	movb	d1, (1, a0)
1:	movbu	(patch_mode), d0
	cmp	0, d0
	beq	9f
	mov	(a0), d1
	cmp	0xf000, d1			/* descrambler setup */
	bne	2f
	cmp	2, d0
	bcc	1f
	sub	d0, d0
	movb	d0, (9, a0)
	bra	9f
1:	movbu	(9, a0), d0
	and	0xe0, d0
	movb	d0, (9, a0)
	bra	9f
2:	cmp	6, d1				/* sector transfer offset */
	bne	9f
	cmp	2, d0
	bcc	9f
	mov	(6, a0), d1
	add	6, d1
	mov	d1, (6, a0)
9:	jmp	FwBP0Exit
.else
bp0_code:					/* replaces jsr at FwBP0Addr */
	and	0xf7ff, psw
	mov	(a0), d1
	cmp	0x0608, d1			/* audio fix */
	bne	1f
	mov	2, d1
	movb	d1, (1, a0)
1:	mov	(a0), d1
	cmp	0xf000, d1			/* descrambler setup */
	bne	1f
	mov	0, d0
	movbu	(patch_mode), d1
	cmp	0, d1
	beq	1f
	movb	d0, (9, a0)
1:	mov	(a0), d1
	cmp	6, d1				/* sector transfer offset */
	bne	2f
	mov	(6, a0), d1
	movbu	(patch_mode), d0
	cmp	0, d0
	beq	1f
	add	6, d1
1:	mov	d1, (6, a0)
2:	jmp	FwBP0Exit
.endif

bp1_handler:
	movbu	(0xfcda), d0
	and	0xfffd, d0
	movb	d0, (0xfcda)
	movbu	(0xfc44), d0
	and	0xfffe, d0
	movb	d0, (0xfc44)
	mov	a3, a0
	add	4, a0
	movbu	(0x805b), d0
	cmp	1, d0
	bne	1f
	mov	(4, a3), a0
1:	mov	bp1_code, a1
.ifdef NR_SUPPORT
	movbu	(patch_mode), d0
	cmp	2, d0
	bcs	1f
	movbu	(nr_servo), d0
	cmp	0, d0
	beq	1f
	mov	bp1_param, a1
1:
.endif
	mov	a1, (32, a0)
	rts

bp1_code:
	jmp	FwBP1Target

.ifdef NR_SUPPORT
/*
 * Runs in place of "mov 1,d2" in the servo parameter write routine, the
 * stack holds the parameter number at (2,a3) and its value at (3,a3), d1
 * the both of them. Sets the values the NR reader firmware uses for GC discs.
 */
bp1_param:
	movbu	(2, a3), d2
	cmp	0, d2
	bne	1f
	movbu	(3, a3), d2
	cmp	0xa0, d2
	bne	9f
	mov	0xc0, d2
	bra	8f
1:	cmp	1, d2
	bne	2f
	movbu	(3, a3), d2
	cmp	0x04, d2
	bne	9f
	mov	0x05, d2
	bra	8f
2:	cmp	91, d2
	bne	9f
	movbu	(3, a3), d2
	cmp	0x0c, d2
	bne	9f
	mov	0x05, d2
8:	movb	d2, (3, a3)
	mov	(2, a3), d1
9:	mov	1, d2
	jmp	FwParamBPExit

/*
 * Disc ID step for NR discs, run by the fw's step runner on the
 * disc ID read. The fw's step checks the control data for
 * "Nintendo Game Disk" (NR discs have "Nintendo Emulation Disk") and the
 * copy protection marks NR discs don't have, so run it for its setup and
 * then report the disc as authd.
 */
nr_auth:
	jsr	FwDiscAuth
	jsr	FwSetAuthOk
	sub	d0, d0
	rts

	.balign	4
nr_auth_list:
	.long	nr_auth
	.long	0
.endif

.ifdef NR_SUPPORT
	.balign	4
.else
	.org	0x1d0
.endif
orig_handler:	.long	FwIntHandler
patch_mode:	.byte	1			/* 0 off, 1 DVD+-R, 2 NR */
cdb_backup:	.byte	0
.ifdef NR_SUPPORT
nr_servo:	.byte	0			/* NR reader servo parameters, host preset */
.endif
.ifdef NR_SUPPORT
	.balign	4
.else
	.org	0x1e0
.endif
lba_offset:	.long	0
.ifdef NR_SUPPORT
	.balign	16
.else
	.org	0x1f0
.endif
