/***************************************************************************
 *
 * All modifications in this file to the original code are
 * (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
 *
 * for details see file COPYING in the DOSEMU distribution
 *
 *
 *  SIMX86 a Intel 80x86 cpu emulator
 *  Copyright (C) 1997,2001 Alberto Vignani, FIAT Research Center
 *				a.vignani@crf.it
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Additional copyright notes:
 *
 * 1. The kernel-level vm86 handling was taken out of the Linux kernel
 *  (linux/arch/i386/kernel/vm86.c). This code originaly was written by
 *  Linus Torvalds with later enhancements by Lutz Molgedey and Hans Lermen.
 *
 ***************************************************************************/

#ifndef _EMU86_EMU86_H
#define _EMU86_EMU86_H

#include <unistd.h>
#include <stdio.h>
#include "econfig.h"
#include <setjmp.h>
#include "emu.h"
#include "timers.h"
#include "pic.h"
#include "cpu-emu.h"
#include "syncpu.h"

#ifdef PROFILE
extern hitimer_t AddTime, SearchTime, ExecTime, CleanupTime;
extern hitimer_t GenTime, LinkTime;
#endif

#ifdef X86_JIT
#define CONFIG_CPUSIM (config.cpusim || (CEmuStat & CeS_INSTREMU))
#else
#define CONFIG_CPUSIM 1
#endif

/* octal digits in a byte: hhmm.mlll */
#define D_HO(b)	(((b)>>6)&3)
#define D_MO(b)	(((b)>>3)&7)
#define D_LO(b)	((b)&7)

#define ADDbfrm		0x00
#define ADDwfrm		0x01
#define ADDbtrm		0x02
#define ADDwtrm		0x03
#define ADDbia		0x04
#define ADDwia		0x05
#define PUSHes		0x06
#define POPes		0x07
#define ORbfrm		0x08
#define ORwfrm		0x09
#define ORbtrm		0x0a
#define ORwtrm		0x0b
#define ORbi		0x0c
#define ORwi		0x0d
#define PUSHcs		0x0e
#define TwoByteESC	0x0f

#define ADCbfrm		0x10
#define ADCwfrm		0x11
#define ADCbtrm		0x12
#define ADCwtrm		0x13
#define ADCbi		0x14
#define ADCwi		0x15
#define PUSHss		0x16
#define POPss		0x17
#define SBBbfrm		0x18
#define SBBwfrm		0x19
#define SBBbtrm		0x1a
#define SBBwtrm		0x1b
#define SBBbi		0x1c
#define SBBwi		0x1d
#define PUSHds		0x1e
#define POPds		0x1f

#define ANDbfrm		0x20
#define ANDwfrm		0x21
#define ANDbtrm		0x22
#define ANDwtrm		0x23
#define ANDbi		0x24
#define ANDwi		0x25
#define SEGes		0x26
#define DAA			0x27
#define SUBbfrm		0x28
#define SUBwfrm		0x29
#define SUBbtrm		0x2a
#define SUBwtrm		0x2b
#define SUBbi		0x2c
#define SUBwi		0x2d
#define SEGcs		0x2e
#define DAS			0x2f

#define XORbfrm		0x30
#define XORwfrm		0x31
#define XORbtrm		0x32
#define XORwtrm		0x33
#define XORbi		0x34
#define XORwi		0x35
#define SEGss		0x36
#define AAA			0x37
#define CMPbfrm		0x38
#define CMPwfrm		0x39
#define CMPbtrm		0x3a
#define CMPwtrm		0x3b
#define CMPbi		0x3c
#define CMPwi		0x3d
#define SEGds		0x3e
#define AAS			0x3f

#define INCax		0x40
#define INCcx		0x41
#define INCdx		0x42
#define INCbx		0x43
#define INCsp		0x44
#define INCbp		0x45
#define INCsi		0x46
#define INCdi		0x47
#define DECax		0x48
#define DECcx		0x49
#define DECdx		0x4a
#define DECbx		0x4b
#define DECsp		0x4c
#define DECbp		0x4d
#define DECsi		0x4e
#define DECdi		0x4f

