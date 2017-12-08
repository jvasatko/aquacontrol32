#include "SPI.h"                   //should be installed together with ESP32 Arduino install
#include "SPIFFS.h"
#include <ESPmDNS.h>               //should be installed together with ESP32 Arduino install
#include <Preferences.h>           //should be installed together with ESP32 Arduino install
#include "Adafruit_GFX.h"          //Install via 'Manage Libraries' in Arduino IDE
#include "Adafruit_ILI9341.h"      //Install via 'Manage Libraries' in Arduino IDE
#include "OneWire.h"
#include "SSD1306.h"               //https://github.com/squix78/esp8266-oled-ssd1306
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <XPT2046_Touchscreen.h> /* https://github.com/PaulStoffregen/XPT2046_Touchscreen */


/**************************************************************************
       OLED I2C address
**************************************************************************/
#define OLED_ADDRESS                      0x3C


/**************************************************************************
       1 = show system data on oled   0 = show light and temps on oled
**************************************************************************/
#define OLED_SHOW_SYSTEMDATA              0


/**************************************************************************
       defines for OLED display orientation
**************************************************************************/
#define OLED_ORIENTATION_NORMAL           1
#define OLED_ORIENTATION_UPSIDEDOWN       2


/**************************************************************************
       Some tft/sdcard breakout boards from China have their TFT MISO pin unconnected.
       These displays will not be detected by aquacontrol.
       Set TFT_HAS_NO_MISO to 1 to override detection and use these tft boards.
**************************************************************************/
#define TFT_HAS_NO_MISO                  1


/**************************************************************************
       defines for TFT display orientation
**************************************************************************/
#define TFT_ORIENTATION_NORMAL           1
#define TFT_ORIENTATION_UPSIDEDOWN       3


/**************************************************************************
       country code for ntp server selection
       https://en.wikipedia.org/wiki/ISO_3166-1_alpha-2
**************************************************************************/
#define COUNTRY_CODE_ISO_3166 "nl"


/**************************************************************************
       update frequency for LEDS in Hz
**************************************************************************/
#define UPDATE_FREQ_LEDS      100


/**************************************************************************
       update frequency for TFT display in Hz
**************************************************************************/
#define UPDATE_FREQ_TFT       5


/**************************************************************************
       update frequency for OLED display in Hz
**************************************************************************/
#define UPDATE_FREQ_OLED      4


/**************************************************************************
       number of bit precission for LEDC timer
**************************************************************************/
#define LEDC_NUMBER_OF_BIT    16


/**************************************************************************
       maximum allowable pwm frequency in Hz
       -remember the rise and fall times of a 330R gate resistor!
**************************************************************************/
#define LEDC_MAXIMUM_FREQ     1300


/**************************************************************************
       the number of LED channels
**************************************************************************/
#define NUMBER_OF_CHANNELS    5


/**************************************************************************
       the maximum number of timers allowed for each channel
**************************************************************************/
#define MAX_TIMERS            50


/**************************************************************************
       LED pin numbers
**************************************************************************/
#define LED0_PIN              22
#define LED1_PIN              21
#define LED2_PIN              17
#define LED3_PIN              16
#define LED4_PIN              26


/**************************************************************************
      SPI pin definitions
**************************************************************************/
#define SPI_TFT_DC_PIN            27  // Goes to TFT DC
#define SPI_SCK_PIN               25  // Goes to TFT SCK/CLK
#define SPI_MOSI_PIN              32  // Goes to TFT MOSI
#define SPI_TFT_RST_PIN           12  // Goes to TFT RESET
#define SPI_TFT_CS_PIN             4  // Goes to TFT CS
#define SPI_SD_CS_PIN              0  // Goes to SD CS
#define SPI_MISO_PIN              39  // Goes to TFT MISO
#define TOUCH_CS_PIN              33  // Goes to TFT T_CS
#define TOUCH_IRQ_PIN             35  // Goes to TFT T_IRQ

//       5v                       // Goes to TFT Vcc-
//       Gnd                      // Goes to TFT Gnd


