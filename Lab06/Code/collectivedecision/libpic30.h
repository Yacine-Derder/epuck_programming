/*
 * Copyright 2007 Microchip Technology, all rights reserved
 * 
 * This file defines useful exports from libpic30.a
 * Not all functions defined in libpic30.a can be usefully called by
 * the user, most are helper functions for the standard C library, libc.a
 *
 */

#ifndef __LIBPIC30_H
#define __LIBPIC30_H

/*
 * The following two functions allow the programmer to attach a file to
 * standard input, and later close the file.  The functions are only useful
 * if the executable will be simulated.
 *
 * With these functions input from the file will be read on-demmand, otherwise
 * the programmer should use the message based stimulus (information on this
 * can be found within MPLAB IDE's help pages).
 */

extern int __attach_input_file(const char *f);
void __close_input_file(void);

/*
 * __C30_UART selects the default UART port that read() and write() will use.  
 * read() is called by fscanf and family, while write() is called by printf 
 * and family.  The default setting (as shipped) is 1, which is suitable for 
 * the dsPICdem 1.1(tm) board.   Modifying this to another value will select
 * UART 2, suitable for the explorer 16(tm) board.
 */

extern int __C30_UART;

/*
 * __delay32() provides a 32-bit delay routine which will delay for the number
 * of cycles denoted by its argument.  The minimum delay is 11 cycles
 * including call and return statements.  With this function only the time
 * required for argument transmission is not accounted for.  Requests for
 * less than 11 cycles of delay will cause an 11 cycle delay.
 */

extern void __delay32(unsigned long cycles);

/*
 * _PROGRAM_END is a symbol in program memory that marks the highest
 * address used by a CODE or PSV section. It should be referenced
 * with the address operator (&) in a built-in function call that
 * accepts the address of an object in program memory.
 *
 * For example:
 *
 *  __builtin_tblpage(&_PROGRAM_END)
 *  __builtin_tbloffset(&_PROGRAM_END)
 *
 *  _prog_addressT big_addr;
 *  _init_prog_address(big_addr, &_PROGRAM_END)
 */

extern __attribute__((space(prog))) int _PROGRAM_END;

/*
 * __memcpy_helper() - not a user-callable function
 *
 * Copies data from program memory to data memory. It does
 * not require the Program Space Visibility (PSV) window. However, it
 * does change the value of TBLPAG during execution.
 *
 * The source address in program memory is specified by a tbl_offset,
 * tbl_page pair. Flags indicate whether the upper byte should be
 * copied, and whether to terminate early after a NULL byte.
 * The return value is an updated source address pair.
 *
 * The number of bytes copied may be even or odd. The source address
 * is always even and is always aligned to the start of the next
 * program memory word.
 */
typedef struct _prog_address_tag {
  union {
    struct {
      int low;
      int high;
    } w;
    long next;
  };
} _prog_addressT;

extern _prog_addressT _memcpy_helper(unsigned int tbl_offset, unsigned int tbl_page,
                                     char *dst, unsigned int len, int flags);

/*
 * Initialize a variable of type _prog_addressT.
 *
 * These variables are not equivalent to C pointers,
 * since each source address corresponds to 2 or 3 chars.
 */

#define _init_prog_address(a,b) {      \
    a.w.low  = __builtin_tbloffset(b); \
    a.w.high = __builtin_tblpage(b);   \
  };

/*
 * _memcpy_p2d16() copies 16 bits of data from each address
 * in program memory to data memory. The source address is
 * specified as type _prog_addressT; the next unused address
 * is returned.
 */

static __inline__ _prog_addressT
_memcpy_p2d16(char *dest, _prog_addressT src, unsigned int len) {
  return _memcpy_helper(src.w.low, src.w.high, dest, len, 0);
}

/*
 * _memcpy_p2d24() copies 24 bits of data from each address
 * in program memory to data memory. The source address is
 * specified as type _prog_addressT; the next unused address
 * is returned.
 */

static __inline__ _prog_addressT
_memcpy_p2d24(char *dest, _prog_addressT src, unsigned int len) {
  return _memcpy_helper(src.w.low, src.w.high, dest, len, 1);
}

