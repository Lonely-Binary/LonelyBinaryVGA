#pragma once
/*
 * Why these enums live in a header instead of the .ino
 *
 * The Arduino build generates prototypes for every function in a sketch and
 * inserts them at the top of the file. If the prototype for
 * `static BtnEvent buttonPoll();` lands above the `enum BtnEvent` you declared
 * further down, the compiler says:
 *
 *     error: 'BtnEvent' does not name a type
 *
 * and points at the function, which looks entirely correct. Moving custom types
 * (enum / struct / class) into a header that the sketch includes puts them ahead
 * of the generated prototypes and the problem disappears.
 *
 * This is specific to .ino files; the identical code in a .cpp is fine.
 */

/* Gestures the button recogniser reports. */
enum BtnEvent
{
  EV_NONE = 0,
  EV_SHORT,
  EV_LONG,
  EV_DOUBLE
};

/* States of the recogniser. The current one is shown on screen so you can
 * follow it against the code. */
enum BtnState
{
  S_IDLE = 0,
  S_PRESSED,
  S_LONG_FIRED,
  S_WAIT_SECOND
};

struct Item
{
  const char *name;
  bool enabled;
};