#define PUSHax		0x50
#define PUSHcx		0x51
#define PUSHdx		0x52
#define PUSHbx		0x53
#define PUSHsp		0x54
#define PUSHbp		0x55
#define PUSHsi		0x56
#define PUSHdi		0x57
#define POPax		0x58
#define POPcx		0x59
#define POPdx		0x5a
#define POPbx		0x5b
#define POPsp		0x5c
#define POPbp		0x5d
#define POPsi		0x5e
#define POPdi		0x5f

#define PUSHA		0x60
#define POPA		0x61
#define BOUND		0x62
#define ARPL		0x63
#define SEGfs		0x64
#define SEGgs		0x65
#define OPERoverride	0x66
#define ADDRoverride	0x67
#define PUSHwi		0x68
#define IMULwrm		0x69
#define PUSHbi		0x6a
#define IMULbrm		0x6b
#define INSb		0x6c
#define INSw		0x6d
#define OUTSb		0x6e
#define OUTSw		0x6f

#define JO			0x70
#define JNO			0x71
#define JB_JNAE		0x72
#define JNB_JAE		0x73
#define JE_JZ		0x74
#define JNE_JNZ		0x75
#define JBE_JNA		0x76
#define JNBE_JA		0x77
#define JS			0x78
#define JNS			0x79
#define JP_JPE		0x7a
#define JNP_JPO		0x7b
#define JL_JNGE		0x7c
#define JNL_JGE		0x7d
#define JLE_JNG		0x7e
#define JNLE_JG		0x7f

#define IMMEDbrm	0x80u
#define IMMEDwrm	0x81u
#define IMMEDbrm2	0x82u
#define IMMEDisrm	0x83u
#define TESTbrm		0x84u
#define TESTwrm		0x85u
#define XCHGbrm		0x86u
#define XCHGwrm		0x87u
#define MOVbfrm		0x88u
#define MOVwfrm		0x89u
#define MOVbtrm		0x8au
#define MOVwtrm		0x8bu
#define MOVsrtrm	0x8cu
#define LEA		0x8du
#define MOVsrfrm	0x8eu
#define POPrm		0x8fu

#define NOP		0x90u
#define XCHGcx		0x91u
#define XCHGdx		0x92u
#define XCHGbx		0x93u
#define XCHGsp		0x94u
#define XCHGbp		0x95u
#define XCHGsi		0x96u
#define XCHGdi		0x97u
#define CBW		0x98u
#define CWD		0x99u
#define CALLl		0x9au
#define oWAIT		0x9bu
#define PUSHF		0x9cu
#define POPF		0x9du
#define SAHF		0x9eu
#define LAHF		0x9fu

#define MOVmal		0xa0u
#define MOVmax		0xa1u
#define MOValm		0xa2u
#define MOVaxm		0xa3u
#define MOVSb		0xa4u
#define MOVSw		0xa5u
#define CMPSb		0xa6u
#define CMPSw		0xa7u
#define	TESTbi		0xa8u
#define TESTwi		0xa9u
#define STOSb		0xaau
#define STOSw		0xabu
#define LODSb		0xacu
#define LODSw		0xadu
#define SCASb		0xaeu
#define SCASw		0xafu

#define MOVial		0xb0u
#define MOVicl		0xb1u
#define MOVidl		0xb2u
#define MOVibl		0xb3u
#define MOViah		0xb4u
#define MOVich		0xb5u
#define MOVidh		0xb6u
#define MOVibh		0xb7u
#define MOViax		0xb8u
#define MOVicx		0xb9u
#define MOVidx		0xbau
#define MOVibx		0xbbu
#define MOVisp		0xbcu
#define MOVibp		0xbdu
#define MOVisi		0xbeu
#define MOVidi		0xbfu

#define SHIFTbi		0xc0u
#define SHIFTwi		0xc1u
#define RETisp		0xc2u
#define RET		0xc3u
#define LES		0xc4u
#define LDS		0xc5u
#define MOVbirm		0xc6u
#define MOVwirm		0xc7u
#define ENTER		0xc8u
#define LEAVE		0xc9u
#define RETlisp		0xcau
#define RETl		0xcbu
#define INT3		0xccu
#define INT		0xcdu
#define INTO		0xceu
#define IRET		0xcfu

