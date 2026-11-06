#include "WeatherActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WeatherClient.h>
#include <WeatherIcons.h>
#include <WeatherSettingsStore.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "UiFonts.h"
#include "WeatherSettingsActivity.h"
#include "activities/NetworkMemoryTrim.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int cardGap = 8;
constexpr int cardPadding = 8;
constexpr int cardRadius = 6;
constexpr int forecastIconSize = 48;
constexpr int iconGap = 4;
constexpr int maxForecastCards = 5;

inline bool getBitmapBit(const uint8_t* bitmap, const int x, const int y) {
  const int rowBytes = WEATHER_ICON_SIZE / 8;
  const int idx = y * rowBytes + (x / 8);
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (x % 8));
  return (bitmap[idx] & mask) != 0;
}

void drawWeatherIconWithOrientation(const GfxRenderer& renderer, const uint8_t* icon, const int x, const int y,
                                    const int size) {
  for (int row = 0; row < size; ++row) {
    for (int col = 0; col < size; ++col) {
      // 0 bits are black (drawn) in these icon bitmaps.
      if (!getBitmapBit(icon, col * WEATHER_ICON_SIZE / size, row * WEATHER_ICON_SIZE / size)) {
        renderer.drawPixel(x + col, y + row);
      }
    }
  }
}

void drawFittedWeatherText(const GfxRenderer& renderer, int fontId, int x, int y, int width, const char* text,
                           EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const auto fitted = renderer.truncatedText(fontId, text, width, style);
  const int textWidth = renderer.getTextWidth(fontId, fitted.c_str(), style);
  if (textWidth <= width) {
    renderer.drawText(fontId, x + (width - textWidth) / 2, y, fitted.c_str(), true, style);
  }
}

void drawSunTime(const GfxRenderer& renderer, int x, int y, int width, const char* time, bool sunrise) {
  constexpr int markerWidth = 11;
  const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, time);
  const int availableWidth = width - markerWidth;
  const int fittedWidth = std::min(availableWidth, textWidth);
  const int startX = x + (width - fittedWidth - markerWidth) / 2;
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int markerX = startX + 3;
  const int centerY = y + lineHeight / 2;
  const int direction = sunrise ? 1 : -1;
  const int tipY = centerY - direction * 3;
  renderer.drawLine(markerX, tipY, markerX, centerY + direction * 3);
  renderer.drawLine(markerX - 3, centerY, markerX, tipY);
  renderer.drawLine(markerX, tipY, markerX + 3, centerY);

  if (textWidth > availableWidth) {
    const float scale = static_cast<float>(availableWidth) / textWidth;
    const int offset = (lineHeight - renderer.getLineHeightScaled(SMALL_FONT_ID, scale)) / 2;
    renderer.drawTextScaled(SMALL_FONT_ID, startX + markerWidth, y + offset, time, true, EpdFontFamily::REGULAR, scale);
  } else {
    renderer.drawText(SMALL_FONT_ID, startX + markerWidth, y, time);
  }
}

StrId getWeatherDescriptionStrId(const int wmoCode) {
  switch (wmoCode) {
    case 0:
      return StrId::STR_WEATHER_DESC_CLEAR_SKY;
    case 1:
      return StrId::STR_WEATHER_DESC_MAINLY_CLEAR;
    case 2:
      return StrId::STR_WEATHER_DESC_PARTLY_CLOUDY;
    case 3:
      return StrId::STR_WEATHER_DESC_OVERCAST;
    case 45:
      return StrId::STR_WEATHER_DESC_FOG;
    case 48:
      return StrId::STR_WEATHER_DESC_RIME_FOG;
    case 51:
      return StrId::STR_WEATHER_DESC_LIGHT_DRIZZLE;
    case 53:
      return StrId::STR_WEATHER_DESC_DRIZZLE;
    case 55:
      return StrId::STR_WEATHER_DESC_DENSE_DRIZZLE;
    case 56:
      return StrId::STR_WEATHER_DESC_FREEZING_DRIZZLE;
    case 57:
      return StrId::STR_WEATHER_DESC_DENSE_FREEZING_DRIZZLE;
    case 61:
      return StrId::STR_WEATHER_DESC_SLIGHT_RAIN;
    case 63:
      return StrId::STR_WEATHER_DESC_MODERATE_RAIN;
    case 65:
      return StrId::STR_WEATHER_DESC_HEAVY_RAIN;
    case 66:
      return StrId::STR_WEATHER_DESC_FREEZING_RAIN;
    case 67:
      return StrId::STR_WEATHER_DESC_HEAVY_FREEZING_RAIN;
    case 71:
      return StrId::STR_WEATHER_DESC_SLIGHT_SNOW;
    case 73:
      return StrId::STR_WEATHER_DESC_MODERATE_SNOW;
    case 75:
      return StrId::STR_WEATHER_DESC_HEAVY_SNOW;
    case 77:
      return StrId::STR_WEATHER_DESC_SNOW_GRAINS;
    case 80:
      return StrId::STR_WEATHER_DESC_SLIGHT_SHOWERS;
    case 81:
      return StrId::STR_WEATHER_DESC_MODERATE_SHOWERS;
    case 82:
      return StrId::STR_WEATHER_DESC_VIOLENT_SHOWERS;
    case 85:
      return StrId::STR_WEATHER_DESC_SNOW_SHOWERS;
    case 86:
      return StrId::STR_WEATHER_DESC_HEAVY_SNOW_SHOWERS;
    case 95:
      return StrId::STR_WEATHER_DESC_THUNDERSTORM;
    case 96:
      return StrId::STR_WEATHER_DESC_THUNDERSTORM_HAIL;
    case 99:
      return StrId::STR_WEATHER_DESC_THUNDERSTORM_HEAVY_HAIL;
    default:
      return StrId::STR_WEATHER_DESC_UNKNOWN;
  }
}

