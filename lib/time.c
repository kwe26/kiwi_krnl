/* time.h / sys/time.h.  Kiwi has no time zones: local time is UTC. */
#include <time.h>
#include <sys/time.h>
#include <kiwi/kiwi.h>

static long long boot_ms = -1;

static long long now_ms(void)
{
    if (boot_ms < 0) {
        long long t = kiwi_syscall(SYS_time, 0, 0, 0, 0, 0);
        boot_ms = t * 1000 - kiwi_uptime_ms();
    }
    return boot_ms + kiwi_uptime_ms();
}

time_t time(time_t *t)
{
    time_t v = (time_t)(now_ms() / 1000);
    if (t)
        *t = v;
    return v;
}

clock_t clock(void)
{
    return (clock_t)kiwi_uptime_ms();
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    long long ms = now_ms();
    tv->tv_sec = (time_t)(ms / 1000);
    tv->tv_usec = (suseconds_t)(ms % 1000) * 1000;
    return 0;
}

int clock_gettime(clockid_t id, struct timespec *ts)
{
    long long ms = id == CLOCK_MONOTONIC ? kiwi_uptime_ms() : now_ms();
    ts->tv_sec = (time_t)(ms / 1000);
    ts->tv_nsec = (long)(ms % 1000) * 1000000;
    return 0;
}

struct tm *gmtime_r(const time_t *t, struct tm *tm)
{
    long long s = *t;
    long long days = s / 86400;
    long rem = (long)(s % 86400);
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    tm->tm_hour = (int)(rem / 3600);
    tm->tm_min = (int)(rem / 60 % 60);
    tm->tm_sec = (int)(rem % 60);
    tm->tm_wday = (int)((days % 7 + 11) % 7);   /* 1970-01-01 was a Thursday */
    /* civil from days (Howard Hinnant) */
    long long z = days + 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = (long)(z - era * 146097);
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    int d = (int)(doy - (153 * mp + 2) / 5 + 1);
    int m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y += m <= 2;
    tm->tm_year = (int)(y - 1900);
    tm->tm_mon = m - 1;
    tm->tm_mday = d;
    static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    tm->tm_yday = cum[m - 1] + d - 1 + (leap && m > 2);
    tm->tm_isdst = 0;
    tm->tm_gmtoff = 0;
    tm->tm_zone = "UTC";
    return tm;
}

struct tm *localtime_r(const time_t *t, struct tm *tm)
{
    return gmtime_r(t, tm);
}

struct tm *gmtime(const time_t *t)
{
    static struct tm tm;
    return gmtime_r(t, &tm);
}

struct tm *localtime(const time_t *t)
{
    return gmtime(t);
}

time_t mktime(struct tm *tm)
{
    long long y = tm->tm_year + 1900LL;
    int m = tm->tm_mon + 1;
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = (long)(y - era * 400);
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + tm->tm_mday - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = era * 146097 + doe - 719468;
    return (time_t)(days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec);
}