#define SHIFTb		0xd0u
#define SHIFTw		0xd1u
#define SHIFTbv		0xd2u
#define SHIFTwv		0xd3u
#define AAM		0xd4u
#define AAD		0xd5u
#define RESERVED1	0xd6u
#define XLAT		0xd7u
#define ESC0		0xd8u
#define ESC1		0xd9u
#define ESC2		0xdau
#define ESC3		0xdbu
#define ESC4		0xdcu
#define ESC5		0xddu
#define ESC6		0xdeu
#define ESC7		0xdfu

#define LOOPNZ_LOOPNE	0xe0u
#define LOOPZ_LOOPE	0xe1u
#define LOOP		0xe2u
#define JCXZ		0xe3u
#define INb		0xe4u
#define INw		0xe5u
#define OUTb		0xe6u
#define OUTw		0xe7u
#define CALLd		0xe8u
#define JMPd		0xe9u
#define JMPld		0xeau
#define JMPsid		0xebu
#define INvb		0xecu
#define INvw		0xedu
#define OUTvb		0xeeu
#define OUTvw		0xefu

#define LOCK		0xf0u
#define REPNE		0xf2u
#define REP		0xf3u
#define HLT		0xf4u
#define CMC		0xf5u
#define GRP1brm 	0xf6u
#define GRP1wrm 	0xf7u
#define CLC		0xf8u
#define STC		0xf9u
#define CLI		0xfau
#define STI		0xfbu
#define CLD		0xfcu
#define STD		0xfdu
#define GRP2brm		0xfeu
#define GRP2wrm		0xffu

// pseudo-opcodes for GRP2wrm
#define CALLi		((GRP2wrm<<8)|Ofs_DX)
#define CALLli		((GRP2wrm<<8)|Ofs_BX)
#define JMPi		((GRP2wrm<<8)|Ofs_SP)
#define JMPli		((GRP2wrm<<8)|Ofs_BP)

// 2 byte opcodes with TwoByteESC (0x0f) prefix
#define JOimmdisp	0x80u
#define JNOimmdisp	0x81u
#define JBimmdisp	0x82u
#define JNBimmdisp	0x83u
#define JZimmdisp	0x84u
#define JNZimmdisp	0x85u
#define JBEimmdisp	0x86u
#define JNBEimmdisp	0x87u
#define JSimmdisp	0x88u
#define JNSimmdisp	0x89u
#define JPimmdisp	0x8au
#define JNPimmdisp	0x8bu
#define JLimmdisp	0x8cu
#define JNLimmdisp	0x8du
#define JLEimmdisp	0x8eu
#define JNLEimmdisp	0x8fu

#define SETObrm		0x90u
#define SETNObrm	0x91u
#define SETBbrm		0x92u
#define SETNBbrm	0x93u
#define SETZbrm		0x94u
#define SETNZbrm	0x95u
#define SETBEbrm	0x96u
#define SETNBEbrm	0x97u
#define SETSbrm		0x98u
#define SETNSbrm	0x99u
#define SETPbrm		0x9au
#define SETNPbrm	0x9bu
#define SETLbrm		0x9cu
#define SETNLbrm	0x9du
#define SETLEbrm	0x9eu
#define SETNLEbrm	0x9fu

// FP opcodes
#define FADDm32r_sti	((ESC0<<3) | (0x0 & 0x38) >> 3)
#define FMULm32r_sti	((ESC0<<3) | (0x8 & 0x38) >> 3)
#define FCOMm32r_sti	((ESC0<<3) | (0x10 /*or 0xd0*/ & 0x38) >> 3)
#define FCOMPm32r_sti	((ESC0<<3) | (0x18 /*or 0xd8*/ & 0x38) >> 3)
#define FSUBm32r_sti	((ESC0<<3) | (0x20 /*or 0xe0*/ & 0x38) >> 3)
#define FSUBRm32r_sti	((ESC0<<3) | (0x28 /*or 0xe8*/ & 0x38) >> 3)
#define FDIVm32r_sti	((ESC0<<3) | (0x30 /*or 0xf0*/ & 0x38) >> 3)
#define FDIVRm32r_sti	((ESC0<<3) | (0x38 /*or 0xf8*/ & 0x38) >> 3)