enum class MoonPhaseType {
  NEW,
  WAXING_CRESCENT,
  FIRST_QUARTER,
  WAXING_GIBBOUS,
  FULL,
  WANING_GIBBOUS,
  LAST_QUARTER,
  WANING_CRESCENT,
};

static MoonPhaseType getMoonPhaseType(float phase) {
  if (phase < 0.03f || phase > 0.97f) return MoonPhaseType::NEW;
  if (phase < 0.22f) return MoonPhaseType::WAXING_CRESCENT;
  if (phase < 0.28f) return MoonPhaseType::FIRST_QUARTER;
  if (phase < 0.47f) return MoonPhaseType::WAXING_GIBBOUS;
  if (phase < 0.53f) return MoonPhaseType::FULL;
  if (phase < 0.72f) return MoonPhaseType::WANING_GIBBOUS;
  if (phase < 0.78f) return MoonPhaseType::LAST_QUARTER;
  return MoonPhaseType::WANING_CRESCENT;
}

static float getMoonPhaseCycleFromTime(time_t timestamp) {
  constexpr double knownNewMoon = 947182440.0;  // Jan 6, 2000 18:14 UTC
  constexpr double lunarCycle = 2551442.8;      // Seconds in synodic month

  double secondsSince = static_cast<double>(timestamp) - knownNewMoon;
  double phase = secondsSince / lunarCycle;
  phase = phase - floor(phase);
  if (phase < 0.0) phase += 1.0;
  return static_cast<float>(phase);
}

static StrId getMoonPhaseStrId(MoonPhaseType type) {
  switch (type) {
    case MoonPhaseType::NEW:
      return StrId::STR_WEATHER_MOON_NEW;
    case MoonPhaseType::WAXING_CRESCENT:
      return StrId::STR_WEATHER_MOON_WAXING_CRESCENT;
    case MoonPhaseType::FIRST_QUARTER:
      return StrId::STR_WEATHER_MOON_FIRST_QUARTER;
    case MoonPhaseType::WAXING_GIBBOUS:
      return StrId::STR_WEATHER_MOON_WAXING_GIBBOUS;
    case MoonPhaseType::FULL:
      return StrId::STR_WEATHER_MOON_FULL;
    case MoonPhaseType::WANING_GIBBOUS:
      return StrId::STR_WEATHER_MOON_WANING_GIBBOUS;
    case MoonPhaseType::LAST_QUARTER:
      return StrId::STR_WEATHER_MOON_LAST_QUARTER;
    case MoonPhaseType::WANING_CRESCENT:
      return StrId::STR_WEATHER_MOON_WANING_CRESCENT;
    default:
      return StrId::STR_WEATHER_DESC_UNKNOWN;
  }
}

static float getMoonIlluminationFromCycle(float cycle) {
  // 0.0 = new, 0.5 = full; convert to illuminated fraction
  const float pi = 3.14159265358979323846f;
  float illum = 0.5f * (1.0f - cosf(2.0f * pi * cycle));
  return illum;
}

