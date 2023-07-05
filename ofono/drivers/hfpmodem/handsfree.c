/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
 *  Copyright (C) 2011  BMW Car IT GmbH. All rights reserved.
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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include <glib.h>
#include <gatchat.h>
#include <gatresult.h>

#include <ofono/log.h>
#include <ofono/modem.h>
#include <ofono/handsfree.h>

#include "hfpmodem.h"
#include "hfp.h"
#include "slc.h"

static const char *binp_prefix[] = { "+BINP:", NULL };
static const char *bvra_prefix[] = { "+BVRA:", NULL };
static const char *none_prefix[] = { NULL };

struct hf_data {
	GAtChat *chat;
	unsigned int ag_features;
	unsigned int ag_chld_features;
	int battchg_index;
	guint register_source;
};

static void hf_generic_set_cb(gboolean ok, GAtResult *result,
				gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_handsfree_cb_t cb = cbd->cb;
	struct ofono_error error;

	decode_at_error(&error, g_at_result_final_response(result));

	cb(&error, cbd->data);
}

static void bsir_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_handsfree *hf = user_data;
	GAtResultIter iter;
	int value;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+BSIR:"))
		return;

	if (!g_at_result_iter_next_number(&iter, &value))
		return;

	ofono_handsfree_set_inband_ringing(hf, (ofono_bool_t) value);
}

static void bvra_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_handsfree *hf = user_data;
	GAtResultIter iter;
	int value;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+BVRA:"))
		return;

	if (!g_at_result_iter_next_number(&iter, &value))
		return;

	ofono_handsfree_voice_recognition_notify(hf, (ofono_bool_t) value);
}

static void ciev_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_handsfree *hf = user_data;
	struct hf_data *hd = ofono_handsfree_get_data(hf);
	int index;
	int value;
	GAtResultIter iter;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+CIEV:"))
		return;

	if (!g_at_result_iter_next_number(&iter, &index))
		return;

	if (index != hd->battchg_index)
		return;

	if (!g_at_result_iter_next_number(&iter, &value))
		return;

	ofono_handsfree_battchg_notify(hf, value);
}

static void cnum_query_cb(gboolean ok, GAtResult *result, gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_handsfree_cnum_query_cb_t cb = cbd->cb;
	GAtResultIter iter;
	struct ofono_phone_number *list = NULL;
	int num = 0;
	struct ofono_error error;

	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok)
		goto out;

	g_at_result_iter_init(&iter, result);

	while (g_at_result_iter_next(&iter, "+CNUM:"))
		num++;

	if (num == 0)
		goto out;

	list = g_new0(struct ofono_phone_number, num);

	g_at_result_iter_init(&iter, result);

	for (num = 0; g_at_result_iter_next(&iter, "+CNUM:"); ) {
		const char *number;
		int service;
		int type;

		if (!g_at_result_iter_skip_next(&iter))
			continue;

		if (!g_at_result_iter_next_string(&iter, &number))
			continue;

		if (!g_at_result_iter_next_number(&iter, &type))
			continue;

		if (!g_at_result_iter_skip_next(&iter))
			continue;

		if (!g_at_result_iter_next_number(&iter, &service))
			continue;

		/* We are only interested in Voice services */
		if (service != 4)
			continue;

		strncpy(list[num].number, number,
				OFONO_MAX_PHONE_NUMBER_LENGTH);
		list[num].number[OFONO_MAX_PHONE_NUMBER_LENGTH] = '\0';
		list[num].type = type;

		DBG("cnum_notify:%s", list[num].number);
		num++;
	}

out:
	cb(&error, num, list, cbd->data);

	g_free(list);

}

static void hfp_cnum_query(struct ofono_handsfree *hf,
				ofono_handsfree_cnum_query_cb_t cb, void *data)
{
	struct hf_data *hd = ofono_handsfree_get_data(hf);
	struct cb_data *cbd = cb_data_new(cb, data);

	if (g_at_chat_send(hd->chat, "AT+CNUM", none_prefix,
					cnum_query_cb, cbd, g_free) > 0)
		return;

	g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, -1, NULL, data);
}

static void bind_notify(GAtResult *result, gpointer user_data)
{
	struct ofono_handsfree *hf = user_data;
	int hf_indicator;
	int active;
	GAtResultIter iter;

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+BIND:"))
		return;

	if (!g_at_result_iter_next_number(&iter, &hf_indicator))
		return;

	if (!g_at_result_iter_next_number(&iter, &active))
		return;

	ofono_handsfree_hf_indicator_active_notify(hf, hf_indicator, active);
}

static gboolean hfp_handsfree_register(gpointer user_data)
{
	struct ofono_handsfree *hf = user_data;
	struct hf_data *hd = ofono_handsfree_get_data(hf);

	hd->register_source = 0;

	g_at_chat_register(hd->chat, "+BSIR:", bsir_notify, FALSE, hf, NULL);
	g_at_chat_register(hd->chat, "+BVRA:", bvra_notify, FALSE, hf, NULL);
	g_at_chat_register(hd->chat, "+CIEV:", ciev_notify, FALSE, hf, NULL);
	g_at_chat_register(hd->chat, "+BIND:", bind_notify, FALSE, hf, NULL);

	if (hd->ag_features & HFP_AG_FEATURE_IN_BAND_RING_TONE)
		ofono_handsfree_set_inband_ringing(hf, TRUE);

	ofono_handsfree_set_ag_features(hf, hd->ag_features);
	ofono_handsfree_set_ag_chld_features(hf, hd->ag_chld_features);
	ofono_handsfree_register(hf);

	return FALSE;
}

