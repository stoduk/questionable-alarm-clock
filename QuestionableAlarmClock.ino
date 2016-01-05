// status: working in old Arduino, not yet ported to new Arduino beyond changing the file name
#include <pins_arduino.h>
#include <LiquidCrystal.h>
#include "QuestionableAlarmClock.h"

#if !defined(__AVR_ATmega328P__)
/*
 * Only support the single board - presumably could support all 128/328 as they
 * are pin compatible.  The major problem here would be moving to more substantially
 * different chips - where SPI pins are different
 */
#error compile fail - board not as expected
#endif

enum {
    DIG_PIN_ALARM_OUTPUT = 0,   // Using D0 and D1 means I can't use serial port any more!
    DIG_PIN_RTC_CHIPSELECT = 1,  // chip select (ss/ce) for RTC, active high
    DIG_PIN_RTC_INTERRUPT = 2,
    DIG_PIN_UNUSED3 = 3, // for Atomic clock module interrupt later..
    DIG_PIN_LCD_D7 = 4,
    DIG_PIN_LCD_D6 = 5,
    DIG_PIN_LCD_D5 = 6,
    DIG_PIN_LCD_D4 = 7,
    DIG_PIN_LCD_ENABLE = 8,
    DIG_PIN_LCD_RS = 9,
    DIG_PIN_BACKLIGHT = 10,
    DIG_PIN_DATAOUT = MOSI,    // fixed by Arduino
    DIG_PIN_DATAIN = MISO,     // fixed by Arduino
    DIG_PIN_SCK = SCK,         // fixed by Arduino
};

#define BACKLIGHT_LEVEL_MAX 80
#define BACKLIGHT_LEVEL_MIN 0
#define BACKLIGHT_LEVEL_INCR 1
#define BACKLIGHT_LEVEL_DECR 2
#define BACKLIGHT_TIMEOUT 2500 // return to stored value after N ms

LiquidCrystal lcd(DIG_PIN_LCD_RS, DIG_PIN_LCD_ENABLE, DIG_PIN_LCD_D4, DIG_PIN_LCD_D5, DIG_PIN_LCD_D6, DIG_PIN_LCD_D7);

static button_state_store_type button_state_store[ANG_PIN_COUNT] = {};
/*
 * The time we switched modes - used to timeout a given mode
 */
static long mode_switch_time = 0;
static long mode_expire_time = 0;
static clk_mode_type mode = CLK_MODE_MAIN;

/*
 * Data read from the RTC for time or alarm
 */
static byte time_data[TIME_DATA_COUNT];

/*
 * Flag to show which alarms are enabled (cleared on startup, set from alarm view mode)
 */
static int alarm_enabled[CLOCK_OPTION_COUNT] = { FALSE, FALSE, FALSE};

/*
 * Track how many questions need to be answered before disabling the alarm
 */
static int questions_remaining = 0;

/*
 * HACK: unfortuately it seems that intermittently the alarm fails to trigger - even though I can see from debugging that we are writing valid data, and when reading the data back (for alarm time view) it is valid.
 *
 * To get things moving, resort to a massive hack.  We read the clock time every loop anyway (to check if display needs updating)
 */
static bool check_alarm_brute_force = FALSE;

/*
 * Even bigger hack to temporarily make it obvious how an alarm was triggered
 */
static bool brute_force_alarm_triggered = FALSE;

//Function SPI_Transfer
char spi_transfer(volatile char data)
{
    /*
    Writing to the SPDR register begins an SPI transaction
    */
    SPDR = data;
    /*
    Loop here until the transaction complete.
    SPIF bit = SPI Interrupt Flag. When interrupts are enabled, &
    SPIE bit = 1 enabling SPI interrupts, this bit sets =1 when
    transaction finished.
    */
    while (!(SPSR & (1<<SPIF)))
    {
    
    };
    // received data appears in the SPDR register
    
    return SPDR;
}

void write_rtc_register(char register_name, byte data)
{
    write_register(register_name, data, DIG_PIN_RTC_CHIPSELECT, HIGH, true, true);
}

char read_rtc_register(char register_name)
{
    return read_register(register_name, DIG_PIN_RTC_CHIPSELECT, HIGH, false, true);
}

// reads a register
char read_register(char register_name, byte cs_pin, byte cs_active_level, boolean read_high, boolean cpha_trailing)
{
    byte in_byte; // why was this char?  we don't want unsigned, surely??
    if(cpha_trailing)
    {
        SPCR = (1<<SPE)|(1<<MSTR)|(1<<CPHA)|(0<<SPR1)|(0<<SPR0);
    }
    else
    {
        SPCR = (1<<SPE)|(1<<MSTR)|(0<<CPHA)|(0<<SPR1)|(0<<SPR0);
    }
    
    if(read_high)
    {
        // need to set bit 7 to indicate a read for the slave device
        register_name |= 128;
    }
    else
    {
        // if read low, means A7 bit should be cleared when reading for the slave device
        register_name &= 127;
    }
    // SS is active low
    digitalWrite(cs_pin, cs_active_level);
    // send the address of the register we want to read first
    spi_transfer(register_name);
    // send nothing, but here‘s when the device sends back the register‘s value as an 8 bit byte
    in_byte = spi_transfer(0);
    // deselect the device..
    if(cs_active_level == HIGH)
    {
        digitalWrite(cs_pin, LOW);
    }
    else
    {
        digitalWrite(cs_pin, HIGH);
    }
    return in_byte;
}

// write to a register
// write_high if true indicates set A7 bit to 1 during a write
void write_register(char register_name, byte data, byte cs_pin, byte cs_active_level, boolean write_high, boolean cpha_trailing)
{
if(cpha_trailing)
{
SPCR = (1<<SPE)|(1<<MSTR)|(1<<CPHA)|(0<<SPR1)|(0<<SPR0);
}
else
{
SPCR = (1<<SPE)|(1<<MSTR)|(0<<CPHA)|(0<<SPR1)|(0<<SPR0);
}
if(write_high)
{
// set A7 bit to 1 during a write for this device
register_name |= 128;
}
else
{
// clear bit 7 to indicate we‘re doing a write for this device
register_name &= 127;
}
// SS is active low
digitalWrite(cs_pin, cs_active_level);
// send the address of the register we want to write
spi_transfer(register_name);
// send the data we‘re writing
spi_transfer(data);
if(cs_active_level == HIGH)
{
digitalWrite(cs_pin, LOW);
}
else
{
digitalWrite(cs_pin, HIGH);
}
}


byte custchar[8][8] = {
 {
   B11111,
   B11111,
   B11111,
   B00000,
   B00000,
   B00000,
   B00000,
   B00000
 }, {
   B00000,
   B00000,
   B00000,
   B00000,
   B00000,
   B11111,
   B11111,
   B11111
 }, {
   B11111,
   B11111,
   B11111,
   B00000,
   B00000,
   B11111,
   B11111,
   B11111
 }, {
   B00000,
   B00000,
   B00000,
   B00000,
   B00000,
   B01110,
   B01110,
   B01110
 }, {
   B00000,
   B00000,
   B00000,
   B01110,
   B01110,
   B01110,
   B00000,
   B00000
 }, {
   B00000,
   B00100,
   B01110,
   B01110,
   B11111,
   B11111,
   B00100,
   B00000
 }, {
   B00110,
   B01001,
   B01001,
   B01111,
   B01111,
   B01111,
   B01111,
   B00000
 }, {
   B00001,
   B00001,
   B00010,
   B00010,
   B00100,
   B10100,
   B01000,
   B00000
 }
};

byte bignums[10][2][3] = {
 {
   {255, 0, 255},
   {255, 1, 255}
 },{
   {0, 255, 254},
   {1, 255, 1}
 },{
   {2, 2, 255},
   {255, 1, 1}
 },{
   {0, 2, 255},
   {1, 1, 255}
 },{
   {255, 1, 255},
   {254, 254, 255}
 },{
   {255, 2, 2},
   {1, 1, 255}
 },{
   {255, 2, 2},
   {255, 1, 255}
 },{
   {0, 0, 255},
   {254, 255, 254}
 },{
   {255, 2, 255},
   {255, 1, 255}
 },{
   {255, 2, 255},
   {254, 254, 255}
 }
};