#define FLDm32r_sti  	((ESC1<<3) | (0x0 & 0x38) >> 3)
#define FXCH        	((ESC1<<3) | (0xc8 & 0x38) >> 3)
#define FSTm32r_FNOP	((ESC1<<3) | (0x10 /*or 0xd0*/ & 0x38) >> 3)
#define FSTPm32r    	((ESC1<<3) | (0x18 & 0x38) >> 3)
#define FD9SLASH4 		((ESC1<<3) | (0x20 /*or 0xe0-0xe5*/ & 0x38) >> 3)
#define    FLDENV 		0x20
#define    FCHS 		0xe0
#define    FABS 		0xe1
#define    FTST 		0xe4
#define    FXAM 		0xe5
#define FLDCONST		((ESC1<<3) | (0xe8 /*or 0xe9-0xee*/ & 0x38) >> 3)
#define    FLDCW		0x28
#define    FLD1 		0xe8
#define    FLDL2T		0xe9
#define    FLDL2E		0xea
#define    FLDPI		0xeb
#define    FLDLG2		0xec
#define    FLDLN2		0xed
#define    FLDZ 		0xee
#define FD9SLASH6		((ESC1<<3) | (0x30 /*or 0xf0-0xf7*/ & 0x38) >> 3)
#define    FSTENV 		0x30
#define    F2XM1 		0xf0
#define    FYL2X 		0xf1
#define    FPTAN 		0xf2
#define    FPATAN 		0xf3
#define    FXTRACT 		0xf4
#define    FPREM1 		0xf5
#define    FDECSTP		0xf6
#define    FINCSTP		0xf7
#define FD9SLASH7		((ESC1<<3) | (0x38 /*or 0xf8-0xff*/ & 0x38) >> 3)
#define    FSTCW 		0x38
#define    FPREM 		0xf8
#define    FYL2XP1 		0xf9
#define    FSQRT 		0xfa
#define    FSINCOS 		0xfb
#define    FRNDINT 		0xfc
#define    FSCALE 		0xfd
#define    FSIN			0xfe
#define    FCOS			0xff

#define FADDm32i		((ESC2<<3) | (0x0 & 0x38) >> 3)
#define FMULm32i		((ESC2<<3) | (0x8 & 0x38) >> 3)
#define FICOMm32i		((ESC2<<3) | (0x10 & 0x38) >> 3)
#define FICOMPm32i		((ESC2<<3) | (0x18 & 0x38) >> 3)
#define FISUBm32i		((ESC2<<3) | (0x20 & 0x38) >> 3)
#define FISUBRm32i_FUCOMPPst1	((ESC2<<3) | (0x28 /*or 0xe9*/ & 0x38) >> 3)
#define FIDIVm32i		((ESC2<<3) | (0x30 & 0x38) >> 3)
#define FIDIVRm32i		((ESC2<<3) | (0x38 & 0x38) >> 3)

#define FILDm32i		((ESC3<<3) | (0x0 & 0x38) >> 3)
#define FISTm32i		((ESC3<<3) | (0x10 & 0x38) >> 3)
#define FISTPm32i		((ESC3<<3) | (0x18 & 0x38) >> 3)
#define FRSTORm94B_FINIT_FCLEX	((ESC3<<3) | (0x20 /*or 0xe3-2*/ & 0x38) >> 3)
#define FLDm80r  		((ESC3<<3) | (0x28 & 0x38) >> 3)
#define FSTPm80r  		((ESC3<<3) | (0x38 & 0x38) >> 3)

