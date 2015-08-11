#include "rtl_433.h"
#include "util.h"

/*
 * Brennstuhl RCS 2044 remote control on 433.92MHz
 *
 * Copyright (C) 2015 Paul Ortyl
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

/*
 * Receiver for the "RCS 2044 N Comfort Wireless Controller Set" sold under
 * the "Brennstuhl" brand.
 *
 * The protocol is also implemented for raspi controlled transmitter on 433.92 MHz:
 * https://github.com/xkonni/raspberry-remote
 */

static int brennstuhl_rcs_2044_process_row(int row, const bitbuffer_t *bitbuffer);
static int brennstuhl_rcs_2044_callback(bitbuffer_t *bitbuffer)
{
  int counter = 0;
  for(int row=0; row<bitbuffer->num_rows; row++)
    counter += brennstuhl_rcs_2044_process_row(row, bitbuffer);
  return counter;
}

static int brennstuhl_rcs_2044_process_row(int row, const bitbuffer_t *bitbuffer)
{
  const uint8_t *b = bitbuffer->bb[row];
  const int length = bitbuffer->bits_per_row[row];
  /* Two bits map to 2 states, 0 1 -> 0 and 1 1 -> 1 */
  int i;
  uint8_t nb[3] = {0};

#if 0
  {
    // print raw bit squence for debug purposes (before exclusion of invalid sequenced is executed)
    time_t time_now;
    char time_str[LOCAL_TIME_BUFLEN];
    time(&time_now);
    local_time_str(time_now, time_str);
    fprintf(stdout, "%s Brennstuhl RCS 2044: received RAW bit sequence (%d bits): ", time_str, length);
    for(int i=0; i<4; i++)
    {
      for(int p=7; p>=0; p--)
        fprintf(stdout, "%d", (b[i]>>p ) & 0x1);
      fprintf(stdout, " ");
    }
    fprintf(stdout, "\n");

    fprintf(stdout, "%s Brennstuhl RCS 2044: received RAW bit sequence (%d bits): ", time_str, length);
    for(int i=0; i<4; i++)
    {
      for(int p=7; p>=0; p--)
        if (p%2)
          fprintf(stdout, ".");
        else
          fprintf(stdout, "%d", (b[i]>>p ) & 0x1);
      fprintf(stdout, " ");
    }
    fprintf(stdout, "\n");

  }
#endif

  /* Test bit pattern for every second bit being 1 */
  if ( 25 != length
       || (b[0]&0xaa) != 0xaa
       || (b[1]&0xaa) != 0xaa
       || (b[2]&0xaa) != 0xaa
       || (b[3]       != 0x80) )
    return 0; /* something is wrong, exit now */

#if 0 && !defined(NDEBUG)
  {
    // print raw bit squence for debug purposes (before exclusion of invalid sequenced is executed)
    time_t time_now;
    char time_str[LOCAL_TIME_BUFLEN];
    time(&time_now);
    local_time_str(time_now, time_str);
    fprintf(stdout, "%s Brennstuhl RCS 2044: received bit sequence: ", time_str);
    for(int i=0; i<4; i++)
    {
      for(int p=6; p>=0; p-=2)
        fprintf(stdout, "%d", (b[i]>>p ) & 0x1);
      fprintf(stdout, " ");
    }
    fprintf(stdout, "\n");
  }
#endif

  /* Only odd bits contain information, even bits are set to 1
   * First 5 odd bits contain system code (the dip switch on the remote),
   * following 5 odd bits code button row pressed on the remote,
   * following 2 odd bits code button column pressed on the remote.
   *
   * One can press many buttons at a time and the corresponding code will be sent.
   * In the code below only use of a single button at a time is reported,
   * all other messages are discarded as invalid.
   */

  /* extract bits for system code */
  int system_code[] =
  {
      b[0]>>6 & 1,
      b[0]>>4 & 1,
      b[0]>>2 & 1,
      b[0]    & 1,
      b[1]>>6 & 1
  };

  /* extract bits for pressed key row */
  int control_key[] =
  {
      b[1]>>4 & 1,  /* Control Key A */
      b[1]>>2 & 1,  /* Control Key B */
      b[1]    & 1,  /* Control Key C */
      b[2]>>6 & 1,  /* Control Key D */
      b[2]>>4 & 1,  /* Control Key E (does not exists on the remote, but can
                                        be set and is accepted by receiver) */
  };

  /* extrat on/off bits (first or second key column on the remote */
  int on  = b[2]>>2 & 1;
  int off = b[2]    & 1;

  {
    /* Test if the message is valid. It is possible to press multiple keys on the
     * remote at the same time.  As all keys are transmitted orthogonally, this
     * information can be transmitted.  This use case is not the usual use case
     * so we can use it for validation of the message:
     * ONLY ONE KEY AT A TIME IS ACCEPTED.
     */
    int found=0;
    for( size_t i=0; i<sizeof(control_key)/sizeof(*control_key); i++)
    {
      if (control_key[i]) {
        if (found)
          return 0; /* at least two buttons have been pressed, reject the message */
        else
          found = 1;
      }
    }

    if (! (on ^ off ) )
      return 0;  /* Pressing simultaneously ON and OFF key is not usefull either */
  }

  char key = 0;
  if      (control_key[0]) key = 'A';
  else if (control_key[1]) key = 'B';
  else if (control_key[2]) key = 'C';
  else if (control_key[3]) key = 'D';
  else if (control_key[4]) key = 'E';
  else return 0; /* None of the keys has been pressed and we still received a message.
                    Skip it. It happens sometimes as the last code repetition */

  {
    time_t time_now;
    char time_str[LOCAL_TIME_BUFLEN];
    time(&time_now);
    local_time_str(time_now, time_str);
    fprintf(stdout, "%s Brennstuhl RCS 2044: system code: %d%d%d%d%d. key: %c, state: %s\n",
      time_str,
      system_code[0], system_code[1], system_code[2], system_code[3], system_code[4],
      key,
      on ? "ON" : ( off ? "OFF" : "BOTH" ) /* "BOTH" is excluded above, but leave it here for debug purposes */
    );
  }

  return 1;
}

r_device brennstuhl_rcs_2044 = {
  .name          = "Brennstuhl RCS 2044",
  .modulation    = OOK_PULSE_PWM_RAW,
  .short_limit   = 150,
  .long_limit    = 1000,
  .reset_limit   = 1000,
  .json_callback = &brennstuhl_rcs_2044_callback,
  .disabled      = 0,
  .demod_arg     = 0,
};