void loadchars() {
 lcd.command(64);
 for (int i = 0; i < 8; i++)
   for (int j = 0; j < 8; j++)
     lcd.write(custchar[i][j]);
 lcd.home();
}
void printbigchar(byte digit, byte col, byte row, byte symbol = 0) {
 if (digit > 9) return;
 for (int i = 0; i < 2; i++) {
   lcd.setCursor(col, row + i);
   for (int j = 0; j < 3; j++) {
     lcd.write(bignums[digit][i][j]);
   }
   lcd.write(254);
 }
 if (symbol == 1) {
   lcd.setCursor(col + 3, row + 1);
   lcd.write(3);
 } else if (symbol == 2) {
   lcd.setCursor(col + 3, row);
   lcd.write(4);
   lcd.setCursor(col + 3, row + 1);
   lcd.write(4);
 }
 
 lcd.setCursor(col + 4, row);
}

static void
read_rtc_time_data (clock_option_type  clock,
                    byte              *time_data)
{
    byte offset = 0;
    switch (clock) {
    case CLOCK_OPTION_TIME:
        offset = 0x00;
        break;
    case CLOCK_OPTION_ALARM1:
        offset = 0x07;
        break;
    case CLOCK_OPTION_ALARM2:
        offset = 0x0B;
        break;
    default:
        break;
    }
    
    for (int i = 0; i < 7; i++) {
        if (i <= TIME_DATA_DAYOFWEEK || clock == CLOCK_OPTION_TIME) {
            time_data[i] = read_rtc_register(offset + i);
        } else {
            time_data[i] = 0; // alarm should not look at year info, but clear to be sure
        }
    }
    
    return;
}

static void
write_rtc_time_data (clock_option_type  clock,
                     byte              *time_data)
{
    byte offset = 0;
    
    Serial.print("Writing time data for ");
        
    switch (clock) {
    case CLOCK_OPTION_TIME:
        offset = 0x80;
        Serial.print("clock");
        break;
    case CLOCK_OPTION_ALARM1:
        offset = 0x87;
        Serial.print("alarm 1");
        break;
    case CLOCK_OPTION_ALARM2:
        offset = 0x8B;
        Serial.print("alarm 2");
        break;
    default:
        break;
    }
    
    /*
     * As per the data sheet, update seconds first (as this resets the internal clock
     * and so as long as we update everything within a second the second value won't
     * increment and cause us problems with values being out of step).
     *
     * Cleverly, seconds is the first value numerically so that just falls out.
     *
     * XXX this currently doesn't mask any values being written - which should be ok 
     * (as writing the numbers should have ensured that the top bits are not set, and we 
     * don't use 12 hour mode currently, so that bit doesn't need setting).
     */
    Serial.print(" "); 
    for (int i = 0; i < 7; i++) {
        if (i <= TIME_DATA_DAYOFWEEK || clock == CLOCK_OPTION_TIME) {
            Serial.print(" i:");
            Serial.print(i);
            Serial.print("-");
            Serial.print(time_data[i], HEX);
            Serial.print(" ");
            /*
             * Make any modifications needed to data (either to set alarm, or keep values legal)
             *
             * Could validate that all values are falling in range, but for now assume numbers
             * are sane and so just setting the special bits (ie. bits to set/unset alarms, or bits 
             * that should always be zero
             */
            switch (i) {
            case TIME_DATA_SECOND:
            case TIME_DATA_MINUTE:
            case TIME_DATA_HOUR:
            case TIME_DATA_DAYOFMONTH:
            case TIME_DATA_MONTH:
                // clear top bit
                time_data[i] &= 0x7F;
                break;
            case TIME_DATA_DAYOFWEEK:
                // check that bottom 4 bits give value between 1 and 7, if not set to 1
                // set the top bit for alarm, clear for clock
                if (((time_data[i] & 0xF) > 0x7) || ((time_data[i] & 0xF) == 0)) {
                    time_data[i] = 0x1;
                }
                if (clock == CLOCK_OPTION_TIME) {
                    time_data[i] &= 0x7F;
                } else {
                    time_data[i] |= 0x80;
                }
                break;
            case TIME_DATA_YEAR:
                // no modification wanted
                break;
            default:
                break;
            }
            Serial.print(time_data[i], HEX);
            Serial.print(";");
            write_rtc_register(offset + i, time_data[i]);
        }
    }
    Serial.print("\n");
    
    return;
}