/**************************************************************************
       TFT display backlight control
**************************************************************************/
#define TFT_BACKLIGHT_PIN       2


/**************************************************************************
      i2c pin definitions for oled
**************************************************************************/
#define I2C_SCL_PIN            19
#define I2C_SDA_PIN            23


/**************************************************************************
       OneWire Dallas sensors are connected to this pin
**************************************************************************/
#define ONEWIRE_PIN           5


/**************************************************************************
       maximum number of Dallas sensors
**************************************************************************/
#define MAX_NUMBER_OF_SENSORS 3


/**************************************************************************
       default hostname if no hostname is set
**************************************************************************/
#define DEFAULT_HOSTNAME_PREFIX "aquacontrol32_"


/**************************************************************************
      Setup included libraries
 *************************************************************************/
XPT2046_Touchscreen touch( TOUCH_CS_PIN, TOUCH_IRQ_PIN );
//XPT2046_Touchscreen touch( TOUCH_CS_PIN );

Adafruit_ILI9341 tft = Adafruit_ILI9341( SPI_TFT_CS_PIN, SPI_TFT_DC_PIN, SPI_TFT_RST_PIN );

Preferences preferences;

OneWire  ds( ONEWIRE_PIN );  /* a 4.7K pull-up resistor is necessary */

SSD1306  OLED( OLED_ADDRESS, I2C_SDA_PIN, I2C_SCL_PIN );

AsyncWebServer server(80);

/**************************************************************************
       type definitions
**************************************************************************/
enum lightStatus_t
{
  LIGHTS_OFF, LIGHTS_ON, LIGHTS_AUTO
};

inline const char* ToString( lightStatus_t status )
{
  switch ( status )
  {
    case LIGHTS_OFF:   return " LIGHTS OFF";
    case LIGHTS_ON:    return " LIGHTS ON ";
    case LIGHTS_AUTO:  return "LIGHTS AUTO";
    default:           return " UNDEFINED ";
  }
}

struct lightTimerArr_t
{
  time_t      time;                    /* time in seconds since midnight so range is 0-86400 */
  byte        percentage;              /* in percentage so range is 0-100 */
};

struct channelData_t
{
  lightTimerArr_t timer[MAX_TIMERS];
  String          name;                /* initially set to 'channel 1' 'channel 2' etc. */
  String          color;               /* interface color, not light color! in hex format*/
  /*                                      Example: '#ff0000' for bright red */
  float           currentPercentage;   /* what percentage is this channel set to */
  byte            pin;                 /* which ESP32 pin is this channel on */
  byte            numberOfTimers;      /* actual number of timers for this channel */
  float           minimumLevel;        /* never dim this channel below this percentage */
};

struct sensorData_t                    /* struct to keep track of Dallas DS18B20 sensors */
{
  byte   addr[8];
  float  tempCelcius;
  char   name[15];
};

/* const */
const char* defaultTimerFile   = "/default.aqu";

/* task priorities */
const uint8_t dimmerTaskPriority       = 7;
const uint8_t webserverTaskPriority    = 5;
const uint8_t tempTaskPriority         = 6;
const uint8_t tftTaskPriority          = 2;
const uint8_t ntpTaskPriority          = 1;
const uint8_t oledTaskPriority         = 1;
const uint8_t wifiTaskPriority         = 1;
const uint8_t loggerTaskPriority       = 0;
const uint8_t spiffsTaskPriority       = 0;


/**************************************************************************
       start of global variables
**************************************************************************/
channelData_t           channel[NUMBER_OF_CHANNELS];

sensorData_t            sensor[MAX_NUMBER_OF_SENSORS];

lightStatus_t           lightStatus;

TaskHandle_t            xDimmerTaskHandle            = NULL;
TaskHandle_t            xTftTaskHandle               = NULL;
TaskHandle_t            xOledTaskHandle              = NULL;
TaskHandle_t            xLoggerTaskHandle            = NULL;

//Boot time is saved
timeval                 systemStart;

char                    hostName[30];