#define FADDm64r_tosti	((ESC4<<3) | (0x0 & 0x38) >> 3)
#define FMULm64r_tosti	((ESC4<<3) | (0x8 & 0x38) >> 3)
#define FCOMm64r     	((ESC4<<3) | (0x10 & 0x38) >> 3)
#define FCOMPm64r    	((ESC4<<3) | (0x18 & 0x38) >> 3)
#define FSUBm64r_FSUBRfromsti ((ESC4<<3) | (0x20 /*or 0xe0*/ & 0x38) >> 3)
#define FSUBRm64r_FSUBfromsti ((ESC4<<3) | (0x28 /*or 0xe8*/ & 0x38) >> 3)
#define FDIVm64r_FDIVRtosti   ((ESC4<<3) | (0x30 /*or 0xf0*/ & 0x38) >> 3)
#define FDIVRm64r_FDIVtosti   ((ESC4<<3) | (0x38 /*or 0xf8*/ & 0x38) >> 3)

#define FLDm64r_FFREE         ((ESC5<<3) | (0x0 /*or 0xc0*/ & 0x38) >> 3)
#define FSTm64r_sti           ((ESC5<<3) | (0x10 /*or 0xd0+i*/ & 0x38) >> 3)
#define FSTPm64r_sti          ((ESC5<<3) | (0x18 /*or 0xd8+i*/ & 0x38) >> 3)
#define FUCOMsti              ((ESC5<<3) | (0x20 /*or 0xe0+i*/ & 0x38) >> 3)
#define FUCOMPsti             ((ESC5<<3) | (0x28 /*or 0xe8+i*/ & 0x38) >> 3)
#define FSAVEm94B             ((ESC5<<3) | (0x30 & 0x38) >> 3)
#define FSTSWm16i             ((ESC5<<3) | (0x38 & 0x38) >> 3)

#define FADDm16i_tostipop     ((ESC6<<3) | (0x0 & 0x38) >> 3)
#define FMULm16i_tostipop     ((ESC6<<3) | (0x8 & 0x38) >> 3)
#define FICOMm16i             ((ESC6<<3) | (0x10 & 0x38) >> 3)
#define FICOMPm16i_FCOMPPst1  ((ESC6<<3) | (0x18 /*or 0xd9*/ & 0x38) >> 3)
#define FISUBm16i_FSUBRPfromsti ((ESC6<<3) | (0x20 /*or 0xe0*/ & 0x38) >> 3)
#define FISUBRm16i_FSUBPfromsti ((ESC6<<3) | (0x28 /*or 0xe8*/ & 0x38) >> 3)
#define FIDIVm16i_FDIVRPtosti ((ESC6<<3) | (0x30 /*or 0xf0*/ & 0x38) >> 3)
#define FIDIVRm16i_FDIVPtosti ((ESC6<<3) | (0x38 /*or 0xf8*/ & 0x38) >> 3)

#define FILDm16i		((ESC7<<3) | (0x0 & 0x38) >> 3)
#define FISTm16i		((ESC7<<3) | (0x10 & 0x38) >> 3)
#define FISTPm16i		((ESC7<<3) | (0x18 & 0x38) >> 3)
#define FBLDm80dec_FSTSWax		((ESC7<<3) | (0x20 /*or 0xe0*/ & 0x38) >> 3)
#define FILDm64i		((ESC7<<3) | (0x28 & 0x38) >> 3)
#define FBSTPm80dec		((ESC7<<3) | (0x30 & 0x38) >> 3)
#define FISTPm64i		((ESC7<<3) | (0x38 & 0x38) >> 3)

/*
 *   control registers
 */
#define CR0_PE         0x00000001
#define CR0_MP         0x00000002
#define CR0_EM         0x00000004
#define CR0_TS         0x00000008
#define CR0_ET         0x00000010
#define CR0_NE         0x00000020
#define CR0_WP         0x00010000
#define CR0_AM         0x00040000
#define CR0_NW         0x20000000
#define CR0_CD         0x40000000
#define CR0_PG         0x80000000
#define CR0_RESERVED   0x1ffaffc0
#define CR0_NOT_IMP    0x00000000

#define CR3_PWT        0x00000008
#define CR3_PCD        0x00000010
#define CR3_PDB_SHIFT  12
#define CR3_PDB_MASK   0xfffff000
#define CR3_RESERVED   0x00000fe7
#define CR3_NOT_IMP    0x00000018