void setup() {
    // LCD first
    loadchars();
    lcd.begin(2, 16);
 
    // ART: temp
    lcd.blink();
 
    // RTC next
    byte in_byte = 0;
    
    /*
     * Serial being enabled really slows things down, but is vital for debugging.
     * So enable if the select button is held down at power up.
     *
     * Since the pin re-org and the need for more pins, I am now using D0/1 - and so
     * cannot use serial any more.  Could probably hack this up somehow if needed..
     */
    if (analogRead(ANG_PIN_BUTTON_SELECT) > HIGH_VAL_MIN) {
        Serial.begin(9600);
    }
    
    // set direction of pins
    
    // interrupt pin: set output high to enable pull-up resistor (needed by RTC)
    pinMode(DIG_PIN_RTC_INTERRUPT, INPUT);
    digitalWrite(DIG_PIN_RTC_INTERRUPT, HIGH);
    
    pinMode(DIG_PIN_BACKLIGHT, OUTPUT);
    pinMode(DIG_PIN_ALARM_OUTPUT, OUTPUT);
    pinMode(DIG_PIN_DATAOUT, OUTPUT); 
    pinMode(DIG_PIN_DATAIN, INPUT);
    pinMode(DIG_PIN_SCK, OUTPUT);
    
    // RTC chip-select: set low initially, as we aren't talking to RTC yet
    pinMode(DIG_PIN_RTC_CHIPSELECT, OUTPUT);
    digitalWrite(DIG_PIN_RTC_CHIPSELECT, LOW); //disable RTC
    
    // ART: before we set anything, check to see what values the chip reports
    
    for (int i = 0; i < CLOCK_OPTION_COUNT; i++) {
        read_rtc_time_data((clock_option_type)i, &time_data[0]);
        
        if (i == CLOCK_OPTION_TIME) {
            Serial.println("Clock data:");
            
            in_byte = time_data[TIME_DATA_YEAR];
            Serial.print("YEAR [");
        
            Serial.print(in_byte, HEX);
            Serial.println("]");
            
            in_byte = time_data[TIME_DATA_MONTH];
            Serial.print("MONTH [");
            Serial.print(in_byte, HEX);
            Serial.println("]");
    
            in_byte = time_data[TIME_DATA_DAYOFMONTH];
            Serial.print("DAY OF MONTH [");
            Serial.print(in_byte, HEX);
            Serial.println("]");
        } else {
            Serial.print("Alarm ");
            Serial.print(i);
            Serial.println(" data:");
        }
        
        in_byte = time_data[TIME_DATA_DAYOFWEEK];
        Serial.print("DAY OF WEEK [");
        Serial.print(in_byte, HEX);
        Serial.println("]");
        
        in_byte = time_data[TIME_DATA_SECOND];
        Serial.print(" SECS=");
        Serial.print(in_byte, HEX);
        in_byte = time_data[TIME_DATA_MINUTE];
        Serial.print(" MINS=");
        Serial.print(in_byte, HEX);
        in_byte = time_data[TIME_DATA_HOUR];
        Serial.print(" HRS=");
        Serial.println(in_byte, HEX);
    }


    // /ART    
    // set up the RTC by enabling the oscillator, disabling the write protect in the control register,
    // enabling AIE0 and AIE1 and the 1HZ Output
    
    // 0×8F to 00000111 = 0×07
    // EOSC Active Low
    
    // WP Active High, so turn it off
    in_byte = read_rtc_register(0x0F);
    
    Serial.print("CTRL REG [");
    Serial.print(in_byte, HEX);
    
    /*
     * Enable both alarm status bits to trigger interrupts (0x1|0x2 set), but use a single interrupt pin (0x4 clear)
     * Enable oscillator and write protect (0x80|0x40 both cleared).  Rest of bits unused, so clear them.
     */
    write_rtc_register(0x8F,0x01|0x02);
    Serial.println("]");
    delay(10);
    
    in_byte = read_rtc_register(0x10);
    Serial.print("STATUS REG [");
    
    Serial.print(in_byte, BIN);
    Serial.println("]");
    
    /*
     * Alarms do not persist across power failure (only time/date)
     *
     * So set the two alarms to some sensible defaults - 08:00 and 09:00, both disabled, both with some valid day (1).
     */
    // set up both alarms at 00 seconds?
    write_rtc_register(0x87,0x00); // A1 seconds (00)
    write_rtc_register(0x88,0x00); // A1 minutes (00)
    write_rtc_register(0x89,0x08); // A1 hours (08 in 24 hour)
    write_rtc_register(0x8A,0x81); // A1 DayOfWeek (1), day needn't match for alarm to trigger
    
    write_rtc_register(0x8B,0x00); // A2 seconds (00)
    write_rtc_register(0x8C,0x00); // A2 minutes (00)
    write_rtc_register(0x8D,0x09); // A2 hours (09 in 24 hour)
    write_rtc_register(0x8E,0x81); // A2 DayOfWeek (1), day needn't match for alarm to trigger
    
    if (analogRead(ANG_PIN_BUTTON_UP) > HIGH_VAL_MIN) {
        // temp code for testing - set both alarms to be the current time
        // just care about hours and minutes (keep seconds zero, keep day as it is)
        in_byte = read_rtc_register(0x1);
        write_rtc_register(0x88, in_byte);
        write_rtc_register(0x8C, in_byte);
        
        in_byte = read_rtc_register(0x2);
        write_rtc_register(0x89, in_byte);
        write_rtc_register(0x8D, in_byte);
    }
    
    in_byte = read_rtc_register(0x06);
    Serial.print("YEAR [");
    
    Serial.print(in_byte, HEX);
    Serial.println("]");
    
    in_byte = read_rtc_register(0x05);
    Serial.print("MONTH [");
    
    Serial.print(in_byte, HEX);
    Serial.println("]");
    
    digitalWrite(DIG_PIN_ALARM_OUTPUT, HIGH);

//#define SETTING_TIME    
#ifdef SETTING_TIME
#define TMP_TIME_HR    0x16 // 22:15:00
#define TMP_TIME_MIN   0x52
#define TMP_TIME_SEC   0x00
#define TMP_DAY_WEEK   0x04 // Sun
#define TMP_DATE_DAY   0x29 // 19th
#define TMP_DATE_MONTH 0x12 // June
#define TMP_DATE_YEAR  0x11 // 2011
    // time is stored in BCD, so setting the value in hex falls out to work.  Trust me :)
    // day of week is arbitrary, choose 1=Mon (range is 1-7)
    Serial.println("Setting time to ");
    Serial.println(TMP_TIME_HR, HEX);
    Serial.println(":");
    Serial.println(TMP_TIME_MIN, HEX);
    Serial.println(":");
    Serial.println(TMP_TIME_SEC, HEX);
    write_rtc_register(0x80, TMP_TIME_SEC);
    write_rtc_register(0x81, TMP_TIME_MIN);
    write_rtc_register(0x82, TMP_TIME_HR);
    write_rtc_register(0x83, TMP_DAY_WEEK); // Sun
    write_rtc_register(0x84, TMP_DATE_DAY); // 19th
    write_rtc_register(0x85, TMP_DATE_MONTH); // June
    write_rtc_register(0x86, TMP_DATE_YEAR); // 2011
#endif
}

static void 
update_clock_time (void)
{
    static byte last_seconds = 0;
    static int last_led_state = 0;
    static long updated = 0;
    int led_state;
    byte in_byte;
    int update = FALSE;
    int force_update = FALSE;
    
    /*
     * Rather than talking to RTC once a loop (which is every 2ms or so!)
     * we can just do it twice a second.  Once a second would probably be enough
     * but this is simple..
     *
     * Also only want to update the LCD when needed - else the display will flicker
     *
     * So use 'led_state' (which sets the 0.5s heartbeat LED) to determine whether
     * anything needs doing - when led_state has changed we want to update the display
     * (to blink the cursor at least), and (arbitrarily) when led_state is TRUE we
     * re-read the clock data (and set the brute force alarm flag if needed).
     *
     * Note: this code obviously assumes we come through this loop more than twice
     * a second - which should always be safe, as we loop ever 2ms or so.  [if that
     * weren't true then if we could enter loop >= 0.5s apart then we might break
     * the brute force alarm]
     */
    led_state = (millis() % 1000 > 500);
    
    /*
     * Want to make sure we update the time_data when first entering this mode.
     */
    if (updated != mode_switch_time) {
        updated = mode_switch_time;
        force_update = TRUE;
    }
    
    if ((last_led_state != led_state) || force_update) {
        update = TRUE;
        last_led_state = led_state;
        
        if (led_state || force_update) {
            read_rtc_time_data(CLOCK_OPTION_TIME, &time_data[0]);
        
            if (last_seconds != time_data[TIME_DATA_SECOND]) {
                last_seconds = time_data[TIME_DATA_SECOND];
                
                if (time_data[TIME_DATA_SECOND] == 0) {
                    check_alarm_brute_force = TRUE;
                }
            }
        }
    }
    
    if (update) {
        Serial.print("LED ");
        Serial.print(last_led_state, DEC);
        Serial.print(" seconds ");
        Serial.print(last_seconds, HEX);
        Serial.print(" brute ");
        Serial.print(check_alarm_brute_force, DEC);
        Serial.print("\n");
        
        // this code makes use of the fact that printing the value in hex for some of these
        // happens to fall out (four bits for 1s, 3 bits for the 10s - which in hex falls out fine!)
        // this is probably an intention of using BCD for the time, rather than chance ;)
        in_byte = time_data[TIME_DATA_HOUR];
        Serial.print(in_byte, HEX);
        printbigchar((in_byte & 0x30) >> 4, 0, 0); // hours digit 1
        if (led_state == LOW) {
            printbigchar(in_byte & 0xF, 4, 0, 2); // hours digit 2 + :
        } else {
            printbigchar(in_byte & 0xF, 4, 0);    // hours digit 2   
        }
        Serial.print(":");
        
        in_byte = time_data[TIME_DATA_MINUTE];
        Serial.print(in_byte, HEX);
        printbigchar((in_byte & 0x70) >> 4, 8, 0);
        printbigchar(in_byte & 0xF, 12, 0);
        Serial.print(":");
        in_byte = time_data[TIME_DATA_SECOND];
        Serial.print(in_byte, HEX);
        
        Serial.print(" Day:");
        in_byte = time_data[TIME_DATA_DAYOFWEEK];
        Serial.print(in_byte, HEX);
        
        Serial.print(" Date:");
        in_byte = time_data[TIME_DATA_DAYOFMONTH];
        Serial.print(in_byte, HEX);
        
        Serial.print(" Month:");
        in_byte = time_data[TIME_DATA_MONTH];
        Serial.print(in_byte, HEX);
        
        Serial.print(" Year:");
        in_byte = time_data[TIME_DATA_YEAR];
        Serial.print(in_byte, HEX);
        Serial.print("\n");    
    
        if (led_state == LOW) {
          led_state = HIGH;
        } else {
          led_state = LOW;
        }
        digitalWrite(DIG_PIN_ALARM_OUTPUT, led_state);
    }
}

