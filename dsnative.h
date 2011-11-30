#define DSN_DLL_VERSION 0.1.2.0
#define DSN_API_VERSION 8

#define DSN_STRINGIFY(s) DSN_TOSTR(s)
#define DSN_TOSTR(s) #s

#define PTS2RT(x) ((REFERENCE_TIME) ((x) * 1E9))
#define RT2PTS(x) (((double) x) / 1E9)