/*
 * _strncpy_p2d16() copies 16 bits of data from each address
 * in program memory to data memory. The operation terminates
 * early if a NULL char is copied. The source address is
 * specified as type _prog_addressT; the next unused address
 * is returned.
 */

static __inline__ _prog_addressT
_strncpy_p2d16(char *dest, _prog_addressT src, unsigned int len) {
  return _memcpy_helper(src.w.low, src.w.high, dest, len, 2);
}

/*
 * _strncpy_p2d24() copies 24 bits of data from each address
 * in program memory to data memory. The operation terminates
 * early if a NULL char is copied. The source address is
 * specified as type _prog_addressT; the next unused address
 * is returned.
 */

static __inline__ _prog_addressT
_strncpy_p2d24(char *dest, _prog_addressT src, unsigned int len) {
  return _memcpy_helper(src.w.low, src.w.high, dest, len, 3);
}

/*
 * Constants for erasing and writing FLASH and EEPROM
 *
 * Lengths in EEDATA memory = bytes
 * Lengths in FLASH memory = words
 *  (1 word = 3 bytes = 2 PC address units)
 */

/* constants for 30F devices */
#ifdef __dsPIC30F__
#define _FLASH_ERASE_CODE                 0x4041
#define _FLASH_WRITE_ROW_CODE             0x4001
#define _EE_ERASE_WORD_CODE               0x4044
#define _EE_ERASE_ROW_CODE                0x4045
#define _EE_ERASE_ALL_CODE                0x4046
#define _EE_WORD_WRITE_CODE               0x4004
#define _EE_ROW_WRITE_CODE                0x4005

#define _FLASH_PAGE     32
#define _FLASH_ROW      32
#define _EE_WORD         2
#define _EE_ROW         32

/* constants for 33F devices */
#elif defined(__dsPIC33F__) 
#define _FLASH_ERASE_CODE                 0x4042
#define _FLASH_ERASE_GENERAL_SEGMENT_CODE 0x404D
#define _FLASH_ERASE_SECURE_SEGMENT_CODE  0x404C
#define _FLASH_WRITE_ROW_CODE             0x4001
#define _FLASH_WRITE_WORD_CODE            0x4003

#define _FLASH_PAGE   512
#define _FLASH_ROW     64

/* constants for 24H devices */
#elif defined(__PIC24H__)
#define _FLASH_ERASE_CODE                 0x4042
#define _FLASH_ERASE_GENERAL_SEGMENT_CODE 0x404D
#define _FLASH_ERASE_SECURE_SEGMENT_CODE  0x404C
#define _FLASH_WRITE_ROW_CODE             0x4001
#define _FLASH_WRITE_WORD_CODE            0x4003

#define _FLASH_PAGE   512
#define _FLASH_ROW     64

/* constants for 24F devices */
#elif defined(__PIC24F__)
#define _FLASH_ERASE_CODE      0x4042
#define _FLASH_WRITE_ROW_CODE  0x4001
#define _FLASH_WRITE_WORD_CODE 0x4003

#define _FLASH_PAGE   512
#define _FLASH_ROW     64

#endif

/*
 * The following helper functions are not user-callable,
 * and may change in the future. The purpose of these
 * functions is to provide an abstraction layer where
 * device or family specific parameters can be specified.
*/
extern void _eedata_helper1(unsigned int tbl_offset, unsigned int tbl_page,
                            int len);
extern void _eedata_helper2(void);
extern void _eedata_helper3(unsigned int tbl_offset, unsigned int tbl_page,
                            int dat);
extern void _eedata_helper4(unsigned int tbl_offset, unsigned int tbl_page,
                            int *src);
extern void _flash_helper1 (unsigned int tbl_offset, unsigned int tbl_page,
                            int code);
extern void _flash_helper2 (unsigned int tbl_offset, unsigned int tbl_page,
                            int *src, int len);
extern void _flash_helper3 (unsigned int tbl_offset, unsigned int tbl_page,
                            long *src, int len);
extern void _flash_helper4 (unsigned int tbl_offset, unsigned int tbl_page,
                            int dat);
extern void _flash_helper5 (unsigned int tbl_offset, unsigned int tbl_page,
                            long dat);
extern void _flash_helper6 (int code);

#ifdef __dsPIC30F__

