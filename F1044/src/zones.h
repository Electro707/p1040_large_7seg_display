#pragma once

/**
 * Looks up the POSIX string corresponding to the given tz database name
 * @param[in]   name   the tz database name for the timezone in question
 * @return             the POSIX string for the timezone in question
 **/
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *name;
  const char *posix_str;
} micro_tz_db_pair;

#define N_TIME_ZONES 425

extern const micro_tz_db_pair micro_tz_db_tzs[N_TIME_ZONES];

const char * micro_tz_db_get_posix_str(const char * name);

#ifdef __cplusplus
}
#endif