#ifndef IO_DATA_H
#define IO_DATA_H
#include <WiFi.h>
#include "forecast_record.h"

// void wifiConnect(void);
std::string get_local_weather_data(Weather_station_data * data, WiFiClient & client);
Weather_station_data* parse_local_data(Weather_station_data * wsd_vals, std::string response);

#endif