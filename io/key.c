#ifndef lint	/* .../sys/psn/io/key.c */
#define _AC_NAME key_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., All Rights Reserved.  {Apple version 2.10 90/03/22 18:38:34}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.10 of key.c on 90/03/22 18:38:34";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)key.c	*/
/*
 *
 */

#include <sys/param.h>
#include <sys/fdb.h>
#include <sys/key.h>
#include <sys/debug.h>


/*
 *	This is the max number of keyboards supported. It must remain 1 until
 *	someone solves the problem of how to associate a particular keyboard
 *	with a particular screen.
 */

#define NDEVICES	1


extern int nulldev();

typedef int (*procedure_t)();

short key_r0[NDEVICES], key_r1[NDEVICES];	/* keyboard register shadows */
static unsigned short key_buff[NDEVICES];	/* buffer for fdb IO */
static char 	*key_save;			/* pointer to asci strings */
static char 	key_buffer[NDEVICES*8];		/* last key sequence */
static int 	key_length[NDEVICES];		/* it's length */
static procedure_t 	key_call[NDEVICES];	/* higher level interrupt
						   service routine */
static unsigned char key_exists_count[NDEVICES];/* existance tests count */
static char 	key_last[NDEVICES];		/* the last keycode received */
static char 	key_down[NDEVICES];		/* there is currently a key down
						   (and a candidate for
						   repeating) */
static unsigned short key_control[NDEVICES];	/* The current state of the
						   keyboard modifier keys
						   (shift, apple etc) */
static char 	key_defwait[NDEVICES];		/* How long to wait before
						   repeating */
static char 	key_defgap[NDEVICES];		/* How long between repeated
						   characters */
static int	key_mode[NDEVICES];		/* the keyboard opoerating mode
						   (ascii/raw/mac) */
static int	key_state[NDEVICES];		/* the device state, used for
						   the lower level fdb FSM */
static int	key_opened[NDEVICES];		/* the keyboard is open */
static int	key_keypad[NDEVICES];		/* the keyboard is in keypad
						   mode */

unsigned char kmapData[NDEVICES][128] = {
0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
0x30,0x31,0x32,0x33,0x34,0x35,0x3b,0x37,0x38,0x39,0x3a,0x7b,0x7c,0x7d,0x7e,0x3f,
0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x3c,0x3d,0x3e,0x36,0x7f
};

