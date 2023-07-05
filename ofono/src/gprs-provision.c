/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2011  Nokia Corporation and/or its subsidiary(-ies).
 *  Copyright (C) 2015-2021  Jolla Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <string.h>

#include <ofono/gprs-provision.h>
#include <ofono/log.h>

static GSList *g_drivers = NULL;

void ofono_gprs_provision_free_settings(
				struct ofono_gprs_provision_data *settings,
				int count)
{
	int i;

	for (i = 0; i < count; i++) {
		g_free(settings[i].provider_name);
		g_free(settings[i].name);
		g_free(settings[i].apn);
		g_free(settings[i].username);
		g_free(settings[i].password);
		g_free(settings[i].message_proxy);
		g_free(settings[i].message_center);
	}

	g_free(settings);
}

ofono_bool_t ofono_gprs_provision_get_settings(const char *mcc,
				const char *mnc, const char *spn,
				struct ofono_gprs_provision_data **settings,
				int *count)
{
	GSList *d;

	if (mcc == NULL || strlen(mcc) == 0 || mnc == NULL || strlen(mnc) == 0)
		return FALSE;

	for (d = g_drivers; d != NULL; d = d->next) {
		const struct ofono_gprs_provision_driver *driver = d->data;

		if (driver->get_settings == NULL)
			continue;

		DBG("Calling provisioning plugin '%s'", driver->name);

		if (driver->get_settings(mcc, mnc, spn, settings, count) < 0)
			continue;

		return TRUE;
	}

	return FALSE;
}

static gint compare_priority(gconstpointer a, gconstpointer b)
{
	const struct ofono_gprs_provision_driver *plugin1 = a;
	const struct ofono_gprs_provision_driver *plugin2 = b;

	return plugin2->priority - plugin1->priority;
}

int ofono_gprs_provision_driver_register(
			const struct ofono_gprs_provision_driver *driver)
{
	DBG("driver: %p name: %s", driver, driver->name);

	g_drivers = g_slist_insert_sorted(g_drivers, (void *) driver,
						compare_priority);
	return 0;
}

void ofono_gprs_provision_driver_unregister(
			const struct ofono_gprs_provision_driver *driver)
{
	DBG("driver: %p name: %s", driver, driver->name);

	g_drivers = g_slist_remove(g_drivers, driver);
}