static void
read_button_state (void)
{
    int i;
    int reading;
    button_state_type new_state;
    long time_now = millis();
    
    /*
     * Reads the current state of the button, and using info stored from
     * the last cycle determines what has changed.
     */
    if (0) {
        Serial.print("Button states (time now ");
        Serial.print(time_now);
        Serial.print("): ");
    }
    for (i = 0; i < ANG_PIN_COUNT; i++) {
        new_state = BUTTON_STATE_RELEASED;
        reading = analogRead(i); 
        if (reading > HIGH_VAL_MIN) {
            /*
             * Button pushed, take action based on when button was first pushed:
             *
             * t < xxxdebounce: RELEASED
             * debounce < t < short: RELEASED [we'll set the value if released]
             * short <  t < long: RELEASED [ditto]
             * long < t: LONG [we report this as soon as button held long enough]
             */
            if (button_state_store[i].first_pressed_time == 0) {
                button_state_store[i].first_pressed_time = time_now;
            }
             
            if (time_now - button_state_store[i].first_pressed_time > DELAY_LONG_PRESS) {
                new_state = BUTTON_STATE_HELD;
            }
        } else {
            /*
             * Button released (or never pushed), take action based on last state:
             *
             * RELEASED: store RELEASED (to treat as no-op)
             * SHORT: store SHORT
             * LONG: store LONG
             * HELD: store RELEASED (HELD state is handled for as long as it is held, not after)
             */
             if (button_state_store[i].last_button_state == BUTTON_STATE_HELD) {
                 new_state = BUTTON_STATE_RELEASED;
             } else if (button_state_store[i].first_pressed_time != 0) {
               if (time_now - button_state_store[i].first_pressed_time > DELAY_SHORT_PRESS) {
                   new_state = BUTTON_STATE_LONG;
               } else if (time_now - button_state_store[i].first_pressed_time > DELAY_DEBOUNCE) {
                 new_state = BUTTON_STATE_SHORT;
               }
             }
             
             button_state_store[i].first_pressed_time = 0;
        }
         
        button_state_store[i].last_button_state = new_state;
         
        if (0/*i == 0*/) {
            Serial.print(i);
            Serial.print(": ");
            Serial.print(reading);
            Serial.print(", pressed@");
            Serial.print(button_state_store[i].first_pressed_time);
            Serial.print(", ");
            Serial.print(button_state_store[i].last_button_state);
            Serial.print("; ");
        }
    }
    if (0) {
        Serial.print("\n");
    }
}

/*
 * After we've read the alarm values from RTC, clear the alarm bits (M bits in
 * data sheet) so we can just access the raw numbers
 */
static void
clear_alarm_bit (clock_option_type  clock,
                 byte              *time_data)
{
    if (clock != CLOCK_OPTION_TIME) {
        for (int j = 0; j < TIME_DATA_DAYOFMONTH; j++) {
            time_data[j] = time_data[j] &= 0x7F; // clear M bit from each value
        }
        
        // also clear the seconds and dayofweek value - as we only ever care about the hours and minutes
        time_data[TIME_DATA_DAYOFWEEK] = 0;
        time_data[TIME_DATA_SECOND] = 0;
    }
}

static void
display_alarms (int force_update)
{
    static long updated = 0;
    clock_option_type clock;
    
    if (updated != mode_switch_time || force_update) {
        /* 
         * We only want to update the display when entering this mode, so store the timestamp
         * of when we do that - and only updated when our stored value differs (ie. we've reentered).
         *
         * Display "  x1: 00:00 y" (where x is alarm icon, y is tick mark)
         */
        lcd.clear();
        for (int i = 0; i < 2; i++) {
            clock = (clock_option_type)(CLOCK_OPTION_ALARM1 + i);
            read_rtc_time_data(clock, &time_data[0]);
            clear_alarm_bit(clock, &time_data[0]);
 
            lcd.setCursor(2, i);
            lcd.write(5);
            lcd.print(i + 1);
            lcd.print(":  ");
            display_time(CLOCK_OPTION_ALARM1); // not TIME is what is needed here
            lcd.print(" ");
            if (alarm_enabled[clock]) {
                lcd.write(7);
            }
        }
        lcd.setCursor(17, 3); // set blinking cursor out of range
        
        updated = mode_switch_time;
    }
}

/*
 * bcd_value_update
 * 
 * Handle updating a BCD value - which is trivial for values less than 10
 * (eg. day of week), but is more complicated for values greater than 10 - we
 * can't just trivially increment as the valid values are not contiguous
 * (eg. 0x09 + 1 -> 0x10, rather than 0x0A as simple calculation would give)
 *
 * Arg: bcd_value
 * IN - Pointer to value to increment
 *
 * Arg: increment
 * IN - value to increment (positive or negative).  Code probably assumes this
 * is smaller than the range of the value.
 *
 * Arg: valid_top_bitmask
 * IN - bitmask of values that are valid for the 10s value in bcd_value.
 *
 * Arg: max_val
 * IN - maximum value (eg. for hours where values can be 00-23, pass 24).
 *
 * Arg: zero_valid
 * IN - TRUE if zero is valid value, FALSE if valid value starts at 1.
 */
static void
bcd_value_update (byte *bcd_value,
                  int   increment,
                  int   valid_top_bitmask,
                  int   max_val,
                  int   zero_valid)
{
    int tens, ones, tmp_val;
    if (increment > 0) {
        Serial.print("Incrementing - ");
    } else {
        Serial.print("Decrementing - ");
    }
    Serial.print("incoming: ");
    Serial.print(*bcd_value, HEX);
    Serial.print(", outgoing: ");
    
    tens = BCD_GET_VALUE_TENS(*bcd_value, valid_top_bitmask << 4);
    ones = BCD_GET_VALUE_ONES(*bcd_value);
    tmp_val = (tens * 10) + ones;
    
    /*
     * Now we have a valid value as a normal number (ie. not BCD!).
     * 
     * If zero is not a valid value then need to shift everything down
     * one before doing the wrapping, and then shift back afterwards.
     * This just ensures the wrapping is correct (eg. for day of week, 
     * valid values 1-7, treat as 0-6 temporarily so we wrap from 1 to 7).
     */
    if (!zero_valid) {
        tmp_val--;
    }
    tmp_val += increment; // increment is positive or negative, so always add
    if (tmp_val < 0) {
        tmp_val = max_val + tmp_val; // needed for decrement
    }
    tmp_val = tmp_val % max_val; // needed for increment
    if (!zero_valid) {
        tmp_val++;
    }

    ones = tmp_val % 10;
    tens = (tmp_val - ones) / 10;
    *bcd_value = ((tens & valid_top_bitmask) << 4) + (ones & 0xF);
    Serial.print(*bcd_value, HEX);
    Serial.println("");
    
    /*
     * Slightly hacky way to make sure we don't increment values
     * faster than is useful - by just sleeping.  Might be better to 
     * track when we last handled and only handle ever N ms after that
     * (but that is obviously more complicated)
     */
    if (increment > 1 || increment < -1) {
        delay(250);
    }
}

static inline void
hour_update (byte *bcd_hour,
             int   increment)
{
    bcd_value_update(bcd_hour, increment, 0x03, 24, TRUE);
}

static inline void
minute_update (byte *bcd_minute,
               int   increment)
{
    bcd_value_update(bcd_minute, increment, 0x07, 60, TRUE);
}

static inline void
second_update (byte *bcd_second,
               int   increment)
{
    bcd_value_update(bcd_second, increment, 0x07, 60, TRUE);
}

enum {
    EDIT_FIELD_HOUR = 0,
    EDIT_FIELD_MINUTE,
    EDIT_FIELD_SECOND,
    EDIT_FIELD_COUNT,
};
// HH:MM:SS
// 01 34 67
int edit_field_offset[EDIT_FIELD_COUNT] = { 1, 4, 7};

/*
 * Prints the time contained in the current 'time_data', at 
 * the current LCD cursor location (so caller needs to set that as needed)
 */
static void
display_time (clock_option_type clock)
{
    if ((time_data[TIME_DATA_HOUR] & 0x30) == 0) {
        lcd.print("0");
    }
    lcd.print(time_data[TIME_DATA_HOUR], HEX);
    
    lcd.print(":");
    if ((time_data[TIME_DATA_MINUTE] & 0x70) == 0) {
        lcd.print("0");
    }
    lcd.print(time_data[TIME_DATA_MINUTE], HEX);
    
    if (clock == CLOCK_OPTION_TIME) {
        lcd.print(":");
        if ((time_data[TIME_DATA_SECOND] & 0x70) == 0) {
            lcd.print("0");
        }
        lcd.print(time_data[TIME_DATA_SECOND], HEX);
    }
}