unsigned char transData[NDEVICES][TRANSDATASIZE] = {
0x00,0x00,0x00,0x00,0x01,0x00,0x02,0x02,0x01,0x00,0x03,0x06,0x04,0x04,
0x05,0x05,0x04,0x04,0x07,0x07,0x08,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x01,0x00,0x01,0x00,0x01,0x00,0x01,0x00,
0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x03,0x06,0x04,0x04,
0x05,0x05,0x04,0x04,0x03,0x06,0x04,0x04,0x05,0x05,0x04,0x04,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,
0x04,0x04,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
0x07,0x07,0x07,0x07,0x07,0x07,0x00,0x09,0x61,0x73,0x64,0x66,0x68,0x67,
0x7a,0x78,0x63,0x76,0x00,0x62,0x71,0x77,0x65,0x72,0x79,0x74,0x31,0x32,
0x33,0x34,0x36,0x35,0x3d,0x39,0x37,0x2d,0x38,0x30,0x5d,0x6f,0x75,0x5b,
0x69,0x70,0x0d,0x6c,0x6a,0x27,0x6b,0x3b,0x5c,0x2c,0x2f,0x6e,0x6d,0x2e,
0x09,0x20,0x60,0x08,0x03,0x1b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x2e,0x1d,0x2a,0x00,0x2b,0x1c,0x1b,0x1f,0x00,0x00,0x2f,
0x03,0x1e,0x2d,0x00,0x00,0x3d,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
0x00,0x38,0x39,0x00,0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x05,0x01,0x0b,0x7f,
0x10,0x04,0x10,0x0c,0x10,0x1c,0x1d,0x1f,0x1e,0x00,0x41,0x53,0x44,0x46,
0x48,0x47,0x5a,0x58,0x43,0x56,0x00,0x42,0x51,0x57,0x45,0x52,0x59,0x54,
0x21,0x40,0x23,0x24,0x5e,0x25,0x2b,0x28,0x26,0x5f,0x2a,0x29,0x7d,0x4f,
0x55,0x7b,0x49,0x50,0x0d,0x4c,0x4a,0x22,0x4b,0x3a,0x7c,0x3c,0x3f,0x4e,
0x4d,0x3e,0x09,0x20,0x7e,0x08,0x03,0x1b,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x2e,0x2a,0x2a,0x00,0x2b,0x2b,0x1b,0x3d,0x00,
0x00,0x2f,0x03,0x2f,0x2d,0x00,0x00,0x3d,0x30,0x31,0x32,0x33,0x34,0x35,
0x36,0x37,0x00,0x38,0x39,0x00,0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,
0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x05,0x01,
0x0b,0x7f,0x10,0x04,0x10,0x0c,0x10,0x1c,0x1d,0x1f,0x1e,0x00,0x41,0x53,
0x44,0x46,0x48,0x47,0x5a,0x58,0x43,0x56,0x00,0x42,0x51,0x57,0x45,0x52,
0x59,0x54,0x31,0x32,0x33,0x34,0x36,0x35,0x3d,0x39,0x37,0x2d,0x38,0x30,
0x5d,0x4f,0x55,0x5b,0x49,0x50,0x0d,0x4c,0x4a,0x27,0x4b,0x3b,0x5c,0x2c,
0x2f,0x4e,0x4d,0x2e,0x09,0x20,0x60,0x08,0x03,0x1b,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2e,0x1d,0x2a,0x00,0x2b,0x1c,0x1b,
0x1f,0x00,0x00,0x2f,0x03,0x1e,0x2d,0x00,0x00,0x3d,0x30,0x31,0x32,0x33,
0x34,0x35,0x36,0x37,0x00,0x38,0x39,0x00,0x00,0x00,0x10,0x10,0x10,0x10,
0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
0x05,0x01,0x0b,0x7f,0x10,0x04,0x10,0x0c,0x10,0x1c,0x1d,0x1f,0x1e,0x00,
0x8c,0xa7,0xb6,0xc4,0xfa,0xa9,0xbd,0xc5,0x8d,0xc3,0x00,0xba,0xcf,0xb7,
0x00,0xa8,0xb4,0xa0,0xc1,0xaa,0xa3,0xa2,0xa4,0xb0,0xad,0xbb,0xa6,0xd0,
0xa5,0xbc,0xd4,0xbf,0x00,0xd2,0x00,0xb9,0x0d,0xc2,0xc6,0xbe,0xfb,0xc9,
0xc7,0xb2,0xd6,0x00,0xb5,0xb3,0x09,0xca,0x00,0x08,0x03,0x1b,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2e,0x1d,0x2a,0x00,0x2b,
0x1c,0x1b,0x1f,0x00,0x00,0x2f,0x03,0x1e,0x2d,0x00,0x00,0x3d,0x30,0x31,
0x32,0x33,0x34,0x35,0x36,0x37,0x00,0x38,0x39,0x00,0x00,0x00,0x10,0x10,
0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
0x10,0x10,0x05,0x01,0x0b,0x7f,0x10,0x04,0x10,0x0c,0x10,0x1c,0x1d,0x1f,
0x1e,0x00,0x81,0xea,0xeb,0xec,0xee,0xed,0xf3,0xf4,0x82,0xd7,0x00,0xf5,
0xce,0xe3,0xe4,0xe5,0xe7,0xe6,0xda,0xdb,0xdc,0xdd,0xdf,0xde,0xb1,0xe1,
0xe0,0xd1,0xa1,0xe2,0xd5,0xaf,0xe8,0xd3,0xe9,0xb8,0x0d,0xf1,0xef,0xae,
0xf0,0xf2,0xc8,0xf8,0xc0,0xf6,0xf7,0xf9,0x09,0xca,0xd9,0x08,0x03,0x1b,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2e,0x2a,0x2a,
0x00,0x2b,0x2b,0x1b,0x3d,0x00,0x00,0x2f,0x03,0x2f,0x2d,0x00,0x00,0x3d,
0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x00,0x38,0x39,0x00,0x00,0x00,
0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
0x10,0x10,0x10,0x10,0x05,0x01,0x0b,0x7f,0x10,0x04,0x10,0x0c,0x10,0x1c,
0x1d,0x1f,0x1e,0x00,0x81,0xea,0xeb,0xec,0xee,0xed,0xf3,0xf4,0x82,0xd7,
0x00,0xf5,0xce,0xe3,0xe4,0xe5,0xe7,0xe6,0xc1,0xaa,0xa3,0xa2,0xa4,0xb0,
0xad,0xbb,0xa6,0xd0,0xa5,0xbc,0xd4,0xaf,0xe8,0xd2,0xe9,0xb8,0x0d,0xf1,
0xef,0xae,0xf0,0xc9,0xc7,0xb2,0xd6,0xf6,0xf7,0xb3,0x09,0xca,0x60,0x08,
0x03,0x1b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x2e,
0x1d,0x2a,0x00,0x2b,0x1c,0x1b,0x1f,0x00,0x00,0x2f,0x03,0x1e,0x2d,0x00,
0x00,0x3d,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x00,0x38,0x39,0x00,
0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
0x10,0x10,0x10,0x10,0x10,0x10,0x05,0x01,0x0b,0x7f,0x10,0x04,0x10,0x0c,
0x10,0x1c,0x1d,0x1f,0x1e,0x00,0x8c,0xa7,0xb6,0xc4,0xfa,0xa9,0xbd,0xc5,
0x8d,0xc3,0x00,0xba,0xcf,0xb7,0xab,0xa8,0xb4,0xa0,0xc1,0xaa,0xa3,0xa2,
0xa4,0xb0,0xad,0xbb,0xa6,0xd0,0xa5,0xbc,0xd4,0xbf,0xac,0xd2,0x5e,0xb9,
0x0d,0xc2,0xc6,0xbe,0xfb,0xc9,0xc7,0xb2,0xd6,0x7e,0xb5,0xb3,0x09,0xca,
0x60,0x08,0x03,0x1b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x2e,0x1d,0x2a,0x00,0x2b,0x1c,0x1b,0x1f,0x00,0x00,0x2f,0x03,0x1e,
0x2d,0x00,0x00,0x3d,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x00,0x38,
0x39,0x00,0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x05,0x01,0x0b,0x7f,0x10,0x04,
0x10,0x0c,0x10,0x1c,0x1d,0x1f,0x1e,0x00,0x01,0x13,0x04,0x06,0x08,0x07,
0x1a,0x18,0x03,0x16,0x30,0x02,0x11,0x17,0x05,0x12,0x19,0x14,0x11,0x12,
0x13,0x14,0x16,0x15,0x1d,0x19,0x17,0x0d,0x18,0x10,0x1d,0x0f,0x15,0x1b,
0x09,0x10,0x0d,0x0c,0x0a,0x07,0x0b,0x1b,0x1c,0x0c,0x0f,0x0e,0x0d,0x0e,
0x09,0x00,0x00,0x08,0x03,0x1b,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x2e,0x1d,0x2a,0x00,0x2b,0x1c,0x1b,0x1f,0x00,0x00,0x2f,
0x03,0x1e,0x2d,0x00,0x00,0x3d,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
0x00,0x38,0x39,0x00,0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x05,0x01,0x0b,0x7f,
0x10,0x04,0x10,0x0c,0x10,0x1c,0x1d,0x1f,0x1e,0x00,0x01,0x13,0x04,0x06,
0x08,0x07,0x1a,0x18,0x03,0x16,0x30,0x02,0x11,0x17,0x05,0x12,0x19,0x14,
0x01,0x00,0x03,0x04,0x1e,0x05,0x0b,0x08,0x06,0x1f,0x0a,0x09,0x1d,0x0f,
0x15,0x1b,0x09,0x10,0x0d,0x0c,0x0a,0x02,0x0b,0x1a,0x1c,0x1c,0x1f,0x0e,
0x0d,0x1e,0x09,0x20,0x1e,0x08,0x03,0x1b,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x2e,0x1d,0x2a,0x00,0x2b,0x1c,0x1b,0x1f,0x00,
0x00,0x2f,0x03,0x1e,0x2d,0x00,0x00,0x3d,0x30,0x31,0x32,0x33,0x34,0x35,
0x36,0x37,0x00,0x38,0x39,0x00,0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,
0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x05,0x01,
0x0b,0x7f,0x10,0x04,0x10,0x0c,0x10,0x1c,0x1d,0x1f,0x1e,0x00,0x00,0x05,
0x03,0x0e,0x00,0x07,0x20,0xab,0x45,0x83,0x61,0x87,0x65,0x8e,0x69,0x92,
0x6f,0x97,0x75,0x9c,0x00,0xab,0x03,0x32,0x00,0x07,0x20,0x60,0x41,0xcb,
0x61,0x88,0x65,0x8f,0x69,0x93,0x6f,0x98,0x75,0x9d,0x00,0x60,0x03,0x22,
0x00,0x06,0x20,0x5e,0x61,0x89,0x65,0x90,0x69,0x94,0x6f,0x99,0x75,0x9e,
0x00,0x5e,0x03,0x20,0x00,0x0a,0x20,0xac,0x41,0x80,0x4f,0x85,0x55,0x86,
0x61,0x8a,0x65,0x91,0x69,0x95,0x6f,0x9a,0x75,0x9f,0x79,0xd8,0x00,0xac,
0x03,0x2d,0x00,0x07,0x20,0x7e,0x41,0xcc,0x4e,0x84,0x4f,0xcd,0x61,0x8b,
0x6e,0x96,0x6f,0x9b,0x00,0x7e 
};						/* Mac KCHR  US adb standard */
int	key_trans_state[NDEVICES];	/* must be saved between calls
						 * to keyTrans.
						 * see Inside Mac V-195
						 */
	
