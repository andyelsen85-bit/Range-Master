#pragma once
// ============================================================
// Shared UTC→local time formatting helpers for UI screens.
// Both utc_mktime() and fmt_local_time() are static inline so
// each translation unit gets its own copy without link conflicts.
// ============================================================
#include <stdio.h>
#include <string.h>
#include <time.h>

// Portable UTC struct tm → time_t  (timegm equivalent).
// timegm is not in ESP-IDF newlib, so computed manually.
static inline time_t utc_mktime(int year, int mon, int mday,
                                 int hour, int min,  int sec)
{
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int y  = year - 1970;
    time_t t = (time_t)y * 365 * 86400;
    // Leap days between 1970 and year-1
    int ly    = year - 1;
    int leaps = (ly/4 - ly/100 + ly/400) - (1969/4 - 1969/100 + 1969/400);
    t += (time_t)leaps * 86400;
    // Days for full months this year
    int is_leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
    for (int m = 0; m < mon - 1; m++) {
        t += (time_t)mdays[m] * 86400;
        if (m == 1 && is_leap) t += 86400;
    }
    t += (time_t)(mday - 1) * 86400;
    t += (time_t)hour * 3600 + min * 60 + sec;
    return t;
}

// Convert a UTC ISO timestamp "YYYY-MM-DDTHH:MM:SS.000Z" to the
// local-time display string "DD.MM.YYYY HH:MM", honouring the TZ
// env-var set by coprocessor (CET-1CEST for Luxembourg).
// dst must be at least 18 bytes; set to "" on parse failure.
static inline void fmt_local_time(const char *iso_utc,
                                   char *dst, size_t dst_len)
{
    dst[0] = '\0';
    if (!iso_utc || !iso_utc[0]) return;
    int yr, mo, dy, hr, mn, sc = 0;
    if (sscanf(iso_utc, "%d-%d-%dT%d:%d:%d",
               &yr, &mo, &dy, &hr, &mn, &sc) < 5) return;
    time_t t = utc_mktime(yr, mo, dy, hr, mn, sc);
    struct tm tml;
    localtime_r(&t, &tml);   // applies CET/CEST via TZ env
    snprintf(dst, dst_len, "%02d.%02d.%04d %02d:%02d",
             tml.tm_mday, tml.tm_mon + 1, tml.tm_year + 1900,
             tml.tm_hour, tml.tm_min);
}