static int hfp_handsfree_probe(struct ofono_handsfree *hf,
				unsigned int vendor, void *data)
{
	struct hfp_slc_info *info = data;
	struct hf_data *hd;
	unsigned int i;

	DBG("");
	hd = g_new0(struct hf_data, 1);
	hd->chat = g_at_chat_clone(info->chat);
	hd->ag_features = info->ag_features;
	hd->ag_chld_features = info->ag_mpty_features;

	ofono_handsfree_set_data(hf, hd);

	hd->battchg_index = info->cind_pos[HFP_INDICATOR_BATTCHG];
	ofono_handsfree_battchg_notify(hf,
					info->cind_val[HFP_INDICATOR_BATTCHG]);

	ofono_handsfree_set_hf_indicators(hf, info->hf_indicators,
						info->num_hf_indicators);

	for (i = 0; i < info->num_hf_indicators; i++)
		ofono_handsfree_hf_indicator_active_notify(hf,
			info->hf_indicators[i],
			info->hf_indicator_active_map & (1 << i));

	hd->register_source = g_idle_add(hfp_handsfree_register, hf);

	return 0;
}

static void hfp_handsfree_remove(struct ofono_handsfree *hf)
{
	struct hf_data *hd = ofono_handsfree_get_data(hf);

	if (hd->register_source != 0)
		g_source_remove(hd->register_source);

	ofono_handsfree_set_data(hf, NULL);

	g_at_chat_unref(hd->chat);
	g_free(hd);
}

static void hfp_request_phone_number_cb(gboolean ok, GAtResult *result,
					gpointer user_data)
{
	struct cb_data *cbd = user_data;
	ofono_handsfree_phone_cb_t cb = cbd->cb;
	GAtResultIter iter;
	struct ofono_error error;
	const char *num;
	int type;
	struct ofono_phone_number phone_number;

	decode_at_error(&error, g_at_result_final_response(result));

	if (!ok) {
		cb(&error, NULL, cbd->data);
		return;
	}

	g_at_result_iter_init(&iter, result);

	if (!g_at_result_iter_next(&iter, "+BINP:"))
		goto fail;

	if (!g_at_result_iter_next_string(&iter, &num))
		goto fail;

	if (!g_at_result_iter_next_number(&iter, &type))
		goto fail;

	DBG("AT+BINP=1 response: %s %d", num, type);

	strncpy(phone_number.number, num,
		OFONO_MAX_PHONE_NUMBER_LENGTH);
	phone_number.number[OFONO_MAX_PHONE_NUMBER_LENGTH] = '\0';
	phone_number.type = type;

	cb(&error, &phone_number, cbd->data);
	return;

fail:
	CALLBACK_WITH_FAILURE(cb, NULL, cbd->data);
}

static void hfp_request_phone_number(struct ofono_handsfree *hf,
					ofono_handsfree_phone_cb_t cb,
					void *data)
{
	struct hf_data *hd = ofono_handsfree_get_data(hf);
	struct cb_data *cbd = cb_data_new(cb, data);

	if (g_at_chat_send(hd->chat, "AT+BINP=1", binp_prefix,
				hfp_request_phone_number_cb,
				cbd, g_free) > 0)
		return;

	g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, NULL, data);
}

static void hfp_voice_recognition(struct ofono_handsfree *hf,
					ofono_bool_t enabled,
					ofono_handsfree_cb_t cb, void *data)
{
	struct hf_data *hd = ofono_handsfree_get_data(hf);
	struct cb_data *cbd = cb_data_new(cb, data);
	char buf[64];

	snprintf(buf, sizeof(buf), "AT+BVRA=%d",
				(int)(enabled));

	if (g_at_chat_send(hd->chat, buf, bvra_prefix,
				hf_generic_set_cb,
				cbd, g_free) > 0)
		return;

	g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, data);
}

static void hfp_disable_nrec(struct ofono_handsfree *hf,
				ofono_handsfree_cb_t cb, void *data)
{
	struct hf_data *hd = ofono_handsfree_get_data(hf);
	struct cb_data *cbd = cb_data_new(cb, data);
	const char *buf = "AT+NREC=0";

	if (g_at_chat_send(hd->chat, buf, none_prefix,
				hf_generic_set_cb, cbd, g_free) > 0)
		return;

	g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, data);
}

static void hfp_hf_indicator(struct ofono_handsfree *hf,
				unsigned short indicator, unsigned int value,
				ofono_handsfree_cb_t cb, void *data)
{
	struct hf_data *hd = ofono_handsfree_get_data(hf);
	struct cb_data *cbd = cb_data_new(cb, data);
	char buf[128];

	snprintf(buf, sizeof(buf), "AT+BIEV=%u,%u", indicator, value);

	if (g_at_chat_send(hd->chat, buf, none_prefix,
				hf_generic_set_cb, cbd, g_free) > 0)
		return;

	g_free(cbd);

	CALLBACK_WITH_FAILURE(cb, data);
}

static const struct ofono_handsfree_driver driver = {
	.name			= "hfpmodem",
	.probe			= hfp_handsfree_probe,
	.remove			= hfp_handsfree_remove,
	.cnum_query		= hfp_cnum_query,
	.request_phone_number	= hfp_request_phone_number,
	.voice_recognition	= hfp_voice_recognition,
	.disable_nrec		= hfp_disable_nrec,
	.hf_indicator		= hfp_hf_indicator,
};

void hfp_handsfree_init(void)
{
	ofono_handsfree_driver_register(&driver);
}

void hfp_handsfree_exit(void)
{
	ofono_handsfree_driver_unregister(&driver);
}