long key_op();
int key_open();
int key_close();

struct key_data key_data = {
	key_open,
	key_close,
	key_op,
};

static int key_intr();
static int key_send();
static int key_repeat();
static int key_ascii();
static int key_timeout();

/*
 *	Values for key_state
 */

#define	STATE_INIT	0	/* non active state */
#define	STATE_REG0	1	/* idle state waiting for poll/intr */
#define	STATE_REG2	2	/* reading register 2 */
#define	STATE_OPEN	3	/* waiting for the initial open */
#define	STATE_ACTIVE	5	/* currently running a talk to reg 0 */
#define	STATE_LISTEN	6	/* waiting for the first listen (turn att on) */

/*
 *	This is the basic keycode to ascii map. It is indexed by the least
 *		significant 7 bits if the key code. It returns a 3 bit key type
 *		and a 5 bit number (whose meaning depends on the key type).
 *		All ascii processing comes through here first before using the
 *		other maps for later processing.
 */

unsigned char key_map1[] = {
	0	|KEY_CAPS,		/* a */
	18	|KEY_CAPS,		/* s */
	3	|KEY_CAPS,		/* d */
	5	|KEY_CAPS,		/* f */
	7	|KEY_CAPS,		/* h */
	6	|KEY_CAPS,		/* g */
	25	|KEY_CAPS,		/* z */
	23	|KEY_CAPS,		/* x */
	2	|KEY_CAPS,		/* c */
	21	|KEY_CAPS,		/* v */
	KEY_EMPTY,
	1	|KEY_CAPS,		/* b */
	16	|KEY_CAPS,		/* q */
	22	|KEY_CAPS,		/* w */
	4	|KEY_CAPS,		/* e */
	17	|KEY_CAPS,		/* r */
	24	|KEY_CAPS,		/* y */
	19	|KEY_CAPS,		/* t */
	1	|KEY_SHIFT,		/* 1 */
	2	|KEY_SHIFT,		/* 2 */
	3	|KEY_SHIFT,		/* 3 */
	4	|KEY_SHIFT,		/* 4 */
	6	|KEY_SHIFT,		/* 6 */
	5	|KEY_SHIFT,		/* 5 */
	11	|KEY_SHIFT,		/* = */
	9	|KEY_SHIFT,		/* 9 */
	7	|KEY_SHIFT,		/* 7 */
	10	|KEY_SHIFT,		/* - */
	8	|KEY_SHIFT,		/* 8 */
	0	|KEY_SHIFT,		/* 0 */
	13	|KEY_SHIFT,		/* ] */
	14	|KEY_CAPS,		/* o */
	20	|KEY_CAPS,		/* u */
	12	|KEY_SHIFT,		/* [ */
	8	|KEY_CAPS,		/* i */
	15	|KEY_CAPS,		/* p */
	3	|KEY_PLAIN,		/* return */
	11	|KEY_CAPS,		/* l */
	9	|KEY_CAPS,		/* j */
	14	|KEY_SHIFT,		/* ' */
	10	|KEY_CAPS,		/* k */
	15	|KEY_SHIFT,		/* ; */
	16	|KEY_SHIFT,		/* \ */
	17	|KEY_SHIFT,		/* , */
	20	|KEY_SHIFT,		/* / */
	13	|KEY_CAPS,		/* n */
	12	|KEY_CAPS,		/* m */
	18	|KEY_SHIFT,		/* . */
	2	|KEY_PLAIN,		/* tab */
	1	|KEY_PLAIN,		/* space */
	19	|KEY_SHIFT,		/* ` */
	0	|KEY_PLAIN,		/* Delete */
	KEY_EMPTY,
	4	|KEY_PLAIN,		/* Escape */
	0	|KEY_SPECIAL,		/* Control */
	1	|KEY_SPECIAL,		/* Open Apple */
	2	|KEY_SPECIAL,		/* Shift */
	3	|KEY_SPECIAL,		/* Caps Lock */
	4	|KEY_SPECIAL,		/* Option */
	5	|KEY_SPECIAL,		/* Left */
	6	|KEY_SPECIAL,		/* Right */
	7	|KEY_SPECIAL,		/* Down */
	8	|KEY_SPECIAL,		/* Up */
	KEY_EMPTY,
	KEY_EMPTY,
	10	|KEY_KEYPAD,		/* . */
	KEY_EMPTY,
	11	|KEY_KEYPAD,		/* * */
	KEY_EMPTY,
	12	|KEY_KEYPAD,		/* + */
	KEY_EMPTY,
	9	|KEY_SPECIAL,		/* Clear */
	KEY_EMPTY,
	KEY_EMPTY,
	KEY_EMPTY,
	13	|KEY_KEYPAD,		/* / */
	16	|KEY_KEYPAD,		/* Enter */
	KEY_EMPTY,
	14	|KEY_KEYPAD,		/* - */
	KEY_EMPTY,
	KEY_EMPTY,
	15	|KEY_KEYPAD,		/* = */
	0	|KEY_KEYPAD,		/* 0 */
	1	|KEY_KEYPAD,		/* 1 */
	2	|KEY_KEYPAD,		/* 2 */
	3	|KEY_KEYPAD,		/* 3 */
	4	|KEY_KEYPAD,		/* 4 */
	5	|KEY_KEYPAD,		/* 5 */
	6	|KEY_KEYPAD,		/* 6 */
	7	|KEY_KEYPAD,		/* 7 */
	KEY_EMPTY,
	8	|KEY_KEYPAD,		/* 8 */
	9	|KEY_KEYPAD,		/* 9 */
	KEY_EMPTY,
	KEY_EMPTY,
	KEY_EMPTY,
	5	|KEY_FUNCTION,		/* F5 */
	6	|KEY_FUNCTION,		/* F6 */
	7	|KEY_FUNCTION,		/* F7 */
	3	|KEY_FUNCTION,		/* F3 */
	8	|KEY_FUNCTION,		/* F8 */
	9	|KEY_FUNCTION,		/* F9 */
	KEY_EMPTY,
	11	|KEY_FUNCTION,		/* F11 */
	KEY_EMPTY,
	13	|KEY_FUNCTION,		/* F13 */
	KEY_EMPTY,
	14	|KEY_FUNCTION,		/* F14 */
	KEY_EMPTY,
	10	|KEY_FUNCTION,		/* F10 */
	KEY_EMPTY,
	12	|KEY_FUNCTION,		/* F12 */
	KEY_EMPTY,
	15	|KEY_FUNCTION,		/* F15 */
	19	|KEY_FUNCTION,		/* help */
	20	|KEY_FUNCTION,		/* home */
	21	|KEY_FUNCTION,		/* page up */
	16	|KEY_FUNCTION,		/* del */
	4	|KEY_FUNCTION,		/* F4 */
	17	|KEY_FUNCTION,		/* end */
	2	|KEY_FUNCTION,		/* F2 */
	18	|KEY_FUNCTION,		/* page down */
	1	|KEY_FUNCTION,		/* F1 */
	2	|KEY_SPECIAL,		/* Shift */
	4	|KEY_SPECIAL,		/* Option */
	0	|KEY_SPECIAL,		/* Control */
	KEY_EMPTY,
	KEY_EMPTY,
};

