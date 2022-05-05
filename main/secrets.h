#ifndef SECRETS_H
#define SECRETS_H

static char respBuf[4096];

// Use your own API key by signing up for a free developer account.
// http://www.wunderground.com/weather/api/
#define WU_API_KEY "af167648533f4fbd967648533fafbdd7"

// Specify your favorite location one of these ways.
//#define WU_LOCATION "CA/HOLLYWOOD"
// Country and city
//#define WU_LOCATION "Australia/Sydney"

// US ZIP code
//#define WU_LOCATION ""
#define ZIP_LOCATION "94070"

// 5 minutes between update checks. The free developer account has a limit
// on the  number of calls so don't go wild.
#define DELAY_NORMAL    (5*60*1000)
// 20 minute delay between updates after an error
#define DELAY_ERROR     (20*60*1000)
#define WEATHER_HOST "api.weather.com"



//===========================================
//WiFi connection
//===========================================
extern char ssid[] = "whitefox-weather"; // WiFi Router ssid
extern char pass[] = "!OcUrA5u!#"; // WiFi Router password

// char ssid[] = "whitefox"; // WiFi Router ssid
// char pass[] = "happilyeverafter"; // WiFi Router password

//===========================================
//db server connection
//===========================================
extern const char* db_server = "192.168.50.105";
extern const int db_port = 3307;
extern char* db_user = "papadakko";
extern char* db_pwd = "!OcUrA5u!#";
extern char* db_socket = "/run/mysqld/mysqld10.sock";

#endif