////////////////////////////////////////////////////////////////////////////
// Greenhouse Telegram Bot
// Keeps an eye on the conditions within your greenhouse, using Telegram.
// For ESP8266 / ESP-01 module with DHT11
// By Patrick van Beem
// Freeware
////////////////////////////////////////////////////////////////////////////

#define DHT11_GPIO 2

// Tried out various libraries. Most are unstable in readings.
#define USE_DHTStable
//#define USE_ADAFRUIT
//#define USE_DHTesp

#include "CTBot.h"
CTBot myBot;
uint32_t lastSenderId = 0;  // We only support one 'client'.

#include "secrets.h"
#include "RoundBufferIndex.h"

// Keeping a log of the last n events we have send / triggered.
#define LOGBUFSIZE 50
String logbuf[LOGBUFSIZE];
RoundBufferIndex<int, LOGBUFSIZE> logidx;

// Sensor settings
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

// Measurement / reporting settings.
// Release
#define MEASURE_INTERVAL 300000
#define MEASURE_RETRY_INTERVAL 10000
#define WARNING_THRESHOLD 32
#define WARNING_INTERVAL   2

/*
// Debug
#define MEASURE_INTERVAL 30000
#define MEASURE_RETRY_INTERVAL 10000
#define WARNING_THRESHOLD 28
#define WARNING_INTERVAL   2
*/

// If we fail to send messages, we remember them and try to resend them.
struct ResendItem {
  unsigned long timestamp;
  String        message;
  ResendItem*   next;
};
ResendItem* resend_head = NULL;
ResendItem* resend_tail = NULL;
int resend_count = 0;
#define MAXRESEND 20

// Keep a min/max trend of the last 24 hours.
const int MIN_MAX_SIZE = 48;
// const int MIN_MAX_INTERVAL_MS = 30*1000; // Debug values
const int MIN_MAX_INTERVAL_MS = 30*60*1000;
unsigned long last_minmax = millis() - MIN_MAX_INTERVAL_MS; // Triggers immediately
int min_temps[MIN_MAX_SIZE];
int max_temps[MIN_MAX_SIZE];
RoundBufferIndex<int, MIN_MAX_SIZE> min_max_idx;

// General control values.
unsigned long last_measured = millis() - MEASURE_INTERVAL;
unsigned long last_measured_ok = last_measured;
bool          values_ok = false;
int           last_status = TEMP_ERROR_TIMEOUT;
const int     INVALID_VALUE = 1000;
int           last_temp = INVALID_VALUE;
int           last_warning = 0;
int           last_humidity = 0;

void setup() {
	/* initialize the Serial
	Serial.begin(115200);
	Serial.println("Starting TelegramBot...");
  */

	// connect the ESP8266 to the desired access point
	myBot.wifiConnect(ssid, pass);

	// set the telegram bot token
	myBot.setTelegramToken(token);

  #ifdef USE_ADAFRUIT
    dht.begin();
  #endif
  #ifdef USE_DHTesp
    dht.setup(DHT11_GPIO, DHTesp::DHT11);
  #endif
}

String ToHMS(unsigned long ms)
{
  unsigned long seconds = ms / 1000;
  unsigned long hours = seconds / 3600;
  seconds = seconds % 3600;
  unsigned long minutes = seconds / 60;
  seconds = seconds % 60;
  String result(hours);
  result += ":";
  if ( minutes < 10 ) result += "0";
  result += String(minutes);
  result += ":";
  if ( seconds < 10 ) result += "0";
  result += String(seconds);
  return result;
}

// Free the first item in the resend chain and update the administration.
void FreeResendHead()
{
  ResendItem* head = resend_head;
  resend_head = head->next;
  delete head;
  resend_count--;
}

// Try to resend earlier failed messages.
void DoResend()
{
  while ( resend_count > 0 )
  {
    if ( !myBot.sendMessage(lastSenderId, ToHMS(millis() - resend_head->timestamp) + " ago: " + resend_head->message) ) break;
    FreeResendHead();
  }
}

// Send a message to Telegram. If not possible, cache max n messages and periodically try to send.
void SendToBot(uint32_t id, const String& message)
{
  DoResend();
  if ( resend_count == MAXRESEND ) FreeResendHead();
  if ( resend_count != 0 || !myBot.sendMessage(id, message) )
  {
    ResendItem* item = new ResendItem();
    item->timestamp = millis();
    item->message = message;
    item->next = NULL;
    if ( resend_count++ == 0 ) resend_head = resend_tail = item;
    else resend_tail = resend_tail->next = item;
  }
}