/*
 *	Map of unshifted 'shift' type keys
 */

char key_map2[] = {
	'0',		/* 0 */
	'1',		/* 1 */
	'2',		/* 2 */
	'3',		/* 3 */
	'4',		/* 4 */
	'5',		/* 5 */
	'6',		/* 6 */
	'7',		/* 7 */
	'8',		/* 8 */
	'9',		/* 9 */
	'-',		/* - */
	'=',		/* = */
	'[',		/* [ */
	']',		/* ] */
	'\'',		/* ' */
	';',		/* ; */
	'\\',		/* \ */
	',',		/* , */
	'.',		/* . */
	'`',		/* ` */
	'/',		/* / */
};

/*
 *	Map of shifted 'shift' type keys
 */

char key_map3[] = {
	')',		/* 0 */
	'!',		/* 1 */
	'@',		/* 2 */
	'#',		/* 3 */
	'$',		/* 4 */
	'%',		/* 5 */
	'^',		/* 6 */
	'&',		/* 7 */
	'*',		/* 8 */
	'(',		/* 9 */
	'_',		/* - */
	'+',		/* = */
	'{',		/* [ */
	'}',		/* ] */
	'"',		/* ' */
	':',		/* ; */
	'|',		/* \ */
	'<',		/* , */
	'>',		/* . */
	'~',		/* ` */
	'?',		/* / */
};

/*
 *	This map contains characters that never change 
 */

char key_map4[] = {
	0x7f,		/* Delete */
	' ',		/* Space */
	'\t',		/* Tab */
	'\r',		/* Return */
	0x1b,		/* Escape */
};

/*
 *	Key map for Keypad keys
 */

char key_map5[] = {
	'0',		/* 0 */
	'1',		/* 1 */
	'2',		/* 2 */
	'3',		/* 3 */
	'4',		/* 4 */
	'5',		/* 5 */
	'6',		/* 6 */
	'7',		/* 7 */
	'8',		/* 8 */
	'9',		/* 9 */
	'.',		/* . */
	'*',		/* * */
	'+',		/* + */
	'/',		/* / */
	'-',		/* - */
	'=',		/* = */
	'\r',		/* Enter */
};

/*
 *	Map for last character of keypad string in keymap mode (ANSI)
 */

char key_map6[] = {
	'p',		/* 0 */
	'q',		/* 1 */
	'r',		/* 2 */
	's',		/* 3 */
	't',		/* 4 */
	'u',		/* 5 */
	'v',		/* 6 */
	'w',		/* 7 */
	'x',		/* 8 */
	'y',		/* 9 */
	'n',		/* . */
	0,		/* * */
	0,		/* + */
	0,		/* / */
	'm',		/* - */
	0,		/* = */
	'M',		/* Enter */
};

/*
 *	To initialize - initialize the keyboard's globals and then
 *		call fdb_open to access the keyboard and start the FSM
 */

key_init()
{
	register int i;

	for (i = 0; i < NDEVICES; i++) {
		key_call[i] =	nulldev;
		key_state[i] =	STATE_INIT;
		key_mode[i] =	KEY_MAC;
		key_opened[i] =	KEY_CLOSED;
		key_control[i] = 0xffff;
		key_exists_count[i] = 5;
		fdb_open(FDB_KEYBOARD, i, key_intr);
	}
}

/*
 *	Key_open sets global variables (including the ISR). It only succeeds
 *		if the device exists and has successfully initialised.
 */

