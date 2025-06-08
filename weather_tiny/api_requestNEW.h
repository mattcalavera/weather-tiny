#ifndef _api_request_weather_forecast_h
#define _api_request_weather_forecast_h

#include <WiFi.h>
#include <ArduinoJson.h>
#include "fmt.h"
#include "config.h"

struct WeatherResponseHourly {
  int date_ts;
  int sunr_ts;
  int suns_ts;
  int temp;
  int feel_t;
  int max_t;
  int min_t;
  int pressure;
  int clouds;
  int wind_bft;
  int wind_deg;
  String icon;
  String descr;
  float snow;
  float rain;
  int pop;

  void print() {
    char buffer[150];
    Serial.println("Weather currently: " + descr);        
    sprintf(buffer, 
      "%8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s", 
      "date_ts", "sunr_ts", "suns_ts", "temp", "feel_t", 
      "max_t", "min_t", "pressure", "clouds", "wind_bft", 
      "wind_deg", "icon", "snow", "rain", "pop");
    Serial.println(buffer);
    sprintf(buffer, 
      "%8s %8s %8s %8d %8d %8d %8d %8d %8d %8d %8d %8s %8.1f %8.1f %8d",
      ts2HM(date_ts), ts2HM(sunr_ts), ts2HM(suns_ts), 
      temp, feel_t, max_t, min_t,
      pressure, clouds, wind_bft, wind_deg,
      icon.c_str(), snow, rain, pop);
    Serial.println(buffer);
  }
};

struct WeatherResponseDaily {
  int date_ts;
  int max_t;
  int min_t;
  int wind_bft;
  int wind_deg;
  int feel_t;
  int pop;
  int pop2;
  float snow;
  float rain;
  float rain2;
  String icon;
  String descr;

  void print() {
    char buffer[100];
    Serial.print("ForeKast: " + ts2dm(date_ts));
    sprintf(buffer, "%8s %8s %8s %8s %8s %8s %8s",
            "max_t", "min_t", "wind_bft", 
            "wind_deg", "pop", "snow", "rain");
    Serial.println(buffer);
    Serial.printf("%15s", "");
    sprintf(buffer, "%8d %8d %8d %8d %8d %8s %8.1f",
            max_t, min_t, wind_bft, wind_deg, pop,
            snow ? "yes" : "no", rain);
    Serial.println(buffer);
  }
};

struct WeatherResponseRainHourly {
  int date_ts;
  int pop;
  float feel_t;
  float snow;
  float rain;
  String icon;

  void print() {
    char buffer[60];
    Serial.print("Rain: " + ts2date(date_ts));
    sprintf(buffer, "%8s %8s %8s %8s %8s",
            "pop", "snow", "rain", "feel", "icon");
    Serial.println(buffer);
    Serial.printf("%25s", "");
    sprintf(buffer, "%8d %8.1f %8.1f %8.1f %8s",
            pop, snow, rain, feel_t, icon.c_str());
    Serial.println(buffer);
  }
};

struct WeatherRequest : Request {
  explicit WeatherRequest() {
    this->server = "api.openweathermap.org";
    this->api_key = OPENWEATHER_KEY;
  }

  explicit WeatherRequest(String server, String api_key) {
    this->server = server;
    this->api_key = api_key;
  }

  explicit WeatherRequest(const Request& request) : Request(request) {}

  void make_path(Location& location) {
    this->path = "/data/2.5/forecast?lat=" + String(location.lat) +
                 "&lon=" + String(location.lon) +
                 "&appid=" + api_key +
                 "&units=metric&lang=" + LANGS[LANG];
  }

  WeatherResponseHourly hourly[1];
  WeatherResponseDaily daily[5];
  WeatherResponseRainHourly rain[5];
};

// === PARSER ===