static void formatSunriseSunsetTime(char* buf, size_t bufSize, time_t epoch, int utcOffsetSeconds, bool use24h) {
  time_t localTime = epoch + utcOffsetSeconds;
  struct tm timeinfo;
  gmtime_r(&localTime, &timeinfo);

  if (use24h) {
    snprintf(buf, bufSize, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  } else {
    int hour = timeinfo.tm_hour % 12;
    if (hour == 0) hour = 12;
    const char* suffix = timeinfo.tm_hour < 12 ? "AM" : "PM";
    snprintf(buf, bufSize, "%d:%02d%s", hour, timeinfo.tm_min, suffix);
  }
}

static time_t getNextFullMoonTime(time_t now) {
  constexpr double knownNewMoon = 947182440.0;
  constexpr double lunarCycle = 2551442.8;

  double secondsSince = static_cast<double>(now) - knownNewMoon;
  double synodic = secondsSince / lunarCycle;
  synodic -= floor(synodic);
  if (synodic < 0.0) synodic += 1.0;

  double untilFull = 0.5 - synodic;
  if (untilFull < 0.0) untilFull += 1.0;

  return now + static_cast<time_t>(untilFull * lunarCycle + 0.5);
}
}  // namespace

void WeatherActivity::onEnter() {
  Activity::onEnter();

  // Force landscape orientation for weather display
  {
    RenderLock lock(*this);
    renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
  }

  state = State::LOADING_CACHE;
  errorMessage.clear();
  forceRefresh = false;
  showRefreshPopup = false;
  requestUpdate();

  loadAndDisplay();
}

void WeatherActivity::onExit() {
  Activity::onExit();

  // Weather screen is always landscape; restore app UI to portrait on exit.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    HalClock::wifiOff(false, SETTINGS.ntpServer);
  }
  weatherData = {};
  restoreDisplayAfterNetworkSession(renderer);
}

void WeatherActivity::loadAndDisplay() {
  constexpr long CACHE_REFRESH_AGE_SECONDS = 30L * 60L;

  if (!WEATHER_SETTINGS.hasLocation()) {
    state = State::ERROR;
    errorMessage = tr(STR_WEATHER_NO_LOCATION);
    requestUpdate();
    return;
  }

  // Try cache first
  WeatherData cached = WeatherClient::getWeather(WEATHER_SETTINGS, forceRefresh);
  if (cached.valid) {
    const time_t now = time(nullptr);
    const long age = (cached.fetchedAt > 0) ? static_cast<long>(now - cached.fetchedAt) : -1;
    const bool cacheIsStale = (age >= 0 && age > CACHE_REFRESH_AGE_SECONDS) || age < 0;
    const bool shouldBackgroundRefresh = forceRefresh || cacheIsStale;

    if (cacheIsStale) {
      LOG_DBG("WEA", "Displayed stale cache (age=%ld s); scheduling refresh", age);
    } else {
      LOG_DBG("WEA", "Displayed fresh cache (age=%ld s); skip auto-refresh", age);
    }

    weatherData = std::move(cached);
    state = State::WEATHER_DISPLAY;
    requestUpdate();

    // Refresh only when forced or when cache is stale.
    if (shouldBackgroundRefresh) {
      checkAndConnectWifi();
    }
    return;
  }

  // Need fresh data - check WiFi
  LOG_DBG("WEA", "No cache to display; proceeding to WiFi/network fetch path");
  checkAndConnectWifi();
}

void WeatherActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = State::FETCHING;
    requestUpdate(true);
    fetchWeather();
    return;
  }

  launchWifiSelection();
}