#define CR4_VME        0x00000001
#define CR4_PVI        0x00000002
#define CR4_TSD        0x00000004
#define CR4_DE         0x00000008
#define CR4_PSE        0x00000010
#define CR4_PAE        0x00000020
#define CR4_MCE        0x00000040
#define CR4_PGE        0x00000080
#define CR4_PCE        0x00000100
#define CR4_OSFXSR     0x00000200
#define CR4_RESERVED   0xfffffc00
#define CR4_NOT_IMP    0x0000012e

#define EFLAGS_CF      0x00000001
#define EFLAGS_SET     0x00000002
#define EFLAGS_PF      0x00000004
#define EFLAGS_AF      0x00000010
#define EFLAGS_ZF      0x00000040
#define EFLAGS_SF      0x00000080
#define EFLAGS_TF      0x00000100
#define EFLAGS_IF      0x00000200
#define EFLAGS_DF      0x00000400
#define EFLAGS_OF      0x00000800
#define EFLAGS_IOPL    0x00003000
#define EFLAGS_NT      0x00004000
#define EFLAGS_RF      0x00010000
#define EFLAGS_VM      0x00020000
#define EFLAGS_AC      0x00040000
#define EFLAGS_VIF     0x00080000
#define EFLAGS_VIP     0x00100000
#define EFLAGS_ID      0x00200000

#define EFLAGS_ALL       (0x003f7fd5)
#define EFLAGS_NORFVM    (EFLAGS_ALL & ~(EFLAGS_RF|EFLAGS_VM))

#define EFLAGS_REAL_32   (EFLAGS_ALL & ~(EFLAGS_VIP|EFLAGS_VIF|EFLAGS_VM))
#define EFLAGS_V8086_32  (EFLAGS_ALL & ~(EFLAGS_VIP|EFLAGS_VIF|EFLAGS_VM|EFLAGS_IOPL))
#define EFLAGS_ALL_16    LOWORD(EFLAGS_ALL)
#define EFLAGS_REAL_16   LOWORD(EFLAGS_REAL_32)
#define EFLAGS_V8086_16  LOWORD(EFLAGS_V8086_32)
#define EFLAGS_CC        0x000008d5

#define EFLAGS_IOPL_SHIFT 12
#define EFLAGS_IOPL_MASK  0x3000

/* www.sandpile.org/80x86/cpuid.shtml */
#define CPUID_FEATURE_FPU    0x00000001
#define CPUID_FEATURE_VME    0x00000002
#define CPUID_FEATURE_DBGE   0x00000004
#define CPUID_FEATURE_PGSZE  0X00000008
#define CPUID_FEATURE_TSC    0x00000010
#define CPUID_FEATURE_MSR    0x00000020
#define CPUID_FEATURE_PAE    0x00000040
#define CPUID_FEATURE_MCK    0x00000080
#define CPUID_FEATURE_CPMX   0x00000100
#define CPUID_FEATURE_APIC   0x00000200
#define CPUID_FEATURE_RSVD1  0x00000400
#define CPUID_FEATURE_SEP    0x00000800
#define CPUID_FEATURE_MTTR   0x00001000
#define CPUID_FEATURE_PGE    0x00002000
#define CPUID_FEATURE_MCA    0x00004000
#define CPUID_FEATURE_CMOV   0x00008000

#define CPUID_FEATURE_PAT    0x00010000
#define CPUID_FEATURE_36PG   0x00020000
#define CPUID_FEATURE_RSVD2  0x00040000 /* pentium II, mendocino? */
#define CPUID_FEATURE_RSVD3  0x00080000

#define CPUID_FEATURE_RSVD4  0x00100000
#define CPUID_FEATURE_RSVD5  0x00200000
#define CPUID_FEATURE_RSVD6  0x00400000
#define CPUID_FEATURE_MMX    0x00800000
#define CPUID_FEATURE_FXSAVE 0x01000000
#define CPUID_FEATURE_PIII   0x02000000  /* pentium III, unknown */
#define CPUID_FEATURE_RSVD   0xfc780400

