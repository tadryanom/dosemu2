/*
 * (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
 *
 * for details see file COPYING in the DOSEMU distribution
 */

/* KEYBOARD BIOS ROUTINE        */
#include <unistd.h>
#include <stdio.h>
#include "bios.h"
#include "cpu.h"
#include "emu.h"
#include "timers.h"
#include "int.h"
#include "memory.h"
#include "coopth.h"
#include "keyboard/keyb_server.h"

#define set_typematic_rate()
#define adjust_keyclick()
enum {
  NON_EXTENDED, EXTENDED
};

/* SUBROUTINE:	Dumb down a key for nonextend keyboards
 * on return:
 * 		-1 => ignore/ditch key, otherwise valid
 */
static unsigned do_extended(unsigned key, int extended)
{
  unsigned char scan_code = key >> 8;

  /* Ensure we actually need to do something */
  if (extended == EXTENDED || key == 0x00e0) {
    return key;
  }

  /* remove 0xE0 from AL for nonextended calls */
  /* is character 0xE0 ? */
  if ((key & 0xff) == 0xe0) {
    key &= ~0xff;           /* yes, set to 0x00 */
    if (scan_code == 0x23)  /* small cyrillic er */
      return 0x00e0;
    /* Hide new scan codes */
    return scan_code >= 0x85 ? -1 : key;
  }

  /* Not always alt keys but nearly */
  if ((key & 0xff) == 0 && (
        (scan_code == 0x01) ||
        (scan_code == 0x0e) ||
        (scan_code >= 0x1a && scan_code < 0x1d) ||
        (scan_code >= 0x27 && scan_code < 0x2a) ||
        (scan_code == 0x2b) ||
        (scan_code >= 0x33 && scan_code < 0x36) ||
        (scan_code == 0x37) ||
        (scan_code == 0x4a) ||
        (scan_code == 0x4c) ||
        (scan_code == 0x4e))) {
    return -1;		/* return ditched key */
  }

  /* Handle special cases */
  if (key == 0xe02f) {     /* DKY_PAD_SLASH */
    return 0x352f;
  }

  if (key == 0xe00d  /* DKY_PAD_ENTER */ ||
      key == 0xe00a) { /* CTRL DKY_PAD_ENTER */
    return (key & 0xff) | 0x1c00;
  }

  return (scan_code >= 0x85 ? -1 : key); /* Hide new scan codes */
}

/* SUBROUTINE:	Get a key from the buffer if there is one.
 * Translate that key if we aren't an extended key service.
 *		returns current value of BIOS_KEYBOARD_BUFFER_HEAD
 *		and sets ax=key if a key is available, else
 *		returns -1
 */
static unsigned get_key(int blocking, int extended)
{
  unsigned key = -1;
  unsigned keyptr;

  do {
    /* get address of next char	*/
    while ((keyptr = READ_WORD(BIOS_KEYBOARD_BUFFER_HEAD)) ==
        READ_WORD(BIOS_KEYBOARD_BUFFER_TAIL)) {
      if (!blocking) {
        _EFLAGS |= ZF;
        return -1;
      }
      set_IF();
      coopth_wait();
      clear_IF();
    }
    /* differences for extended calls */
    key = do_extended(READ_WORD(BIOS_DATA_SEG + keyptr), extended);
    if (key == -1) {
        keyptr += 2;
        /* check for wrap around	*/
        if (keyptr == READ_WORD(BIOS_KEYBOARD_BUFFER_END)) {
          /* wrap - get buffer start	*/
          keyptr = READ_WORD(BIOS_KEYBOARD_BUFFER_START);
        }
        /* save it as new pointer	*/
        WRITE_WORD(BIOS_KEYBOARD_BUFFER_HEAD, keyptr);
    }
  } while (key == -1);
  LWORD(eax) = key;
  _EFLAGS &= ~ZF;
  return keyptr;
}

static unsigned check_key_available(int extended)
{
  unsigned keyptr = get_key(0, extended);
  if(keyptr == -1) {
    if(!port60_ready)
      trigger_idle();
    else
      reset_idle(0);
    idle_enable2(500, 20, 2, "int16");
  } else {
    reset_idle(1);
  }
  return get_key(0, extended);
}

static void read_key(int extended)
{
  unsigned keyptr = get_key(1, extended);

  if (keyptr == -1) {
    /* should not be here - blocking call */
    return;
  }

  keyptr += 2;
  /* check for wrap around        */
  if (keyptr == READ_WORD(BIOS_KEYBOARD_BUFFER_END)) {
    /* wrap - get buffer start	*/
    keyptr = READ_WORD(BIOS_KEYBOARD_BUFFER_START);
  }
  /* save it as new pointer	*/
  WRITE_WORD(BIOS_KEYBOARD_BUFFER_HEAD, keyptr);
}

static void get_shift_flags(void)
{
  unsigned char fl1, fl2, fl3;

  fl1 = READ_BYTE(BIOS_KEYBOARD_FLAGS1);
  /* get extended shift flags*/
  fl2 = READ_BYTE(BIOS_KEYBOARD_FLAGS2);
  fl3 = READ_BYTE(BIOS_KEYBOARD_FLAGS3);
  /* disallow bits 2, 3, & 7 	*/

  /* clear everything but sysreq (0x04) in fl2 */
  /* and move it to the correct bit            */

  /* bits 2 & 3 come from fl3	*/

  LWORD(eax) = (((fl2 & 0x73) | ((fl2 & 0x04) << 5) | (fl3 & 0xc)) << 8) | fl1;
}

/* SUBROUTINE: store keycode in the BIOS keyboard buffer,
 * return 1 if buffer is not full.
 */
static int store_key(unsigned keycode)
{
  unsigned keyptr;

  keyptr = READ_WORD(BIOS_KEYBOARD_BUFFER_TAIL) + 2;
  if (keyptr == READ_WORD(BIOS_KEYBOARD_BUFFER_END))
    keyptr = READ_WORD(BIOS_KEYBOARD_BUFFER_START);

  if (READ_WORD(BIOS_KEYBOARD_BUFFER_HEAD)==keyptr) { /* full */
    return 0;
  }

  WRITE_WORD(BIOS_DATA_SEG + READ_WORD(BIOS_KEYBOARD_BUFFER_TAIL), keycode);
  WRITE_WORD(BIOS_KEYBOARD_BUFFER_TAIL, keyptr);
  return 1;
}

static void store_key_in_buffer(void)
{
  /* return al=0 (success)	*/
  /* return al=1 (buffer full)	*/
  LO(ax) = (store_key(LWORD(ecx)) ? 0 : 1);
}

static void get_kbd_features(void)
{
  _AX = 0x20;  /* enh kbd functionality, nothing else */
}

int int16(void)
{
  switch(HI(ax))
  {
  case 0:
    int_yield();
    read_key(NON_EXTENDED);
    break;
  case 1:
    int_yield();
    check_key_available(NON_EXTENDED);
    break;
  case 2:
  case 0x12:
    int_yield();
    get_shift_flags();
    break;
  case 3:
    set_typematic_rate();
    break;
  case 4:
    adjust_keyclick();
    break;
  case 5:
    store_key_in_buffer();
    break;
  case 0x9:
    get_kbd_features();
    break;
  case 0x10:
    int_yield();
    read_key(EXTENDED);
    break;
  case 0x11:
    int_yield();
    check_key_available(EXTENDED);
    break;
  default:
    break;
  }
  return 1;
}
