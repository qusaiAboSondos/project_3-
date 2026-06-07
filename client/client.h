#ifndef CLIENT_H
#define CLIENT_H

#include "../common/config.h"

int getCurrentVersion(const char *version_file);
int CheckForUpdate(const Config *cfg);

#endif
