#include <iostream>
#include <string>

#include "io_data.h"
#include "secrets.h"

#include <MySQL_Generic.h>

#define MYSQL_DEBUG_PORT      Serial
// Debug Level from 0 to 4
#define _MYSQL_LOGLEVEL_      4

std::string get_local_weather_data(Weather_station_data * data) {
//   int hourPtr = timeinfo.tm_hour;
  IPAddress server(192, 168, 50, 105);
  // uint16_t server_port = db_port;
//   Serial.printf("Version %f\n\n", BUILD_VER);

//  char query[1024];
  std::string response = "";
  String query = "SELECT * FROM weatherdb.master_sensor_vals ORDER BY TIMESTAMP DESC LIMIT 1";
  const char QUERY_VALS[] = "SELECT * FROM weatherdb.master_sensor_vals ORDER BY TIMESTAMP DESC LIMIT 1";
  // har GET_DATA[] = "INSERT INTO weatherdb.master_sensor_vals ( DS18B20_TEMP, WIND_DIR, WIND_CARD_DIR, WIND_SPEED, RAIN, SI1145_UV_INDEX, SI1145_VIS_LIGHT, SI1145_INF_LIGHT, BSEC_RAW_TEMP, BSEC_RAW_HUMIDITY, BSEC_TEMP, BSEC_HUMIDITY, BSEC_PRESSURE, BSEC_GAS_RESISTANCE, BSEC_IAQ, BSEC_IAQ_ACCURACY, BSEC_STATIC_IAQ, BSEC_CO2_EQUIV, BSEC_BREATH_VOC_EQUIV, BH1750_LUX, BATTERY_VOLTAGE, BUILD_VER ) VALUES (%.3f, %.3f, \"%s\", %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f)";
  // char INSERT_DATA[] = "INSERT INTO weatherdb.master_sensor_vals ( DS18B20_TEMP, WIND_DIR, WIND_CARD_DIR, WIND_SPEED, RAIN, SI1145_UV_INDEX, SI1145_VIS_LIGHT, SI1145_INF_LIGHT, BSEC_RAW_TEMP, BSEC_RAW_HUMIDITY, BSEC_PRESSURE, BSEC_GAS_RESISTANCE, BH1750_LUX, BATTERY_VOLTAGE, BUILD_VER ) VALUES (%.3f, %.3f, \"%s\", %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f)";
//   sprintf(query, QUERY, environment->tempF, 
//                               environment->windDir, 
//                               String(environment->windCardDir), 
//                               environment->windSpeed, 
//                               rainfall.hourlyRainfall[hourPtr] * 0.011, 
//                               environment->uvIndex, 
//                               environment->visLight, 
//                               environment->infLight, 
//                               environment->bsecRawTemp, 
//                               environment->bsecRawHumidity, 
//                               environment->bsecTemp, 
//                               environment->bsecHumidity, 
//                               environment->bsecPressure, 
//                               environment->bsecGasResistance, 
//                               environment->bsecIaq, 
//                               environment->bsecIaqAccuracy, 
//                               environment->bsecStaticIaq, 
//                               environment->bsecCo2Equiv, 
//                               environment->bsecBreathVocEquiv, 
//                               environment->lux, 
//                               environment->batteryVoltage,
//                               BUILD_VER);
//  client.stop();
  MySQL_Connection conn((Client *)&client);

  MYSQL_DISPLAY(MYSQL_MARIADB_GENERIC_VERSION);
  MySQL_Query query_mem = MySQL_Query(&conn);
  // MYSQL_DISPLAY("Connecting to " + db_server.c_str());

  if (conn.connectNonBlocking(server, db_port, db_user, db_pwd) != RESULT_FAIL) {
    
    if (conn.connected()) {
      MYSQL_DISPLAY("Connected");
      // Execute the query
      // KH, check if valid before fetching
      if ( !query_mem.execute(query.c_str()) ) {
        MYSQL_DISPLAY("query error");
      } else {
        MYSQL_DISPLAY("Data Queried.");
        column_names *cols = query_mem.get_columns();

        for (int f = 0; f < cols->num_fields; f++) 
        {
            MYSQL_DISPLAY0(cols->fields[f]->name);
            
            if (f < cols->num_fields - 1) 
            {
            MYSQL_DISPLAY0(",");
            }
        }
        MYSQL_DISPLAY();
        // Read the rows and print them
        row_values *row = NULL;
        do 
        {
            row = query_mem.get_next_row();
            
            if (row != NULL) 
            {
            for (int f = 0; f < cols->num_fields; f++) 
            {
                MYSQL_DISPLAY0(row->values[f]);
                response.append(row->values[f]);
                
                if (f < cols->num_fields - 1) 
                {
                  MYSQL_DISPLAY0(",");
                  response.append(",");
                }
            }
            
            MYSQL_DISPLAY();
            }
        } while (row != NULL);
      }
    } else {
      MYSQL_DISPLAY("Disconnected from Server. Can't insert.");
    } 
  } else {
    MYSQL_DISPLAY("\nConnect failed. Trying again on next iteration.");
  }

  conn.close(); 
  if (response != "") {
    parse_local_data(data, response);                    // close the connection;
  }
  return response;
}