void WeatherActivity::launchWifiSelection() {
  // Release the secondary framebuffer before the radio is initialized. Waiting
  // until fetchWeather() is too late: WiFi association itself needs a large
  // contiguous allocation on the ESP32-C3.
  trimMemoryForNetworkSession(renderer, "WEA");
  state = State::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void WeatherActivity::onWifiSelectionComplete(bool connected) {
  // Re-apply landscape after returning from WiFi selection (which uses portrait)
  {
    RenderLock lock(*this);
    renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
  }
  LOG_DBG("WEA", "onWifiSelectionComplete connected=%d wifiStatus=%d", connected ? 1 : 0, (int)WiFi.status());

  if (connected) {
    state = State::CHECK_WIFI;
    wifiWaitStartedAtMs = millis();
    LOG_DBG("WEA", "state -> CHECK_WIFI (waitStart=%lu)", (unsigned long)wifiWaitStartedAtMs);
    requestUpdate(true);
  } else {
    // Leave Wi-Fi cleanup to onExit().
    state = State::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}

void WeatherActivity::fetchWeather() {
  LOG_DBG("WEA", "fetchWeather[1] enter status=%d ip=%s", (int)WiFi.status(), WiFi.localIP().toString().c_str());
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    LOG_ERR("WEA", "fetchWeather[2] WiFi not ready status=%d ip=%s", (int)WiFi.status(),
            WiFi.localIP().toString().c_str());
    state = State::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
    return;
  }

  trimMemoryForNetworkSession(renderer, "WEA");
  LOG_DBG("WEA", "fetchWeather[3] WeatherClient::getWeather before");
  WeatherData freshData = WeatherClient::getWeather(WEATHER_SETTINGS, true);
  LOG_DBG("WEA", "fetchWeather[4] WeatherClient::getWeather after valid=%d", freshData.valid ? 1 : 0);
  if (freshData.valid) {
    weatherData = std::move(freshData);
    state = State::WEATHER_DISPLAY;
  } else if (weatherData.valid) {
    // Keep a stale forecast visible when a background refresh fails. The old
    // implementation replaced it with an error screen during API outages.
    LOG_ERR("WEA", "Refresh failed; retaining cached weather data");
    state = State::WEATHER_DISPLAY;
  } else {
    state = State::ERROR;
    errorMessage = freshData.errorMessage.empty() || freshData.errorMessage == "Network error"
                       ? tr(STR_WEATHER_FETCH_FAILED)
                       : freshData.errorMessage.c_str();
  }
  showRefreshPopup = false;
  requestUpdate();
}

void WeatherActivity::openSettingsActivity() {
  {
    RenderLock lock(*this);
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  }
  startActivityForResult(std::make_unique<WeatherSettingsActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) {
                           {
                             RenderLock lock(*this);
                             renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
                           }
                           forceRefresh = true;
                           loadAndDisplay();
                         });
}

void WeatherActivity::triggerRefresh(const bool showPopup) {
  showRefreshPopup = showPopup;
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = State::FETCHING;
    requestUpdate(true);
    fetchWeather();
  } else {
    launchWifiSelection();
  }
}

void WeatherActivity::loop() {
  if (state == State::WIFI_SELECTION) {
    return;  // Handled by WifiSelectionActivity
  }

  if (state == State::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      openSettingsActivity();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
               mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      triggerRefresh(false);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  if (state == State::LOADING_CACHE || state == State::CHECK_WIFI || state == State::FETCHING) {
    if (state == State::CHECK_WIFI) {
      LOG_DBG("WEA", "loop CHECK_WIFI status=%d ip=%s elapsed=%lu", (int)WiFi.status(),
              WiFi.localIP().toString().c_str(), (unsigned long)(millis() - wifiWaitStartedAtMs));
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        // Give lwIP/sockets a short window to settle after activity handoff.
        if (wifiWaitStartedAtMs > 0 && millis() - wifiWaitStartedAtMs < 1200) {
          LOG_DBG("WEA", "loop CHECK_WIFI waiting for settle window");
          return;
        }
        LOG_DBG("WEA", "state CHECK_WIFI -> FETCHING");
        state = State::FETCHING;
        requestUpdate(true);
        fetchWeather();
        return;
      }

      if (wifiWaitStartedAtMs > 0 && millis() - wifiWaitStartedAtMs > 15000) {
        LOG_ERR("WEA", "CHECK_WIFI timeout after %lu ms", (unsigned long)(millis() - wifiWaitStartedAtMs));
        state = State::ERROR;
        errorMessage = tr(STR_WIFI_CONN_FAILED);
        requestUpdate();
        return;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  // WEATHER_DISPLAY state
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSettingsActivity();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
             mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    triggerRefresh(true);
  }
}

void WeatherActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (state == State::LOADING_CACHE || (state == State::FETCHING && (!showRefreshPopup || !weatherData.valid))) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_LOADING));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::ERROR) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels =
        mappedInput.mapLabels(tr(STR_BACK), tr(STR_WEATHER_SETTINGS_SHORT), tr(STR_WEATHER_REFRESH), "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // In LandscapeClockwise, physical bottom buttons sit on the logical left.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentX = metrics.buttonHintsHeight + cardGap;
  const int contentWidth = pageWidth - contentX - cardGap;
  constexpr int currentWidth = 216;
  const int bodyLine = renderer.getLineHeight(SMALL_FONT_ID);
  const int headingLine = renderer.getLineHeight(UI_10_FONT_ID);
  const int currentHeight = headingLine + renderer.getLineHeight(UI_12_FONT_ID) + 7 * bodyLine;
  const int forecastHeight = headingLine + 9 * bodyLine;
  const int cardHeight = std::max(currentHeight, forecastHeight) + forecastIconSize + 2 * (cardPadding + iconGap);

  renderCurrentConditions(contentX, cardGap, currentWidth, cardHeight);
  renderDailyForecast(contentX + currentWidth + cardGap, cardGap, contentWidth - currentWidth - cardGap, cardHeight);

  const int graphY = cardGap + cardHeight + cardGap;
  renderHourlyGraph(contentX, graphY, contentWidth, pageHeight - graphY - cardGap);

  // Button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_WEATHER_SETTINGS_SHORT), tr(STR_WEATHER_REFRESH), "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (state == State::FETCHING && showRefreshPopup) {
    // Full weather frame already composed into the write buffer above; overlay the popup on it.
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP), /*overlayDisplayedFrame=*/false);
  } else {
    renderer.displayBuffer();
  }
}