bool handle_weather_forecast(WiFiClient& stream, Request request) {
  WeatherRequest& wr = static_cast<WeatherRequest&>(request);

  const size_t CAPACITY = 20 * 1024;
  DynamicJsonDocument doc(CAPACITY);
  DeserializationError error = deserializeJson(doc, stream);

  if (error) {
    Serial.println("Errore parsing JSON: " + String(error.c_str()));
    return false;
  }

  JsonArray list = doc["list"];
  if (list.isNull()) {
    Serial.println("JSON malformato");
    return false;
  }

  struct DayData {
    int count = 0;
    float tmin = 1e6, tmax = -1e6;
    float rain = 0;
    int pop = 0, wind_deg = 0;
    String icon = "", descr = "";
  };

  DayData days[5] = {};
  int day_idx = -1;
  int current_day = -1;

  for (JsonObject f : list) {
    int dt = f["dt"];
    struct tm* tm = localtime((time_t*)&dt);
    if (tm->tm_mday != current_day) {
      current_day = tm->tm_mday;
      day_idx++;
      if (day_idx >= 5) break;
    }

    float temp = f["main"]["temp"];
    float rain = f["rain"]["3h"] | 0.0f;
    int pop = int((f["pop"] | 0.0f) * 100);
    int deg = f["wind"]["deg"] | 0;

    auto& d = days[day_idx];
    d.tmin = min(d.tmin, temp);
    d.tmax = max(d.tmax, temp);
    d.rain += rain;
    d.pop += pop;
    d.wind_deg += deg;
    d.count++;

    if (d.icon == "") {
      d.icon = f["weather"][0]["icon"].as<const char*>();
      d.descr = f["weather"][0]["description"].as<const char*>();
    }
  }

  for (int i = 0; i < 5; ++i) {
    wr.daily[i].date_ts = now() + i * 86400;
    wr.daily[i].min_t = int(round(days[i].tmin));
    wr.daily[i].max_t = int(round(days[i].tmax));
    wr.daily[i].pop = days[i].pop / max(1, days[i].count);
    wr.daily[i].rain = days[i].rain;
    wr.daily[i].wind_deg = days[i].wind_deg / max(1, days[i].count);
    wr.daily[i].wind_bft = int(wr.daily[i].wind_deg / 5.5);
    wr.daily[i].descr = days[i].descr;
    wr.daily[i].icon = days[i].icon;
  }

  // Previsione oraria (solo prima entry)
  JsonObject f = list[0];
  wr.hourly[0].date_ts = f["dt"];
  wr.hourly[0].temp = int(round(f["main"]["temp"].as<float>()));
  wr.hourly[0].feel_t = int(round(f["main"]["feels_like"].as<float>()));
  wr.hourly[0].pressure = f["main"]["pressure"] | 0;
  wr.hourly[0].clouds = f["clouds"]["all"] | 0;
  wr.hourly[0].wind_deg = f["wind"]["deg"] | 0;
  wr.hourly[0].wind_bft = int(wr.hourly[0].wind_deg / 5.5);
  wr.hourly[0].rain = f["rain"]["3h"] | 0.0f;
  wr.hourly[0].snow = f["snow"]["3h"] | 0.0f;
  wr.hourly[0].pop = int((f["pop"] | 0.0f) * 100);
  wr.hourly[0].descr = f["weather"][0]["description"].as<const char*>();
  wr.hourly[0].icon = f["weather"][0]["icon"].as<const char*>();

  // rain[0..4] semplificato: copia i primi 5
  for (int i = 0; i < 5; ++i) {
    JsonObject f = list[i];
    wr.rain[i].date_ts = f["dt"];
    wr.rain[i].rain = f["rain"]["3h"] | 0.0f;
    wr.rain[i].snow = f["snow"]["3h"] | 0.0f;
    wr.rain[i].pop = int((f["pop"] | 0.0f) * 100);
    wr.rain[i].feel_t = f["main"]["feels_like"] | 0.0f;
    wr.rain[i].icon = f["weather"][0]["icon"].as<const char*>();
  }

  return true;
}

#endif