// The main program loop.
void loop() {
  bool doread = false;      // Read from the sensor.
  bool doreport = false;    // Send the result to Telegram.
  bool logresult = false;   // Log the result in the history log buffer.
  bool testtemp = false;    // Received a debug (fake) temperature via Telegram
  String result;            // The result for this loop iteration.
  unsigned long tm = millis();  // Freeze the time.

  // Move the historical trend to a new slot every <interval>.
  // Always triggers at the first loop iteration.
  if ( tm - last_minmax >= MIN_MAX_INTERVAL_MS )
  {
    last_minmax += MIN_MAX_INTERVAL_MS;
    ++min_max_idx;
    if ( last_status == TEMP_OK )
      min_temps[min_max_idx] = max_temps[min_max_idx] = last_temp;
    else
      min_temps[min_max_idx] = max_temps[min_max_idx] = INVALID_VALUE;
  }

	// a variable to store telegram message data
	TBMessage msg;

	// if there is an incoming message...
	if (myBot.getNewMessage(msg))
  {
    lastSenderId = msg.sender.id;
    if ( msg.text == "Read" ) doread = doreport = true;
    else if ( msg.text == "Get") doreport = true;
    else if ( msg.text == "Log")
    {
      int totalsize = 32; // For the header.
      logidx.Loop([&totalsize](int idx){ totalsize += logbuf[idx].length(); });
      String reply;
      reply.reserve(totalsize + logidx.Used() * 2);
      reply += "Log at " + ToHMS(millis()) + "\n";
      reply += "Resend buffer: " + String(resend_count) + "\n";
      logidx.Loop([&](int idx)
      {
        reply += logbuf[idx] + "\n";
      });
      myBot.sendMessage(msg.sender.id, reply);
    }
    else if ( msg.text == "Trend")
    {
      String reply;
      reply.reserve(48 * 20);
      int uur  =  (min_max_idx.Used() - 1) / 2;
      bool half = min_max_idx.Used() % 2 == 0;
      min_max_idx.Loop([&](int idx){
        reply += String(idx) + " ";
        reply += String(uur) + (half ? ":30 " : ":00 ");
        if ( min_temps[idx] == INVALID_VALUE ) reply += "Invalid\n";
        else reply += String(min_temps[idx]) + " " + String(max_temps[idx]) + "\n";
        half = !half;
        if ( half ) uur--;
      });
      myBot.sendMessage(msg.sender.id, reply);
    }
    else if ( msg.text.startsWith("Tt") )       // Debug: register fake temperature "Tt 20" = 20 degrees
    {
      testtemp = true;
      last_temp = last_humidity = msg.text.substring(3).toInt();
      last_status = TEMP_OK;
    }
    else if ( msg.text.startsWith("TestLog") )  // Debug: But the message in the log.
    {
      result = msg.text;
      logresult = true; //
    }
    else myBot.sendMessage(msg.sender.id, msg.text);
  } // msg received.

  // Read new temp / humidity values.
  if ( testtemp ||  doread || (tm - last_measured_ok > MEASURE_INTERVAL && tm - last_measured > MEASURE_RETRY_INTERVAL) )
  {
    DoResend(); // Try to resend messages in the same rithm as we do measurements.
    last_measured = tm;
    if ( !testtemp )
    {
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
    }

    if ( last_status == TEMP_OK )
    {
      values_ok = true;
      last_measured_ok = tm;

      // Process warnings
      if ( last_temp >= WARNING_THRESHOLD || last_warning >= WARNING_THRESHOLD )
      {
        if ( last_temp >= last_warning + WARNING_INTERVAL )
        {
          last_warning = last_temp;
          result += "\xE2\x9A\xA0 "; // Warning sign
          doreport = true;
          logresult = true;
        }
        else if ( last_temp < WARNING_THRESHOLD )
        {
          result += "\xF0\x9F\x91\x8D "; // Thumb up
          last_warning = 0;
          doreport = true;
          logresult = true;
        }
        else if ( last_temp <= last_warning - WARNING_INTERVAL )
        {
          last_warning = last_temp;
          result += "\xE2\xAC\x87 ";  // Down arrow
          doreport = true;
          logresult = true;
        }
      }

      // Update the min/max values.
      if ( min_temps[min_max_idx] == INVALID_VALUE || last_temp < min_temps[min_max_idx] ) min_temps[min_max_idx] = last_temp;
      if ( max_temps[min_max_idx] == INVALID_VALUE || last_temp > max_temps[min_max_idx] ) max_temps[min_max_idx] = last_temp;
    }
  }

  // If we need reporting, extend with the measured values and send to Telegram.
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
    SendToBot(lastSenderId, result);
  }

  // If we failed reading valid values for a long time, report this too.
  if ( values_ok && tm - last_measured_ok > MEASURE_INTERVAL * 2 )
  {
    values_ok = false;
    result = "\xE2\x8C\x9B Failed to read valid values."; // hourglass
    logresult = true;
    SendToBot(lastSenderId, result);
  }

  // If needed, log the result in the round log buffer.
  if ( logresult )
  {
    logbuf[++logidx] = ToHMS(millis()) + " " + result;
  }

	// wait a bit.
	delay(1000);
}
