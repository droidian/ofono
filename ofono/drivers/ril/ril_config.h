/*
 *  oFono - Open Source Telephony - RIL-based devices
 *
 *  Copyright (C) 2015-2021 Jolla Ltd.
 *  Copyright (C) 2019-2020 Open Mobile Platform LLC.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef RIL_CONFIG_H
#define RIL_CONFIG_H

#include "ril_types.h"

#include <ofono/conf.h>

#define RILCONF_SETTINGS_GROUP OFONO_COMMON_SETTINGS_GROUP

GUtilInts *ril_config_get_ints(GKeyFile *file, const char *group,
					const char *key);
char *ril_config_ints_to_string(GUtilInts *ints, char separator);

#endif /* RIL_CONFIG_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