/*
 * _erase_eedata() erases a word or a row of
 * eedata memory on 30F devices. The address is
 * specified with type _prog_addressT. The length
 * may be _EE_WORD or _EE_ROW (bytes).
*/
static __inline__
void _erase_eedata(_prog_addressT dst, int len) {
_eedata_helper1(dst.w.low, dst.w.high, len);
}

/*
 * _erase_eedata_all() erases the entire
 * range of eedata memory on 30F devices.
 */
#define _erase_eedata_all _eedata_helper2

/*
 * _wait_eedata() waits for an erase
 * or write operation to complete.
 */
#define _wait_eedata() { while (NVMCONbits.WR); }

/*
 * _write_eedata_word() writes 16 bits of
 * eedata memory on 30F devices. The address is
 * specified with type _prog_addressT.
*/
static __inline__
void _write_eedata_word(_prog_addressT dst, int dat) {
_eedata_helper3(dst.w.low, dst.w.high, dat);
}

/*
 * _write_eedata_row() writes __EE_ROW bytes
 * of eedata memory on 30F devices. The address is
 * specified with type _prog_addressT.
*/
static __inline__
void _write_eedata_row(_prog_addressT dst, int *src) {
_eedata_helper4(dst.w.low, dst.w.high, src);
}
#endif

#if defined(__dsPIC33F__) || defined(__PIC24H__) || defined(__dsPIC30F__) || defined(__PIC24F__)

/*
** Functions for Erasing and Writing FLASH
**
** For erasing, size = _FLASH_PAGE (words)
**
** For writing, size = _FLASH_ROW (words),
**   the source may specify 16 or 24 bits per word.
**
** On 33F and 24H devices, single word writes
**   are also available (16 or 24 bits)
*/

/*
 * _erase_flash erases a page of FLASH memory.
 * The page address is specified with type
 * _prog_addressT.
 */
static __inline__
void _erase_flash(_prog_addressT dst) {
_flash_helper1(dst.w.low, dst.w.high, _FLASH_ERASE_CODE);
}

/*
 * _write_flash16() writes a row of FLASH memory
 * with 16-bit data. The row address is specified with
 * type _prog_addressT. The source address is an
 * array of type int, with _FLASH_ROW elements.
 * Note that the row must be erased before
 * any write can be successful.
 *
 * This function includes a workaround for
 * the YGAG Device ID errata.
 */
static __inline__
void _write_flash16(_prog_addressT dst, int *src) {
_flash_helper2(dst.w.low, dst.w.high, src, _FLASH_ROW);
}

/*
 * _write_flash24() writes a row of FLASH memory
 * with 24-bit data. The row address is specified with
 * type _prog_addressT. The source address is an
 * array of type long, with _FLASH_ROW elements.
 * Note that the row must be erased before
 * any write can be successful.
 *
 * This function includes a workaround for
 * the YGAG Device ID errata.
 */
static __inline__
void _write_flash24(_prog_addressT dst, long *src) {
_flash_helper3(dst.w.low, dst.w.high, src, _FLASH_ROW);
}
#endif

#if defined(__dsPIC33F__) || defined(__PIC24H__) || defined(__PIC24F__)

/*
 * _write_flash_word16() writes a word of FLASH memory
 * with 16 bits of data. Word writes are supported on
 * 33F, 24H, and 24F devices. The row address is specified
 * with type _prog_addressT. Note that the location must
 * be erased before any write can be successful.
 *
 * This function is currently disabled for 33F and 24H
 * families because of the YGAG Device ID errata.
 */
#if defined(__PIC24F__)
static __inline__
void _write_flash_word16(_prog_addressT dst, int dat) {
_flash_helper4(dst.w.low, dst.w.high, dat);
}
#endif

/*
 * _write_flash_word24() writes a word of FLASH memory
 * with 24-bits of data.  Word writes are supported on
 * 33F, 24H, and 24F devices. The row address is specified
 * with type _prog_addressT. Note that the location must
 * be erased before any write can be successful.
 *
 * This function is currently disabled for 33F and 24H
 * families because of the YGAG Device ID errata.
 */
#if defined(__PIC24F__)
static __inline__
void _write_flash_word24(_prog_addressT dst, long dat) {
_flash_helper5(dst.w.low, dst.w.high, dat);
}
#endif

#endif

#endif