static void
display_date (void)
{
    /*
     * "................"
     * "TIME:  18:56:23 "
     * "Fri  7 May 2012 "
     */
    switch (time_data[TIME_DATA_DAYOFWEEK]) {
    case 0x1:
        lcd.print("Mon");
        break;
    case 0x2:
        lcd.print("Tue");
        break;
    case 0x3:
        lcd.print("Wed");
        break;
    case 0x4:
        lcd.print("Thu");
        break;
    case 0x5:
        lcd.print("Fri");
        break;
    case 0x6:
        lcd.print("Sat");
        break;
    case 0x7:
        lcd.print("Sun");
        break;
    default:
        lcd.print(time_data[TIME_DATA_DAYOFWEEK], HEX);
        break;
    }
    lcd.print(" ");
    if ((time_data[TIME_DATA_DAYOFMONTH] & 0x30) == 0) {
        lcd.print("0");
    } 
    lcd.print(time_data[TIME_DATA_DAYOFMONTH], HEX);
    lcd.print(" ");
    switch (time_data[TIME_DATA_MONTH]) {
    case 0x1:
        lcd.print("Jan");
        break;
    case 0x2:
        lcd.print("Feb");
        break;
    case 0x3:
        lcd.print("Mar");
        break;
    case 0x4:
        lcd.print("Apr");
        break;
    case 0x5:
        lcd.print("May");
        break;
    case 0x6:
        lcd.print("Jun");
        break;
    case 0x7:
        lcd.print("Jul");
        break;
    case 0x8:
        lcd.print("Aug");
        break;
    case 0x9:
        lcd.print("Sep");
        break;
    case 0x10:
        lcd.print("Oct");
        break;
    case 0x11:
        lcd.print("Nov");
        break;
    case 0x12:
        lcd.print("Dec");
        break;
    default:
        lcd.print(time_data[TIME_DATA_MONTH], HEX);
        break;
    }
    lcd.print(" 20");
    if ((time_data[TIME_DATA_YEAR] & 0xF0) == 0) {
        lcd.print("0");
    } 
    lcd.print(time_data[TIME_DATA_YEAR], HEX);
    
    lcd.setCursor(17, 3); // set blinking cursor out of range
}

static void
edit_time (clock_option_type clock)
{
    static long updated = 0;
    static int edit_field = 0;
    int column;
    int update_display = FALSE;
    int update_cursor = FALSE;
    int ones;
    int tens;
    int tmp_val = 0;
    int increment;
    
    if (updated != mode_switch_time) {
        /* 
         * On entering this mode, read the time and store in temporary variables - and trigger updating the display.
         * 
         * After that, update the display only when a button has been pushed to change the time, and 
         * update the RTC once we've been told to.
         */
        read_rtc_time_data(clock, &time_data[0]);
        clear_alarm_bit(clock, &time_data[0]);
        update_display = TRUE;
        updated = mode_switch_time;
        edit_field = EDIT_FIELD_HOUR;
    }
    
    if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
        // store the values we have, no need to update the display
        // this function will set the top (M) bits for alarms to match what we want
        write_rtc_time_data(clock, &time_data[0]);
    } else if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_UP].last_button_state) ||
               BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_DOWN].last_button_state)) {
        // increment the currently selected value - change value, and cursor position
        // XXX holding buttons doesn't work so well - because we handle it immediately so
        //     the increment is very valid.  Want to handle it periodically when
        //     held, but not every cycle
        update_display = TRUE;
        if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_UP].last_button_state)) {
            increment = BUTTON_STATE_TO_INCREMENT(button_state_store[ANG_PIN_BUTTON_UP].last_button_state);
        } else {
            increment = -BUTTON_STATE_TO_INCREMENT(button_state_store[ANG_PIN_BUTTON_DOWN].last_button_state);
        }
        switch (edit_field) {
        case EDIT_FIELD_HOUR:
            hour_update(&time_data[TIME_DATA_HOUR], increment);
            break;
        case EDIT_FIELD_MINUTE:
            minute_update(&time_data[TIME_DATA_MINUTE], increment);
            break;
        case EDIT_FIELD_SECOND:
            second_update(&time_data[TIME_DATA_SECOND], increment);
            break;
        case EDIT_FIELD_COUNT:
        default:
            Serial.println("Urgh - shouldn't get here (up button, edit field)");
            break;
        }
    } else if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_LEFT].last_button_state)) {
        // move left one edit field, wrapping as needed - just changing cursor position
        if (edit_field == 0) {
            edit_field = EDIT_FIELD_COUNT;
        }
        edit_field--;
        
        if ((clock != CLOCK_OPTION_TIME) && (edit_field == EDIT_FIELD_SECOND)) {
            edit_field--;
        }
        update_cursor = TRUE;
    } else if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_RIGHT].last_button_state)) {
        // move right one edit field, wrapping as needed - just changing cursor position
        edit_field++;
        if ((clock != CLOCK_OPTION_TIME) && (edit_field == EDIT_FIELD_SECOND)) {
            edit_field++;
        }
        if (edit_field == EDIT_FIELD_COUNT) {
            edit_field = 0;
        }
        update_cursor = TRUE;
    }
    
    if (update_display) {
        /*
         * Display one of:
         * - "TIME:  00:00:00 "
         * - "  x1:  00:00 " (where x is alarm icon)
         */
        lcd.clear();
        if (clock == CLOCK_OPTION_TIME) {
            lcd.print("TIME:  ");
        } else {
            lcd.print("  ");
            lcd.write(5);
            lcd.print(clock);
            lcd.print(":  ");
        }
        display_time(clock);
        
        update_cursor = TRUE;
    }
    if (update_cursor) {
        // set the cursor position, and update the expire time
        lcd.setCursor(7 + edit_field_offset[edit_field], 0);
        mode_update(FALSE);
    }
}

enum {
    EDIT_FIELD_DATE_DAYOFWEEK = 0,
    EDIT_FIELD_DATE_DAYOFMONTH,
    EDIT_FIELD_DATE_MONTH,
    EDIT_FIELD_DATE_YEAR,
    EDIT_FIELD_DATE_COUNT,
};
// Mon 07 Feb 2012
// 012 45 789 1234
int edit_date_field_offset[EDIT_FIELD_DATE_COUNT] = { 2, 5, 9, 14};

static inline void
dayofweek_update (byte *bcd_dayofweek,
                  int   increment)
{
    bcd_value_update(bcd_dayofweek, increment, 0x00, 7, FALSE);
}

static inline void
dayofmonth_update (byte *bcd_dayofmonth,
                   int   increment)
{
    bcd_value_update(bcd_dayofmonth, increment, 0x03, 31, FALSE);
}

static inline void
month_update (byte *bcd_month,
              int   increment)
{
    bcd_value_update(bcd_month, increment, 0x03, 12, FALSE);
}

static inline void
year_update (byte *bcd_year,
             int   increment)
{
    bcd_value_update(bcd_year, increment, 0x0F, 100, TRUE);
}