void WeatherActivity::renderCurrentConditions(int x, int y, int w, int h) {
  const auto& cur = weatherData.current;
  renderer.drawRoundedRect(x, y, w, h, 1, cardRadius, true);
  int textY = y + cardPadding;
  const auto drawLine = [&](const char* text, int fontId = SMALL_FONT_ID,
                            EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
    drawFittedWeatherText(renderer, fontId, x + cardPadding, textY, w - 2 * cardPadding, text, style);
    textY += renderer.getLineHeight(fontId);
  };

  const auto locationName = WEATHER_SETTINGS.getLocationName();
  if (!locationName.empty()) drawLine(locationName.c_str(), UI_10_FONT_ID, EpdFontFamily::BOLD);

  if (weatherData.fetchedAt > 0) {
    struct tm timeinfo;
    const time_t localTime = weatherData.fetchedAt + weatherData.utcOffsetSeconds;
    gmtime_r(&localTime, &timeinfo);
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    drawLine(timeBuf);
  }

  const auto icon = getWeatherIconLarge(getWeatherIconType(cur.weatherCode, cur.isDay));
  drawWeatherIconWithOrientation(renderer, icon, x + (w - forecastIconSize) / 2, textY + iconGap, forecastIconSize);
  textY += forecastIconSize + 2 * iconGap;

  char value[32];
  const char* unitSuffix = WEATHER_SETTINGS.getTempUnit() == WeatherTempUnit::CELSIUS ? "C" : "F";
  snprintf(value, sizeof(value), "%.1f %s", cur.temperature, unitSuffix);
  drawLine(value, UI_12_FONT_ID, EpdFontFamily::BOLD);

  snprintf(value, sizeof(value), "%.1f %s", cur.apparentTemperature, unitSuffix);
  drawLine((std::string(tr(STR_WEATHER_FEELS_LIKE)) + ": " + value).c_str());
  drawLine(I18N.get(getWeatherDescriptionStrId(cur.weatherCode)));

  snprintf(value, sizeof(value), "%.0f %s %s", cur.windSpeed, WEATHER_SETTINGS.getWindUnitParam(),
           getWindDirectionText(cur.windDirection));
  drawLine((std::string(tr(STR_WEATHER_WIND)) + ": " + value).c_str());

  snprintf(value, sizeof(value), "%d%%", cur.humidity);
  drawLine((std::string(tr(STR_WEATHER_HUMIDITY)) + ": " + value).c_str());

  snprintf(value, sizeof(value), "%.1f %s", cur.precipitation, WEATHER_SETTINGS.getPrecipUnitParam());
  drawLine((std::string(tr(STR_WEATHER_PRECIP)) + ": " + value).c_str());

  snprintf(value, sizeof(value), "UV: %.1f", cur.uvIndex);
  drawLine(value);
}