double                  ledcActualFrequency;
uint16_t                ledcMaxValue;
uint8_t                 ledcNumberOfBits;

byte                    numberOfFoundSensors;

float                   tftBrightness                 = 80;                         /* in percent */
uint8_t                 tftOrientation                = TFT_ORIENTATION_NORMAL;

uint8_t                 oledContrast;                                               /* 0 .. 15 */
uint8_t                 oledOrientation               = OLED_ORIENTATION_NORMAL;

/*****************************************************************************************

       end of global variables

*****************************************************************************************/


void setup()
{
  pinMode( LED0_PIN, OUTPUT );
  pinMode( LED1_PIN, OUTPUT );
  pinMode( LED2_PIN, OUTPUT );
  pinMode( LED3_PIN, OUTPUT );
  pinMode( LED4_PIN, OUTPUT );
  pinMode( TFT_BACKLIGHT_PIN, OUTPUT );

  pinMode( I2C_SCL_PIN, INPUT_PULLUP );
  pinMode( I2C_SDA_PIN, INPUT_PULLUP );

  btStop();

  Serial.begin( 115200 );
  Serial.println();
  Serial.println( "aquacontrol32" );
  Serial.print( "ESP32 SDK: " );
  Serial.println( ESP.getSdkVersion() );
  Serial.println();

  SPI.begin( SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN );
  //SPI.setFrequency( 40000000 );

  tft.begin( 20000000, SPI );

  if ( TFT_HAS_NO_MISO || tft.readcommand8( ILI9341_RDSELFDIAG ) == 0xE0 )
  {
    touch.begin();
    Serial.println( TFT_HAS_NO_MISO ? "Forced ILI9341 start." : "ILI9341 display found." );
    xTaskCreatePinnedToCore(
      tftTask,                        /* Function to implement the task */
      "tftTask",                      /* Name of the task */
      3000,                           /* Stack size in words */
      NULL,                           /* Task input parameter */
      tftTaskPriority,                /* Priority of the task */
      &xTftTaskHandle,                /* Task handle. */
      1);                             /* Core where the task should run */
  }
  else
  {
    Serial.println( "No ILI9341 found" );
  }

  Wire.begin( I2C_SDA_PIN, I2C_SCL_PIN );

  Wire.beginTransmission( OLED_ADDRESS );
  uint8_t err = Wire.endTransmission();
  if ( err == 0 )
  {
    Serial.printf( "Found I2C device at address 0x%x.\n", OLED_ADDRESS );
    xTaskCreatePinnedToCore(
      oledTask,                       /* Function to implement the task */
      "oledTask",                     /* Name of the task */
      2000,                           /* Stack size in words */
      NULL,                           /* Task input parameter */
      oledTaskPriority,               /* Priority of the task */
      &xOledTaskHandle,               /* Task handle. */
      1);                             /* Core where the task should run */
  }
  else
  {
    Serial.println( "No I2C device found." );
  }

  xTaskCreatePinnedToCore(
    wifiTask,                       /* Function to implement the task */
    "wifiTask",                     /* Name of the task */
    2000,                           /* Stack size in words */
    NULL,                           /* Task input parameter */
    wifiTaskPriority,               /* Priority of the task */
    NULL,                           /* Task handle. */
    1);

  xTaskCreatePinnedToCore(
    tempTask,                       /* Function to implement the task */
    "tempTask",                     /* Name of the task */
    4000,                           /* Stack size in words */
    NULL,                           /* Task input parameter */
    tempTaskPriority,               /* Priority of the task */
    NULL,                           /* Task handle. */
    1);                             /* Core where the task should run */

  xTaskCreatePinnedToCore(
    spiffsTask,                     /* Function to implement the task */
    "spiffsTask",                   /* Name of the task */
    2000,                           /* Stack size in words */
    NULL,                           /* Task input parameter */
    spiffsTaskPriority,             /* Priority of the task */
    NULL,                           /* Task handle. */
    1);                             /* Core where the task should run */
}

void loop()
{
  vTaskDelete( NULL );
}
