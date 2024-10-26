/*********************************************************************************************
 * Datetime
 *
 * This library was originally created by JeeLabs / Adafruit.
 * It provides core functionality for handling datetime.
 *
 * Downloaded on 6-08-2024 from https://github.com/adafruit/TinyRTCLib/blob/master/TinyRTClib.h
 *
 * Modifications by Stan:
 * - Removed unneeded functionality
 *********************************************************************************************/

#include "datetime.hpp"

#include "spine/core/debugging.hpp"

#include <cstdio>
#include <cstring>

// *****************************************
//  Static Functions only used in this file
// *****************************************

#if !defined(UNIX_OFFSET)
// SECONDS_FROM_1970_TO_2000
// Difference between the Y2K and the UNIX epochs, in seconds.
// To convert a Y2K timestamp to UNIX...
#    define UNIX_OFFSET 946684800UL
#endif

#if !defined(NTP_OFFSET)
// SECONDS_FROM_1900_TO_2000
// Difference between the Y2K and the NTP epochs, in seconds.
// To convert a Y2K timestamp to NTP...
#    define NTP_OFFSET 3155673600UL
#endif

static constexpr uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/**
 * @brief function which calculates if a year is a leap year
 *
 * @param year
 * @return true
 * @return false
 */
static bool isleapYear(const int year) {
    // check if divisible by 4
    if (year % 4) {
        return false;
    }
    // only check other, when first failed
    return (year % 100 || year % 400 == 0);
}

/**
 * @brief calculate the days since January 1 (0...365)
 *
 * @param year e.g.: 2022
 * @param month 1...12
 * @param day 1...31
 * @return int16_t
 */
static int calcYearDay(const int year, const int month, const int day) {
    auto days = day - 1;
    for (uint8_t i = 1; i < month; ++i)
        days += daysInMonth[i - 1];
    if (month > 2 && isleapYear(year)) ++days;
    return days;
}

// *****************************************
//   Member functions for DateTime object
// *****************************************
/**
 * @brief Construct a new Date Time:: Date Time object
 *
 * @param unix_timestamp
 */
DateTime::DateTime(time_t unix_timestamp)
    : _unix_timestamp{unix_timestamp}, _y2k_timestamp{static_cast<time_t>(unix_timestamp - UNIX_OFFSET)} {
    spn_assert(unix_timestamp - UNIX_OFFSET <= LONG_MAX);
    gmtime_r(&_unix_timestamp, &_tm);
}

/**
 * @brief Construct a new Date Time:: Date Time object
 *
 * @param year year e.g. 2022
 * @param month months since January - [ 1...12 ]
 * @param day day of the month - [ 1...31 ]
 * @param hour hours since midnight - [ 0...23 ]
 * @param min inutes after the hour - [ 0...59 ]
 * @param sec seconds after the minute - [ 0...59 ]
 * @param wday wdays since Sunday - [ 1...7 ]
 * @param dst Daylight Saving Time flag
 */
DateTime::DateTime(int year, int month, int day, int hour, int min, int sec, int wday, int yday, int dst) {
    _tm.tm_sec = sec;
    _tm.tm_min = min;
    _tm.tm_hour = hour;
    _tm.tm_mday = day;
    _tm.tm_mon = month - 1;
    _tm.tm_year = year - 1900;
    _tm.tm_wday = wday - 1;
    _tm.tm_yday = yday;
    _tm.tm_isdst = dst;

    set_timestamps();
}

/**
 * @brief Construct a new Date Time:: Date Time object by givin the precompiler marcos
 * as __DATE__ and __TIME__
 *
 * @param date as  Mmm dd yyyy (e.g. "Jan 14 2012")
 * @param time as HH:MM:SS (e.g. "23:59:01")
 */
DateTime::DateTime(const char* date, const char* time) {
    static const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    static char month_buff[5] = {'0', '0', '0', '0', '\0'};
    int year, day;
    sscanf(date, "%4s %2d %4d", month_buff, &day, &year);
    int month = (strstr(month_names, month_buff) - month_names) / 3 + 1;
    _tm.tm_year = year - 1900;
    _tm.tm_mon = month - 1;
    _tm.tm_mday = day;
    int hour, min, sec;
    sscanf(time, "%i:%i:%i", &hour, &min, &sec);
    _tm.tm_hour = hour;
    _tm.tm_min = min;
    _tm.tm_sec = sec;
    _tm.tm_yday = calcYearDay(year, month, day);
    set_timestamps();
}

/**
 * @brief Set the timestamps by using struct tm entries
 *
 */
void DateTime::set_timestamps() {
#if defined(__AVR__)
    _y2k_timestamp = mktime(&_tm);
    _unix_timestamp = _y2k_timestamp + UNIX_OFFSET;
#else
    _unix_timestamp = mktime(&_tm);
    _y2k_timestamp = _unix_timestamp - UNIX_OFFSET;
#endif
}

/**
 * @brief function to format a DateTime string in an buffer based on the standard strftime function
 *
 *  see: https://cplusplus.com/reference/ctime/strftime/
 *  or:  https://en.cppreference.com/w/cpp/chrono/c/strftime
 *
 * @param buffer buffer for time string
 * @param buffersize size of buffer
 * @param formatSpec define format see strftime
 * @return size_t length of used buffer
 */
size_t DateTime::strf_DateTime(char* buffer, size_t buffersize, const char* formatSpec) {
    size_t len{strftime(buffer, buffersize, formatSpec, &_tm)};
    return len;
}