key_open(id, intr, mode)
procedure_t intr;
{
	if (id < 0 || id >= NDEVICES) {		/* bad dev */
	    return(0);
	}

	/* From what I can tell, it does no harm to keep trying the init
	 * until it works. This allows /etc/init to keep trying the open
	 * until the keyboard is plugged in. RRA
	 */
	if (key_state[id] == STATE_INIT) {
	    fdb_close(FDB_KEYBOARD);            /* get tables cleared */
	    key_init();                         /* try initting again */
	}

	if (key_state[id] == STATE_INIT) {      /* didn't init */
	    return(0);
	}

	key_keypad[id] =	0;
	key_call[id] =		intr;
	if (!key_mode[id])	/* if key_mode has already been set via
				   key_op(), don't do it again! */
		key_mode[id] =	mode;
	key_defwait[id] =	KEY_DEFWAIT;
	key_defgap[id] =	KEY_DEFGAP;
	key_opened[id] =	KEY_OPEN;
	return(1);
}

/*
 *	Close simply marks it as closed and cancels any timeouts
 */

key_close(id)
{
	untimeout(key_timeout, id);
	key_call[id] = nulldev;
	key_opened[id] = KEY_CLOSED;
}

/*
 *	This routine is put in the callout queue when keyboard repeats are
 *	required in ascii mode. If calls key_repeat to repeat the character
 *	(string) and then puts itself back into the queue
 */

static
key_timeout(id)
register int id;
{
	register int s;

	s = spl1();
	if (key_down[id]) {
		if (key_mode[id] == KEY_ASCII || key_mode[id] == KEY_MAC) {
			key_repeat(id);
			timeout(key_timeout, id, key_defgap[id]);
		}
	}
	splx(s);
}

/*
 *	key_op is able to be called by the next higher level. It is normally
 *		used to save the current status of the device when someone
 *		wishes to take controll of it. It allows one to set keyboard
 *		options, and at the same time recover their previous values.
 */

long
key_op(id, op, x)
long x;
{
	int s;
	long t;
	
	switch(op) {
	case KEY_OP_KEYPAD:
		t = key_keypad[id];
		key_keypad[id] = x;
		return(t);;

	case KEY_OP_MODE:
		t = key_mode[id];
		key_mode[id] = x;
		return(t);

	case KEY_OP_WAIT:
		t = key_defwait[id];
		if (x < 1) {
			key_defwait[id] = 1;
		} else {
			key_defwait[id] = x;
		}
		return(t);
		break;

	case KEY_OP_GAP:
		t = key_defgap[id];
		if (x < 1) {
			key_defgap[id] = 1;
		} else {
			key_defgap[id] = x;
		}
		return(t);

	case KEY_OP_INTR:
		s = splhi();
		t = (long)key_call[id];
		key_call[id] = (procedure_t)x;
		splx(s);
		return(t);

	case KEY_OP_OPEN:
		return(key_opened[id]);
		
	case KEY_OP_KCHR:
	    {
		struct kchr_ioctl {
			int size;
			char *data;
		} *uap = (struct kchr_ioctl *)x;
		if (uap->size > TRANSDATASIZE)
		    return -1;
		if (copyin(uap->data, (char *)transData, uap->size))
		    return -1;
		key_trans_state[id] = 0;
		return 0;
	    }
	case KEY_OP_KMAP:
	    {
		struct kmap_ioctl {
			int size;
			char *data;
		} *uap = (struct kmap_ioctl *)x;
		if (uap->size > sizeof kmapData)
		    return -1;
		if (copyin(uap->data, (char *)kmapData, uap->size))
		    return -1;
		return 0;
	    }

	}
}

/*
 *	This is the ISR from the fdb driver (refer to the extensive
 *	documentation in fdb.c for the detail on how this works). It forms a
 *	FSM that handles device initialization and responds to device events
 *	reported by the lower level. It works in 3 modes ascii/ascii-raw/raw.
 *
 *	On initialisation the key driver runs the following state machine:
 *
 *	Event				Action
 *	=====				======
 *
 *	initialisation			fdb_open()
 *					/
 *		------------------------
 *		v
 *	FDB_EXISTS (timeout)		quit .... no device
 *		v
 *	FDB_EXISTS (no timeout)		fdb_flush()
 *					/
 *		------------------------
 *		v
 *	FDB_FLUSH			fdb_listen()
 *					/
 *		------------------------
 *		v
 *	FDB_LISTEN			fdb_talk() (register = 2)
 *					/
 *		------------------------
 *		v
 *	FDB_TALK			fdb_talk() (register = 0)
 *					(now in active/normal state)
 *
 *	Once it gets to this state it responds to 4 events
 *
 *	FDB_TALK (timeout)		go to sleep, wait for more events
 *
 *	FDB_TALK (no timeout)		process received characters
 *					fdb_talk() (register = 0)
 *
 *	FDB_POLL			process received characters
 *
 *	FDB_INT				if no talk pending fdb_talk()
 *
 *	FDB_UNINT			mark device idle
 */