static void
edit_date (void)
{
    static long updated = 0;
    static int edit_field = 0;
    int column;
    int update_display = FALSE;
    int update_cursor = FALSE;
    int ones;
    int tens;
    int tmp_val = 0;
    int increment;
    
    /*
     * XXX this has rather a lot in common with edit_time()..
     */
    if (updated != mode_switch_time) {
        /* 
         * On entering this mode, read the time and store in temporary
         * variables - and trigger updating the display.
         * 
         * After that, update the display only when a button has been pushed
         * to change the time, and 
         * update the RTC once we've been told to.
         */
        read_rtc_time_data(CLOCK_OPTION_TIME, &time_data[0]);
        clear_alarm_bit(CLOCK_OPTION_TIME, &time_data[0]);
        update_display = TRUE;
        updated = mode_switch_time;
        edit_field = EDIT_FIELD_DATE_DAYOFWEEK;
    }
    
    if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
        // store the values we have, no need to update the display
        // this function will set the top (M) bits for alarms to match what we want
        write_rtc_time_data(CLOCK_OPTION_TIME, &time_data[0]);
    } else if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_UP].last_button_state) ||
               BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_DOWN].last_button_state)) {
        // increment the currently selected value - change value, and cursor position
        // XXX holding buttons doesn't work so well - because we handle it
        //     immediately so the increment is very valid.  Want to handle
        //     it periodically when held, but not every cycle
        update_display = TRUE;
        if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_UP].last_button_state)) {
            increment = BUTTON_STATE_TO_INCREMENT(button_state_store[ANG_PIN_BUTTON_UP].last_button_state);
        } else {
            increment = -BUTTON_STATE_TO_INCREMENT(button_state_store[ANG_PIN_BUTTON_DOWN].last_button_state);
        }
        switch (edit_field) {
        case EDIT_FIELD_DATE_DAYOFWEEK:
            Serial.print("Updating day of week (");
            Serial.print(time_data[TIME_DATA_DAYOFWEEK], HEX);
            Serial.print(" - ");
            Serial.print(increment);
            Serial.print(")\n");
            dayofweek_update(&time_data[TIME_DATA_DAYOFWEEK], increment);
            break;
        case EDIT_FIELD_DATE_DAYOFMONTH:
            dayofmonth_update(&time_data[TIME_DATA_DAYOFMONTH], increment);
            break;
        case EDIT_FIELD_DATE_MONTH:
            month_update(&time_data[TIME_DATA_MONTH], increment);
            break;
        case EDIT_FIELD_DATE_YEAR:
            year_update(&time_data[TIME_DATA_YEAR], increment);
            break;
        case EDIT_FIELD_DATE_COUNT:    
        default:
            Serial.println("Urgh - shouldn't get here (up button, edit field)");
            break;
        }
    } else if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_LEFT].last_button_state)) {
        // move left one edit field, wrapping as needed - just changing cursor position
        if (edit_field == 0) {
            edit_field = EDIT_FIELD_DATE_COUNT;
        }
        edit_field--;
        update_cursor = TRUE;
    } else if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_RIGHT].last_button_state)) {
        // move right one edit field, wrapping as needed - just changing cursor position
        edit_field++;
        if (edit_field == EDIT_FIELD_DATE_COUNT) {
            edit_field = 0;
        }
        update_cursor = TRUE;
    }
    
    if (update_display) {
        /*
         * Display:
         * - "Mon 07 Feb 2012"
         */
        lcd.clear();
        display_date();
        
        update_cursor = TRUE;
    }
    if (update_cursor) {
        // set the cursor position, and update the expire time
        lcd.setCursor(0 + edit_date_field_offset[edit_field], 0);
        mode_update(FALSE);
    }
}

static inline void
mode_reset (void)
{
    mode = CLK_MODE_MAIN;
    mode_switch_time = millis();
    mode_expire_time = 0;
    Serial.println("Moving to main mode");
}

static inline void
mode_update (int mode_changed)
{
    if (mode_changed) {
        mode_switch_time = millis();
    }
    switch (mode) {
    case CLK_MODE_ALARM_QUESTION_ASK:
    case CLK_MODE_ALARM_QUESTION_VALIDATE:
        mode_expire_time = millis() + TIMEOUT_MODE_ALARM;
        break;
    default:
        mode_expire_time = millis() + TIMEOUT_MODE_GENERAL;
        break;
    }
}

typedef enum {
    BACKLIGHT_MODE_DAY = 0,
    BACKLIGHT_MODE_NIGHT,
    BACKLIGHT_MODE_COUNT,
} backlight_mode_type;

static backlight_mode_type backlight_mode = BACKLIGHT_MODE_DAY;
static unsigned int backlight_level_stored[BACKLIGHT_MODE_COUNT] = {40, 1}; // values chosen to look reasonable
static int backlight_level_current = 40;
static long backlight_changed_time = 0;

static void
update_backlight (void)
{
    int modify = 0;
    int target;
    int restart_timer = FALSE;
    
    /*
     * Have two backlight modes - day and night
     *
     * When in either mode allow the level to be increased/decreased with UP/DOWN buttons.
     * The setting will return to normal value after timeout, unless RIGHT button pressed to save
     *
     * Holding down UP button puts in day mode, DOWN in night mode.
     *
     * When changing the brightness (switching modes, or resetting modes) the brightness will change
     * gradually..  Unfortuately it seems this doesn't give a smooth fade up/down - as at lower values
     * the output changes most noticeably.  Could change the increment values depending on the current
     * value to fix this - may not be worth the effort..
     */
    if (BUTTON_HELD(button_state_store[ANG_PIN_BUTTON_UP].last_button_state)) {
        // switch to day mode, lose current working value
        backlight_mode = BACKLIGHT_MODE_DAY;
        //backlight_level_current = backlight_level_stored[backlight_mode];
        backlight_changed_time = 0;
    } else if (BUTTON_HELD(button_state_store[ANG_PIN_BUTTON_DOWN].last_button_state)) {
        // switch to night mode, lose current working value
        backlight_mode = BACKLIGHT_MODE_NIGHT;
        //backlight_level_current = backlight_level_stored[backlight_mode];
        backlight_changed_time = 0;
    } else if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_UP].last_button_state)) {
        // increment current value, don't store
        modify = BACKLIGHT_LEVEL_INCR;
        target = BACKLIGHT_LEVEL_MAX;
        restart_timer = TRUE;
    } else if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_DOWN].last_button_state)) {
        // decrement current value, don't store
        modify = -BACKLIGHT_LEVEL_DECR;
        target = BACKLIGHT_LEVEL_MIN;
        restart_timer = TRUE;
    } else if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_RIGHT].last_button_state)) {
        // store current value for the current mode
        backlight_level_stored[backlight_mode] = backlight_level_current;
        restart_timer = TRUE;
    } 

    if (restart_timer) {
        backlight_changed_time = millis();
    }
    
    if ((backlight_level_current != backlight_level_stored[backlight_mode]) &&
               (millis() - backlight_changed_time > BACKLIGHT_TIMEOUT)) {
        // see if we need to timeout the value, dim back to normal
        if (backlight_level_stored[backlight_mode] > backlight_level_current) {
            modify = +1;
        } else {
            modify = -1;
        }
        //modify = backlight_level_stored[backlight_mode] - backlight_level_current;
        target = backlight_level_stored[backlight_mode];
    }
    
    
    if (modify != 0) {
        // we are incrementing or decrementing the value, keeping it in range, and resetting the time
        backlight_level_current += modify;
        
        if (modify > 0) {
            if (backlight_level_current > target) {
                backlight_level_current = target;
            }
        } else {
            if (backlight_level_current < target) {
                backlight_level_current = target;
            }
        }
    }
         
    analogWrite(DIG_PIN_BACKLIGHT, backlight_level_current); 
}

static inline int
is_alarm_active_brute_force (void)
{
    int alarm_active = FALSE;
    byte alarm_data[TIME_DATA_COUNT];
    clock_option_type clock;

    // read the time
    read_rtc_time_data(CLOCK_OPTION_TIME, &time_data[0]);
    Serial.print("Time: ");
    Serial.print(time_data[TIME_DATA_HOUR], HEX);
    Serial.print(":");
    Serial.print(time_data[TIME_DATA_MINUTE], HEX);
    Serial.print(":");
    Serial.print(time_data[TIME_DATA_SECOND], HEX);
    Serial.print("\n");
    
    for (int i = 0; i < 2; i++) {
        clock = (clock_option_type)(CLOCK_OPTION_ALARM1 + i);
        
        if (alarm_enabled[clock]) {
            read_rtc_time_data(clock, &alarm_data[0]);
            clear_alarm_bit(clock, &alarm_data[0]);
            
            Serial.print("Alarm ");
            Serial.print(clock);
            Serial.print(": ");
            Serial.print(alarm_data[TIME_DATA_HOUR], HEX);
            Serial.print(":");
            Serial.print(alarm_data[TIME_DATA_MINUTE], HEX);
            Serial.print(":");
            Serial.print(alarm_data[TIME_DATA_SECOND], HEX);
            Serial.print("\n");
            
            if ((time_data[TIME_DATA_HOUR] == alarm_data[TIME_DATA_HOUR]) &&
                (time_data[TIME_DATA_MINUTE] == alarm_data[TIME_DATA_MINUTE]) &&
                (time_data[TIME_DATA_SECOND] == alarm_data[TIME_DATA_SECOND])) {
                Serial.println("Alarm triggered by brute force!");
                brute_force_alarm_triggered = TRUE;  // ARTHACK: major hack for initial debugging
                alarm_active = TRUE;
            }
        }
    }
    
    return (alarm_active);
}

