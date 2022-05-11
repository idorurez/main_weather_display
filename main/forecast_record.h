#ifndef FORECAST_RECORD_H_
#define FORECAST_RECORD_H_

#include <Arduino.h>

typedef struct { // For current Day and Day 1, 2, 3, etc
  String   Dt;
  String   Period;
  String   Icon;
  String   Trend;
  String   Main0;
  String   Forecast0;
  String   Forecast1;
  String   Forecast2;
  String   Description;
  String   Time;
  String   Country;
  float    lat;
  float    lon;
  float    Temperature;
  float    Humidity;
  float    High;
  float    Low;
  float    Winddir;
  float    Windspeed;
  float    Rainfall;
  float    Snowfall;
  float    Pressure;
  int      Cloudcover;
  int      Visibility;
  int      Sunrise;
  int      Sunset;
  int      Timezone;
} Forecast_record_type;

typedef struct 
{
  char* time_stamp;
  float tempC = 0;
  float tempF = 0;
  float windSpeed = 0;
  double windDir = 0;
  char windCardDir[16] = "NULL";
  float rain = 0;

  float bsecRawTemp = 0;
  float bsecRawHumidity = 0;
  float bsecTemp = 0;
  float bsecPressure = 0;
  float bsecGasResistance = 0;
  float bsecHumidity = 0;
  float bsecIaq = 0; 
  float bsecIaqAccuracy = 0;
  float bsecStaticIaq = 0;
  float bsecCo2Equiv = 0;
  float bsecBreathVocEquiv = 0;
  
  float uvIndex = 0;
  float visLight = 0;
  float infLight = 0;
  float lux = 0;
  float batteryVoltage = 0;
  
  int photoResistor = 0;
  int batteryAdc = 0;
  float buildVer = 0;
} Weather_station_data;

#endif /* ifndef FORECAST_RECORD_H_ */
