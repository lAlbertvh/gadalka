// Swiss Ephemeris wrapper - uses real library if available, mock otherwise
#pragma once

#if defined(JYOTISH_REAL_SWISSEPH)
    #include <swephexp.h>
    // Real Swiss Ephemeris constants are already defined in swephexp.h
#else
    // Mock Swiss Ephemeris for development without the library
    
    // Planet IDs
    #define SE_SUN        0
    #define SE_MOON       1
    #define SE_MARS       2
    #define SE_MERCURY    3
    #define SE_JUPITER    4
    #define SE_VENUS      5
    #define SE_SATURN     6
    #define SE_TRUE_NODE  10

    // Flags
    #define SEFLG_SWIEPH     1
    #define SEFLG_SPEED      2
    #define SEFLG_SIDEREAL   4
    #define SEFLG_SIDERAL    4

    // Sideral modes
    #define SE_SIDM_LAHIRI   1

    // Calendar
    #define SE_GREG_CAL      1

    // Mock functions
    inline void swe_set_ephe_path(const char* path) {
        (void)path;
    }

    inline void swe_set_sid_mode(int mode, double t0, double ayan_t0) {
        (void)mode; (void)t0; (void)ayan_t0;
    }

    inline double swe_julday(int year, int month, int day, double hour, int cal) {
        (void)cal;
        int a = (14 - month) / 12;
        int y = year + 4800 - a;
        int m = month + 12 * a - 3;
        double jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
        return jd + (hour - 12.0) / 24.0;
    }

    inline int swe_calc_ut(double jd, int planet, int flags, double* xx, char* serr) {
        (void)jd; (void)planet; (void)flags; (void)serr;
        static const double mock_positions[] = {
            120.0,  // Sun
            200.0,  // Moon
            300.0,  // Mars
            150.0,  // Mercury
            180.0,  // Jupiter
            100.0,  // Venus
            250.0,  // Saturn
            0.0,    // Rahu
            0.0,    // Ketu
        };
        
        if (planet >= 0 && planet < 9) {
            xx[0] = mock_positions[planet];
            xx[1] = 0.0;
            xx[2] = 1.0;
            xx[3] = 0.0;
            xx[4] = 0.0;
            xx[5] = 0.0;
            return 0;
        }
        return -1;
    }

    inline int swe_houses_ex(double jd, int flags, double geolat, double geolon, char hsys, double* cusps, double* ascmc) {
        (void)jd; (void)geolat; (void)geolon; (void)hsys; (void)flags;
        for (int i = 0; i < 13; ++i) cusps[i] = i * 30.0;
        ascmc[0] = 0.0;
        return 0;
    }

    inline const char* swe_get_planet_name(int planet) {
        static const char* names[] = {
            "Sun", "Moon", "Mars", "Mercury", "Jupiter", "Venus", "Saturn", "Uranus", "Neptune", "Pluto", "Mean Node", "True Node"
        };
        if (planet >= 0 && planet <= 10) return names[planet];
        return "Unknown";
    }
#endif