#define MSR_TSC              0x00000010
#define MSR_MTRR_CAP         0x000000fe
#define MSR_SYSENTER_CS      0x00000174
#define MSR_SYSENTER_ESP     0x00000175
#define MSR_SYSENTER_EIP     0x00000176

#define IS_CF_SET		((EFLAGS & EFLAGS_CF)!=0)
#define IS_AF_SET		((EFLAGS & EFLAGS_AF)!=0)
#define IS_ZF_SET		((EFLAGS & EFLAGS_ZF)!=0)
#define IS_SF_SET		((EFLAGS & EFLAGS_SF)!=0)
#define IS_OF_SET		((EFLAGS & EFLAGS_OF)!=0)
#define IS_PF_SET		((EFLAGS & EFLAGS_PF)!=0)

/*
 *  ID VIP VIF AC VM RF 0 NT IOPL OF DF IF TF SF ZF 0 AF 0 PF 1 CF
 *                                 1  1  0  1  1  1 0  1 0  1 0  1
 */
#define SAFE_MASK	(EFLAGS_OF|EFLAGS_DF|EFLAGS_TF|EFLAGS_SF| \
			 EFLAGS_ZF|EFLAGS_AF|EFLAGS_PF|EFLAGS_CF| /* 0xDD5 */ \
			 (eTSSMASK & ~IOPL_MASK))
#define notSAFE_MASK	(~SAFE_MASK&0x3fffff)
#define RETURN_MASK	((0xFFF&~EFLAGS_IF) | eTSSMASK)

#define REALMODE()		((TheCPU.cr[0] & CR0_PE)==0)
#define V86MODE()		((TheCPU.eflags&EFLAGS_VM)!=0)
#define PROTMODE()		(!REALMODE() && !V86MODE())
#define REALADDR()		(TheCPU.mode & MREALA)

#if 1
/*
 * CPL is 0 in 'real' realmode, 3 (but can be less) in VM86 mode
 * in PM it depends on the CS DPL
 *
 * if CPL<=IOPL the following insns DO NOT trap:
 *	IN, INS, OUT, OUTS, CLI, STI
 * only POPF and IRET can change IOPL iff CPL==0
 * only POPF can change IF iff CPL<=IOPL
 */
//#define CPL	(REALMODE()? 0:(V86MODE()? 3:(TheCPU.cs&3)))
#define CPL	(V86MODE()? 3:(TheCPU.cs&3))
#define IOPL	((EFLAGS & EFLAGS_IOPL_MASK) >> EFLAGS_IOPL_SHIFT)
#else	/* simple */
#define CPL	3
#define IOPL	0
#endif

/////////////////////////////////////////////////////////////////////////////
/*
 * INT 00 C  - CPU-generated - DIVIDE ERROR
 * INT 01 C  - CPU-generated - SINGLE STEP
 * INT 02 C  - external hardware - NON-MASKABLE INTERRUPT
 * INT 03 C  - CPU-generated - BREAKPOINT
 * INT 04 C  - CPU-generated - INTO DETECTED OVERFLOW
 * INT 05 C  - CPU-generated (80186+) - BOUND RANGE EXCEEDED
 * INT 06 C  - CPU-generated (80286+) - INVALID OPCODE
 * INT 07 C  - CPU-generated (80286+) - PROCESSOR EXTENSION NOT AVAILABLE
 * INT 08 C  - CPU-generated (80286+) - DOUBLE EXCEPTION DETECTED
 * INT 09 C  - CPU-generated (80286,80386) - PROCESSOR EXTENSION PROTECTION ERROR
 * INT 0A CP - CPU-generated (80286+) - INVALID TASK STATE SEGMENT
 * INT 0B CP - CPU-generated (80286+) - SEGMENT NOT PRESENT
 * INT 0C C  - CPU-generated (80286+) - STACK FAULT
 * INT 0D C  - CPU-generated (80286+) - GENERAL PROTECTION VIOLATION
 * INT 0E C  - CPU-generated (80386+ native mode) - PAGE FAULT
 * INT 10 C  - CPU-generated (80286+) - COPROCESSOR ERROR
 * INT 11    - CPU-generated (80486+) - ALIGNMENT CHECK
 * INT 12    - CPU-generated (Pentium) - MACHINE CHECK EXCEPTION
 */
