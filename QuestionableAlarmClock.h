#ifndef __QUESTIONABLE_ALARM_CLOCK_H__
#define __QUESTIONABLE_ALARM_CLOCK_H__

/*
 * Note: this debounce code is slightly odd, due to the way I want to use it
 * 
 * It very much uses the assumption that the default state is low (ie. push to make not pressed)
 * and that we are interested in it being a short, long, or held action.
 */
#define TRUE !FALSE
#define FALSE 0

/*
 * Cut off when reading a switch attached to analog port - 1023/1024 are common, so this 
 * gives plenty of leeway.  Assume similar leeway on reading zero values.
 */
#define HIGH_VAL_MIN 1000
#define LOW_VAL_MAX 10

/*
 * Delay, in ms, used for switch debounce.  This is the time from value being first read to
 * when it is considered to have stabilised
 */ 
#define DELAY_DEBOUNCE    50
#define DELAY_SHORT_PRESS 150
#define DELAY_LONG_PRESS  350

#define TIMEOUT_MODE_GENERAL  5000 // ms
#define TIMEOUT_MODE_ALARM   15000 // ms

#define BUTTON_PRESSED(x) (((x) == BUTTON_STATE_LONG) || ((x) == BUTTON_STATE_SHORT))
#define BUTTON_HELD(x) ((x) == BUTTON_STATE_HELD)
#define BUTTON_PRESSED_OR_HELD(x) (BUTTON_PRESSED(x) || BUTTON_HELD(x))
#define BUTTON_STATE_TO_INCREMENT(x) ((x) == BUTTON_STATE_HELD ? 5 : 1)
#define BCD_GET_VALUE_TENS(bcd, valid_bitmask) (((bcd) & (valid_bitmask)) >> 4)
#define BCD_GET_VALUE_ONES(bcd) ((bcd) & 0xF)

/*
 * Voltage of RTC CR2032 battery at which point we consider it to be low, which is 2.7V
 * (from nominal voltage of 3.0V).  Value is scaled by 10, so 0-50 for 0.0-5.0V.
 */
#define RTC_BATTERY_LOW_VOLTS_X10 27 

/*
 * Each of these values represents a mode - with MAIN being the mode we fall back
 * to after a timeout.
 *
 * We have two alarms, hence a mode for each of those.
 *
 * XXX could probably draw a chart of possible transitions, which button presses
 * we are interested in at different points
 */
typedef enum {
  CLK_MODE_MAIN = 0,
  CLK_MODE_ALARM_VIEW,
  CLK_MODE_ALARM_EDIT1,
  CLK_MODE_ALARM_EDIT2,
  CLK_MODE_TIME_EDIT,
  CLK_MODE_DATE_EDIT,
  CLK_MODE_ALARM_ACTIVE,
  CLK_MODE_ALARM_QUESTION_ASK,
  CLK_MODE_ALARM_QUESTION_VALIDATE,
  CLK_MODE_CLOCK_VIEW_DETAIL,
  CLK_MODE_BATTERY_CHECK,
} clk_mode_type;

typedef enum {
  BUTTON_STATE_RELEASED,
  BUTTON_STATE_SHORT,
  BUTTON_STATE_LONG,
  BUTTON_STATE_HELD,
} button_state_type;

enum {
  ANG_PIN_RTC_BATTERY = 0,
  ANG_PIN_BUTTON_SELECT,
  ANG_PIN_BUTTON_RIGHT,
  ANG_PIN_BUTTON_LEFT,
  ANG_PIN_BUTTON_DOWN,
  ANG_PIN_BUTTON_UP,
  ANG_PIN_COUNT, // must be last
};

typedef struct {
    button_state_type last_button_state;
    long              first_pressed_time;
} button_state_store_type;

/*
 * Which clock is being edited.
 *
 * Note: code makes use of ALARM1 being 1 and ALARM2 being 2 (for printing)
 */
typedef enum {
    CLOCK_OPTION_TIME = 0,
    CLOCK_OPTION_ALARM1,
    CLOCK_OPTION_ALARM2,
    CLOCK_OPTION_COUNT,
} clock_option_type;

enum {
    TIME_DATA_SECOND = 0,
    TIME_DATA_MINUTE,
    TIME_DATA_HOUR,
    TIME_DATA_DAYOFWEEK,
    TIME_DATA_DAYOFMONTH,
    TIME_DATA_MONTH,
    TIME_DATA_YEAR,
    TIME_DATA_COUNT,
};

#endif /* __QUESTIONABLE_ALARM_CLOCK_H__ */
