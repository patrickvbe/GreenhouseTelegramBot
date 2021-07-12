/*
 Name:		    echoBot.ino
 Created:	    12/21/2017
 Author:	    Stefano Ledda <shurillu@tiscalinet.it>
 Description: a simple example that check for incoming messages
              and reply the sender with the received message
*/
#define DHT11_GPIO 2
#define USE_DHTStable
//#define USE_ADAFRUIT
//#define USE_DHTesp

#include "CTBot.h"
CTBot myBot;

#include "secrets.h"

#ifdef USE_DHTStable
  #include "DHTStable.h"
  DHTStable DHT;
  #define TEMP_OK DHTLIB_OK
  #define TEMP_ERROR_TIMEOUT DHTLIB_ERROR_TIMEOUT
  #define TEMP_ERROR_CHECKSUM DHTLIB_ERROR_CHECKSUM
#endif
#ifdef USE_ADAFRUIT
  #include "DHT.h"
  DHT dht(DHT11_GPIO, DHT11);
  #define TEMP_OK 0
  #define TEMP_ERROR_TIMEOUT 1
  #define TEMP_ERROR_CHECKSUM 2
#endif
#ifdef USE_DHTesp
  #include "DHTesp.h"
  DHTesp dht;
  #define TEMP_OK 0
  #define TEMP_ERROR_TIMEOUT 1
  #define TEMP_ERROR_CHECKSUM 2
#endif

#define MEASURE_INTERVAL 300000
#define MEASURE_RETRY_INTERVAL 10000
#define WARNING_THRESHOLD 32
#define WARNING_INTERVAL   2

unsigned long last_measured = 0;
unsigned long last_measured_ok = 0;
bool          values_ok = false;
int           last_status = TEMP_OK;
int           last_temp = 0;
int           last_warning = 0;
int           last_humidity = 0;

void setup() {
	// initialize the Serial
	Serial.begin(115200);
	Serial.println("Starting TelegramBot...");

	// connect the ESP8266 to the desired access point
	myBot.wifiConnect(ssid, pass);

	// set the telegram bot token
	myBot.setTelegramToken(token);
	
	// check if all things are ok
	if (myBot.testConnection())
		Serial.println("\ntestConnection OK");
	else
		Serial.println("\ntestConnection NOK");

  #ifdef USE_ADAFRUIT
    dht.begin();
  #endif
  #ifdef USE_DHTesp
    dht.setup(DHT11_GPIO, DHTesp::DHT11);
  #endif
}

void loop() {
  bool doread = false;
  bool doreport = false;
  String result;

	// a variable to store telegram message data
	TBMessage msg;

	// if there is an incoming message...
	if (myBot.getNewMessage(msg))
  {
    if ( msg.text == "Read" ) doread = doreport = true;
    else if ( msg.text == "Get") doreport = true;
    else myBot.sendMessage(msg.sender.id, msg.text);
  }	 

  unsigned long tm = millis();
  if ( doread || (tm - last_measured_ok > MEASURE_INTERVAL && tm - last_measured > MEASURE_RETRY_INTERVAL) )
  {
    last_measured = tm;
    #ifdef USE_DHTStable
      if ( (last_status = DHT.read11(DHT11_GPIO)) == TEMP_OK )
      {
        last_temp = DHT.getTemperature();
        last_humidity = DHT.getHumidity();
      }
    #endif
    #ifdef USE_ADAFRUIT
      last_temp = dht.readTemperature();
      last_humidity = dht.readHumidity();
      last_status = last_temp > 1000 ? TEMP_ERROR_TIMEOUT : TEMP_OK;
    #endif
    #ifdef USE_DHTesp
      delay(dht.getMinimumSamplingPeriod());
      last_humidity = dht.getHumidity();
      last_temp = dht.getTemperature();
      last_status = last_temp > 1000 ? TEMP_ERROR_TIMEOUT : TEMP_OK;
    #endif
    if ( last_status == TEMP_OK )
    {
      values_ok = true;
      last_measured_ok = tm;
      if ( last_temp >= WARNING_THRESHOLD || last_warning >= WARNING_THRESHOLD )
      {
        if ( last_temp >= last_warning + WARNING_INTERVAL )
        {
          last_warning = last_temp;
          result += "\xE2\x9A\xA0 "; // Warning sign
          doreport = true;
        }
        else if ( last_temp < WARNING_THRESHOLD )
        {
          result += "\xF0\x9F\x91\x8D "; // Thumb up
          last_warning = 0;
          doreport = true;
        }
        else if ( last_temp <= last_warning - WARNING_INTERVAL )
        {
          last_warning = last_temp;
          result += "\xE2\xAC\x87 ";  // Down arrow
          doreport = true;
        }
      }
    }
  }

  if ( doreport )
  {
    if ( values_ok )
    {
      result += last_temp;
      result += "°C ";
      result += last_humidity;
      result += "% (";
      int age = (tm - last_measured_ok) / 1000;
      int minutes = age / 60;
      int seconds = age - (minutes * 60);
      result += minutes;
      result += ":";
      if ( seconds < 10 ) result += "0";
      result += seconds;
      result += " ago)";
    }
    else
    {
      result += "Invalid values"; 
    }
    myBot.sendMessage(msg.sender.id, result);
  }

  if ( values_ok && tm - last_measured_ok > MEASURE_INTERVAL * 2 )
  {
    values_ok = false;
    myBot.sendMessage(msg.sender.id, "\xE2\x8C\x9B Failed to read valid values."); // hourglass
  }

	// wait 500 milliseconds
	delay(500);
}