static inline int
is_alarm_active (void)
{
    int alarm_active = FALSE;
    byte in_byte;
    
    /*
     * Check the interrupt pin to see if alarm has been set.  Also check the
     * two interrupt flags in the status bit (though this is strictly
     * unnecessary, and I don't think I've ever found them to be out of step).
     * Finally if the seconds value in time register is zero, then manually
     * check whether the alarms match the current time.  Belt and braces.
     *
     * Return true if an alarm has gone active,  and if it matches our global
     * variable that shows if alarm is active..
     *
     * If either interrupt set then clear it by reading the appropriate alarm
     * data.
     */
     
    /*
     * 1) Check interrupt pin.  If set then see which alarm triggered, and check
     * whether it is one we want to listen too.
     *
     * This should be reliable but isn't for some reason :(
     */
    if (analogRead(DIG_PIN_RTC_INTERRUPT) < LOW_VAL_MAX) {
        Serial.println("Interrupt pin triggered!");

        /*
         * 2) Check the interrupt bits in status register
         */ 
        in_byte = read_rtc_register(0x10);
        if (in_byte) {
            Serial.print("STATUS: ");
            Serial.print(in_byte, BIN);
            Serial.print("\n");
        }
        if ((in_byte & 0x1) != 0) {
            if (alarm_enabled[CLOCK_OPTION_ALARM1]) {
                Serial.println("Alarm 1 enabled and triggered");
                alarm_active = TRUE;
            }
            read_rtc_register(0x7);
        }
        if ((in_byte & 0x2) != 0) {
            if (alarm_enabled[CLOCK_OPTION_ALARM2]) {
                Serial.println("Alarm 2 enabled and triggered");
                alarm_active = TRUE;
            }
            read_rtc_register(0xB);
        }
    }
    
    
    /*
     * 3) Now manually check the alarm if the clock time shows a zero time
     */
     // ARTHACK: major hack, to force below handling
    //alarm_active = FALSE;
    
    if (!alarm_active &&
        check_alarm_brute_force && 
        (alarm_enabled[CLOCK_OPTION_ALARM1] || alarm_enabled[CLOCK_OPTION_ALARM2])) {
        check_alarm_brute_force = FALSE;
        
        alarm_active = is_alarm_active_brute_force();
    }
        
    
    /*
     * If anything has indicated the alarm is active then set up anything
     * that wants to only be done once when alarm goes off..
     */
    if (alarm_active) {
        analogWrite(DIG_PIN_BACKLIGHT, backlight_level_stored[BACKLIGHT_MODE_DAY]);
        questions_remaining = 5;
        delay(500); // ARTHACK: temporary hack while trying to figure out bug?
    }
    
    return (alarm_active);
}

static void
alarm_active (void)
{
    static long triggered = 0;
    
    if (triggered != mode_switch_time) {
        /* 
         * Alarm has triggered - we can come back here multiple times though
         * (as if user doesn't enter anything for a while we come back here).
         */
        Serial.print("Setting alarm at ");
        Serial.print(triggered);
        Serial.print("\n");
        triggered = mode_switch_time;
        lcd.clear();
        lcd.setCursor(2, 0);
        lcd.write(5);
        
        if (brute_force_alarm_triggered) {
            lcd.print(" BRUTAL ALARM!!!");
            brute_force_alarm_triggered = FALSE;
        } else {
            lcd.print(" ALARM!!!");
        }
        lcd.setCursor(17, 3); // set blinking cursor out of range
        
        tone(DIG_PIN_ALARM_OUTPUT, 4186, 10000);
        
        // ARTHACK - wondering if we are skipping setting the alarm due to some 
        // false button presses - bit of a long shot - but try always entering the
        // alarm mode and have a sleep so we stick here for long enough to notice.
        delay(500);
    }
}

static int q_f1 = 0;
static int q_f2 = 0;
static int q_f3 = 0;
static int q_answer = 0;
static int q_guess = 0;

static void
question_ask (void)
{
    static long triggered = 0;
    int update_display = FALSE;
    int increment = 0;
    
    if (triggered != mode_switch_time) {
        /* 
         * On entering this mode, generate a new question and update the display.
         *
         * After that, update the display only when a button has been pushed to change the guess.
         *
         * The boot time when we entered this mode should be pretty random..
         */
        triggered = mode_switch_time;
        
        randomSeed(mode_switch_time); 
        q_f1 = random(1, 100);
        q_f2 = random(1, 100);
        q_f3 = random(1, 100);
        q_answer = q_f1 + q_f2 - q_f3;
        q_guess = q_answer;// ARTHACK + random(-20, +21);
        
        Serial.print(q_f1);
        Serial.print(" + ");
        Serial.print(q_f2);
        Serial.print(" - ");
        Serial.print(q_f3);
        Serial.print(" = ");
        Serial.print(q_answer);
        Serial.print(" => guess ");
        Serial.print(q_guess);
        Serial.print("\n");
        
        update_display = TRUE;
    }
    
    if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_UP].last_button_state)) {
        increment = BUTTON_STATE_TO_INCREMENT(button_state_store[ANG_PIN_BUTTON_UP].last_button_state);
    } else if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_DOWN].last_button_state)) {
        increment = -BUTTON_STATE_TO_INCREMENT(button_state_store[ANG_PIN_BUTTON_DOWN].last_button_state);
    }
    if (increment != 0) {
       q_guess += increment;
       mode_update(FALSE);
       update_display = TRUE;
    } 
      
    if (update_display) {
        /*
         * Range of answers is 99+99-1=198 to 1+1-99=-97 - so three digits max
         *
         * Display:
         * - " xx+xx-xx = xxx " or
         * - " xx+xx-xx = -xx "
         */
        lcd.clear();
        lcd.print(" ");
        if (q_f1 < 10) {
            lcd.print(" ");
        }
        lcd.print(q_f1);
        lcd.print("+");
        if (q_f2 < 10) {
            lcd.print(" ");
        }
        lcd.print(q_f2);
        lcd.print("-");
        if (q_f3 < 10) {
            lcd.print(" ");
        }
        lcd.print(q_f3);
        lcd.print(" = ");
        if (q_guess < 0) {
            if (q_guess > -10) {
                lcd.print(" ");
            }
        } else if (q_guess < 100) {
            lcd.print(" ");
            if (q_guess < 10) {
                lcd.print(" ");
            }
        }
        lcd.print(q_guess);
        
        lcd.setCursor(14, 0);
    }
    
    if (increment != 0) {
        /*
         * Slightly hacky way to make sure we don't increment values
         * faster than is useful - by just sleeping.  Might be better to 
         * track when we last handled and only handle ever N ms after that
         * (but that is obviously more complicated)
         */
        if (increment > 1 || increment < -1) {
            delay(250);
        }
    }
}

static void
question_validate (void)
{
    static long triggered = 0;
    int update_display = FALSE;
    
    if (triggered != mode_switch_time) {
        /* 
         * On entering this mode, generate a new question and update the display.
         *
         * After that, update the display only when a button has been pushed to change the guess.
         *
         * The boot time when we entered this mode should be pretty random..
         */
        triggered = mode_switch_time;
        lcd.clear();
        if (q_guess == q_answer) {
            Serial.println("Correct answer!");
            lcd.print("Correct answer!");
            questions_remaining--;
        } else {
            Serial.println("Incorrect answer!");
            lcd.print("Incorrect answer..");
        }
        lcd.setCursor(17, 3); // set blinking cursor out of range
    }
}