static
key_intr(id, cmd, tim)
register id;
{
	switch(cmd) {
	case FDB_UNINT:

		/*
		 *	A device poll was canceled, mark the device as inactive
		 */

		if (key_state[id] == STATE_ACTIVE)
			key_state[id] = STATE_REG0;
		break;

	case FDB_INT:

		/*
		 *	The lower level requested a device poll, if we are not
		 *	already in the process of running an fdb transaction
		 *	then start one and mark us as busy
		 */

		if (key_state[id] == STATE_REG0) {
			fdb_talk(FDB_KEYBOARD, id, 0, &key_buff[id]);
			key_state[id] = STATE_ACTIVE;
			return(1);
		}
		return(0);

	case FDB_POLL:

		/*
		 *	A hardware poll succeeded - fake out the buffer and the
		 *	timeout to that it looks as if an fdb_talk() succeeded.
		 *	Then fall through into the FDB_TALK handler
		 */

		if (key_state[id] != STATE_REG0 && 
		    key_state[id] != STATE_ACTIVE)
			break;
		key_buff[id] = tim;
		tim = 0;
		/* Fall through */
	case FDB_TALK:

		/*
		 *	If a timeout occured go to the idle state waiting for a
		 *	FDB_POLL or FDB_INT. Otherwise process the incoming 
		 *	data. If we really got an FDB_TALK (not a poll) then
		 *	start another one to force us to be the hardware poller.
		 *	We always read register 2 first (the modifier state) to
		 *	initialise the local copy of its state. After that we
		 *	read register 0 for character data.
		 */

		if (!tim) {		/* there is no message */
			switch (key_mode[id]) {
			case KEY_ASCII:
				if (key_state[id] == STATE_REG2) {
					key_control[id] = key_buff[id];
					fdb_talk(FDB_KEYBOARD, id, 0,
							&key_buff[id]);
					key_state[id] = STATE_ACTIVE;
					return;
				} else {

					/*
					 *	For each key movement call
					 *	key_ascii to convert it to
					 *	ascii. If a key is still down
					 *	then start autorepeat. Otherwise
					 *	stop it.
					 */

					tim = (key_buff[id]&0xff) != 0xff;
					if ((key_buff[id]&0xff00) != 0xff00)
						key_ascii(id,
						   (key_buff[id]>>8)&0xff, 
						    !(key_buff[id]&0x80));
					if (tim) key_ascii(id,
						   key_buff[id]&0xff, 0);
				}
				break;
			case KEY_MAC:
				if (key_state[id] == STATE_REG2) {
					key_control[id] = key_buff[id];
					fdb_talk(FDB_KEYBOARD, id, 0,
							&key_buff[id]);
					key_state[id] = STATE_ACTIVE;
					return;
				} else {

					/*
					 * For each key movement call
					 * key_mac to convert it to
					 * ascii. If a key is still down
					 * then start autorepeat. Otherwise
					 * stop it.
					 */

					tim = (key_buff[id]&0xff) != 0xff;
					if ((key_buff[id]&0xff00) != 0xff00)
						key_mac(id,
						   (key_buff[id]>>8)&0xff, 
						    !(key_buff[id]&0x80));
					if (tim) key_mac(id,
						   key_buff[id]&0xff, 0);
				}
				break;
			case KEY_ARAW:
				if (key_state[id] == STATE_REG2) {
					fdb_talk(FDB_KEYBOARD, id, 0,
						&key_buff[id]);
					key_state[id] = STATE_ACTIVE;
					return;
				} else {

					/*
					 *	In this mode simply pass back
					 *	the 8 bit character code as if
					 *	it were an ascii character.
					 */

					if (key_buff[id] == 0x7f7f) {
						(*key_call[id])(id,
								KC_CHAR,
								0x7f,
								1);
						(*key_call[id])(id,
								KC_CHAR,
								0x7f,
								0);
					} else {
						tim = (key_buff[id]&0xff) !=
								0xff;
						if ((key_buff[id]&0xff00) !=
								0xff00)
						    (*key_call[id])(id,
							KC_CHAR,
							(key_buff[id]>>8)&0xff,
							tim);
						if (tim)
						    (*key_call[id])(id,
						    	KC_CHAR,
						    	key_buff[id]&0xff,
						    	0);
					}
				}
				break;
			default:
				if (key_state[id] == STATE_REG2) {
					key_r1[id] = key_buff[id];
					key_state[id] = STATE_REG0;
					(*key_call[id])(id, KC_RAW2,
							key_buff[id], 0);
					fdb_talk(FDB_KEYBOARD, id, 0,
							&key_buff[id]);
					key_state[id] = STATE_ACTIVE;
					return;
				} else {

					/*
					 *	In this mode pass back the 
					 *	register contents returned
					 *	from the device
					 */

					key_r0[id] = key_buff[id];
					(*key_call[id])(id, KC_RAW0,
							key_buff[id], 0);
				}
			}
			if (cmd != FDB_POLL) {
				fdb_talk(FDB_KEYBOARD, id, 0, &key_buff[id]);
				key_state[id] = STATE_ACTIVE;
			}
		} else {
			key_state[id] = STATE_REG0;
		}
		break;

	case FDB_LISTEN:

		/*
		 *	listen to enable service requests is done. Now do a
		 *	fdb_talk() to register 2 in order to read the keyboard
		 *	modifiers.
		 */

		fdb_talk(FDB_KEYBOARD, id, 2, &key_buff[id]);
		key_state[id] = STATE_REG2;
		break;

	case FDB_EXISTS:

		/*
		 *	The device exists (if it didn't time out). If so then
		 *	run a flush transaction to clear out its buffers
		 */

		if (tim) {
			/*
			 *	If timed out, retry FDB_EXISTS a few times.
			 *	Datadesk doesn't respond right after reset.
			 */
			if (key_exists_count[id]) {
				key_exists_count[id]--;
				fdb_exists(FDB_KEYBOARD, id);
			}
		}
		else {
			fdb_flush(FDB_KEYBOARD, id);
		}
		break;

	case FDB_FLUSH:

		/*
		 *	A flush transaction completed. Now do a listen to set
	 	 *	the device's service enable request bit.
		 */

		key_state[id] = STATE_LISTEN;
		key_buff[id] = 0x2000 | (FDB_KEYBOARD<<8) | 1;
		fdb_listen(FDB_KEYBOARD, id, 3, &key_buff[id], 2);
		break;

	case FDB_RESET:
		return;
	}
}

/*
 *	This routine uses the character code passed to it to look up the
 *	ascii tables and map it into one or more ascii characters.
 */