void WeatherActivity::renderDailyForecast(int x, int y, int w, int h) {
  const int numDays = std::min(static_cast<int>(weatherData.daily.size()), maxForecastCards);
  if (numDays == 0) return;

  const StrId dayNameIds[] = {StrId::STR_WEATHER_DAY_SUN, StrId::STR_WEATHER_DAY_MON, StrId::STR_WEATHER_DAY_TUE,
                              StrId::STR_WEATHER_DAY_WED, StrId::STR_WEATHER_DAY_THU, StrId::STR_WEATHER_DAY_FRI,
                              StrId::STR_WEATHER_DAY_SAT};
  const StrId monthNameIds[] = {
      StrId::STR_WEATHER_MONTH_JAN, StrId::STR_WEATHER_MONTH_FEB, StrId::STR_WEATHER_MONTH_MAR,
      StrId::STR_WEATHER_MONTH_APR, StrId::STR_WEATHER_MONTH_MAY, StrId::STR_WEATHER_MONTH_JUN,
      StrId::STR_WEATHER_MONTH_JUL, StrId::STR_WEATHER_MONTH_AUG, StrId::STR_WEATHER_MONTH_SEP,
      StrId::STR_WEATHER_MONTH_OCT, StrId::STR_WEATHER_MONTH_NOV, StrId::STR_WEATHER_MONTH_DEC};
  const char* unitSuffix = WEATHER_SETTINGS.getTempUnit() == WeatherTempUnit::CELSIUS ? "C" : "F";

  // Advance from the first weekday to avoid duplicate day labels around timezone changes.
  struct tm firstDayInfo;
  const time_t firstLocalDate = weatherData.daily[0].date + weatherData.utcOffsetSeconds;
  gmtime_r(&firstLocalDate, &firstDayInfo);
  const int firstWeekday = firstDayInfo.tm_wday;

  const int availableWidth = w - cardGap * (numDays - 1);
  const int baseDayWidth = availableWidth / numDays;
  const int extraPx = availableWidth % numDays;
  int cardX = x;

  for (int i = 0; i < numDays; ++i) {
    const auto& day = weatherData.daily[i];
    const int cardWidth = baseDayWidth + (i < extraPx ? 1 : 0);
    renderer.drawRoundedRect(cardX, y, cardWidth, h, 1, cardRadius, true);
    const int textX = cardX + cardPadding;
    const int textWidth = cardWidth - 2 * cardPadding;
    int textY = y + cardPadding;
    const auto drawLine = [&](const char* text, int fontId = SMALL_FONT_ID,
                              EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
      drawFittedWeatherText(renderer, fontId, textX, textY, textWidth, text, style);
      textY += renderer.getLineHeight(fontId);
    };

    struct tm timeinfo;
    const time_t localDate = day.date + weatherData.utcOffsetSeconds;
    gmtime_r(&localDate, &timeinfo);
    drawLine(I18N.get(dayNameIds[(firstWeekday + i) % 7]), UI_10_FONT_ID, EpdFontFamily::BOLD);
    const auto date = std::string(I18N.get(monthNameIds[timeinfo.tm_mon])) + " " + std::to_string(timeinfo.tm_mday);
    drawLine(date.c_str());

    const auto icon = getWeatherIconLarge(getWeatherIconType(day.weatherCode, true));
    drawWeatherIconWithOrientation(renderer, icon, cardX + (cardWidth - forecastIconSize) / 2, textY + iconGap,
                                   forecastIconSize);
    textY += forecastIconSize + 2 * iconGap;
    drawLine(I18N.get(getWeatherDescriptionStrId(day.weatherCode)));

    char value[32];
    snprintf(value, sizeof(value), "%.0f / %.0f %s", day.tempMax, day.tempMin, unitSuffix);
    if (renderer.getTextWidth(SMALL_FONT_ID, value, EpdFontFamily::BOLD) > textWidth) {
      snprintf(value, sizeof(value), "%.0f/%.0f%s", day.tempMax, day.tempMin, unitSuffix);
    }
    drawLine(value, SMALL_FONT_ID, EpdFontFamily::BOLD);

    snprintf(value, sizeof(value), "%.1f %s", day.precipSum, WEATHER_SETTINGS.getPrecipUnitParam());
    const auto precipitation = std::string(tr(STR_WEATHER_PRECIP)) + ": " + value;
    drawLine(renderer.getTextWidth(SMALL_FONT_ID, precipitation.c_str()) <= textWidth ? precipitation.c_str() : value);

    snprintf(value, sizeof(value), "UV: %.0f", day.uvIndexMax);
    drawLine(value);

    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    formatSunriseSunsetTime(value, sizeof(value), day.sunrise, weatherData.utcOffsetSeconds, !SETTINGS.clockFormat12h);
    drawSunTime(renderer, textX, textY, textWidth, value, true);
    textY += lineHeight;
    formatSunriseSunsetTime(value, sizeof(value), day.sunset, weatherData.utcOffsetSeconds, !SETTINGS.clockFormat12h);
    drawSunTime(renderer, textX, textY, textWidth, value, false);
    textY += lineHeight;

    float phase = day.moonPhase;
    if (phase < 0.0f || phase > 1.0f) phase = getMoonPhaseCycleFromTime(localDate + 12 * 3600);
    drawLine(I18N.get(getMoonPhaseStrId(getMoonPhaseType(phase))));
    snprintf(value, sizeof(value), "%d%%", static_cast<int>(getMoonIlluminationFromCycle(phase) * 100.0f + 0.5f));
    drawLine(value);

    cardX += cardWidth + cardGap;
  }
}