// 0 TIMESTAMP,
// 1 DS18B20_TEMP,
// 2 WIND_DIR,
// 3 WIND_CARD_DIR,
// 4 WIND_SPEED,
// 5 RAIN,
// 6 BH1750_LUX,
// 7 SI1145_UV_INDEX,
// 8 SI1145_VIS_LIGHT,
// 9 SI1145_INF_LIGHT,
// 10 BATTERY_VOLTAGE,
// 11 BSEC_RAW_TEMP,
// 12 BSEC_RAW_HUMIDITY,
// 13 BSEC_TEMP,
// 14 BSEC_HUMIDITY,
// 15 BSEC_PRESSURE,
// 16 BSEC_GAS_RESISTANCE,
// 17 BSEC_IAQ,
// 18 BSEC_IAQ_ACCURACY,
// 19 BSEC_STATIC_IAQ,
// 20 BSEC_CO2_EQUIV,
// 21 BSEC_BREATH_VOC_EQUIV,
// 22 BUILD_VER

Weather_station_data* parse_local_data(Weather_station_data * wsd_vals, std::string response) {
  char *ptr = NULL;
  byte index = 0;
  char *parsed[200];
  
  char unf_data[response.length() + 1];
  strcpy(unf_data, response.c_str());

  ptr = strtok(unf_data, ",");
  while (ptr != NULL) {
    parsed[index] = ptr;
    index++;
    ptr = strtok(NULL, ",");
  }
  // wsd_vals[0].time_stamp = parsed[0];
  // wsd_vals[0].tempF = std::stof(parsed[1]);
  // wsd_vals[0].windDir = std::stod(parsed[2]);
  // wsd_vals[0].windCardDir = parsed[3];
  // wsd_vals[0].windSpeed = std::stof(parsed[4]);
  // wsd_vals[0].rain = std::stof(parsed[5]);
  // wsd_vals[0].uvIndex = std::stof(parsed[6]);
  // wsd_vals[0].visLight = std::stof(parsed[7]);
  // wsd_vals[0].infLight = std::stof(parsed[8]);
  // wsd_vals[0].bsecRawTemp = std::stof(parsed[9]);
  // wsd_vals[0].bsecRawHumidity = std::stof(parsed[10]);
  // wsd_vals[0].bsecTemp = std::stof(parsed[11]);
  // wsd_vals[0].bsecHumidity = std::stof(parsed[12]);
  // wsd_vals[0].bsecPressure = std::stof(parsed[13]);
  // wsd_vals[0].bsecGasResistance = std::stof(parsed[14]);
  // wsd_vals[0].bsecIaq = std::stof(parsed[15]);
  // wsd_vals[0].bsecIaqAccuracy = std::stof(parsed[16]);
  // wsd_vals[0].bsecStaticIaq = std::stof(parsed[17]);
  // wsd_vals[0].bsecCo2Equiv = std::stof(parsed[18]);
  // wsd_vals[0].bsecBreathVocEquiv = std::stof(parsed[19]);
  // wsd_vals[0].lux = std::stof(parsed[20]);
  // wsd_vals[0].batteryVoltage = std::stof(parsed[21]);
  // wsd_vals[0].buildVer = std::stof(parsed[22]);

  wsd_vals[0].time_stamp = parsed[0];
  wsd_vals[0].tempF = std::stof(parsed[1]);
  wsd_vals[0].windDir = std::stod(parsed[2]);
  wsd_vals[0].windCardDir = parsed[3];
  wsd_vals[0].windSpeed = std::stof(parsed[4]);
  wsd_vals[0].rain = std::stof(parsed[5]);
  wsd_vals[0].lux = std::stof(parsed[6]);
  wsd_vals[0].uvIndex = std::stof(parsed[7]);
  wsd_vals[0].visLight = std::stof(parsed[8]);
  wsd_vals[0].infLight = std::stof(parsed[9]);
  wsd_vals[0].batteryVoltage = std::stof(parsed[10]);
  wsd_vals[0].bsecRawTemp = std::stof(parsed[11]);
  wsd_vals[0].bsecRawHumidity = std::stof(parsed[12]);
  wsd_vals[0].bsecTemp = std::stof(parsed[13]);
  wsd_vals[0].bsecHumidity = std::stof(parsed[14]);
  wsd_vals[0].bsecPressure = std::stof(parsed[15]);
  wsd_vals[0].bsecGasResistance = std::stof(parsed[16]);
  wsd_vals[0].bsecIaq = std::stof(parsed[17]);
  wsd_vals[0].bsecIaqAccuracy = std::stof(parsed[18]);
  wsd_vals[0].bsecStaticIaq = std::stof(parsed[19]);
  wsd_vals[0].bsecCo2Equiv = std::stof(parsed[20]);
  wsd_vals[0].bsecBreathVocEquiv = std::stof(parsed[21]);
  wsd_vals[0].buildVer = std::stof(parsed[22]);


  return wsd_vals;
}