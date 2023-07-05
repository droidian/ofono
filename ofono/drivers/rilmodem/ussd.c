/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
 *  Copyright (C) 2013 Jolla Ltd
 *  Copyright (C) 2013 Canonical Ltd
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/ussd.h>
#include <smsutil.h>
#include <util.h>

#include "gril.h"

#include "rilmodem.h"

struct ussd_data {
	GRil *ril;
};

static gboolean request_success(gpointer data)
{
	struct cb_data *cbd = data;
	ofono_ussd_cb_t cb = cbd->cb;

	CALLBACK_WITH_SUCCESS(cb, cbd->data);
	g_free(cbd);

	return FALSE;
}

static void ril_ussd_cb(struct ril_msg *message, gpointer user_data)
{
	struct ofono_ussd *ussd = user_data;
	struct ussd_data *ud = ofono_ussd_get_data(ussd);

	/*
	 * We fake an ON_USSD event if there was an error sending the request,
	 * as core will be waiting for one to respond to the Initiate() call.
	 * Note that we already made the callback (see ril_ussd_request()).
	 */
	if (message->error == RIL_E_SUCCESS)
		g_ril_print_response_no_args(ud->ril, message);
	else
		ofono_ussd_notify(ussd, OFONO_USSD_STATUS_NOT_SUPPORTED,
					0, NULL, 0);
}

static void ril_ussd_request(struct ofono_ussd *ussd, int dcs,
			const unsigned char *pdu, int len,
			ofono_ussd_cb_t cb, void *data)
{
	struct ussd_data *ud = ofono_ussd_get_data(ussd);
	struct cb_data *cbd = cb_data_new(cb, data, ussd);
	char *text;
	struct parcel rilp;
	int ret;

	text = ussd_decode(dcs, len, pdu);
	if (!text)
		goto error;

	parcel_init(&rilp);
	parcel_w_string(&rilp, text);

	g_ril_append_print_buf(ud->ril, "(%s)", text);

	ret = g_ril_send(ud->ril, RIL_REQUEST_SEND_USSD,
				&rilp, ril_ussd_cb, ussd, NULL);
	g_free(text);

	/*
	 * TODO: Is g_idle_add necessary?
	 * We do not wait for the SEND_USSD reply to do the callback, as some
	 * networks send it after sending one or more ON_USSD events. From the
	 * ofono core perspective, Initiate() does not return until one ON_USSD
	 * event is received: making here a successful callback just makes the
	 * core wait for that event.
	 */
	if (ret > 0) {
		g_idle_add(request_success, cbd);
		return;
	}

error:
	g_free(cbd);
	CALLBACK_WITH_FAILURE(cb, data);
}

static void ril_ussd_cancel_cb(struct ril_msg *message, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	struct ofono_ussd *ussd = cbd->user;
	struct ussd_data *ud = ofono_ussd_get_data(ussd);
	ofono_ussd_cb_t cb = cbd->cb;

	if (message->error == RIL_E_SUCCESS) {
		g_ril_print_response_no_args(ud->ril, message);
		CALLBACK_WITH_SUCCESS(cb, cbd->data);
	} else {
		CALLBACK_WITH_FAILURE(cb, cbd->data);
	}
}

static void ril_ussd_cancel(struct ofono_ussd *ussd,
				ofono_ussd_cb_t cb, void *user_data)
{
	struct ussd_data *ud = ofono_ussd_get_data(ussd);
	struct cb_data *cbd = cb_data_new(cb, user_data, ussd);

	if (g_ril_send(ud->ril, RIL_REQUEST_CANCEL_USSD, NULL,
				ril_ussd_cancel_cb, cbd, g_free) > 0)
		return;

	g_free(cbd);
	CALLBACK_WITH_FAILURE(cb, user_data);
}

static void ril_ussd_notify(struct ril_msg *message, gpointer user_data)
{
	struct ofono_ussd *ussd = user_data;
	struct ussd_data *ud = ofono_ussd_get_data(ussd);
	struct parcel rilp;
	int numstr;
	char *typestr;
	int type;
	char *str = NULL;
	gsize written;
	char *ucs2;

	g_ril_init_parcel(message, &rilp);

	numstr = parcel_r_int32(&rilp);
	if (numstr < 1)
		return;

	typestr = parcel_r_string(&rilp);
	if (typestr == NULL || *typestr == '\0')
		return;

	type = *typestr - '0';
	g_free(typestr);

	if (numstr > 1)
		str = parcel_r_string(&rilp);

	g_ril_append_print_buf(ud->ril, "{%d,%s}", type, str);

	g_ril_print_unsol(ud->ril, message);

	/* To fix bug in MTK: USSD-Notify arrive with type 2 instead of 0 */
	if (g_ril_vendor(ud->ril) == OFONO_RIL_VENDOR_MTK &&
			str != NULL && type == 2)
		type = 0;

	if (str == NULL) {
		ofono_ussd_notify(ussd, type, 0, NULL, 0);
		return;
	}

	/*
	 * With data coding scheme 0x48, we are saying that the ussd string is a
	 * UCS-2 string, uncompressed, and with unspecified message class. For
	 * the DCS coding, see 3gpp 23.038, sect. 5.
	 */
	ucs2 = g_convert(str, -1, "UCS-2BE//TRANSLIT",
					"UTF-8", NULL, &written, NULL);
	g_free(str);

	if (ucs2 == NULL) {
		ofono_error("%s: Error transcoding", __func__);
		return;
	}

	ofono_ussd_notify(ussd, type, 0x48, (unsigned char *) ucs2, written);
	g_free(ucs2);
}

static gboolean ril_delayed_register(gpointer user_data)
{
	struct ofono_ussd *ussd = user_data;
	struct ussd_data *ud = ofono_ussd_get_data(ussd);

	DBG("");

	ofono_ussd_register(ussd);

	/* Register for USSD responses */
	g_ril_register(ud->ril, RIL_UNSOL_ON_USSD, ril_ussd_notify, ussd);

	return FALSE;
}

static int ril_ussd_probe(struct ofono_ussd *ussd,
					unsigned int vendor,
					void *user)
{
	GRil *ril = user;
	struct ussd_data *ud = g_new0(struct ussd_data, 1);

	ud->ril = g_ril_clone(ril);
	ofono_ussd_set_data(ussd, ud);
	g_idle_add(ril_delayed_register, ussd);

	return 0;
}

static void ril_ussd_remove(struct ofono_ussd *ussd)
{
	struct ussd_data *ud = ofono_ussd_get_data(ussd);
	ofono_ussd_set_data(ussd, NULL);

	g_ril_unref(ud->ril);
	g_free(ud);
}

static const struct ofono_ussd_driver driver = {
	.name		= RILMODEM,
	.probe		= ril_ussd_probe,
	.remove		= ril_ussd_remove,
	.request	= ril_ussd_request,
	.cancel		= ril_ussd_cancel
};

void ril_ussd_init(void)
{
	ofono_ussd_driver_register(&driver);
}

void ril_ussd_exit(void)
{
	ofono_ussd_driver_unregister(&driver);
}