void WeatherActivity::renderHourlyGraph(int x, int y, int w, int h) {
  if (weatherData.hourly.empty()) return;

  // Localized weekday names (indexed by tm_wday: 0=Sun..6=Sat)
  const StrId dayNameIds[] = {StrId::STR_WEATHER_DAY_SUN, StrId::STR_WEATHER_DAY_MON, StrId::STR_WEATHER_DAY_TUE,
                              StrId::STR_WEATHER_DAY_WED, StrId::STR_WEATHER_DAY_THU, StrId::STR_WEATHER_DAY_FRI,
                              StrId::STR_WEATHER_DAY_SAT};

  constexpr int graphFont = UI_HINT_FONT_ID;
  constexpr int leftMargin = 62;  // Space for Y-axis labels with unit suffix
  constexpr int rightMargin = 10;
  constexpr int precipBarMaxHeight = 18;
  const int lineHeight = renderer.getLineHeight(graphFont);
  const int ascender = renderer.getFontAscenderSize(graphFont);
  const char* graphTitle = tr(STR_WEATHER_48H_FORECAST);
  int titleAbove, titleBelow;
  renderer.getTextInkMetrics(graphFont, graphTitle, EpdFontFamily::BOLD, &titleAbove, &titleBelow);
  int dayAbove = 0, dayBelow = 0;
  for (const StrId dayNameId : dayNameIds) {
    int above, below;
    renderer.getTextInkMetrics(graphFont, I18N.get(dayNameId), EpdFontFamily::BOLD, &above, &below);
    dayAbove = std::max(dayAbove, above);
    dayBelow = std::max(dayBelow, below);
  }
  int timeAbove, timeBelow;
  renderer.getTextInkMetrics(graphFont, "00:00", EpdFontFamily::REGULAR, &timeAbove, &timeBelow);
  const int dayTop = titleAbove + titleBelow + 4;
  const int topMargin = dayTop + dayAbove + dayBelow + 4;
  const int bottomMargin = timeAbove + timeBelow + 4;

  const int graphX = x + leftMargin;
  const int graphY = y + topMargin;
  const int graphW = w - leftMargin - rightMargin;
  const int graphH = h - topMargin - bottomMargin - precipBarMaxHeight;
  const int precipY = graphY + graphH;

  const size_t numPoints = weatherData.hourly.size();
  if (numPoints < 2 || graphH <= 0 || graphW <= 0) return;

  // Find day boundaries in hourly data (start index of each day).
  std::vector<size_t> dayStartIndices;
  dayStartIndices.reserve(8);
  dayStartIndices.push_back(0);
  struct tm prevDayInfo;
  time_t prevLocalTime = weatherData.hourly[0].time + weatherData.utcOffsetSeconds;
  gmtime_r(&prevLocalTime, &prevDayInfo);
  for (size_t i = 1; i < numPoints; i++) {
    struct tm curDayInfo;
    time_t localTime = weatherData.hourly[i].time + weatherData.utcOffsetSeconds;
    gmtime_r(&localTime, &curDayInfo);
    if (curDayInfo.tm_year != prevDayInfo.tm_year || curDayInfo.tm_yday != prevDayInfo.tm_yday) {
      dayStartIndices.push_back(i);
      prevDayInfo = curDayInfo;
    }
  }

  // Find temperature range
  float tempMin = weatherData.hourly[0].temperature;
  float tempMax = tempMin;
  float maxPrecip = 0;
  for (const auto& hr : weatherData.hourly) {
    tempMin = std::min(tempMin, hr.temperature);
    tempMax = std::max(tempMax, hr.temperature);
    maxPrecip = std::max(maxPrecip, hr.precipitation);
  }

  // Add padding to temp range
  float tempRange = tempMax - tempMin;
  if (tempRange < 5.0f) {
    float mid = (tempMax + tempMin) / 2.0f;
    tempMin = mid - 2.5f;
    tempMax = mid + 2.5f;
  }
  tempMin -= 1.0f;
  tempMax += 1.0f;
  tempRange = tempMax - tempMin;

  // Draw graph title centered over the graph area.
  drawFittedWeatherText(renderer, graphFont, graphX, y + titleAbove - ascender, graphW, graphTitle,
                        EpdFontFamily::BOLD);

  // Draw weekday labels above the chart area at each day start.
  for (size_t dayIndex = 0; dayIndex < dayStartIndices.size(); ++dayIndex) {
    const size_t startIdx = dayStartIndices[dayIndex];
    struct tm dayInfo;
    time_t localTime = weatherData.hourly[startIdx].time + weatherData.utcOffsetSeconds;
    gmtime_r(&localTime, &dayInfo);

    const char* dayName = I18N.get(dayNameIds[dayInfo.tm_wday]);
    const int labelX = graphX + static_cast<int>(startIdx * graphW / (numPoints - 1)) + 3;
    const int dayEnd = dayIndex + 1 < dayStartIndices.size()
                           ? static_cast<int>(dayStartIndices[dayIndex + 1] * graphW / (numPoints - 1))
                           : graphW;
    const int dayLabelWidth = renderer.getTextWidth(graphFont, dayName, EpdFontFamily::BOLD);
    if (labelX + dayLabelWidth <= graphX + dayEnd - 3) {
      renderer.drawText(graphFont, labelX, y + dayTop + dayAbove - ascender, dayName, true, EpdFontFamily::BOLD);
    }
  }

  // Draw Y-axis labels (temperature)
  const int numIntervals = std::clamp(graphH / lineHeight, 1, 4);
  const float tempStep = tempRange / numIntervals;
  const char* unitSuffix = WEATHER_SETTINGS.getTempUnit() == WeatherTempUnit::CELSIUS ? "C" : "F";
  for (int i = 0; i <= numIntervals; ++i) {
    float temp = tempMax - i * tempStep;
    int labelY = graphY + i * graphH / numIntervals;
    char label[16];
    snprintf(label, sizeof(label), "%.0f %s", temp, unitSuffix);
    drawFittedWeatherText(renderer, graphFont, x + 2, labelY - lineHeight / 2, leftMargin - 5, label);
    // Grid line (dashed effect using short segments)
    for (int gx = graphX; gx < graphX + graphW; gx += 8) {
      renderer.drawPixel(gx, labelY);
    }
  }

  // Draw temperature line
  int prevPx = -1, prevPy = -1;
  for (size_t i = 0; i < numPoints; i++) {
    int px = graphX + static_cast<int>(i * graphW / (numPoints - 1));
    float normalized = (weatherData.hourly[i].temperature - tempMin) / tempRange;
    int py = graphY + graphH - static_cast<int>(normalized * graphH);

    if (prevPx >= 0) {
      renderer.drawLine(prevPx, prevPy, px, py, 2, true);
    }
    prevPx = px;
    prevPy = py;
  }

  // Draw precipitation bars
  if (maxPrecip > 0) {
    for (size_t i = 0; i < numPoints; i++) {
      float precip = weatherData.hourly[i].precipitation;
      if (precip <= 0) continue;

      int px = graphX + static_cast<int>(i * graphW / (numPoints - 1));
      int barH = static_cast<int>((precip / maxPrecip) * precipBarMaxHeight);
      if (barH < 1) barH = 1;
      int barW = std::max(1, graphW / static_cast<int>(numPoints) - 1);
      renderer.fillRect(px - barW / 2, precipY + precipBarMaxHeight - barH, barW, barH);
    }
  }

  // Draw day separator lines for day boundaries after the first day.
  for (size_t boundaryIdx = 1; boundaryIdx < dayStartIndices.size(); boundaryIdx++) {
    const size_t i = dayStartIndices[boundaryIdx];
    const int px = graphX + static_cast<int>(i * graphW / (numPoints - 1));
    renderer.drawLine(px, graphY, px, precipY + precipBarMaxHeight);
  }

  // Draw X-axis labels (every 6 hours)
  int lastLabelEnd = x;
  for (size_t i = 0; i < numPoints; i += 6) {
    int px = graphX + static_cast<int>(i * graphW / (numPoints - 1));
    struct tm timeinfo;
    time_t localTime = weatherData.hourly[i].time + weatherData.utcOffsetSeconds;
    gmtime_r(&localTime, &timeinfo);
    char label[8];
    snprintf(label, sizeof(label), "%02d:00", timeinfo.tm_hour);
    const int labelWidth = renderer.getTextWidth(graphFont, label);
    const int labelX = std::clamp(px - labelWidth / 2, x, x + w - labelWidth);
    if (labelX >= lastLabelEnd + 3) {
      renderer.drawText(graphFont, labelX, precipY + precipBarMaxHeight + 3 + timeAbove - ascender, label);
      lastLabelEnd = labelX + labelWidth;
    }

    // Vertical grid line
    for (int gy = graphY; gy < precipY + precipBarMaxHeight; gy += 8) {
      renderer.drawPixel(px, gy);
    }
  }

  // Draw axes
  renderer.drawLine(graphX, graphY, graphX, precipY + precipBarMaxHeight);  // Y axis
  renderer.drawLine(graphX, precipY + precipBarMaxHeight, graphX + graphW,
                    precipY + precipBarMaxHeight);  // X axis
}