static
key_ascii(id, r0, flag)
register int id;
register int r0;
int flag;
{
	register int c;
	register int len;

	/*
	 *	If the code represents a key up code then if it is a
	 *	modifier keep track of it otherwise if it was the last
	 *	key to go down mark it as up.
	 */

	if (r0&0x80) {
		r0 &= 0x7f;
		if (((c = key_map1[r0])&KEY_TYPE) == KEY_SPECIAL) {
			switch (c&KEY_VALUE) {
			case 0:		/* Control */
				key_control[id] |= KEY_R1_CONTROL;
				return;
			case 1:		/* Open Apple */
				key_control[id] |= KEY_R1_OAPPLE;
				return;
			case 2:		/* Shift */
				key_control[id] |= KEY_R1_SHIFT;
				return;
			case 3:		/* Caps Lock */
				key_control[id] |= KEY_R1_CAPSLOCK;
				return;
			case 4:		/* Option */
				key_control[id] |= KEY_R1_OPTION;
				return;
			default:
				break;
			}
		}
		if (r0 != key_last[id]) 
			return;
		if (key_down[id])
			untimeout(key_timeout, id);
		key_down[id] = 0;
		return;
	}

	/*
	 *	Set up the key save buffer (this is where we put the latest
	 *	character string produced so that it can be repeated)
	 */

	len = 1;
	key_save = &key_buffer[8*id];

	/*
	 *	Check to see if the character code is one we know of
	 *	if not ignore it
	 */

	if ((c = key_map1[r0]) == KEY_EMPTY) {
		return;
	}

	/*
	 *	Now decode the character depending on its type from the main
	 *	table (key_map1)
	 */

	switch (c&KEY_TYPE) {
	case KEY_CAPS:

		/*
		 *	If it is a character that respoinds to capslock (all
		 *	letters) then calculate the characters value and bias
		 *	it if either capslock or shift is down. If it is a
		 *	controll character process it also.
		 */

		c &= KEY_VALUE;
		if (!(key_control[id]&KEY_R1_CONTROL)) {
			c++;
		} else
		if ((key_control[id]&(KEY_R1_CAPSLOCK|KEY_R1_SHIFT)) != 
		        (KEY_R1_CAPSLOCK|KEY_R1_SHIFT)) {
			c += 'A';
		} else {
			c += 'a';
		}
		if ((key_control[id]&KEY_R1_OPTION) == 0) 
			c |= 0x80;
		break;

	case KEY_SHIFT:

		/*
		 *	process characters that have shifted values that cant
		 *	be easily calculated from their unshifted ones
		 */

		if (!(key_control[id]&KEY_R1_SHIFT)) {
			c = key_map3[c&KEY_VALUE];
		} else {
			c = key_map2[c&KEY_VALUE];
		}
		if ((key_control[id]&KEY_R1_CONTROL) == 0) 
			c &= 0x1f;
		if ((key_control[id]&KEY_R1_OPTION) == 0) 
			c |= 0x80;
		break;

	case KEY_PLAIN:

		/*
		 *	these keys never change
		 */

		c = key_map4[c&KEY_VALUE];
		break;

	case KEY_KEYPAD:

		/*
		 *	keypad keys operate in two modes .... either as the
		 *	key that they represent or in ansi mode where they
		 *	generate an escape sequence
		 */

		if (key_keypad[id] && key_map6[c&KEY_VALUE]) {
			key_send(id, 0x1b, 1);
			key_send(id, 'O', 1);
			len = 3;
			c = key_map6[c&KEY_VALUE];
		} else {
			c = key_map5[c&KEY_VALUE];
		}
		break;

	case KEY_SPECIAL:

		/*
		 *	special keys fall into two main types ... cursor
		 *	movement keys ... which are mapped to escape sequences
		 *	and modifier keys which are kept track of.
		 */

		switch (c&KEY_VALUE) {
		case 0:		/* Control */
			key_control[id] &= ~KEY_R1_CONTROL;
			return;
		case 1:		/* Open Apple */
			key_control[id] &= ~KEY_R1_OAPPLE;
			return;
		case 2:		/* Shift */
			key_control[id] &= ~KEY_R1_SHIFT;
			return;
		case 3:		/* Caps Lock */
			key_control[id] &= ~KEY_R1_CAPSLOCK;
			return;
		case 4:		/* Option */
			key_control[id] &= ~KEY_R1_OPTION;
			return;
		case 5:		/* Left */
			c = 'D';
			goto movement;
		case 6:		/* Right */
			c = 'C';
			goto movement;
		case 7:		/* Down */
			c = 'B';
			goto movement;
		case 8:		/* Up */
			c = 'A';
		movement:
			key_send(id, 0x1b, 1);
			key_send(id, 'O', 1);
			len = 3;
			break;
		case 9:		/* Clear */
			return;
		}
		break;
	case KEY_FUNCTION:
		c &= KEY_VALUE;
		if (!(key_control[id]&KEY_R1_SHIFT)) {
			c += 'a' - 'A';
		}
		key_send(id, 0x01, 1);
		key_send(id, c+'@'-1, 1);
		c = 0x0d;
		len = 3;
		break;
	}
	key_last[id] = r0;
	key_send(id, c, flag);
	key_length[id] = len;
	if (key_down[id])
		untimeout(key_timeout, id);
	timeout(key_timeout, id, key_defwait[id]);
	key_down[id] = 1;
}

/*
 *	This routine passes back a character to the next higher level.
 *	and saves its value for later repeating
 */

static
key_send(id, c, flag)
{
	(*key_call[id])(id, KC_CHAR, c, flag);
	*key_save++ = c;
}

/*
 *	This routine repeats a key by sending its saved representation out to
 *	the next higher level
 */

static
key_repeat(id)
{
	register int i;
	register char *cp;

	cp = &key_buffer[8*id];
	for (i = 0; i < key_length[id]; i++) {
		(*key_call[id])(id, KC_CHAR, *cp++, i != key_length[id]-1);
	}
}

/*
 *	This routine uses the character code passed to it to look up the
 *	Macintosh transData table and map it into one or more ascii characters.
 */

#define CONTROL_KEY 	0x3B
#define OPTION_KEY	0x3A
#define SHIFT_KEY 	0x38
#define COMMAND_KEY	0x37
#define CAPS_LOCK_KEY	0x39
#define DELETE_KEY	0x33
#define ENTER_KEY	0x4C
#define UP_KEY		0x7E
#define DOWN_KEY	0x7D
#define RIGHT_KEY	0x7C
#define LEFT_KEY	0x7B
#define CR		0x0D
#define DELETE		0x7f

#define controlKey	(1 << 12)
#define optionKey	(1 << 11)
#define capsLockKey	(1 << 10)
#define shiftKey	(1 << 9)
#define commandKey	(1 << 8)