#define EXCP00_DIVZ	1
#define EXCP01_SSTP	2
#define EXCP02_NMI	3
#define EXCP03_INT3	4
#define EXCP04_INTO	5
#define EXCP05_BOUND	6
#define EXCP06_ILLOP	7
#define EXCP07_PREX	8
#define EXCP08_DBLE	9
#define EXCP09_XERR	10
#define EXCP0A_TSS	11
#define EXCP0B_NOSEG	12
#define EXCP0C_STACK	13
#define EXCP0D_GPF	14
#define EXCP0E_PAGE	15
#define EXCP10_COPR	17
#define EXCP11_ALGN	18
#define EXCP12_MCHK	19

#define EXCP_GOBACK	64
#define EXCP_SIGNAL	65
#define EXCP_PICSIGNAL	66
#define EXCP_STISIGNAL	67
#define EXCP_MODESWITCH	68

/////////////////////////////////////////////////////////////////////////////

#ifndef min
#define min(a,b)	((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b)	((a)>(b)?(a):(b))
#endif
#ifndef PAGE_SHIFT
#define PAGE_SHIFT		12
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE		(1UL << PAGE_SHIFT)
#endif
#ifndef PAGE_MASK
#define PAGE_MASK		(~(PAGE_SIZE-1))
#endif

/////////////////////////////////////////////////////////////////////////////
//
extern unsigned int return_addr;
extern jmp_buf jmp_env;
extern int in_vm86_emu, in_dpmi_emu;
extern unsigned long eTSSMASK;
extern int Running;		/* into interpreter loop */
extern unsigned int mMaxMem;
extern int UseLinker;
extern int PageFaults;

extern volatile int CEmuStat;
extern volatile int InCompiledCode;
//
unsigned char *do_hwint(int mode, int intno);
unsigned int Interp86(unsigned int PC, int mode);
//
int _ModRM(unsigned char opc, unsigned int PC, int mode);
#define ModRM(o, p, m) ({ \
    int __l = _ModRM(o, p, m); \
    if (REG1 == -1) { \
        CODE_FLUSH(); \
        goto illegal_op; \
    } \
    if (CONFIG_CPUSIM && V86MODE() && !((m) & (ADDR16 | MLEA)) && TR1.d > 0xffff) { \
        CODE_FLUSH(); \
        goto not_permitted; \
    } \
    __l; \
})
int ModRMSim(unsigned int PC, int mode);
int ModGetReg1(unsigned int PC, int mode);
//
char *e_emu_disasm(unsigned char *org, int is32, unsigned int refseg);
char *e_print_regs(void);
char *e_print_scp_regs(cpuctx_t *scp, int pmode);
const char *e_trace_fp(void);
void GCPrint(unsigned char *cp, unsigned char *cbase, int len);
char *showreg(signed char r);
char *showmode(unsigned int m);
void Cpu2Reg (void);
int e_debug_check(unsigned int PC);
int e_mprotect(unsigned int addr, size_t len);
int e_querymprotrange(unsigned int addr, size_t len);
int e_markpage(unsigned int addr, size_t len);
int e_unmarkpage(unsigned int addr, size_t len);
int e_querymark(unsigned int addr, size_t len);
int e_querymark_all(unsigned int addr, size_t len);
void m_munprotect(unsigned int addr, unsigned int len, unsigned char *eip);
void mprot_init(void);
void mprot_end(void);
void InitGenCodeBuf(void);
void *AllocGenCodeBuf(size_t size);
void FreeGenCodeBuf(void *ptr);
//
void CollectStat(void);
//
/////////////////////////////////////////////////////////////////////////////
#ifdef HOST_ARCH_X86
int e_handle_pagefault(dosaddr_t addr, unsigned err, sigcontext_t *scp);
int e_handle_fault(sigcontext_t *scp);
void init_emu_npu_x86(void);
#endif
void init_emu_npu(void);

void e_VgaMovs(unsigned char **rdi, unsigned char **rsi, unsigned int rep,
	       int dp, unsigned int access);

#endif // _EMU86_EMU86_H