static void
clock_view_details (void)
{
    static long updated = 0;
    
    if (updated != mode_switch_time) {
        updated = mode_switch_time;
        read_rtc_time_data(CLOCK_OPTION_TIME, &time_data[0]);
        lcd.clear();
        lcd.print("TIME:  ");
        display_time(CLOCK_OPTION_TIME);
        lcd.setCursor(0, 1);
        display_date();
    }
}

static bool
battery_check (long *voltage_x10)
{
    long value; // store int value in long so we can scale up without wrapping
    bool battery_ok = TRUE;

    //2.7V, which is 553 in 0-1023 range, is considered "dead"
    value = analogRead(ANG_PIN_RTC_BATTERY);
    value *= 50;
    value /= 1024;
    
    if (value < RTC_BATTERY_LOW_VOLTS_X10) {
        battery_ok = FALSE;
    }      
    if (voltage_x10) {
        *voltage_x10 = value;
    }
    
    return (battery_ok);
}

static void
battery_level_display (void)
{
    static long updated = 0;
    bool battery_ok = FALSE;
    long battery_level_x10 = 0; // Scaled by 10 (ie. 0 - 50, for 0.0-5.0V)
    
    if (updated != mode_switch_time) {
        updated = mode_switch_time;
        
        battery_ok = battery_check(&battery_level_x10);
        
        lcd.clear();
        lcd.print("Battery level:");
        lcd.setCursor(0, 1);

        if (battery_level_x10 < 10) {
            lcd.print("0");
        } else {
            lcd.print(battery_level_x10 / 10);
        }
        lcd.print(".");
        lcd.print(battery_level_x10 % 10);
        lcd.print("V");
        if (!battery_ok) {
            lcd.print("  ALERT");
        }
    }
}

void
loop (void)
{
    clock_option_type clock;
    int               force_update;
    int               i;
    
    read_button_state();
    
    if (is_alarm_active()) {
        mode = CLK_MODE_ALARM_ACTIVE;
        mode_switch_time = millis();
    }

    switch (mode) {
    case CLK_MODE_MAIN:
        // check if anything interesting has happened (button presses, interrupts)
        // if so then change modes
        // else update the current clock display
        // this mode has no timeout
        if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
            // switch modes!
            mode = CLK_MODE_ALARM_VIEW;
            mode_update(TRUE);
        } else if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_LEFT].last_button_state)) {
            // switch modes
            mode = CLK_MODE_CLOCK_VIEW_DETAIL;
            mode_update(TRUE);
        } else if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_RIGHT].last_button_state)) {
            // switch modes
            mode = CLK_MODE_BATTERY_CHECK;
            mode_update(TRUE);
        } else {
            update_backlight();
            update_clock_time();
        }
        
        break;
        
    case CLK_MODE_ALARM_VIEW:
        // show the alarm details.
        // the timeout for VIEW is to switch back to MAIN
        if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
            mode = CLK_MODE_ALARM_EDIT1;
            mode_update(TRUE);
            Serial.println("Moving to time set mode");
        } else {
            force_update = FALSE;
            if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_UP].last_button_state)) {
                alarm_enabled[CLOCK_OPTION_ALARM1] = !alarm_enabled[CLOCK_OPTION_ALARM1];
                force_update = TRUE;
            } else if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_DOWN].last_button_state)) {
                alarm_enabled[CLOCK_OPTION_ALARM2] = !alarm_enabled[CLOCK_OPTION_ALARM2];
                force_update = TRUE;
            }
            if (force_update) {
                Serial.print("Alarm setting changed, now: ");
                for (i = 0; i < CLOCK_OPTION_COUNT; i++) {
                    Serial.print(i);
                    Serial.print("-");
                    Serial.print(alarm_enabled[i]);
                    Serial.print("; ");
                }
                
                Serial.println("");
                mode_update(FALSE); // extend the mode timeout
            }
              
            display_alarms(force_update);
        }

        break;
        
    case CLK_MODE_ALARM_EDIT1:
    case CLK_MODE_ALARM_EDIT2:
        // edit the appropriate alarm details
        // the timeout for either mode is to go back to MAIN without any change
        if (mode == CLK_MODE_ALARM_EDIT1) {
            clock = CLOCK_OPTION_ALARM1;
        } else {
            clock = CLOCK_OPTION_ALARM2;
        }
        
        if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
            /*
             * If Select is pressed then store and switch to next edit mode.
             *
             * If held then store and return to main
             */
            edit_time(clock); // store the edit changes
            if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
                if (mode == CLK_MODE_ALARM_EDIT1) {
                    mode = CLK_MODE_ALARM_EDIT2;
                } else {
                    mode = CLK_MODE_TIME_EDIT;
                }
                mode_update(TRUE);
            } else {
                mode_reset();
            }
              

        } else {
            edit_time(clock);
        }

        break;
    case CLK_MODE_TIME_EDIT:
        // edit the clock time
        // the timeout for this mode is to go back ot MAIN without any change
        if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
            edit_time(CLOCK_OPTION_TIME); // store the edit changes
            if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
                mode = CLK_MODE_DATE_EDIT;
                mode_update(TRUE);
            } else {
                mode_reset();
            }        
        } else {
            edit_time(CLOCK_OPTION_TIME);
        }
        break;
    case CLK_MODE_DATE_EDIT:
        // edit the clock date
        // the timeout for this mode is to go back ot MAIN without any change
        if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
            edit_date(); // store the edit changes
            if (BUTTON_PRESSED(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
                mode_reset(); // no other mode to switch to currently..
            } else {
                mode_reset();
            }        
        } else {
            edit_date();
        }
        break;
    case CLK_MODE_ALARM_ACTIVE:
        // ARTHACK - temporarily always go in here - should be safe to do, we'll only "enter" the mode once
        // TBH, makes me wonder why I don't do this all the time :)
        alarm_active();
        if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
            // short term have a simple button press to clear the alarm state
            noTone(DIG_PIN_ALARM_OUTPUT);
            mode = CLK_MODE_ALARM_QUESTION_ASK;
            mode_update(TRUE);
        } else {
            alarm_active();
        }
        break;
    case CLK_MODE_ALARM_QUESTION_ASK:
        if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
            // switch modes to see if the response is correct..
            mode = CLK_MODE_ALARM_QUESTION_VALIDATE;
            mode_update(TRUE);
        } else {
            question_ask();
        }
        break;
        
    case CLK_MODE_ALARM_QUESTION_VALIDATE:
        if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
            // time for another question?
            if (questions_remaining > 0) {
                mode = CLK_MODE_ALARM_QUESTION_ASK;
            } else {
                mode = CLK_MODE_MAIN;
            }
            mode_update(TRUE);
        } else {
            question_validate();
        }    
        break;
        
    case CLK_MODE_CLOCK_VIEW_DETAIL:
        if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
            mode_reset();
        } else {
            clock_view_details();
        }
        break;
    case CLK_MODE_BATTERY_CHECK:
        if (BUTTON_PRESSED_OR_HELD(button_state_store[ANG_PIN_BUTTON_SELECT].last_button_state)) {
            mode_reset();
        } else {
            battery_level_display();
        }
        break;
    default:
        // urgh.  shouldn't get here.
        break;
    }
    
    if (millis() >= mode_expire_time) {
        switch (mode) {
        case CLK_MODE_MAIN:
            // nothing to do
            break;
        case CLK_MODE_ALARM_ACTIVE:
            // don't want to exit early!
            break;
        case CLK_MODE_ALARM_QUESTION_ASK:
        case CLK_MODE_ALARM_QUESTION_VALIDATE:
            // resound the alarm if we haven't had any input in a while..
            Serial.println("Alarm timeout - back to alarm mode");
            mode = CLK_MODE_ALARM_ACTIVE;
            mode_update(TRUE);
            break;
        default:
            // reset to main mode
            Serial.println("Timeout - back to main mode");
            mode_reset();
            break;
        }
    }
}