/*
  key_mac:  only called when in console emulator mode
*/
static
key_mac(id, r0, flag)
	register int id;
	register int r0;
	int flag;
{
	register int c;
	register int len;
	static short keycode[NDEVICES];
	long key;

	/*
	 * If the code represents a key up code then if it is a
	 * modifier keep track of it otherwise if it was the last
	 * key to go down mark it as up.
	 */

	if (r0 & 0x80) {		/* key up? */
		r0 = kmapData[id][r0&0x7f];
		switch (r0) {
		case CONTROL_KEY:
			keycode[id] &= ~controlKey;
			break;
		case OPTION_KEY:
			keycode[id] &= ~optionKey;
			break;
		case CAPS_LOCK_KEY:
			keycode[id] &= ~capsLockKey;
			break;
		case SHIFT_KEY:
			keycode[id] &= ~shiftKey;
			break;
		case COMMAND_KEY:
			keycode[id] &= ~commandKey;
			break;
		default:
			break;
		}
		keycode[id] |= r0;
		(void)KeyTrans(transData, keycode[id], &key_trans_state[id]);
		keycode[id] &= ~0xff;
		if (r0 != key_last[id]) 
			return;
		if (key_down[id])
			untimeout(key_timeout, id);
		key_down[id] = 0;
		return;
	}

	/* we now have a key down
	 *
	 * Set up the key save buffer (this is where we put the latest
	 * character string produced so that it can be repeated)
	 */

	/*
	 * translate physical key code to virtual
	 */
	r0 = kmapData[id][r0&0x7f];

	len = 1;
	key_save = &key_buffer[8*id];

	switch (r0) {
	case CONTROL_KEY:
		keycode[id] |= controlKey;
		break;
	case OPTION_KEY:
		keycode[id] |= optionKey;
		break;
	case CAPS_LOCK_KEY:
		keycode[id] |= capsLockKey;
		break;
	case SHIFT_KEY:
		keycode[id] |= shiftKey;
		break;
	case COMMAND_KEY:
		keycode[id] |= commandKey;
		break;
	default:
		/* 
		   Note the following are special key hacks to make up for short
		   commings in the transData key mapping table.
		*/
		if (((keycode[id] & ~(controlKey|shiftKey)) == 0) &&
		   (keycode[id] & controlKey)  &&
		   (keycode[id] & shiftKey)) {
			/*
			  iff the control and shift modifier keys are down, remove
			  the control modifier to get an accurate translation from
			  keyTrans()
			*/
			keycode[id] &= ~controlKey;
			keycode[id] |= r0;
			key = KeyTrans(transData, keycode[id], &key_trans_state[id]);
			keycode[id] |= controlKey;  /* apply this later (see below) */
		}
		else {
			keycode[id] |= r0;
			key = KeyTrans(transData, keycode[id], &key_trans_state[id]);
		}
		switch (keycode[id]) {
		case DELETE_KEY:
			key_send(id, DELETE, flag);	/* don't send bksp char! */
			break;
		case UP_KEY:
			key_send(id, 0x1b, 1);		/* vt100 emulation */
			key_send(id, 'O', 1);
			key_send(id, 'A', flag);
			len = 3;
			break;
		case DOWN_KEY:
			key_send(id, 0x1b, 1);
			key_send(id, 'O', 1);
			key_send(id, 'B', flag);
			len = 3;
			break;
		case RIGHT_KEY:
			key_send(id, 0x1b, 1);
			key_send(id, 'O', 1);
			key_send(id, 'C', flag);
			len = 3;
			break;
		case LEFT_KEY:
			key_send(id, 0x1b, 1);
			key_send(id, 'O', 1);
			key_send(id, 'D', flag);
			len = 3;
			break;
		case ENTER_KEY:  /* so we don't send kill char when in console emul. */
			if (key_mode[id] == KEY_MAC)
			    key_send(id, CR, flag);
			break;
		default:
			if (key & 0xff0000)
				key = key >> 16;
			if (keycode[id] & controlKey)
				key_send(id, key & 0x1f, flag);
			else
				key_send(id, key & 0xff, flag);
		}
		keycode[id] &= ~0xff;
		key_last[id] = r0;
		key_length[id] = len;
		if (key_down[id])
		    untimeout(key_timeout, id);
		timeout(key_timeout, id, key_defwait[id]);
		key_down[id] = 1;
	}
}

/*
 * KeyTrans in C minor
 *
 * by Ed Tecot
 *
 * Copyright (c) 1987 Apple Computer, Inc.
 *
 * This code implements the keyboard translation procedure in C for use
 * in the A/UX kernal.  Play at 33 1/3 RPM.
 *
 * Modification History:
 *	22 Mar 87	EMT	Created.
 */

typedef struct {
    short		version;
    unsigned char	modifier[256];
    short		numTables;
    unsigned char	table[0][128];
} TransRec;

typedef struct {
    unsigned char	inChar, outChar;
} Completor;

typedef struct {
    short		numCompletors;
    Completor		compl[0];
} CompTab;

typedef struct {
    unsigned char	tabNo, keycode;
    CompTab		completors;
} DeadKey;

#define defaultKey(theComp) *(short *) &theComp->compl[theComp->numCompletors]

KeyTrans(transData, keycode, state)
    TransRec	*transData;
    short	 keycode;
    long	*state;
{
    register unsigned char	tabno = transData->modifier[keycode >> 8];
    register unsigned char	mykey = keycode & 0x7F;
    register unsigned long	result = transData->table[tabno][mykey];

    if (result) {		/* Not a dead key */
	if (*state && !(keycode & 0x80)) { /* Key down and previous key dead */
	    register CompTab	*myComp;
	    register int		i;
	    myComp = (CompTab *) ((char *) transData + *state);
	    *state = 0;
	    for (i = 0; i < myComp->numCompletors; i++) {
		if (myComp->compl[i].inChar == result) {
		    return myComp->compl[i].outChar;
		}
	    }
	    /* If we got this far, a completor was not found. */
	    result += defaultKey(myComp) << 16;
	}
    }
    else {			/* Might be a dead key */
	register int	numDead = *(short *)
	    &transData->table[transData->numTables][0];
	register DeadKey	*myDead;
	myDead = (DeadKey *) (&transData->table[transData->numTables][0] + 2);
	for (; numDead > 0; numDead--) {
	    if ((myDead->tabNo == tabno) && (myDead->keycode == mykey)) {
		/* It's a dead key */
		register CompTab	*myComp;
		myComp = &myDead->completors;
		if (keycode & 0x80) { /* Key went up */
		    result = defaultKey(myComp);
		}
		else {		/* Key went down */
		    if (*state) { /* Previous key dead as well */
			register CompTab	*xComp;
			xComp = (CompTab *) ((char *) transData + *state);
			result = defaultKey(xComp);
		    }
		    if (myComp->numCompletors) {
			*state = (char *) myComp - (char *) transData;
		    }
		    else {
			*state = 0;
			result <<= 16;
			result += defaultKey(myComp);
		    }
		}
		numDead = 0;	/* Stop the loop */
	    }
	    if (numDead) {
		myDead =
		    (DeadKey *)(&myDead->completors.compl[myDead->completors.numCompletors]
			+ 1);
	    }
	}
    }
    return result;
}
