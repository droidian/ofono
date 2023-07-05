/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2008-2011  Intel Corporation. All rights reserved.
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <glib.h>
#include <glib/gprintf.h>

#include <ofono/types.h>
#include "smsutil.h"
#include "stkutil.h"
#include "util.h"

#include "stk-test-data.h"

#define MAX_ITEM 100

struct sms_submit_test {
	gboolean rd;
	enum sms_validity_period_format vpf;
	gboolean rp;
	gboolean udhi;
	gboolean srr;
	guint8 mr;
	struct sms_address daddr;
	guint8 pid;
	guint8 dcs;
	struct sms_validity_period vp;
	guint8 udl;
	guint8 ud[160];
};

struct sms_test {
	struct sms_address sc_addr;
	enum sms_type type;
	union {
		struct sms_deliver deliver;
		struct sms_deliver_ack_report deliver_ack_report;
		struct sms_deliver_err_report deliver_err_report;
		struct sms_submit_test submit;
		struct sms_submit_ack_report submit_ack_report;
		struct sms_submit_err_report submit_err_report;
		struct sms_command command;
		struct sms_status_report status_report;
	};
};

static gboolean g_mem_equal(const unsigned char *v1, const unsigned char *v2,
				unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++)
		if (v1[i] != v2[i])
			return FALSE;

	return TRUE;
}

static inline void check_common_bool(const ofono_bool_t command,
					const ofono_bool_t test)
{
	g_assert(command == test);
}

static inline void check_common_byte(const unsigned char command,
					const unsigned char test)
{
	g_assert(command == test);
}

static inline void check_common_text(const char *command, const char *test)
{
	if (test == NULL) {
		g_assert(command == NULL);
		return;
	}

	g_assert(command != NULL);
	g_assert(g_str_equal(command, test));
}

static inline void check_common_byte_array(
				const struct stk_common_byte_array *command,
				const struct stk_common_byte_array *test)
{
	if (test->len == 0) {
		g_assert(command->len == 0);
		return;
	}

	g_assert(command->len != 0);
	g_assert(command->len == test->len);
	g_assert(g_mem_equal(command->array, test->array, test->len));
}

/* Defined in TS 102.223 Section 8.1 */
static inline void check_address(const struct stk_address *command,
					const struct stk_address *test)
{
	g_assert(command->ton_npi == test->ton_npi);
	check_common_text(command->number, test->number);
}

/* Defined in TS 102.223 Section 8.2 */
static inline void check_alpha_id(const char *command, const char *test)
{
	if (test != NULL && strlen(test) > 0)
		check_common_text(command, test);
	else
		g_assert(command == NULL);
}

/* Defined in TS 102.223 Section 8.3 */
static void check_subaddress(const struct stk_subaddress *command,
					const struct stk_subaddress *test)
{
	if (test->len == 0) {
		g_assert(command->len == 0);
		return;
	}

	g_assert(command->len != 0);
	g_assert(g_mem_equal(command->subaddr, test->subaddr, test->len));
}

/* Defined in TS 102.223 Section 8.4 */
static void check_ccp(const struct stk_ccp *command,
					const struct stk_ccp *test)
{
	if (test->len == 0) {
		g_assert(command->len == 0);
		return;
	}

	g_assert(command->len != 0);
	g_assert(g_mem_equal(command->ccp, test->ccp, test->len));
}

/* Defined in TS 102.223 Section 8.8 */
static void check_duration(const struct stk_duration *command,
					const struct stk_duration *test)
{
	g_assert(command->unit == test->unit);
	g_assert(command->interval == test->interval);
}

/* Defined in TS 102.223 Section 8.9 */
static void check_item(const struct stk_item *command,
					const struct stk_item *test)
{
	g_assert(command->id == test->id);
	check_common_text(command->text, test->text);
}

/* Defined in TS 102.223 Section 8.10 */
static inline void check_item_id(const unsigned char command,
					const unsigned char test)
{
	check_common_byte(command, test);
}

static void check_items(GSList *command, const struct stk_item *test)
{
	struct stk_item *si;
	GSList *l;
	unsigned int i = 0;

	for (l = command; l; l = l->next) {
		si = l->data;
		check_item(si, &test[i++]);
	}

	g_assert(test[i].id == 0);
}

/* Defined in TS 102.223 Section 8.11 */
static void check_response_length(const struct stk_response_length *command,
					const struct stk_response_length *test)
{
	g_assert(command->min == test->min);
	g_assert(command->max == test->max);
}

/* Defined in TS 102.223 Section 8.13 */
static void check_gsm_sms(const struct sms *command,
					const struct sms_test *test)
{
	g_assert(command->sc_addr.number_type == test->sc_addr.number_type);
	g_assert(command->sc_addr.numbering_plan ==
			test->sc_addr.numbering_plan);
	g_assert(g_str_equal(command->sc_addr.address, test->sc_addr.address));

	switch (test->type) {
	case SMS_TYPE_SUBMIT: {
		const struct sms_submit *cs = &command->submit;
		const struct sms_submit_test *ts = &test->submit;
		enum sms_charset charset;

		g_assert(cs->rd == ts->rd);
		g_assert(cs->vpf == ts->vpf);
		g_assert(cs->rp == ts->rp);
		g_assert(cs->udhi == ts->udhi);
		g_assert(cs->srr == ts->srr);
		g_assert(cs->mr == ts->mr);

		g_assert(cs->daddr.number_type == ts->daddr.number_type);
		g_assert(cs->daddr.numbering_plan == ts->daddr.numbering_plan);
		g_assert(g_str_equal(cs->daddr.address, ts->daddr.address));

		g_assert(cs->pid == ts->pid);
		g_assert(cs->dcs == ts->dcs);

		switch (ts->vpf) {
		case SMS_VALIDITY_PERIOD_FORMAT_RELATIVE:
			g_assert(cs->vp.relative == ts->vp.relative);
			break;
		case SMS_VALIDITY_PERIOD_FORMAT_ABSOLUTE: {
			const struct sms_scts *ca = &cs->vp.absolute;
			const struct sms_scts *ta = &ts->vp.absolute;
			g_assert(ca->year == ta->year);
			g_assert(ca->month == ta->month);
			g_assert(ca->day == ta->day);
			g_assert(ca->hour == ta->hour);
			g_assert(ca->minute == ta->minute);
			g_assert(ca->second == ta->second);
			g_assert(ca->has_timezone == ta->has_timezone);

			if (ta->has_timezone)
				g_assert(ca->timezone == ta->timezone);

			break;
		}
		case SMS_VALIDITY_PERIOD_FORMAT_ENHANCED:
			g_assert(g_mem_equal(cs->vp.enhanced,
							ts->vp.enhanced, 7));
			break;
		default:
			break;
		}

		g_assert(cs->udl == ts->udl);

		sms_dcs_decode(ts->dcs, NULL, &charset, NULL, NULL);

		if (charset == SMS_CHARSET_8BIT)
			g_assert(g_str_equal(cs->ud, ts->ud));
		else {
			GSList *sms_list = NULL;
			char *message;
			sms_list = g_slist_prepend(sms_list, (void *)command);
			message = sms_decode_text(sms_list);
			g_assert(g_str_equal(message, ts->ud));
			g_free(message);
			g_slist_free(sms_list);
		}

		break;
	}
	default:
		g_assert(FALSE);
	}
}

/* Defined in TS 102.223 Section 8.14 */
static inline void check_ss(const struct stk_ss *command,
					const struct stk_ss *test)
{
	g_assert(command->ton_npi == test->ton_npi);
	check_common_text(command->ss, test->ss);
}

/* Defined in TS 102.223 Section 8.15 */
static inline void check_text(const char *command, const char *test)
{
	check_common_text(command, test);
}

/* Defined in TS 102.223 Section 8.16 */
static inline void check_tone(const ofono_bool_t command,
					const ofono_bool_t test)
{
	check_common_bool(command, test);
}

/* Defined in TS 102.223 Section 8.17 */
static inline void check_ussd(const struct stk_ussd_string *command,
							const char *test)
{
	char *utf8 = ussd_decode(command->dcs, command->len, command->string);
	check_common_text(utf8, test);
	g_free(utf8);
}

/* Defined in TS 102.223 Section 8.18 */
static void check_file_list(GSList *command, const struct stk_file *test)
{
	struct stk_file *sf;
	GSList *l;
	unsigned int i = 0;

	for (l = command; l; l = l->next) {
		sf = l->data;
		g_assert(sf->len == test[i].len);
		g_assert(g_mem_equal(sf->file, test[i++].file, sf->len));
	}

	g_assert(test[i].len == 0);
}

/* Defined in TS 102.223 Section 8.23 */
static inline void check_default_text(const char *command, const char *test)
{
	check_common_text(command, test);
}

/* Defined in TS 102.223 Section 8.24 */
static void check_items_next_action_indicator(
			const struct stk_items_next_action_indicator *command,
			const struct stk_items_next_action_indicator *test)
{
	g_assert(command->len == test->len);
	g_assert(g_mem_equal(command->list, test->list, test->len));
}

/* Defined in TS 102.223 Section 8.25 */
static void check_event_list(const struct stk_event_list *command,
				const struct stk_event_list *test)
{
	g_assert(command->len == test->len);
	g_assert(g_mem_equal(command->list, test->list, test->len));
}

/* Defined in TS 102.223 Section 8.31 */
static void check_icon_id(const struct stk_icon_id *command,
					const struct stk_icon_id *test)
{
	g_assert(command->id == test->id);
	g_assert(command->qualifier == test->qualifier);
}

/* Defined in TS 102.223 Section 8.32 */
static void check_item_icon_id_list(const struct stk_item_icon_id_list *command,
				const struct stk_item_icon_id_list *test)
{
	g_assert(command->qualifier == test->qualifier);
	g_assert(command->len == test->len);
	g_assert(g_mem_equal(command->list, test->list, test->len));
}

/* Defined in TS 102.223 Section 8.35 */
static void check_c_apdu(const struct stk_c_apdu *command,
				const struct stk_c_apdu *test)
{
	g_assert(command->cla == test->cla);
	g_assert(command->ins == test->ins);
	g_assert(command->p1 == test->p1);
	g_assert(command->p2 == test->p2);
	g_assert(command->lc == test->lc);
	g_assert(g_mem_equal(command->data, test->data, test->lc));

	if (test->has_le)
		g_assert(command->le == test->le);
}

/* Defined in TS 102.223 Section 8.37 */
static inline void check_timer_id(const unsigned char command,
					const unsigned char test)
{
	check_common_byte(command, test);
}

/* Defined in TS 102.223 Section 8.38 */
static inline void check_timer_value(const struct stk_timer_value *command,
					const struct stk_timer_value *test)
{
	g_assert(command->hour == test->hour);
	g_assert(command->minute == test->minute);
	g_assert(command->second == test->second);
}

/* Defined in TS 102.223 Section 8.40 */
static inline void check_at_command(const char *command, const char *test)
{
	check_common_text(command, test);
}

/* Defined in TS 102.223 Section 8.43 */
static inline void check_imm_resp(const unsigned char command,
					const unsigned char test)
{
	check_common_byte(command, test);
}

/* Defined in TS 102.223 Section 8.44 */
static inline void check_dtmf_string(const char *command, const char *test)
{
	check_common_text(command, test);
}

/* Defined in TS 102.223 Section 8.45 */
static inline void check_language(const char *command, const char *test)
{
	check_common_text(command, test);
}

/* Defined in TS 102.223 Section 8.47 */
static inline void check_browser_id(const unsigned char command,
					const unsigned char test)
{
	check_common_byte(command, test);
}

/* Defined in TS 102.223 Section 8.48 */
static inline void check_url(const char *command, const char *test)
{
	check_common_text(command, test);
}

/* Defined in TS 102.223 Section 8.49 */
static inline void check_bearer(const struct stk_common_byte_array *command,
				const struct stk_common_byte_array *test)
{
	check_common_byte_array(command, test);
}

/* Defined in TS 102.223 Section 8.50 */
static void check_provisioning_file_reference(const struct stk_file *command,
					const struct stk_file *test)
{
	g_assert(command->len == test->len);
	g_assert(g_mem_equal(command->file, test->file, test->len));
}

static void check_provisioning_file_references(GSList *command,
						const struct stk_file *test)
{
	struct stk_file *sf;
	GSList *l;
	unsigned int i = 0;

	for (l = command; l; l = l->next) {
		sf = l->data;
		check_provisioning_file_reference(sf, &test[i++]);
	}

	g_assert(test[i].len == 0);
}

/* Defined in TS 102.223 Section 8.52 */
static void check_bearer_desc(const struct stk_bearer_description *command,
				const struct stk_bearer_description *test)
{
	g_assert(command->type == test->type);

	if (test->type == STK_BEARER_TYPE_GPRS_UTRAN) {
		check_common_byte(command->gprs.precedence,
				test->gprs.precedence);
		check_common_byte(command->gprs.delay,
				test->gprs.delay);
		check_common_byte(command->gprs.reliability,
				test->gprs.reliability);
		check_common_byte(command->gprs.peak,
				test->gprs.peak);
		check_common_byte(command->gprs.mean,
				test->gprs.mean);
		check_common_byte(command->gprs.pdp_type,
				test->gprs.pdp_type);

		return;
	}
}

/* Defined in TS 102.223 Section 8.53 */
static inline void check_channel_data(
				const struct stk_common_byte_array *command,
				const struct stk_common_byte_array *test)
{
	check_common_byte_array(command, test);
}

/* Defined in TS 102.223 Section 8.58 */
static inline void check_other_address(
				const struct stk_other_address *command,
				const struct stk_other_address *test)
{
	check_common_byte(command->type, test->type);

	if (test->type == STK_ADDRESS_IPV4)
		g_assert(command->addr.ipv4 == test->addr.ipv4);
	else
		g_assert(g_mem_equal(command->addr.ipv6, test->addr.ipv6, 16));
}

/* Defined in TS 102.223 Section 8.59 */
static void check_uicc_te_interface(const struct stk_uicc_te_interface *command,
				const struct stk_uicc_te_interface *test)
{
	check_common_byte(command->protocol, test->protocol);
	g_assert(command->port == test->port);
}

/* Defined in TS 102.223 Section 8.60 */
static inline void check_aid(const struct stk_aid *command,
					const struct stk_aid *test)
{
	g_assert(g_mem_equal(command->aid, test->aid, test->len));
}

/* Defined in TS 102.223 Section 8.70 */
static inline void check_network_access_name(const char *command,
						const char *test)
{
	check_common_text(command, test);
}

/* Defined in TS 102.223 Section 8.71 */
static inline void check_cdma_sms_tpdu(
				const struct stk_common_byte_array *command,
				const struct stk_common_byte_array *test)
{
	check_common_byte_array(command, test);
}

static void check_text_attr_html(const struct stk_text_attribute *test,
				char *text, const char *expected_html)
{
	char *html;
	unsigned short attrs[256];
	int i;

	if (expected_html == NULL)
		return;

	for (i = 0; i < test->len; i += 4) {
		attrs[i] = test->attributes[i];
		attrs[i + 1] = test->attributes[i + 1];
		attrs[i + 2] = test->attributes[i + 2];
		attrs[i + 3] = test->attributes[i + 3];
	}
	html = stk_text_to_html(text, attrs, test->len / 4);

	g_assert(memcmp(html, expected_html, strlen(expected_html)) == 0);

	g_free(html);
}

/* Defined in TS 102.223 Section 8.72 */
static void check_text_attr(const struct stk_text_attribute *command,
					const struct stk_text_attribute *test)
{
	g_assert(command->len == test->len);
	g_assert(g_mem_equal(command->attributes, test->attributes, test->len));
}

/* Defined in TS 102.223 Section 8.73 */
static void check_item_text_attribute_list(
			const struct stk_item_text_attribute_list *command,
			const struct stk_item_text_attribute_list *test)
{
	g_assert(command->len == test->len);
	g_assert(g_mem_equal(command->list, test->list, test->len));
}

/* Defined in TS 102.223 Section 8.80 */
static void check_frame_id(const struct stk_frame_id *command,
					const struct stk_frame_id *test)
{
	g_assert(command->has_id == test->has_id);
	if (test->has_id)
		g_assert(command->id == test->id);
}

struct display_text_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	const char *text;
	struct stk_icon_id icon_id;
	ofono_bool_t immediate_response;
	struct stk_duration duration;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
	const char *html;
};

static struct display_text_test display_text_data_111 = {
	.pdu = display_text_111,
	.pdu_len = sizeof(display_text_111),
	.qualifier = 0x80,
	.text = "Toolkit Test 1"
};

static struct display_text_test display_text_data_131 = {
	.pdu = display_text_131,
	.pdu_len = sizeof(display_text_131),
	.qualifier = 0x81,
	.text = "Toolkit Test 2"
};

static struct display_text_test display_text_data_141 = {
	.pdu = display_text_141,
	.pdu_len = sizeof(display_text_141),
	.qualifier = 0x80,
	.text = "Toolkit Test 3"
};

static struct display_text_test display_text_data_151 = {
	.pdu = display_text_151,
	.pdu_len = sizeof(display_text_151),
	.qualifier = 0x00,
	.text = "Toolkit Test 4"
};

static struct display_text_test display_text_data_161 = {
	.pdu = display_text_161,
	.pdu_len = sizeof(display_text_161),
	.qualifier = 0x80,
	.text = "This command instructs the ME to display a text message. "
			"It allows the SIM to define the priority of that "
			"message, and the text string format. Two types of "
			"prio"
};

static struct display_text_test display_text_data_171 = {
	.pdu = display_text_171,
	.pdu_len = sizeof(display_text_171),
	.qualifier = 0x80,
	.text = "<GO-BACKWARDS>"
};

static struct display_text_test display_text_data_511 = {
	.pdu = display_text_511,
	.pdu_len = sizeof(display_text_511),
	.qualifier = 0x80,
	.text = "Basic Icon",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct display_text_test display_text_data_521 = {
	.pdu = display_text_521,
	.pdu_len = sizeof(display_text_521),
	.qualifier = 0x80,
	.text = "Colour Icon",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct display_text_test display_text_data_531 = {
	.pdu = display_text_531,
	.pdu_len = sizeof(display_text_531),
	.qualifier = 0x80,
	.text = "Basic Icon",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct display_text_test display_text_data_611 = {
	.pdu = display_text_611,
	.pdu_len = sizeof(display_text_611),
	.qualifier = 0x80,
	.text = "ЗДРАВСТВУЙТЕ"
};

static struct display_text_test display_text_data_711 = {
	.pdu = display_text_711,
	.pdu_len = sizeof(display_text_711),
	.qualifier = 0x80,
	.text = "10 Second",
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 10,
	}
};

static struct display_text_test display_text_data_811 = {
	.pdu = display_text_811,
	.pdu_len = sizeof(display_text_811),
	.qualifier = 0x80,
	.text = "Text Attribute 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 },
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 1</span>"
		"</div>",
};

static struct display_text_test display_text_data_821 = {
	.pdu = display_text_821,
	.pdu_len = sizeof(display_text_821),
	.qualifier = 0x80,
	.text = "Text Attribute 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x01, 0xB4 },
	},
	.html = "<div style=\"text-align: center;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 1</span>"
		"</div>",
};

static struct display_text_test display_text_data_831 = {
	.pdu = display_text_831,
	.pdu_len = sizeof(display_text_831),
	.qualifier = 0x80,
	.text = "Text Attribute 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x02, 0xB4 },
	},
	.html = "<div style=\"text-align: right;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 1</span>"
		"</div>",
};

static struct display_text_test display_text_data_841 = {
	.pdu = display_text_841,
	.pdu_len = sizeof(display_text_841),
	.qualifier = 0x80,
	.text = "Text Attribute 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x04, 0xB4 },
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"big;color: #347235;background-color: #FFFF00;\">"
		"Text Attribute 1</span></div>",
};

static struct display_text_test display_text_data_851 = {
	.pdu = display_text_851,
	.pdu_len = sizeof(display_text_851),
	.qualifier = 0x80,
	.text = "Text Attribute 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x08, 0xB4 },
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"small;color: #347235;background-color: #FFFF00;\">"
		"Text Attribute 1</span></div>",
};

static struct display_text_test display_text_data_861 = {
	.pdu = display_text_861,
	.pdu_len = sizeof(display_text_861),
	.qualifier = 0x80,
	.text = "Text Attribute 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x10, 0xB4 },
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-weight: "
		"bold;color: #347235;background-color: #FFFF00;\">"
		"Text Attribute 1</span></div>",
};

static struct display_text_test display_text_data_871 = {
	.pdu = display_text_871,
	.pdu_len = sizeof(display_text_871),
	.qualifier = 0x80,
	.text = "Text Attribute 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x20, 0xB4 },
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-style: "
		"italic;color: #347235;background-color: #FFFF00;\">"
		"Text Attribute 1</span></div>",
};

static struct display_text_test display_text_data_881 = {
	.pdu = display_text_881,
	.pdu_len = sizeof(display_text_881),
	.qualifier = 0x80,
	.text = "Text Attribute 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x40, 0xB4 },
	},
	.html = "<div style=\"text-align: left;\"><span style=\""
		"text-decoration: underline;color: #347235;"
		"background-color: #FFFF00;\">Text Attribute 1</span></div>",
};

static struct display_text_test display_text_data_891 = {
	.pdu = display_text_891,
	.pdu_len = sizeof(display_text_891),
	.qualifier = 0x80,
	.text = "Text Attribute 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x80, 0xB4 },
	},
	.html = "<div style=\"text-align: left;\"><span style=\""
		"text-decoration: line-through;color: #347235;"
		"background-color: #FFFF00;\">Text Attribute 1</span></div>",
};

static struct display_text_test display_text_data_911 = {
	.pdu = display_text_911,
	.pdu_len = sizeof(display_text_911),
	.qualifier = 0x80,
	.text = "你好"
};

static struct display_text_test display_text_data_1011 = {
	.pdu = display_text_1011,
	.pdu_len = sizeof(display_text_1011),
	.qualifier = 0x80,
	.text = "80ル"
};

/* Defined in TS 102.384 Section 27.22.4.1 */
static void test_display_text(gconstpointer data)
{
	const struct display_text_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_DISPLAY_TEXT);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_DISPLAY);

	g_assert(command->display_text.text);
	check_text(command->display_text.text, test->text);
	check_icon_id(&command->display_text.icon_id, &test->icon_id);
	check_imm_resp(command->display_text.immediate_response,
						test->immediate_response);
	check_duration(&command->display_text.duration, &test->duration);
	check_text_attr(&command->display_text.text_attr,
						&test->text_attr);
	check_text_attr_html(&command->display_text.text_attr,
				command->display_text.text,
				test->html);
	check_frame_id(&command->display_text.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct get_inkey_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *text;
	struct stk_icon_id icon_id;
	struct stk_duration duration;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
	char *html;
};

static unsigned char get_inkey_711[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x80, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x2B, 0x22 };

static unsigned char get_inkey_712[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x80, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x2B, 0x22 };

static unsigned char get_inkey_912[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static unsigned char get_inkey_922[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static unsigned char get_inkey_932[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static unsigned char get_inkey_942[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22, 0xD0, 0x04, 0x00,
						0x09, 0x00, 0xB4 };

static unsigned char get_inkey_943[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static unsigned char get_inkey_952[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22, 0xD0, 0x04, 0x00,
						0x09, 0x00, 0xB4 };

static unsigned char get_inkey_953[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static unsigned char get_inkey_962[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22, 0xD0, 0x04, 0x00,
						0x09, 0x00, 0xB4 };

static unsigned char get_inkey_963[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static unsigned char get_inkey_972[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22, 0xD0, 0x04, 0x00,
						0x09, 0x00, 0xB4 };

static unsigned char get_inkey_973[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static unsigned char get_inkey_982[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22, 0xD0, 0x04, 0x00,
						0x09, 0x00, 0xB4 };

static unsigned char get_inkey_983[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static unsigned char get_inkey_992a[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22, 0xD0, 0x04, 0x00,
						0x09, 0x00, 0xB4 };

static unsigned char get_inkey_992b[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static unsigned char get_inkey_993[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static unsigned char get_inkey_9102[] = { 0xD0, 0x15, 0x81, 0x03, 0x01, 0x22,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0A, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x22,
						0x23, 0x22 };

static struct get_inkey_test get_inkey_data_111 = {
	.pdu = get_inkey_111,
	.pdu_len = sizeof(get_inkey_111),
	.qualifier = 0x00,
	.text = "Enter \"+\""
};

static struct get_inkey_test get_inkey_data_121 = {
	.pdu = get_inkey_121,
	.pdu_len = sizeof(get_inkey_121),
	.qualifier = 0x00,
	.text = "Enter \"0\""
};

static struct get_inkey_test get_inkey_data_131 = {
	.pdu = get_inkey_131,
	.pdu_len = sizeof(get_inkey_131),
	.qualifier = 0x00,
	.text = "<GO-BACKWARDS>"
};

static struct get_inkey_test get_inkey_data_141 = {
	.pdu = get_inkey_141,
	.pdu_len = sizeof(get_inkey_141),
	.qualifier = 0x00,
	.text = "<ABORT>"
};

static struct get_inkey_test get_inkey_data_151 = {
	.pdu = get_inkey_151,
	.pdu_len = sizeof(get_inkey_151),
	.qualifier = 0x01,
	.text = "Enter \"q\""
};

static struct get_inkey_test get_inkey_data_161 = {
	.pdu = get_inkey_161,
	.pdu_len = sizeof(get_inkey_161),
	.qualifier = 0x01,
	.text = "Enter \"x\". This command instructs the ME to display text, "
		"and to expect the user to enter a single character. Any "
		"response entered by the user shall be passed t"
};

static struct get_inkey_test get_inkey_data_211 = {
	.pdu = get_inkey_211,
	.pdu_len = sizeof(get_inkey_211),
	.qualifier = 0x00,
	.text = "<TIME-OUT>"
};

static struct get_inkey_test get_inkey_data_311 = {
	.pdu = get_inkey_311,
	.pdu_len = sizeof(get_inkey_311),
	.qualifier = 0x00,
	.text = "ЗДРАВСТВУЙТЕ"
};

static struct get_inkey_test get_inkey_data_321 = {
	.pdu = get_inkey_321,
	.pdu_len = sizeof(get_inkey_321),
	.qualifier = 0x00,
	.text = "ЗДРАВСТВУЙТЕЗДРАВСТВУЙТЕ"
		"ЗДРАВСТВУЙТЕЗДРАВСТВУЙТЕ"
		"ЗДРАВСТВУЙТЕЗДРАВСТВУЙ"
};

static struct get_inkey_test get_inkey_data_411 = {
	.pdu = get_inkey_411,
	.pdu_len = sizeof(get_inkey_411),
	.qualifier = 0x03,
	.text = "Enter"
};

static struct get_inkey_test get_inkey_data_511 = {
	.pdu = get_inkey_511,
	.pdu_len = sizeof(get_inkey_511),
	.qualifier = 0x04,
	.text = "Enter YES"
};

static struct get_inkey_test get_inkey_data_512 = {
	.pdu = get_inkey_512,
	.pdu_len = sizeof(get_inkey_512),
	.qualifier = 0x04,
	.text = "Enter NO"
};

static struct get_inkey_test get_inkey_data_611 = {
	.pdu = get_inkey_611,
	.pdu_len = sizeof(get_inkey_611),
	.qualifier = 0x00,
	.text = "<NO-ICON>",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct get_inkey_test get_inkey_data_621 = {
	.pdu = get_inkey_621,
	.pdu_len = sizeof(get_inkey_621),
	.qualifier = 0x00,
	.text = "<BASIC-ICON>",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct get_inkey_test get_inkey_data_631 = {
	.pdu = get_inkey_631,
	.pdu_len = sizeof(get_inkey_631),
	.qualifier = 0x00,
	.text = "<NO-ICON>",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct get_inkey_test get_inkey_data_641 = {
	.pdu = get_inkey_641,
	.pdu_len = sizeof(get_inkey_641),
	.qualifier = 0x00,
	.text = "<COLOUR-ICON>",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct get_inkey_test get_inkey_data_711 = {
	.pdu = get_inkey_711,
	.pdu_len = sizeof(get_inkey_711),
	.qualifier = 0x80,
	.text = "Enter \"+\""
};

static struct get_inkey_test get_inkey_data_712 = {
	.pdu = get_inkey_712,
	.pdu_len = sizeof(get_inkey_712),
	.qualifier = 0x80,
	.text = "Enter \"+\""
};

static struct get_inkey_test get_inkey_data_811 = {
	.pdu = get_inkey_811,
	.pdu_len = sizeof(get_inkey_811),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 10
	}
};

static struct get_inkey_test get_inkey_data_911 = {
	.pdu = get_inkey_911,
	.pdu_len = sizeof(get_inkey_911),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter \"+\"</span></div>",
};

static struct get_inkey_test get_inkey_data_912 = {
	.pdu = get_inkey_912,
	.pdu_len = sizeof(get_inkey_912),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_921 = {
	.pdu = get_inkey_921,
	.pdu_len = sizeof(get_inkey_921),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x01, 0xB4 }
	},
	.html = "<div style=\"text-align: center;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter \"+\"</span>"
		"</div>",
};

static struct get_inkey_test get_inkey_data_922 = {
	.pdu = get_inkey_922,
	.pdu_len = sizeof(get_inkey_922),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_931 = {
	.pdu = get_inkey_931,
	.pdu_len = sizeof(get_inkey_931),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x02, 0xB4 }
	},
	.html = "<div style=\"text-align: right;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter \"+\"</span>"
		"</div>",
};

static struct get_inkey_test get_inkey_data_932 = {
	.pdu = get_inkey_932,
	.pdu_len = sizeof(get_inkey_932),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_941 = {
	.pdu = get_inkey_941,
	.pdu_len = sizeof(get_inkey_941),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x04, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"big;color: #347235;background-color: #FFFF00;\">Enter \"+\""
		"</span></div>",
};

static struct get_inkey_test get_inkey_data_942 = {
	.pdu = get_inkey_942,
	.pdu_len = sizeof(get_inkey_942),
	.qualifier = 0x00,
	.text = "Enter \"#\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter \"#\"</span></div>",
};

static struct get_inkey_test get_inkey_data_943 = {
	.pdu = get_inkey_943,
	.pdu_len = sizeof(get_inkey_943),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_951 = {
	.pdu = get_inkey_951,
	.pdu_len = sizeof(get_inkey_951),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x08, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"small;color: #347235;background-color: #FFFF00;\">"
		"Enter \"+\"</span></div>",
};

static struct get_inkey_test get_inkey_data_952 = {
	.pdu = get_inkey_952,
	.pdu_len = sizeof(get_inkey_952),
	.qualifier = 0x00,
	.text = "Enter \"#\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter \"#\"</span></div>",
};

static struct get_inkey_test get_inkey_data_953 = {
	.pdu = get_inkey_953,
	.pdu_len = sizeof(get_inkey_953),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_961 = {
	.pdu = get_inkey_961,
	.pdu_len = sizeof(get_inkey_961),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x10, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-weight: "
		"bold;color: #347235;background-color: #FFFF00;\">Enter \"+\""
		"</span></div>",
};

static struct get_inkey_test get_inkey_data_962 = {
	.pdu = get_inkey_962,
	.pdu_len = sizeof(get_inkey_962),
	.qualifier = 0x00,
	.text = "Enter \"#\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter \"#\"</span></div>",
};

static struct get_inkey_test get_inkey_data_963 = {
	.pdu = get_inkey_963,
	.pdu_len = sizeof(get_inkey_963),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_971 = {
	.pdu = get_inkey_971,
	.pdu_len = sizeof(get_inkey_971),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x20, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-style: "
		"italic;color: #347235;background-color: #FFFF00;\">"
		"Enter \"+\"</span></div>",
};

static struct get_inkey_test get_inkey_data_972 = {
	.pdu = get_inkey_972,
	.pdu_len = sizeof(get_inkey_972),
	.qualifier = 0x00,
	.text = "Enter \"#\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter \"#\"</span></div>",
};

static struct get_inkey_test get_inkey_data_973 = {
	.pdu = get_inkey_973,
	.pdu_len = sizeof(get_inkey_973),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_981 = {
	.pdu = get_inkey_981,
	.pdu_len = sizeof(get_inkey_981),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x40, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\""
		"text-decoration: underline;color: #347235;"
		"background-color: #FFFF00;\">Enter \"+\"</span></div>",
};

static struct get_inkey_test get_inkey_data_982 = {
	.pdu = get_inkey_982,
	.pdu_len = sizeof(get_inkey_982),
	.qualifier = 0x00,
	.text = "Enter \"#\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter \"#\"</span></div>",
};

static struct get_inkey_test get_inkey_data_983 = {
	.pdu = get_inkey_983,
	.pdu_len = sizeof(get_inkey_983),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_991 = {
	.pdu = get_inkey_991,
	.pdu_len = sizeof(get_inkey_991),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x80, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\""
		"text-decoration: line-through;color: #347235;"
		"background-color: #FFFF00;\">Enter \"+\"</span></div>",
};

static struct get_inkey_test get_inkey_data_992a = {
	.pdu = get_inkey_992a,
	.pdu_len = sizeof(get_inkey_992a),
	.qualifier = 0x00,
	.text = "Enter \"#\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter \"#\"</span></div>",
};

static struct get_inkey_test get_inkey_data_992b = {
	.pdu = get_inkey_992b,
	.pdu_len = sizeof(get_inkey_992b),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_993 = {
	.pdu = get_inkey_993,
	.pdu_len = sizeof(get_inkey_993),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_9101 = {
	.pdu = get_inkey_9101,
	.pdu_len = sizeof(get_inkey_9101),
	.qualifier = 0x00,
	.text = "Enter \"+\"",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x09, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter \"+\"</span></div>",
};

static struct get_inkey_test get_inkey_data_9102 = {
	.pdu = get_inkey_9102,
	.pdu_len = sizeof(get_inkey_9102),
	.qualifier = 0x00,
	.text = "Enter \"#\""
};

static struct get_inkey_test get_inkey_data_1011 = {
	.pdu = get_inkey_1011,
	.pdu_len = sizeof(get_inkey_1011),
	.qualifier = 0x00,
	.text = "你好"
};

static struct get_inkey_test get_inkey_data_1021 = {
	.pdu = get_inkey_1021,
	.pdu_len = sizeof(get_inkey_1021),
	.qualifier = 0x00,
	.text = "你好你好你好你好你好你好你好你好你好你好"
		"你好你好你好你好你好你好你好你好你好你好"
		"你好你好你好你好你好你好你好你好你好你好"
		"你好你好你好你好你好"
};

static struct get_inkey_test get_inkey_data_1111 = {
	.pdu = get_inkey_1111,
	.pdu_len = sizeof(get_inkey_1111),
	.qualifier = 0x03,
	.text = "Enter"
};

static struct get_inkey_test get_inkey_data_1211 = {
	.pdu = get_inkey_1211,
	.pdu_len = sizeof(get_inkey_1211),
	.qualifier = 0x00,
	.text = "ル"
};

static struct get_inkey_test get_inkey_data_1221 = {
	.pdu = get_inkey_1221,
	.pdu_len = sizeof(get_inkey_1221),
	.qualifier = 0x00,
	.text = "ルルルルルルルルルルルルルルルルルルルル"
		"ルルルルルルルルルルルルルルルルルルルル"
		"ルルルルルルルルルルルルルルルルルルルル"
		"ルルルルルルルルルル"
};

static struct get_inkey_test get_inkey_data_1311 = {
	.pdu = get_inkey_1311,
	.pdu_len = sizeof(get_inkey_1311),
	.qualifier = 0x03,
	.text = "Enter"
};

/* Defined in TS 102.384 Section 27.22.4.2 */
static void test_get_inkey(gconstpointer data)
{
	const struct get_inkey_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_GET_INKEY);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	g_assert(command->get_inkey.text);
	check_text(command->get_inkey.text, test->text);
	check_icon_id(&command->get_inkey.icon_id, &test->icon_id);
	check_duration(&command->get_inkey.duration, &test->duration);
	check_text_attr(&command->get_inkey.text_attr,
						&test->text_attr);
	check_text_attr_html(&command->get_inkey.text_attr,
				command->get_inkey.text, test->html);
	check_frame_id(&command->get_inkey.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct get_input_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *text;
	struct stk_response_length resp_len;
	char *default_text;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
	char *html;
};

static unsigned char get_input_711[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x80, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x31,
						0x32, 0x33, 0x34, 0x35, 0x91,
						0x02, 0x05, 0x05 };

static unsigned char get_input_812[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x32,
						0x32, 0x32, 0x32, 0x32, 0x91,
						0x02, 0x05, 0x05 };

static unsigned char get_input_822[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x32,
						0x32, 0x32, 0x32, 0x32, 0x91,
						0x02, 0x05, 0x05 };

static unsigned char get_input_832[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x32,
						0x32, 0x32, 0x32, 0x32, 0x91,
						0x02, 0x05, 0x05 };

static unsigned char get_input_842[] = { 0xD0, 0x21, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x32,
						0x32, 0x32, 0x32, 0x32, 0x91,
						0x02, 0x05, 0x05, 0xD0, 0x04,
						0x00, 0x0B, 0x00, 0xB4 };

static unsigned char get_input_843[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x33,
						0x33, 0x33, 0x33, 0x33, 0x91,
						0x02, 0x05, 0x05 };

static unsigned char get_input_852[] = { 0xD0, 0x21, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x32,
						0x32, 0x32, 0x32, 0x32, 0x91,
						0x02, 0x05, 0x05, 0xD0, 0x04,
						0x00, 0x0B, 0x00, 0xB4 };

static unsigned char get_input_853[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x33,
						0x33, 0x33, 0x33, 0x33, 0x91,
						0x02, 0x05, 0x05 };

static unsigned char get_input_862[] = { 0xD0, 0x21, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x32,
						0x32, 0x32, 0x32, 0x32, 0x91,
						0x02, 0x05, 0x05, 0xD0, 0x04,
						0x00, 0x0B, 0x00, 0xB4 };

static unsigned char get_input_863[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x33,
						0x33, 0x33, 0x33, 0x33, 0x91,
						0x02, 0x05, 0x05 };

static unsigned char get_input_872[] = { 0xD0, 0x21, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x32,
						0x32, 0x32, 0x32, 0x32, 0x91,
						0x02, 0x05, 0x05, 0xD0, 0x04,
						0x00, 0x0B, 0x00, 0xB4 };

static unsigned char get_input_873[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x33,
						0x33, 0x33, 0x33, 0x33, 0x91,
						0x02, 0x05, 0x05 };

static unsigned char get_input_882[] = { 0xD0, 0x21, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x32,
						0x32, 0x32, 0x32, 0x32, 0x91,
						0x02, 0x05, 0x05, 0xD0, 0x04,
						0x00, 0x0B, 0x00, 0xB4 };

static unsigned char get_input_883[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x33,
						0x33, 0x33, 0x33, 0x33, 0x91,
						0x02, 0x05, 0x05 };

static unsigned char get_input_892[] = { 0xD0, 0x21, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x32,
						0x32, 0x32, 0x32, 0x32, 0x91,
						0x02, 0x05, 0x05, 0xD0, 0x04,
						0x00, 0x0B, 0x00, 0xB4 };

static unsigned char get_input_893[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x33,
						0x33, 0x33, 0x33, 0x33, 0x91,
						0x02, 0x05, 0x05 };

static unsigned char get_input_8102[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x23,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x8D, 0x0C, 0x04, 0x45, 0x6E,
						0x74, 0x65, 0x72, 0x20, 0x32,
						0x32, 0x32, 0x32, 0x32, 0x91,
						0x02, 0x05, 0x05 };

static struct get_input_test get_input_data_111 = {
	.pdu = get_input_111,
	.pdu_len = sizeof(get_input_111),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_121 = {
	.pdu = get_input_121,
	.pdu_len = sizeof(get_input_121),
	.qualifier = 0x08,
	.text = "Enter 67*#+",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_131 = {
	.pdu = get_input_131,
	.pdu_len = sizeof(get_input_131),
	.qualifier = 0x01,
	.text = "Enter AbCdE",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_141 = {
	.pdu = get_input_141,
	.pdu_len = sizeof(get_input_141),
	.qualifier = 0x04,
	.text = "Password 1<SEND>2345678",
	.resp_len = {
		.min = 4,
		.max = 8
	}
};

static struct get_input_test get_input_data_151 = {
	.pdu = get_input_151,
	.pdu_len = sizeof(get_input_151),
	.qualifier = 0x00,
	.text = "Enter 1..9,0..9,0(1)",
	.resp_len = {
		.min = 1,
		.max = 20
	}
};

static struct get_input_test get_input_data_161 = {
	.pdu = get_input_161,
	.pdu_len = sizeof(get_input_161),
	.qualifier = 0x00,
	.text = "<GO-BACKWARDS>",
	.resp_len = {
		.min = 0,
		.max = 8
	}
};

static struct get_input_test get_input_data_171 = {
	.pdu = get_input_171,
	.pdu_len = sizeof(get_input_171),
	.qualifier = 0x00,
	.text = "<ABORT>",
	.resp_len = {
		.min = 0,
		.max = 8
	}
};

static struct get_input_test get_input_data_181 = {
	.pdu = get_input_181,
	.pdu_len = sizeof(get_input_181),
	.qualifier = 0x00,
	.text = "***1111111111###***2222222222###***3333333333###"
		"***4444444444###***5555555555###***6666666666###"
		"***7777777777###***8888888888###***9999999999###"
		"***0000000000###",
	.resp_len = {
		.min = 160,
		.max = 160
	}
};

static struct get_input_test get_input_data_191 = {
	.pdu = get_input_191,
	.pdu_len = sizeof(get_input_191),
	.qualifier = 0x00,
	.text = "<SEND>",
	.resp_len = {
		.min = 0,
		.max = 1
	}
};

static struct get_input_test get_input_data_1101 = {
	.pdu = get_input_1101,
	.pdu_len = sizeof(get_input_1101),
	.qualifier = 0x00,
	.text = "",
	.resp_len = {
		.min = 1,
		.max = 5
	}
};

static struct get_input_test get_input_data_211 = {
	.pdu = get_input_211,
	.pdu_len = sizeof(get_input_211),
	.qualifier = 0x00,
	.text = "<TIME-OUT>",
	.resp_len = {
		.min = 0,
		.max = 10
	}
};

static struct get_input_test get_input_data_311 = {
	.pdu = get_input_311,
	.pdu_len = sizeof(get_input_311),
	.qualifier = 0x01,
	.text = "ЗДРАВСТВУЙТЕ",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_321 = {
	.pdu = get_input_321,
	.pdu_len = sizeof(get_input_321),
	.qualifier = 0x01,
	.text = "ЗДРАВСТВУЙТЕЗДРАВСТВУЙТЕ"
		"ЗДРАВСТВУЙТЕЗДРАВСТВУЙТЕ"
		"ЗДРАВСТВУЙТЕЗДРАВСТВУЙ",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_411 = {
	.pdu = get_input_411,
	.pdu_len = sizeof(get_input_411),
	.qualifier = 0x03,
	.text = "Enter Hello",
	.resp_len = {
		.min = 12,
		.max = 12
	}
};

static struct get_input_test get_input_data_421 = {
	.pdu = get_input_421,
	.pdu_len = sizeof(get_input_421),
	.qualifier = 0x03,
	.text = "Enter Hello",
	.resp_len = {
		.min = 5,
		.max = 0xFF
	}
};

static struct get_input_test get_input_data_511 = {
	.pdu = get_input_511,
	.pdu_len = sizeof(get_input_511),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.default_text = "12345"
};

static struct get_input_test get_input_data_521 = {
	.pdu = get_input_521,
	.pdu_len = sizeof(get_input_521),
	.qualifier = 0x00,
	.text = "Enter:",
	.resp_len = {
		.min = 160,
		.max = 160
	},
	.default_text = "***1111111111###***2222222222###***3333333333###"
			"***4444444444###***5555555555###***6666666666###"
			"***7777777777###***8888888888###***9999999999###"
			"***0000000000###"
};

static struct get_input_test get_input_data_611 = {
	.pdu = get_input_611,
	.pdu_len = sizeof(get_input_611),
	.qualifier = 0x00,
	.text = "<NO-ICON>",
	.resp_len = {
		.min = 0,
		.max = 10
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct get_input_test get_input_data_621 = {
	.pdu = get_input_621,
	.pdu_len = sizeof(get_input_621),
	.qualifier = 0x00,
	.text = "<BASIC-ICON>",
	.resp_len = {
		.min = 0,
		.max = 10
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct get_input_test get_input_data_631 = {
	.pdu = get_input_631,
	.pdu_len = sizeof(get_input_631),
	.qualifier = 0x00,
	.text = "<NO-ICON>",
	.resp_len = {
		.min = 0,
		.max = 10
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct get_input_test get_input_data_641 = {
	.pdu = get_input_641,
	.pdu_len = sizeof(get_input_641),
	.qualifier = 0x00,
	.text = "<COLOUR-ICON>",
	.resp_len = {
		.min = 0,
		.max = 10
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct get_input_test get_input_data_711 = {
	.pdu = get_input_711,
	.pdu_len = sizeof(get_input_711),
	.qualifier = 0x80,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_811 = {
	.pdu = get_input_811,
	.pdu_len = sizeof(get_input_811),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter 12345</span></div>"
};

static struct get_input_test get_input_data_812 = {
	.pdu = get_input_812,
	.pdu_len = sizeof(get_input_812),
	.qualifier = 0x00,
	.text = "Enter 22222",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_821 = {
	.pdu = get_input_821,
	.pdu_len = sizeof(get_input_821),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x01, 0xB4 }
	},
	.html = "<div style=\"text-align: center;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter 12345</span>"
		"</div>",
};

static struct get_input_test get_input_data_822 = {
	.pdu = get_input_822,
	.pdu_len = sizeof(get_input_822),
	.qualifier = 0x00,
	.text = "Enter 22222",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_831 = {
	.pdu = get_input_831,
	.pdu_len = sizeof(get_input_831),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x02, 0xB4 }
	},
	.html = "<div style=\"text-align: right;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter 12345</span>"
		"</div>",
};

static struct get_input_test get_input_data_832 = {
	.pdu = get_input_832,
	.pdu_len = sizeof(get_input_832),
	.qualifier = 0x00,
	.text = "Enter 22222",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_841 = {
	.pdu = get_input_841,
	.pdu_len = sizeof(get_input_841),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x04, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"big;color: #347235;background-color: #FFFF00;\">Enter 12345"
		"</span></div>",
};

static struct get_input_test get_input_data_842 = {
	.pdu = get_input_842,
	.pdu_len = sizeof(get_input_842),
	.qualifier = 0x00,
	.text = "Enter 22222",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter 22222</span></div>"
};

static struct get_input_test get_input_data_843 = {
	.pdu = get_input_843,
	.pdu_len = sizeof(get_input_843),
	.qualifier = 0x00,
	.text = "Enter 33333",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_851 = {
	.pdu = get_input_851,
	.pdu_len = sizeof(get_input_851),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x08, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"small;color: #347235;background-color: #FFFF00;\">Enter "
		"12345</span></div>",
};

static struct get_input_test get_input_data_852 = {
	.pdu = get_input_852,
	.pdu_len = sizeof(get_input_852),
	.qualifier = 0x00,
	.text = "Enter 22222",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter 22222</span></div>",
};

static struct get_input_test get_input_data_853 = {
	.pdu = get_input_853,
	.pdu_len = sizeof(get_input_853),
	.qualifier = 0x00,
	.text = "Enter 33333",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_861 = {
	.pdu = get_input_861,
	.pdu_len = sizeof(get_input_861),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x10, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-weight: "
		"bold;color: #347235;background-color: #FFFF00;\">Enter "
		"12345</span></div>"
};

static struct get_input_test get_input_data_862 = {
	.pdu = get_input_862,
	.pdu_len = sizeof(get_input_862),
	.qualifier = 0x00,
	.text = "Enter 22222",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter 22222</span></div>",
};

static struct get_input_test get_input_data_863 = {
	.pdu = get_input_863,
	.pdu_len = sizeof(get_input_863),
	.qualifier = 0x00,
	.text = "Enter 33333",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_871 = {
	.pdu = get_input_871,
	.pdu_len = sizeof(get_input_871),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x20, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-style: "
		"italic;color: #347235;background-color: #FFFF00;\">Enter "
		"12345</span></div>",
};

static struct get_input_test get_input_data_872 = {
	.pdu = get_input_872,
	.pdu_len = sizeof(get_input_872),
	.qualifier = 0x00,
	.text = "Enter 22222",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter 22222</span></div>",
};

static struct get_input_test get_input_data_873 = {
	.pdu = get_input_873,
	.pdu_len = sizeof(get_input_873),
	.qualifier = 0x00,
	.text = "Enter 33333",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_881 = {
	.pdu = get_input_881,
	.pdu_len = sizeof(get_input_881),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x40, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span "
		"style=\"text-decoration: underline;color: #347235;"
		"background-color: #FFFF00;\">Enter 12345</span></div>",
};

static struct get_input_test get_input_data_882 = {
	.pdu = get_input_882,
	.pdu_len = sizeof(get_input_882),
	.qualifier = 0x00,
	.text = "Enter 22222",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter 22222</span></div>",
};

static struct get_input_test get_input_data_883 = {
	.pdu = get_input_883,
	.pdu_len = sizeof(get_input_883),
	.qualifier = 0x00,
	.text = "Enter 33333",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_891 = {
	.pdu = get_input_891,
	.pdu_len = sizeof(get_input_891),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x80, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span "
		"style=\"text-decoration: line-through;color: #347235;"
		"background-color: #FFFF00;\">Enter 12345</span></div>",
};

static struct get_input_test get_input_data_892 = {
	.pdu = get_input_892,
	.pdu_len = sizeof(get_input_892),
	.qualifier = 0x00,
	.text = "Enter 22222",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter 22222</span></div>",
};

static struct get_input_test get_input_data_893 = {
	.pdu = get_input_893,
	.pdu_len = sizeof(get_input_893),
	.qualifier = 0x00,
	.text = "Enter 33333",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_8101 = {
	.pdu = get_input_8101,
	.pdu_len = sizeof(get_input_8101),
	.qualifier = 0x00,
	.text = "Enter 12345",
	.resp_len = {
		.min = 5,
		.max = 5
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Enter 12345</span></div>",
};

static struct get_input_test get_input_data_8102 = {
	.pdu = get_input_8102,
	.pdu_len = sizeof(get_input_8102),
	.qualifier = 0x00,
	.text = "Enter 22222",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_911 = {
	.pdu = get_input_911,
	.pdu_len = sizeof(get_input_911),
	.qualifier = 0x01,
	.text = "你好",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_921 = {
	.pdu = get_input_921,
	.pdu_len = sizeof(get_input_921),
	.qualifier = 0x01,
	.text = "你好你好你好你好你好你好你好你好你好你好"
		"你好你好你好你好你好你好你好你好你好你好"
		"你好你好你好你好你好你好你好你好你好你好"
		"你好你好你好你好你好",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_1011 = {
	.pdu = get_input_1011,
	.pdu_len = sizeof(get_input_1011),
	.qualifier = 0x03,
	.text = "Enter Hello",
	.resp_len = {
		.min = 2,
		.max = 2
	}
};

static struct get_input_test get_input_data_1021 = {
	.pdu = get_input_1021,
	.pdu_len = sizeof(get_input_1021),
	.qualifier = 0x03,
	.text = "Enter Hello",
	.resp_len = {
		.min = 5,
		.max = 0xFF
	}
};

static struct get_input_test get_input_data_1111 = {
	.pdu = get_input_1111,
	.pdu_len = sizeof(get_input_1111),
	.qualifier = 0x01,
	.text = "ル",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_1121 = {
	.pdu = get_input_1121,
	.pdu_len = sizeof(get_input_1121),
	.qualifier = 0x01,
	.text = "ルルルルルルルルルルルルルルルルルルルル"
		"ルルルルルルルルルルルルルルルルルルルル"
		"ルルルルルルルルルルルルルルルルルルルル"
		"ルルルルルルルルルル",
	.resp_len = {
		.min = 5,
		.max = 5
	}
};

static struct get_input_test get_input_data_1211 = {
	.pdu = get_input_1211,
	.pdu_len = sizeof(get_input_1211),
	.qualifier = 0x03,
	.text = "Enter Hello",
	.resp_len = {
		.min = 2,
		.max = 2
	}
};

static struct get_input_test get_input_data_1221 = {
	.pdu = get_input_1221,
	.pdu_len = sizeof(get_input_1221),
	.qualifier = 0x03,
	.text = "Enter Hello",
	.resp_len = {
		.min = 5,
		.max = 0xFF
	}
};

/* Defined in TS 102.384 Section 27.22.4.3 */
static void test_get_input(gconstpointer data)
{
	const struct get_input_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_GET_INPUT);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	if (test->text)
		g_assert(command->get_input.text);
	check_text(command->get_input.text, test->text);
	check_response_length(&command->get_input.resp_len, &test->resp_len);
	check_default_text(command->get_input.default_text, test->default_text);
	check_icon_id(&command->get_input.icon_id, &test->icon_id);
	check_text_attr(&command->get_input.text_attr,
						&test->text_attr);
	check_text_attr_html(&command->get_input.text_attr,
				command->get_input.text, test->html);
	check_frame_id(&command->get_input.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct more_time_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
};

static struct more_time_test more_time_data_111 = {
	.pdu = more_time_111,
	.pdu_len = sizeof(more_time_111),
	.qualifier = 0x00,
};

/* Defined in TS 102.384 Section 27.22.4.4 */
static void test_more_time(gconstpointer data)
{
	const struct get_input_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_MORE_TIME);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	stk_command_free(command);
}

struct play_tone_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *alpha_id;
	unsigned char tone;
	struct stk_duration duration;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
	char *html;
};

static struct play_tone_test play_tone_data_111 = {
	.pdu = play_tone_111,
	.pdu_len = sizeof(play_tone_111),
	.qualifier = 0x00,
	.alpha_id = "Dial Tone",
	.tone = STK_TONE_TYPE_DIAL_TONE,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

static struct play_tone_test play_tone_data_112 = {
	.pdu = play_tone_112,
	.pdu_len = sizeof(play_tone_112),
	.qualifier = 0x00,
	.alpha_id = "Sub. Busy",
	.tone = STK_TONE_TYPE_BUSY_TONE,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

static struct play_tone_test play_tone_data_113 = {
	.pdu = play_tone_113,
	.pdu_len = sizeof(play_tone_113),
	.qualifier = 0x00,
	.alpha_id = "Congestion",
	.tone = STK_TONE_TYPE_CONGESTION,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

static struct play_tone_test play_tone_data_114 = {
	.pdu = play_tone_114,
	.pdu_len = sizeof(play_tone_114),
	.qualifier = 0x00,
	.alpha_id = "RP Ack",
	.tone = STK_TONE_TYPE_RP_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

static struct play_tone_test play_tone_data_115 = {
	.pdu = play_tone_115,
	.pdu_len = sizeof(play_tone_115),
	.qualifier = 0x00,
	.alpha_id = "No RP",
	.tone = STK_TONE_TYPE_CALL_DROPPED,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

static struct play_tone_test play_tone_data_116 = {
	.pdu = play_tone_116,
	.pdu_len = sizeof(play_tone_116),
	.qualifier = 0x00,
	.alpha_id = "Spec Info",
	.tone = STK_TONE_TYPE_ERROR,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

static struct play_tone_test play_tone_data_117 = {
	.pdu = play_tone_117,
	.pdu_len = sizeof(play_tone_117),
	.qualifier = 0x00,
	.alpha_id = "Call Wait",
	.tone = STK_TONE_TYPE_CALL_WAITING,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

static struct play_tone_test play_tone_data_118 = {
	.pdu = play_tone_118,
	.pdu_len = sizeof(play_tone_118),
	.qualifier = 0x00,
	.alpha_id = "Ring Tone",
	.tone = STK_TONE_TYPE_RINGING,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

static struct play_tone_test play_tone_data_119 = {
	.pdu = play_tone_119,
	.pdu_len = sizeof(play_tone_119),
	.qualifier = 0x00,
	.alpha_id = "This command instructs the ME to play an audio tone. "
			"Upon receiving this command, the ME shall check "
			"if it is currently in, or in the process of setting "
			"up (SET-UP message sent to the network, see "
			"GSM\"04.08\"(8)), a speech call. - If the ME I"
};

static struct play_tone_test play_tone_data_1110 = {
	.pdu = play_tone_1110,
	.pdu_len = sizeof(play_tone_1110),
	.qualifier = 0x00,
	.alpha_id = "Beep",
	.tone = STK_TONE_TYPE_GENERAL_BEEP,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_1111 = {
	.pdu = play_tone_1111,
	.pdu_len = sizeof(play_tone_1111),
	.qualifier = 0x00,
	.alpha_id = "Positive",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_1112 = {
	.pdu = play_tone_1112,
	.pdu_len = sizeof(play_tone_1112),
	.qualifier = 0x00,
	.alpha_id = "Negative",
	.tone = STK_TONE_TYPE_NEGATIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_1113 = {
	.pdu = play_tone_1113,
	.pdu_len = sizeof(play_tone_1113),
	.qualifier = 0x00,
	.alpha_id = "Quick",
	.tone = STK_TONE_TYPE_GENERAL_BEEP,
	.duration = {
		.unit = STK_DURATION_TYPE_SECOND_TENTHS,
		.interval = 2
	}
};

static struct play_tone_test play_tone_data_1114 = {
	.pdu = play_tone_1114,
	.pdu_len = sizeof(play_tone_1114),
	.qualifier = 0x00,
	.alpha_id = "<ABORT>",
	.tone = STK_TONE_TYPE_ERROR,
	.duration = {
		.unit = STK_DURATION_TYPE_MINUTES,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_1115 = {
	.pdu = play_tone_1115,
	.pdu_len = sizeof(play_tone_1115),
	.qualifier = 0x00
};

static struct play_tone_test play_tone_data_211 = {
	.pdu = play_tone_211,
	.pdu_len = sizeof(play_tone_211),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_212 = {
	.pdu = play_tone_212,
	.pdu_len = sizeof(play_tone_212),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_213 = {
	.pdu = play_tone_213,
	.pdu_len = sizeof(play_tone_213),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_311 = {
	.pdu = play_tone_311,
	.pdu_len = sizeof(play_tone_311),
	.qualifier = 0x00,
	.alpha_id = "<BASIC-ICON>",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.icon_id = {
	    .qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
	    .id = 0x01
	}
};

static struct play_tone_test play_tone_data_321 = {
	.pdu = play_tone_321,
	.pdu_len = sizeof(play_tone_321),
	.qualifier = 0x00,
	.alpha_id = "<BASIC-ICON>",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.icon_id = {
	    .qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
	    .id = 0x01
	}
};

static struct play_tone_test play_tone_data_331 = {
	.pdu = play_tone_331,
	.pdu_len = sizeof(play_tone_331),
	.qualifier = 0x00,
	.alpha_id = "<COLOUR-ICON>",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.icon_id = {
	    .qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
	    .id = 0x02
	}
};

static struct play_tone_test play_tone_data_341 = {
	.pdu = play_tone_341,
	.pdu_len = sizeof(play_tone_341),
	.qualifier = 0x00,
	.alpha_id = "<COLOUR-ICON>",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.icon_id = {
	    .qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
	    .id = 0x02
	}
};

static struct play_tone_test play_tone_data_411 = {
	.pdu = play_tone_411,
	.pdu_len = sizeof(play_tone_411),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 1</span>"
		"</div>",
};

static struct play_tone_test play_tone_data_412 = {
	.pdu = play_tone_412,
	.pdu_len = sizeof(play_tone_412),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_421 = {
	.pdu = play_tone_421,
	.pdu_len = sizeof(play_tone_421),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x01, 0xB4 }
	},
	.html = "<div style=\"text-align: center;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 1</span>"
		"</div>",
};

static struct play_tone_test play_tone_data_422 = {
	.pdu = play_tone_422,
	.pdu_len = sizeof(play_tone_422),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_431 = {
	.pdu = play_tone_431,
	.pdu_len = sizeof(play_tone_431),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x02, 0xB4 }
	},
	.html = "<div style=\"text-align: right;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 1</span>"
		"</div>",
};

static struct play_tone_test play_tone_data_432 = {
	.pdu = play_tone_432,
	.pdu_len = sizeof(play_tone_432),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_441 = {
	.pdu = play_tone_441,
	.pdu_len = sizeof(play_tone_441),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x04, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"big;color: #347235;background-color: #FFFF00;\">"
		"Text Attribute 1</span></div>",
};

static struct play_tone_test play_tone_data_442 = {
	.pdu = play_tone_442,
	.pdu_len = sizeof(play_tone_442),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 2</span>"
		"</div>",
};

static struct play_tone_test play_tone_data_443 = {
	.pdu = play_tone_443,
	.pdu_len = sizeof(play_tone_443),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_451 = {
	.pdu = play_tone_451,
	.pdu_len = sizeof(play_tone_451),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x08, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"small;color: #347235;background-color: #FFFF00;\">"
		"Text Attribute 1</span></div>",
};

static struct play_tone_test play_tone_data_452 = {
	.pdu = play_tone_452,
	.pdu_len = sizeof(play_tone_452),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 2</span>"
		"</div>",
};

static struct play_tone_test play_tone_data_453 = {
	.pdu = play_tone_453,
	.pdu_len = sizeof(play_tone_453),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_461 = {
	.pdu = play_tone_461,
	.pdu_len = sizeof(play_tone_461),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x10, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-weight: "
		"bold;color: #347235;background-color: #FFFF00;\">"
		"Text Attribute</span></div> 1"
};

static struct play_tone_test play_tone_data_462 = {
	.pdu = play_tone_462,
	.pdu_len = sizeof(play_tone_462),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 2</span>"
		"</div>",
};

static struct play_tone_test play_tone_data_463 = {
	.pdu = play_tone_463,
	.pdu_len = sizeof(play_tone_463),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_471 = {
	.pdu = play_tone_471,
	.pdu_len = sizeof(play_tone_471),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x20, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-style: "
		"italic;color: #347235;background-color: #FFFF00;\">"
		"Text Attribute</span></div> 1",
};

static struct play_tone_test play_tone_data_472 = {
	.pdu = play_tone_472,
	.pdu_len = sizeof(play_tone_472),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 2</span>"
		"</div>",
};

static struct play_tone_test play_tone_data_473 = {
	.pdu = play_tone_473,
	.pdu_len = sizeof(play_tone_473),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_481 = {
	.pdu = play_tone_481,
	.pdu_len = sizeof(play_tone_481),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x40, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span "
		"style=\"text-decoration: underline;color: #347235;"
		"background-color: #FFFF00;\">Text Attribute 1</span></div>",
};

static struct play_tone_test play_tone_data_482 = {
	.pdu = play_tone_482,
	.pdu_len = sizeof(play_tone_482),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 2</span>"
		"</div>",
};

static struct play_tone_test play_tone_data_483 = {
	.pdu = play_tone_483,
	.pdu_len = sizeof(play_tone_483),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_491 = {
	.pdu = play_tone_491,
	.pdu_len = sizeof(play_tone_491),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x80, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span "
		"style=\"text-decoration: line-through;color: #347235;"
		"background-color: #FFFF00;\">Text Attribute 1</span></div>",
};

static struct play_tone_test play_tone_data_492 = {
	.pdu = play_tone_492,
	.pdu_len = sizeof(play_tone_492),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 2</span>"
		"</div>",
};

static struct play_tone_test play_tone_data_493 = {
	.pdu = play_tone_493,
	.pdu_len = sizeof(play_tone_493),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_4101 = {
	.pdu = play_tone_4101,
	.pdu_len = sizeof(play_tone_4101),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Text Attribute 1</span>"
		"</div>",
};

static struct play_tone_test play_tone_data_4102 = {
	.pdu = play_tone_4102,
	.pdu_len = sizeof(play_tone_4102),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_511 = {
	.pdu = play_tone_511,
	.pdu_len = sizeof(play_tone_511),
	.qualifier = 0x00,
	.alpha_id = "中一",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_512 = {
	.pdu = play_tone_512,
	.pdu_len = sizeof(play_tone_512),
	.qualifier = 0x00,
	.alpha_id = "中一",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_513 = {
	.pdu = play_tone_513,
	.pdu_len = sizeof(play_tone_513),
	.qualifier = 0x00,
	.alpha_id = "中一",
	.tone = STK_TONE_TYPE_POSITIVE_ACK,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 1
	}
};

static struct play_tone_test play_tone_data_611 = {
	.pdu = play_tone_611,
	.pdu_len = sizeof(play_tone_611),
	.qualifier = 0x00,
	.alpha_id = "80ル0",
	.tone = STK_TONE_TYPE_DIAL_TONE,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

static struct play_tone_test play_tone_data_612 = {
	.pdu = play_tone_612,
	.pdu_len = sizeof(play_tone_612),
	.qualifier = 0x00,
	.alpha_id = "81ル1",
	.tone = STK_TONE_TYPE_DIAL_TONE,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

static struct play_tone_test play_tone_data_613 = {
	.pdu = play_tone_613,
	.pdu_len = sizeof(play_tone_613),
	.qualifier = 0x00,
	.alpha_id = "82ル2",
	.tone = STK_TONE_TYPE_DIAL_TONE,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 5
	}
};

/* Defined in TS 102.384 Section 27.22.4.5 */
static void test_play_tone(gconstpointer data)
{
	const struct play_tone_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_PLAY_TONE);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_EARPIECE);

	check_alpha_id(command->play_tone.alpha_id, test->alpha_id);
	check_tone(command->play_tone.tone, test->tone);
	check_duration(&command->play_tone.duration, &test->duration);
	check_icon_id(&command->play_tone.icon_id, &test->icon_id);
	check_text_attr(&command->play_tone.text_attr, &test->text_attr);
	check_text_attr_html(&command->play_tone.text_attr,
				command->play_tone.alpha_id, test->html);
	check_frame_id(&command->play_tone.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct poll_interval_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	struct stk_duration duration;
};

static struct poll_interval_test poll_interval_data_111 = {
	.pdu = poll_interval_111,
	.pdu_len = sizeof(poll_interval_111),
	.qualifier = 0x00,
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 20
	}
};

/* Defined in TS 102.384 Section 27.22.4.6 */
static void test_poll_interval(gconstpointer data)
{
	const struct poll_interval_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_POLL_INTERVAL);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_duration(&command->poll_interval.duration, &test->duration);

	stk_command_free(command);
}

struct setup_menu_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *alpha_id;
	struct stk_item items[MAX_ITEM];
	struct stk_items_next_action_indicator next_act;
	struct stk_icon_id icon_id;
	struct stk_item_icon_id_list item_icon_id_list;
	struct stk_text_attribute text_attr;
	struct stk_item_text_attribute_list item_text_attr_list;
	char *html;
};

static unsigned char setup_menu_111[] = { 0xD0, 0x3B, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0C, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x8F,
						0x07, 0x01, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x31, 0x8F, 0x07,
						0x02, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x32, 0x8F, 0x07, 0x03,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x33, 0x8F, 0x07, 0x04, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x34 };

static unsigned char setup_menu_112[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0C, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x8F,
						0x04, 0x11, 0x4F, 0x6E, 0x65,
						0x8F, 0x04, 0x12, 0x54, 0x77,
						0x6F };

static unsigned char setup_menu_113[] = { 0xD0, 0x0D, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x00, 0x8F, 0x00 };

static unsigned char setup_menu_121[] = { 0xD0, 0x81, 0xFC, 0x81, 0x03, 0x01,
						0x25, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x0A, 0x4C, 0x61,
						0x72, 0x67, 0x65, 0x4D, 0x65,
						0x6E, 0x75, 0x31, 0x8F, 0x05,
						0x50, 0x5A, 0x65, 0x72, 0x6F,
						0x8F, 0x04, 0x4F, 0x4F, 0x6E,
						0x65, 0x8F, 0x04, 0x4E, 0x54,
						0x77, 0x6F, 0x8F, 0x06, 0x4D,
						0x54, 0x68, 0x72, 0x65, 0x65,
						0x8F, 0x05, 0x4C, 0x46, 0x6F,
						0x75, 0x72, 0x8F, 0x05, 0x4B,
						0x46, 0x69, 0x76, 0x65, 0x8F,
						0x04, 0x4A, 0x53, 0x69, 0x78,
						0x8F, 0x06, 0x49, 0x53, 0x65,
						0x76, 0x65, 0x6E, 0x8F, 0x06,
						0x48, 0x45, 0x69, 0x67, 0x68,
						0x74, 0x8F, 0x05, 0x47, 0x4E,
						0x69, 0x6E, 0x65, 0x8F, 0x06,
						0x46, 0x41, 0x6C, 0x70, 0x68,
						0x61, 0x8F, 0x06, 0x45, 0x42,
						0x72, 0x61, 0x76, 0x6F, 0x8F,
						0x08, 0x44, 0x43, 0x68, 0x61,
						0x72, 0x6C, 0x69, 0x65, 0x8F,
						0x06, 0x43, 0x44, 0x65, 0x6C,
						0x74, 0x61, 0x8F, 0x05, 0x42,
						0x45, 0x63, 0x68, 0x6F, 0x8F,
						0x09, 0x41, 0x46, 0x6F, 0x78,
						0x2D, 0x74, 0x72, 0x6F, 0x74,
						0x8F, 0x06, 0x40, 0x42, 0x6C,
						0x61, 0x63, 0x6B, 0x8F, 0x06,
						0x3F, 0x42, 0x72, 0x6F, 0x77,
						0x6E, 0x8F, 0x04, 0x3E, 0x52,
						0x65, 0x64, 0x8F, 0x07, 0x3D,
						0x4F, 0x72, 0x61, 0x6E, 0x67,
						0x65, 0x8F, 0x07, 0x3C, 0x59,
						0x65, 0x6C, 0x6C, 0x6F, 0x77,
						0x8F, 0x06, 0x3B, 0x47, 0x72,
						0x65, 0x65, 0x6E, 0x8F, 0x05,
						0x3A, 0x42, 0x6C, 0x75, 0x65,
						0x8F, 0x07, 0x39, 0x56, 0x69,
						0x6F, 0x6C, 0x65, 0x74, 0x8F,
						0x05, 0x38, 0x47, 0x72, 0x65,
						0x79, 0x8F, 0x06, 0x37, 0x57,
						0x68, 0x69, 0x74, 0x65, 0x8F,
						0x06, 0x36, 0x6D, 0x69, 0x6C,
						0x6C, 0x69, 0x8F, 0x06, 0x35,
						0x6D, 0x69, 0x63, 0x72, 0x6F,
						0x8F, 0x05, 0x34, 0x6E, 0x61,
						0x6E, 0x6F, 0x8F, 0x05, 0x33,
						0x70, 0x69, 0x63, 0x6F };

static unsigned char setup_menu_122[] = { 0xD0, 0x81, 0xF3, 0x81, 0x03, 0x01,
						0x25, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x0A, 0x4C, 0x61,
						0x72, 0x67, 0x65, 0x4D, 0x65,
						0x6E, 0x75, 0x32, 0x8F, 0x1D,
						0xFF, 0x31, 0x20, 0x43, 0x61,
						0x6C, 0x6C, 0x20, 0x46, 0x6F,
						0x72, 0x77, 0x61, 0x72, 0x64,
						0x20, 0x55, 0x6E, 0x63, 0x6F,
						0x6E, 0x64, 0x69, 0x74, 0x69,
						0x6F, 0x6E, 0x61, 0x6C, 0x8F,
						0x1C, 0xFE, 0x32, 0x20, 0x43,
						0x61, 0x6C, 0x6C, 0x20, 0x46,
						0x6F, 0x72, 0x77, 0x61, 0x72,
						0x64, 0x20, 0x4F, 0x6E, 0x20,
						0x55, 0x73, 0x65, 0x72, 0x20,
						0x42, 0x75, 0x73, 0x79, 0x8F,
						0x1B, 0xFD, 0x33, 0x20, 0x43,
						0x61, 0x6C, 0x6C, 0x20, 0x46,
						0x6F, 0x72, 0x77, 0x61, 0x72,
						0x64, 0x20, 0x4F, 0x6E, 0x20,
						0x4E, 0x6F, 0x20, 0x52, 0x65,
						0x70, 0x6C, 0x79, 0x8F, 0x25,
						0xFC, 0x34, 0x20, 0x43, 0x61,
						0x6C, 0x6C, 0x20, 0x46, 0x6F,
						0x72, 0x77, 0x61, 0x72, 0x64,
						0x20, 0x4F, 0x6E, 0x20, 0x55,
						0x73, 0x65, 0x72, 0x20, 0x4E,
						0x6F, 0x74, 0x20, 0x52, 0x65,
						0x61, 0x63, 0x68, 0x61, 0x62,
						0x6C, 0x65, 0x8F, 0x20, 0xFB,
						0x35, 0x20, 0x42, 0x61, 0x72,
						0x72, 0x69, 0x6E, 0x67, 0x20,
						0x4F, 0x66, 0x20, 0x41, 0x6C,
						0x6C, 0x20, 0x4F, 0x75, 0x74,
						0x67, 0x6F, 0x69, 0x6E, 0x67,
						0x20, 0x43, 0x61, 0x6C, 0x6C,
						0x73, 0x8F, 0x24, 0xFA, 0x36,
						0x20, 0x42, 0x61, 0x72, 0x72,
						0x69, 0x6E, 0x67, 0x20, 0x4F,
						0x66, 0x20, 0x41, 0x6C, 0x6C,
						0x20, 0x4F, 0x75, 0x74, 0x67,
						0x6F, 0x69, 0x6E, 0x67, 0x20,
						0x49, 0x6E, 0x74, 0x20, 0x43,
						0x61, 0x6C, 0x6C, 0x73, 0x8F,
						0x13, 0xF9, 0x37, 0x20, 0x43,
						0x4C, 0x49, 0x20, 0x50, 0x72,
						0x65, 0x73, 0x65, 0x6E, 0x74,
						0x61, 0x74, 0x69, 0x6F, 0x6E };

static unsigned char setup_menu_123[] = { 0xD0, 0x81, 0xFC, 0x81, 0x03, 0x01,
						0x25, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x81, 0xEC, 0x54,
						0x68, 0x65, 0x20, 0x53, 0x49,
						0x4D, 0x20, 0x73, 0x68, 0x61,
						0x6C, 0x6C, 0x20, 0x73, 0x75,
						0x70, 0x70, 0x6C, 0x79, 0x20,
						0x61, 0x20, 0x73, 0x65, 0x74,
						0x20, 0x6F, 0x66, 0x20, 0x6D,
						0x65, 0x6E, 0x75, 0x20, 0x69,
						0x74, 0x65, 0x6D, 0x73, 0x2C,
						0x20, 0x77, 0x68, 0x69, 0x63,
						0x68, 0x20, 0x73, 0x68, 0x61,
						0x6C, 0x6C, 0x20, 0x62, 0x65,
						0x20, 0x69, 0x6E, 0x74, 0x65,
						0x67, 0x72, 0x61, 0x74, 0x65,
						0x64, 0x20, 0x77, 0x69, 0x74,
						0x68, 0x20, 0x74, 0x68, 0x65,
						0x20, 0x6D, 0x65, 0x6E, 0x75,
						0x20, 0x73, 0x79, 0x73, 0x74,
						0x65, 0x6D, 0x20, 0x28, 0x6F,
						0x72, 0x20, 0x6F, 0x74, 0x68,
						0x65, 0x72, 0x20, 0x4D, 0x4D,
						0x49, 0x20, 0x66, 0x61, 0x63,
						0x69, 0x6C, 0x69, 0x74, 0x79,
						0x29, 0x20, 0x69, 0x6E, 0x20,
						0x6F, 0x72, 0x64, 0x65, 0x72,
						0x20, 0x74, 0x6F, 0x20, 0x67,
						0x69, 0x76, 0x65, 0x20, 0x74,
						0x68, 0x65, 0x20, 0x75, 0x73,
						0x65, 0x72, 0x20, 0x74, 0x68,
						0x65, 0x20, 0x6F, 0x70, 0x70,
						0x6F, 0x72, 0x74, 0x75, 0x6E,
						0x69, 0x74, 0x79, 0x20, 0x74,
						0x6F, 0x20, 0x63, 0x68, 0x6F,
						0x6F, 0x73, 0x65, 0x20, 0x6F,
						0x6E, 0x65, 0x20, 0x6F, 0x66,
						0x20, 0x74, 0x68, 0x65, 0x73,
						0x65, 0x20, 0x6D, 0x65, 0x6E,
						0x75, 0x20, 0x69, 0x74, 0x65,
						0x6D, 0x73, 0x20, 0x61, 0x74,
						0x20, 0x68, 0x69, 0x73, 0x20,
						0x6F, 0x77, 0x6E, 0x20, 0x64,
						0x69, 0x73, 0x63, 0x72, 0x65,
						0x74, 0x69, 0x6F, 0x6E, 0x2E,
						0x20, 0x45, 0x61, 0x63, 0x68,
						0x20, 0x69, 0x74, 0x65, 0x6D,
						0x20, 0x63, 0x6F, 0x6D, 0x70,
						0x72, 0x69, 0x73, 0x65, 0x73,
						0x20, 0x61, 0x20, 0x73, 0x68,
						0x8F, 0x02, 0x01, 0x59 };

static unsigned char setup_menu_211[] = { 0xD0, 0x3B, 0x81, 0x03, 0x01, 0x25,
						0x80, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0C, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x8F,
						0x07, 0x01, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x31, 0x8F, 0x07,
						0x02, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x32, 0x8F, 0x07, 0x03,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x33, 0x8F, 0x07, 0x04, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x34 };

static unsigned char setup_menu_311[] = { 0xD0, 0x41, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0C, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x8F,
						0x07, 0x01, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x31, 0x8F, 0x07,
						0x02, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x32, 0x8F, 0x07, 0x03,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x33, 0x8F, 0x07, 0x04, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x34,
						0x18, 0x04, 0x13, 0x10, 0x15,
						0x26 };

static unsigned char setup_menu_411[] = { 0xD0, 0x3C, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0C, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x8F,
						0x07, 0x01, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x31, 0x8F, 0x07,
						0x02, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x32, 0x8F, 0x07, 0x03,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x33, 0x9E, 0x02, 0x01, 0x01,
						0x9F, 0x04, 0x01, 0x05, 0x05,
						0x05 };

static unsigned char setup_menu_421[] = { 0xD0, 0x3C, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0C, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x8F,
						0x07, 0x01, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x31, 0x8F, 0x07,
						0x02, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x32, 0x8F, 0x07, 0x03,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x33, 0x9E, 0x02, 0x00, 0x01,
						0x9F, 0x04, 0x00, 0x05, 0x05,
						0x05 };

static unsigned char setup_menu_511[] = { 0xD0, 0x29, 0x81, 0x03, 0x01, 0x25,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0C, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x8F,
						0x07, 0x01, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x31, 0x8F, 0x07,
						0x02, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x32 };

static unsigned char setup_menu_611[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x31, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0xD0, 0x04,
						0x00, 0x0E, 0x00, 0xB4, 0xD1,
						0x0C, 0x00, 0x06, 0x00, 0xB4,
						0x00, 0x06, 0x00, 0xB4, 0x00,
						0x06, 0x00, 0xB4 };

static unsigned char setup_menu_612[] = { 0xD0, 0x34, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x32, 0x8F, 0x07, 0x04, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x34,
						0x8F, 0x07, 0x05, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x35, 0x8F,
						0x07, 0x06, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x36 };

static unsigned char setup_menu_621[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x31, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0xD0, 0x04,
						0x00, 0x0E, 0x01, 0xB4, 0xD1,
						0x0C, 0x00, 0x06, 0x01, 0xB4,
						0x00, 0x06, 0x01, 0xB4, 0x00,
						0x06, 0x01, 0xB4 };

static unsigned char setup_menu_622[] = { 0xD0, 0x34, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x32, 0x8F, 0x07, 0x04, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x34,
						0x8F, 0x07, 0x05, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x35, 0x8F,
						0x07, 0x06, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x36 };

static unsigned char setup_menu_631[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x31, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0xD0, 0x04,
						0x00, 0x0E, 0x02, 0xB4, 0xD1,
						0x0C, 0x00, 0x06, 0x02, 0xB4,
						0x00, 0x06, 0x02, 0xB4, 0x00,
						0x06, 0x02, 0xB4 };

static unsigned char setup_menu_632[] = { 0xD0, 0x34, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x32, 0x8F, 0x07, 0x04, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x34,
						0x8F, 0x07, 0x05, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x35, 0x8F,
						0x07, 0x06, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x36 };

static unsigned char setup_menu_641[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x31, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0xD0, 0x04,
						0x00, 0x0E, 0x04, 0xB4, 0xD1,
						0x0C, 0x00, 0x06, 0x04, 0xB4,
						0x00, 0x06, 0x04, 0xB4, 0x00,
						0x06, 0x04, 0xB4 };

static unsigned char setup_menu_642[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x32, 0x8F, 0x07, 0x04, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x34,
						0x8F, 0x07, 0x05, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x35, 0x8F,
						0x07, 0x06, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x36, 0xD0, 0x04,
						0x00, 0x0E, 0x00, 0xB4, 0xD1,
						0x0C, 0x00, 0x06, 0x00, 0xB4,
						0x00, 0x06, 0x00, 0xB4, 0x00,
						0x06, 0x00, 0xB4 };

static unsigned char setup_menu_643[] = { 0xD0, 0x34, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x33, 0x8F, 0x07, 0x07, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x37,
						0x8F, 0x07, 0x08, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x38, 0x8F,
						0x07, 0x09, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x39 };

static unsigned char setup_menu_651[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x31, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0xD0, 0x04,
						0x00, 0x0E, 0x08, 0xB4, 0xD1,
						0x0C, 0x00, 0x06, 0x08, 0xB4,
						0x00, 0x06, 0x08, 0xB4, 0x00,
						0x06, 0x08, 0xB4 };

static unsigned char setup_menu_661[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x31, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0xD0, 0x04,
						0x00, 0x0E, 0x10, 0xB4, 0xD1,
						0x0C, 0x00, 0x06, 0x10, 0xB4,
						0x00, 0x06, 0x10, 0xB4, 0x00,
						0x06, 0x10, 0xB4 };

static unsigned char setup_menu_671[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x31, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0xD0, 0x04,
						0x00, 0x0E, 0x20, 0xB4, 0xD1,
						0x0C, 0x00, 0x06, 0x20, 0xB4,
						0x00, 0x06, 0x20, 0xB4, 0x00,
						0x06, 0x20, 0xB4 };

static unsigned char setup_menu_681[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x31, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0xD0, 0x04,
						0x00, 0x0E, 0x40, 0xB4, 0xD1,
						0x0C, 0x00, 0x06, 0x40, 0xB4,
						0x00, 0x06, 0x40, 0xB4, 0x00,
						0x06, 0x40, 0xB4 };

static unsigned char setup_menu_691[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x20,
						0x31, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0xD0, 0x04,
						0x00, 0x0E, 0x80, 0xB4, 0xD1,
						0x0C, 0x00, 0x06, 0x80, 0xB4,
						0x00, 0x06, 0x80, 0xB4, 0x00,
						0x06, 0x80, 0xB4 };

static unsigned char setup_menu_6101[] = { 0xD0, 0x46, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0C, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x4D, 0x65, 0x6E, 0x75, 0x8F,
						0x07, 0x01, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x31, 0x8F, 0x07,
						0x02, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x32, 0x8F, 0x07, 0x03,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x33, 0xD0, 0x04, 0x00, 0x0C,
						0x00, 0xB4, 0xD1, 0x0C, 0x00,
						0x06, 0x00, 0xB4, 0x00, 0x06,
						0x00, 0xB4, 0x00, 0x06, 0x00,
						0xB4 };

static unsigned char setup_menu_711[] = { 0xD0, 0x81, 0x9C, 0x81, 0x03, 0x01,
						0x25, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x19, 0x80, 0x04,
						0x17, 0x04, 0x14, 0x04, 0x20,
						0x04, 0x10, 0x04, 0x12, 0x04,
						0x21, 0x04, 0x22, 0x04, 0x12,
						0x04, 0x23, 0x04, 0x19, 0x04,
						0x22, 0x04, 0x15, 0x8F, 0x1C,
						0x01, 0x80, 0x04, 0x17, 0x04,
						0x14, 0x04, 0x20, 0x04, 0x10,
						0x04, 0x12, 0x04, 0x21, 0x04,
						0x22, 0x04, 0x12, 0x04, 0x23,
						0x04, 0x19, 0x04, 0x22, 0x04,
						0x15, 0x00, 0x31, 0x8F, 0x1C,
						0x02, 0x80, 0x04, 0x17, 0x04,
						0x14, 0x04, 0x20, 0x04, 0x10,
						0x04, 0x12, 0x04, 0x21, 0x04,
						0x22, 0x04, 0x12, 0x04, 0x23,
						0x04, 0x19, 0x04, 0x22, 0x04,
						0x15, 0x00, 0x32, 0x8F, 0x1C,
						0x03, 0x80, 0x04, 0x17, 0x04,
						0x14, 0x04, 0x20, 0x04, 0x10,
						0x04, 0x12, 0x04, 0x21, 0x04,
						0x22, 0x04, 0x12, 0x04, 0x23,
						0x04, 0x19, 0x04, 0x22, 0x04,
						0x15, 0x00, 0x33, 0x8F, 0x1C,
						0x04, 0x80, 0x04, 0x17, 0x04,
						0x14, 0x04, 0x20, 0x04, 0x10,
						0x04, 0x12, 0x04, 0x21, 0x04,
						0x22, 0x04, 0x12, 0x04, 0x23,
						0x04, 0x19, 0x04, 0x22, 0x04,
						0x15, 0x00, 0x34 };

static unsigned char setup_menu_712[] = { 0xD0, 0x60, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x19, 0x80, 0x04, 0x17,
						0x04, 0x14, 0x04, 0x20, 0x04,
						0x10, 0x04, 0x12, 0x04, 0x21,
						0x04, 0x22, 0x04, 0x12, 0x04,
						0x23, 0x04, 0x19, 0x04, 0x22,
						0x04, 0x15, 0x8F, 0x1C, 0x11,
						0x80, 0x04, 0x17, 0x04, 0x14,
						0x04, 0x20, 0x04, 0x10, 0x04,
						0x12, 0x04, 0x21, 0x04, 0x22,
						0x04, 0x12, 0x04, 0x23, 0x04,
						0x19, 0x04, 0x22, 0x04, 0x15,
						0x00, 0x35, 0x8F, 0x1C, 0x12,
						0x80, 0x04, 0x17, 0x04, 0x14,
						0x04, 0x20, 0x04, 0x10, 0x04,
						0x12, 0x04, 0x21, 0x04, 0x22,
						0x04, 0x12, 0x04, 0x23, 0x04,
						0x19, 0x04, 0x22, 0x04, 0x15,
						0x00, 0x36 };

static unsigned char setup_menu_713[] = { 0xD0, 0x0D, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x00, 0x8F, 0x00 };

static unsigned char setup_menu_811[] = { 0xD0, 0x3C, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x09, 0x80, 0x5D, 0xE5,
						0x51, 0x77, 0x7B, 0xB1, 0x53,
						0x55, 0x8F, 0x08, 0x01, 0x80,
						0x98, 0x79, 0x76, 0xEE, 0x4E,
						0x00, 0x8F, 0x08, 0x02, 0x80,
						0x98, 0x79, 0x76, 0xEE, 0x4E,
						0x8C, 0x8F, 0x08, 0x03, 0x80,
						0x98, 0x79, 0x76, 0xEE, 0x4E,
						0x09, 0x8F, 0x08, 0x04, 0x80,
						0x98, 0x79, 0x76, 0xEE, 0x56,
						0xDB };

static unsigned char setup_menu_812[] = { 0xD0, 0x20, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x09, 0x80, 0x5D, 0xE5,
						0x51, 0x77, 0x7B, 0xB1, 0x53,
						0x55, 0x8F, 0x04, 0x11, 0x80,
						0x4E, 0x00, 0x8F, 0x04, 0x12,
						0x80, 0x4E, 0x8C };

static unsigned char setup_menu_813[] = { 0xD0, 0x0D, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x00, 0x8F, 0x00 };

static unsigned char setup_menu_911[] = { 0xD0, 0x44, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x09, 0x80, 0x00, 0x38,
						0x00, 0x30, 0x30, 0xEB, 0x00,
						0x30, 0x8F, 0x0A, 0x01, 0x80,
						0x00, 0x38, 0x00, 0x30, 0x30,
						0xEB, 0x00, 0x31, 0x8F, 0x0A,
						0x02, 0x80, 0x00, 0x38, 0x00,
						0x30, 0x30, 0xEB, 0x00, 0x32,
						0x8F, 0x0A, 0x03, 0x80, 0x00,
						0x38, 0x00, 0x30, 0x30, 0xEB,
						0x00, 0x33, 0x8F, 0x0A, 0x04,
						0x80, 0x00, 0x38, 0x00, 0x30,
						0x30, 0xEB, 0x00, 0x34 };

static unsigned char setup_menu_912[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x09, 0x80, 0x00, 0x38,
						0x00, 0x30, 0x30, 0xEB, 0x00,
						0x30, 0x8F, 0x0A, 0x11, 0x80,
						0x00, 0x38, 0x00, 0x30, 0x30,
						0xEB, 0x00, 0x35, 0x8F, 0x0A,
						0x12, 0x80, 0x00, 0x38, 0x00,
						0x30, 0x30, 0xEB, 0x00, 0x36 };

static unsigned char setup_menu_913[] = { 0xD0, 0x0D, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x00, 0x8F, 0x00 };

/* Negative case: No item is present */
static unsigned char setup_menu_neg_1[] = { 0xD0, 0x0B, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x00 };

/* Negative case: Two empty items*/
static unsigned char setup_menu_neg_2[] = { 0xD0, 0x0F, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x00, 0x8F, 0x00, 0x8F,
						0x00 };

/* Negative case: valid item + empty item */
static unsigned char setup_menu_neg_3[] = { 0xD0, 0x16, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x00, 0x8F, 0x07, 0x01,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x31, 0x8F, 0x00 };

/* Negative case: empty item + valid item */
static unsigned char setup_menu_neg_4[] = { 0xD0, 0x16, 0x81, 0x03, 0x01, 0x25,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x00, 0x8F, 0x00, 0x8F,
						0x07, 0x01, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x31 };

static struct setup_menu_test setup_menu_data_111 = {
	.pdu = setup_menu_111,
	.pdu_len = sizeof(setup_menu_111),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
		{ .id = 4, .text = "Item 4" },
	}
};

static struct setup_menu_test setup_menu_data_112 = {
	.pdu = setup_menu_112,
	.pdu_len = sizeof(setup_menu_112),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu",
	.items = {
		{ .id = 0x11, .text = "One" },
		{ .id = 0x12, .text = "Two" },
	}
};

static struct setup_menu_test setup_menu_data_113 = {
	.pdu = setup_menu_113,
	.pdu_len = sizeof(setup_menu_113),
	.qualifier = 0x00,
	.alpha_id = ""
};

static struct setup_menu_test setup_menu_data_121 = {
	.pdu = setup_menu_121,
	.pdu_len = sizeof(setup_menu_121),
	.qualifier = 0x00,
	.alpha_id = "LargeMenu1",
	.items = {
		{ .id = 0x50, .text = "Zero" },
		{ .id = 0x4F, .text = "One" },
		{ .id = 0x4E, .text = "Two" },
		{ .id = 0x4D, .text = "Three" },
		{ .id = 0x4C, .text = "Four" },
		{ .id = 0x4B, .text = "Five" },
		{ .id = 0x4A, .text = "Six" },
		{ .id = 0x49, .text = "Seven" },
		{ .id = 0x48, .text = "Eight" },
		{ .id = 0x47, .text = "Nine" },
		{ .id = 0x46, .text = "Alpha" },
		{ .id = 0x45, .text = "Bravo" },
		{ .id = 0x44, .text = "Charlie" },
		{ .id = 0x43, .text = "Delta" },
		{ .id = 0x42, .text = "Echo" },
		{ .id = 0x41, .text = "Fox-trot" },
		{ .id = 0x40, .text = "Black" },
		{ .id = 0x3F, .text = "Brown" },
		{ .id = 0x3E, .text = "Red" },
		{ .id = 0x3D, .text = "Orange" },
		{ .id = 0x3C, .text = "Yellow" },
		{ .id = 0x3B, .text = "Green" },
		{ .id = 0x3A, .text = "Blue" },
		{ .id = 0x39, .text = "Violet" },
		{ .id = 0x38, .text = "Grey" },
		{ .id = 0x37, .text = "White" },
		{ .id = 0x36, .text = "milli" },
		{ .id = 0x35, .text = "micro" },
		{ .id = 0x34, .text = "nano" },
		{ .id = 0x33, .text = "pico" },
	}
};

static struct setup_menu_test setup_menu_data_122 = {
	.pdu = setup_menu_122,
	.pdu_len = sizeof(setup_menu_122),
	.qualifier = 0x00,
	.alpha_id = "LargeMenu2",
	.items = {
		{ .id = 0xFF, .text = "1 Call Forward Unconditional" },
		{ .id = 0xFE, .text = "2 Call Forward On User Busy" },
		{ .id = 0xFD, .text = "3 Call Forward On No Reply" },
		{ .id = 0xFC, .text = "4 Call Forward On User Not Reachable" },
		{ .id = 0xFB, .text = "5 Barring Of All Outgoing Calls" },
		{ .id = 0xFA, .text = "6 Barring Of All Outgoing Int Calls" },
		{ .id = 0xF9, .text = "7 CLI Presentation" },
	}
};

static struct setup_menu_test setup_menu_data_123 = {
	.pdu = setup_menu_123,
	.pdu_len = sizeof(setup_menu_123),
	.qualifier = 0x00,
	.alpha_id = "The SIM shall supply a set of menu items, which shall "
			"be integrated with the menu system (or other MMI "
			"facility) in order to give the user the opportunity "
			"to choose one of these menu items at his own "
			"discretion. Each item comprises a sh",
	.items = {
		{ .id = 0x01, .text = "Y" }
	}
};

static struct setup_menu_test setup_menu_data_211 = {
	.pdu = setup_menu_211,
	.pdu_len = sizeof(setup_menu_211),
	.qualifier = 0x80,
	.alpha_id = "Toolkit Menu",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
		{ .id = 4, .text = "Item 4" },
	}
};

static struct setup_menu_test setup_menu_data_311 = {
	.pdu = setup_menu_311,
	.pdu_len = sizeof(setup_menu_311),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
		{ .id = 4, .text = "Item 4" },
	},
	.next_act = {
		.list = { STK_COMMAND_TYPE_SEND_SMS,
				STK_COMMAND_TYPE_SETUP_CALL,
				STK_COMMAND_TYPE_LAUNCH_BROWSER,
				STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO },
		.len = 4
	}
};

static struct setup_menu_test setup_menu_data_411 = {
	.pdu = setup_menu_411,
	.pdu_len = sizeof(setup_menu_411),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 1
	},
	.item_icon_id_list = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.list = { 5, 5, 5 },
		.len = 3
	}
};

static struct setup_menu_test setup_menu_data_421 = {
	.pdu = setup_menu_421,
	.pdu_len = sizeof(setup_menu_421),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 1
	},
	.item_icon_id_list = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.list = { 5, 5, 5 },
		.len = 3
	}
};

static struct setup_menu_test setup_menu_data_511 = {
	.pdu = setup_menu_511,
	.pdu_len = sizeof(setup_menu_511),
	.qualifier = 0x01,
	.alpha_id = "Toolkit Menu",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	}
};

static struct setup_menu_test setup_menu_data_611 = {
	.pdu = setup_menu_611,
	.pdu_len = sizeof(setup_menu_611),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4,
				0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Menu 1</span>"
		"</div>",
};

static struct setup_menu_test setup_menu_data_612 = {
	.pdu = setup_menu_612,
	.pdu_len = sizeof(setup_menu_612),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 2",
	.items = {
		{ .id = 4, .text = "Item 4" },
		{ .id = 5, .text = "Item 5" },
		{ .id = 6, .text = "Item 6" },
	}
};

static struct setup_menu_test setup_menu_data_621 = {
	.pdu = setup_menu_621,
	.pdu_len = sizeof(setup_menu_621),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x01, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x01, 0xB4, 0x00, 0x06, 0x01, 0xB4,
				0x00, 0x06, 0x01, 0xB4 }
	},
	.html = "<div style=\"text-align: center;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Menu 1</span>"
		"</div>"
};

static struct setup_menu_test setup_menu_data_622 = {
	.pdu = setup_menu_622,
	.pdu_len = sizeof(setup_menu_622),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 2",
	.items = {
		{ .id = 4, .text = "Item 4" },
		{ .id = 5, .text = "Item 5" },
		{ .id = 6, .text = "Item 6" },
	}
};

/*
 * Some problem with data of item #3 in item_text_attr_list
 * and the explanation
 */
static struct setup_menu_test setup_menu_data_631 = {
	.pdu = setup_menu_631,
	.pdu_len = sizeof(setup_menu_631),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x02, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x02, 0xB4, 0x00, 0x06, 0x02, 0xB4,
				0x00, 0x06, 0x02, 0xB4 }
	},
	.html = "<div style=\"text-align: right;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Menu 1</span>"
		"</div>"
};

static struct setup_menu_test setup_menu_data_632 = {
	.pdu = setup_menu_632,
	.pdu_len = sizeof(setup_menu_632),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 2",
	.items = {
		{ .id = 4, .text = "Item 4" },
		{ .id = 5, .text = "Item 5" },
		{ .id = 6, .text = "Item 6" },
	}
};

static struct setup_menu_test setup_menu_data_641 = {
	.pdu = setup_menu_641,
	.pdu_len = sizeof(setup_menu_641),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x04, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x04, 0xB4, 0x00, 0x06, 0x04, 0xB4,
				0x00, 0x06, 0x04, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"big;color: #347235;background-color: #FFFF00;\">"
		"Toolkit Menu 1</span></div>",
};

static struct setup_menu_test setup_menu_data_642 = {
	.pdu = setup_menu_642,
	.pdu_len = sizeof(setup_menu_642),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 2",
	.items = {
		{ .id = 4, .text = "Item 4" },
		{ .id = 5, .text = "Item 5" },
		{ .id = 6, .text = "Item 6" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4,
				0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Menu 2</span>"
		"</div>",
};

static struct setup_menu_test setup_menu_data_643 = {
	.pdu = setup_menu_643,
	.pdu_len = sizeof(setup_menu_643),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 3",
	.items = {
		{ .id = 7, .text = "Item 7" },
		{ .id = 8, .text = "Item 8" },
		{ .id = 9, .text = "Item 9" },
	}
};

static struct setup_menu_test setup_menu_data_651 = {
	.pdu = setup_menu_651,
	.pdu_len = sizeof(setup_menu_651),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x08, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x08, 0xB4, 0x00, 0x06, 0x08, 0xB4,
				0x00, 0x06, 0x08, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"small;color: #347235;background-color: #FFFF00;\">"
		"Toolkit Menu 1</span></div>",
};

static struct setup_menu_test setup_menu_data_661 = {
	.pdu = setup_menu_661,
	.pdu_len = sizeof(setup_menu_661),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x10, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x10, 0xB4, 0x00, 0x06, 0x10, 0xB4,
				0x00, 0x06, 0x10, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-weight: "
		"bold;color: #347235;background-color: #FFFF00;\">"
		"Toolkit Menu 1</span></div>",
};

static struct setup_menu_test setup_menu_data_671 = {
	.pdu = setup_menu_671,
	.pdu_len = sizeof(setup_menu_671),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x20, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x20, 0xB4, 0x00, 0x06, 0x20, 0xB4,
				0x00, 0x06, 0x20, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-style: "
		"italic;color: #347235;background-color: #FFFF00;\">"
		"Toolkit Menu 1</span></div>"
};

static struct setup_menu_test setup_menu_data_681 = {
	.pdu = setup_menu_681,
	.pdu_len = sizeof(setup_menu_681),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x40, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x40, 0xB4, 0x00, 0x06, 0x40, 0xB4,
				0x00, 0x06, 0x40, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span "
		"style=\"text-decoration: underline;color: #347235;"
		"background-color: #FFFF00;\">Toolkit Menu 1</span></div>",
};

static struct setup_menu_test setup_menu_data_691 = {
	.pdu = setup_menu_691,
	.pdu_len = sizeof(setup_menu_691),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x80, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x80, 0xB4, 0x00, 0x06, 0x80, 0xB4,
				0x00, 0x06, 0x80, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span "
		"style=\"text-decoration: line-through;color: #347235;"
		"background-color: #FFFF00;\">Toolkit Menu 1</span></div>",
};

static struct setup_menu_test setup_menu_data_6101 = {
	.pdu = setup_menu_6101,
	.pdu_len = sizeof(setup_menu_6101),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Menu",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0C, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 12,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4,
				0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Menu</span>"
		"</div>",
};

static struct setup_menu_test setup_menu_data_711 = {
	.pdu = setup_menu_711,
	.pdu_len = sizeof(setup_menu_711),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.items = {
		{ .id = 1, .text = "ЗДРАВСТВУЙТЕ1" },
		{ .id = 2, .text = "ЗДРАВСТВУЙТЕ2" },
		{ .id = 3, .text = "ЗДРАВСТВУЙТЕ3" },
		{ .id = 4, .text = "ЗДРАВСТВУЙТЕ4" },
	}
};

static struct setup_menu_test setup_menu_data_712 = {
	.pdu = setup_menu_712,
	.pdu_len = sizeof(setup_menu_712),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.items = {
		{ .id = 0x11, .text = "ЗДРАВСТВУЙТЕ5" },
		{ .id = 0x12, .text = "ЗДРАВСТВУЙТЕ6" },
	}
};

static struct setup_menu_test setup_menu_data_713 = {
	.pdu = setup_menu_713,
	.pdu_len = sizeof(setup_menu_713),
	.qualifier = 0x00,
	.alpha_id = ""
};

static struct setup_menu_test setup_menu_data_811 = {
	.pdu = setup_menu_811,
	.pdu_len = sizeof(setup_menu_811),
	.qualifier = 0x00,
	.alpha_id = "工具箱单",
	.items = {
		{ .id = 1, .text = "项目一" },
		{ .id = 2, .text = "项目二" },
		{ .id = 3, .text = "项目三" },
		{ .id = 4, .text = "项目四" },
	}
};

static struct setup_menu_test setup_menu_data_812 = {
	.pdu = setup_menu_812,
	.pdu_len = sizeof(setup_menu_812),
	.qualifier = 0x00,
	.alpha_id = "工具箱单",
	.items = {
		{ .id = 0x11, .text = "一" },
		{ .id = 0x12, .text = "二" },
	}
};

static struct setup_menu_test setup_menu_data_813 = {
	.pdu = setup_menu_813,
	.pdu_len = sizeof(setup_menu_813),
	.qualifier = 0x00,
	.alpha_id = ""
};

static struct setup_menu_test setup_menu_data_911 = {
	.pdu = setup_menu_911,
	.pdu_len = sizeof(setup_menu_911),
	.qualifier = 0x00,
	.alpha_id = "80ル0",
	.items = {
		{ .id = 1, .text = "80ル1" },
		{ .id = 2, .text = "80ル2" },
		{ .id = 3, .text = "80ル3" },
		{ .id = 4, .text = "80ル4" },
	}
};

static struct setup_menu_test setup_menu_data_912 = {
	.pdu = setup_menu_912,
	.pdu_len = sizeof(setup_menu_912),
	.qualifier = 0x00,
	.alpha_id = "80ル0",
	.items = {
		{ .id = 0x11, .text = "80ル5" },
		{ .id = 0x12, .text = "80ル6" },
	}
};

static struct setup_menu_test setup_menu_data_913 = {
	.pdu = setup_menu_913,
	.pdu_len = sizeof(setup_menu_913),
	.qualifier = 0x00,
	.alpha_id = ""
};

static struct setup_menu_test setup_menu_data_neg_1 = {
	.pdu = setup_menu_neg_1,
	.pdu_len = sizeof(setup_menu_neg_1)
};

static struct setup_menu_test setup_menu_data_neg_2 = {
	.pdu = setup_menu_neg_2,
	.pdu_len = sizeof(setup_menu_neg_2)
};

static struct setup_menu_test setup_menu_data_neg_3 = {
	.pdu = setup_menu_neg_3,
	.pdu_len = sizeof(setup_menu_neg_3)
};

static struct setup_menu_test setup_menu_data_neg_4 = {
	.pdu = setup_menu_neg_4,
	.pdu_len = sizeof(setup_menu_neg_4)
};

/* Defined in TS 102.384 Section 27.22.4.7 */
static void test_setup_menu(gconstpointer data)
{
	const struct setup_menu_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_SETUP_MENU);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_alpha_id(command->setup_menu.alpha_id, test->alpha_id);
	check_items(command->setup_menu.items, test->items);
	check_items_next_action_indicator(&command->setup_menu.next_act,
						&test->next_act);
	check_icon_id(&command->setup_menu.icon_id, &test->icon_id);
	check_item_icon_id_list(&command->setup_menu.item_icon_id_list,
					&test->item_icon_id_list);
	check_text_attr(&command->setup_menu.text_attr, &test->text_attr);
	check_item_text_attribute_list(&command->setup_menu.item_text_attr_list,
					&test->item_text_attr_list);
	check_text_attr_html(&command->setup_menu.text_attr,
				command->setup_menu.alpha_id, test->html);
	stk_command_free(command);
}

static void test_setup_menu_missing_val(gconstpointer data)
{
	const struct setup_menu_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_MISSING_VALUE);

	stk_command_free(command);
}

static void test_setup_menu_neg(gconstpointer data)
{
	const struct setup_menu_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_DATA_NOT_UNDERSTOOD);

	stk_command_free(command);
}

struct select_item_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *alpha_id;
	struct stk_item items[MAX_ITEM];
	struct stk_items_next_action_indicator next_act;
	unsigned char item_id;
	struct stk_icon_id icon_id;
	struct stk_item_icon_id_list item_icon_id_list;
	struct stk_text_attribute text_attr;
	struct stk_item_text_attribute_list item_text_attr_list;
	struct stk_frame_id frame_id;
	char *html;
};

static unsigned char select_item_111[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0x8F, 0x07,
						0x04, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x34 };

static unsigned char select_item_121[] = { 0xD0, 0x81, 0xFC, 0x81, 0x03, 0x01,
						0x24, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x0A, 0x4C, 0x61,
						0x72, 0x67, 0x65, 0x4D, 0x65,
						0x6E, 0x75, 0x31, 0x8F, 0x05,
						0x50, 0x5A, 0x65, 0x72, 0x6F,
						0x8F, 0x04, 0x4F, 0x4F, 0x6E,
						0x65, 0x8F, 0x04, 0x4E, 0x54,
						0x77, 0x6F, 0x8F, 0x06, 0x4D,
						0x54, 0x68, 0x72, 0x65, 0x65,
						0x8F, 0x05, 0x4C, 0x46, 0x6F,
						0x75, 0x72, 0x8F, 0x05, 0x4B,
						0x46, 0x69, 0x76, 0x65, 0x8F,
						0x04, 0x4A, 0x53, 0x69, 0x78,
						0x8F, 0x06, 0x49, 0x53, 0x65,
						0x76, 0x65, 0x6E, 0x8F, 0x06,
						0x48, 0x45, 0x69, 0x67, 0x68,
						0x74, 0x8F, 0x05, 0x47, 0x4E,
						0x69, 0x6E, 0x65, 0x8F, 0x06,
						0x46, 0x41, 0x6C, 0x70, 0x68,
						0x61, 0x8F, 0x06, 0x45, 0x42,
						0x72, 0x61, 0x76, 0x6F, 0x8F,
						0x08, 0x44, 0x43, 0x68, 0x61,
						0x72, 0x6C, 0x69, 0x65, 0x8F,
						0x06, 0x43, 0x44, 0x65, 0x6C,
						0x74, 0x61, 0x8F, 0x05, 0x42,
						0x45, 0x63, 0x68, 0x6F, 0x8F,
						0x09, 0x41, 0x46, 0x6F, 0x78,
						0x2D, 0x74, 0x72, 0x6F, 0x74,
						0x8F, 0x06, 0x40, 0x42, 0x6C,
						0x61, 0x63, 0x6B, 0x8F, 0x06,
						0x3F, 0x42, 0x72, 0x6F, 0x77,
						0x6E, 0x8F, 0x04, 0x3E, 0x52,
						0x65, 0x64, 0x8F, 0x07, 0x3D,
						0x4F, 0x72, 0x61, 0x6E, 0x67,
						0x65, 0x8F, 0x07, 0x3C, 0x59,
						0x65, 0x6C, 0x6C, 0x6F, 0x77,
						0x8F, 0x06, 0x3B, 0x47, 0x72,
						0x65, 0x65, 0x6E, 0x8F, 0x05,
						0x3A, 0x42, 0x6C, 0x75, 0x65,
						0x8F, 0x07, 0x39, 0x56, 0x69,
						0x6F, 0x6C, 0x65, 0x74, 0x8F,
						0x05, 0x38, 0x47, 0x72, 0x65,
						0x79, 0x8F, 0x06, 0x37, 0x57,
						0x68, 0x69, 0x74, 0x65, 0x8F,
						0x06, 0x36, 0x6D, 0x69, 0x6C,
						0x6C, 0x69, 0x8F, 0x06, 0x35,
						0x6D, 0x69, 0x63, 0x72, 0x6F,
						0x8F, 0x05, 0x34, 0x6E, 0x61,
						0x6E, 0x6F, 0x8F, 0x05, 0x33,
						0x70, 0x69, 0x63, 0x6F };

static unsigned char select_item_131[] = { 0xD0, 0x81, 0xFB, 0x81, 0x03, 0x01,
						0x24, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x0A, 0x4C, 0x61,
						0x72, 0x67, 0x65, 0x4D, 0x65,
						0x6E, 0x75, 0x32, 0x8F, 0x1E,
						0xFF, 0x43, 0x61, 0x6C, 0x6C,
						0x20, 0x46, 0x6F, 0x72, 0x77,
						0x61, 0x72, 0x64, 0x69, 0x6E,
						0x67, 0x20, 0x55, 0x6E, 0x63,
						0x6F, 0x6E, 0x64, 0x69, 0x74,
						0x69, 0x6F, 0x6E, 0x61, 0x6C,
						0x8F, 0x1D, 0xFE, 0x43, 0x61,
						0x6C, 0x6C, 0x20, 0x46, 0x6F,
						0x72, 0x77, 0x61, 0x72, 0x64,
						0x69, 0x6E, 0x67, 0x20, 0x4F,
						0x6E, 0x20, 0x55, 0x73, 0x65,
						0x72, 0x20, 0x42, 0x75, 0x73,
						0x79, 0x8F, 0x1C, 0xFD, 0x43,
						0x61, 0x6C, 0x6C, 0x20, 0x46,
						0x6F, 0x72, 0x77, 0x61, 0x72,
						0x64, 0x69, 0x6E, 0x67, 0x20,
						0x4F, 0x6E, 0x20, 0x4E, 0x6F,
						0x20, 0x52, 0x65, 0x70, 0x6C,
						0x79, 0x8F, 0x26, 0xFC, 0x43,
						0x61, 0x6C, 0x6C, 0x20, 0x46,
						0x6F, 0x72, 0x77, 0x61, 0x72,
						0x64, 0x69, 0x6E, 0x67, 0x20,
						0x4F, 0x6E, 0x20, 0x55, 0x73,
						0x65, 0x72, 0x20, 0x4E, 0x6F,
						0x74, 0x20, 0x52, 0x65, 0x61,
						0x63, 0x68, 0x61, 0x62, 0x6C,
						0x65, 0x8F, 0x1E, 0xFB, 0x42,
						0x61, 0x72, 0x72, 0x69, 0x6E,
						0x67, 0x20, 0x4F, 0x66, 0x20,
						0x41, 0x6C, 0x6C, 0x20, 0x4F,
						0x75, 0x74, 0x67, 0x6F, 0x69,
						0x6E, 0x67, 0x20, 0x43, 0x61,
						0x6C, 0x6C, 0x73, 0x8F, 0x2C,
						0xFA, 0x42, 0x61, 0x72, 0x72,
						0x69, 0x6E, 0x67, 0x20, 0x4F,
						0x66, 0x20, 0x41, 0x6C, 0x6C,
						0x20, 0x4F, 0x75, 0x74, 0x67,
						0x6F, 0x69, 0x6E, 0x67, 0x20,
						0x49, 0x6E, 0x74, 0x65, 0x72,
						0x6E, 0x61, 0x74, 0x69, 0x6F,
						0x6E, 0x61, 0x6C, 0x20, 0x43,
						0x61, 0x6C, 0x6C, 0x73, 0x8F,
						0x11, 0xF9, 0x43, 0x4C, 0x49,
						0x20, 0x50, 0x72, 0x65, 0x73,
						0x65, 0x6E, 0x74, 0x61, 0x74,
						0x69, 0x6F, 0x6E };

static unsigned char select_item_141[] = { 0xD0, 0x22, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0B, 0x53, 0x65, 0x6C,
						0x65, 0x63, 0x74, 0x20, 0x49,
						0x74, 0x65, 0x6D, 0x8F, 0x04,
						0x11, 0x4F, 0x6E, 0x65, 0x8F,
						0x04, 0x12, 0x54, 0x77, 0x6F };

static unsigned char select_item_151[] = { 0xD0, 0x81, 0xFD, 0x81, 0x03, 0x01,
						0x24, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x81, 0xED, 0x54,
						0x68, 0x65, 0x20, 0x53, 0x49,
						0x4D, 0x20, 0x73, 0x68, 0x61,
						0x6C, 0x6C, 0x20, 0x73, 0x75,
						0x70, 0x70, 0x6C, 0x79, 0x20,
						0x61, 0x20, 0x73, 0x65, 0x74,
						0x20, 0x6F, 0x66, 0x20, 0x69,
						0x74, 0x65, 0x6D, 0x73, 0x20,
						0x66, 0x72, 0x6F, 0x6D, 0x20,
						0x77, 0x68, 0x69, 0x63, 0x68,
						0x20, 0x74, 0x68, 0x65, 0x20,
						0x75, 0x73, 0x65, 0x72, 0x20,
						0x6D, 0x61, 0x79, 0x20, 0x63,
						0x68, 0x6F, 0x6F, 0x73, 0x65,
						0x20, 0x6F, 0x6E, 0x65, 0x2E,
						0x20, 0x45, 0x61, 0x63, 0x68,
						0x20, 0x69, 0x74, 0x65, 0x6D,
						0x20, 0x63, 0x6F, 0x6D, 0x70,
						0x72, 0x69, 0x73, 0x65, 0x73,
						0x20, 0x61, 0x20, 0x73, 0x68,
						0x6F, 0x72, 0x74, 0x20, 0x69,
						0x64, 0x65, 0x6E, 0x74, 0x69,
						0x66, 0x69, 0x65, 0x72, 0x20,
						0x28, 0x75, 0x73, 0x65, 0x64,
						0x20, 0x74, 0x6F, 0x20, 0x69,
						0x6E, 0x64, 0x69, 0x63, 0x61,
						0x74, 0x65, 0x20, 0x74, 0x68,
						0x65, 0x20, 0x73, 0x65, 0x6C,
						0x65, 0x63, 0x74, 0x69, 0x6F,
						0x6E, 0x29, 0x20, 0x61, 0x6E,
						0x64, 0x20, 0x61, 0x20, 0x74,
						0x65, 0x78, 0x74, 0x20, 0x73,
						0x74, 0x72, 0x69, 0x6E, 0x67,
						0x2E, 0x20, 0x4F, 0x70, 0x74,
						0x69, 0x6F, 0x6E, 0x61, 0x6C,
						0x6C, 0x79, 0x20, 0x74, 0x68,
						0x65, 0x20, 0x53, 0x49, 0x4D,
						0x20, 0x6D, 0x61, 0x79, 0x20,
						0x69, 0x6E, 0x63, 0x6C, 0x75,
						0x64, 0x65, 0x20, 0x61, 0x6E,
						0x20, 0x61, 0x6C, 0x70, 0x68,
						0x61, 0x20, 0x69, 0x64, 0x65,
						0x6E, 0x74, 0x69, 0x66, 0x69,
						0x65, 0x72, 0x2E, 0x20, 0x54,
						0x68, 0x65, 0x20, 0x61, 0x6C,
						0x70, 0x68, 0x61, 0x20, 0x69,
						0x64, 0x65, 0x6E, 0x74, 0x69,
						0x66, 0x69, 0x65, 0x72, 0x20,
						0x69, 0x8F, 0x02, 0x01, 0x59 };

static unsigned char select_item_161[] = { 0xD0, 0x81, 0xF3, 0x81, 0x03, 0x01,
						0x24, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x0A, 0x30, 0x4C,
						0x61, 0x72, 0x67, 0x65, 0x4D,
						0x65, 0x6E, 0x75, 0x8F, 0x1D,
						0xFF, 0x31, 0x20, 0x43, 0x61,
						0x6C, 0x6C, 0x20, 0x46, 0x6F,
						0x72, 0x77, 0x61, 0x72, 0x64,
						0x20, 0x55, 0x6E, 0x63, 0x6F,
						0x6E, 0x64, 0x69, 0x74, 0x69,
						0x6F, 0x6E, 0x61, 0x6C, 0x8F,
						0x1C, 0xFE, 0x32, 0x20, 0x43,
						0x61, 0x6C, 0x6C, 0x20, 0x46,
						0x6F, 0x72, 0x77, 0x61, 0x72,
						0x64, 0x20, 0x4F, 0x6E, 0x20,
						0x55, 0x73, 0x65, 0x72, 0x20,
						0x42, 0x75, 0x73, 0x79, 0x8F,
						0x1B, 0xFD, 0x33, 0x20, 0x43,
						0x61, 0x6C, 0x6C, 0x20, 0x46,
						0x6F, 0x72, 0x77, 0x61, 0x72,
						0x64, 0x20, 0x4F, 0x6E, 0x20,
						0x4E, 0x6F, 0x20, 0x52, 0x65,
						0x70, 0x6C, 0x79, 0x8F, 0x25,
						0xFC, 0x34, 0x20, 0x43, 0x61,
						0x6C, 0x6C, 0x20, 0x46, 0x6F,
						0x72, 0x77, 0x61, 0x72, 0x64,
						0x20, 0x4F, 0x6E, 0x20, 0x55,
						0x73, 0x65, 0x72, 0x20, 0x4E,
						0x6F, 0x74, 0x20, 0x52, 0x65,
						0x61, 0x63, 0x68, 0x61, 0x62,
						0x6C, 0x65, 0x8F, 0x20, 0xFB,
						0x35, 0x20, 0x42, 0x61, 0x72,
						0x72, 0x69, 0x6E, 0x67, 0x20,
						0x4F, 0x66, 0x20, 0x41, 0x6C,
						0x6C, 0x20, 0x4F, 0x75, 0x74,
						0x67, 0x6F, 0x69, 0x6E, 0x67,
						0x20, 0x43, 0x61, 0x6C, 0x6C,
						0x73, 0x8F, 0x24, 0xFA, 0x36,
						0x20, 0x42, 0x61, 0x72, 0x72,
						0x69, 0x6E, 0x67, 0x20, 0x4F,
						0x66, 0x20, 0x41, 0x6C, 0x6C,
						0x20, 0x4F, 0x75, 0x74, 0x67,
						0x6F, 0x69, 0x6E, 0x67, 0x20,
						0x49, 0x6E, 0x74, 0x20, 0x43,
						0x61, 0x6C, 0x6C, 0x73, 0x8F,
						0x13, 0xF9, 0x37, 0x20, 0x43,
						0x4C, 0x49, 0x20, 0x50, 0x72,
						0x65, 0x73, 0x65, 0x6E, 0x74,
						0x61, 0x74, 0x69, 0x6F, 0x6E };

static unsigned char select_item_211[] = { 0xD0, 0x39, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0x18, 0x03,
						0x13, 0x10, 0x26 };

static unsigned char select_item_311[] = { 0xD0, 0x37, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0x90, 0x01,
						0x02 };

static unsigned char select_item_411[] = { 0xD0, 0x34, 0x81, 0x03, 0x01, 0x24,
						0x80, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33 };

static unsigned char select_item_511[] = { 0xD0, 0x3E, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0x9E, 0x02,
						0x01, 0x01, 0x9F, 0x04, 0x01,
						0x05, 0x05, 0x05 };

static unsigned char select_item_521[] = { 0xD0, 0x3E, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33, 0x9E, 0x02,
						0x00, 0x01, 0x9F, 0x04, 0x00,
						0x05, 0x05, 0x05 };

static unsigned char select_item_611[] = { 0xD0, 0x34, 0x81, 0x03, 0x01, 0x24,
						0x03, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33 };

static unsigned char select_item_621[] = { 0xD0, 0x34, 0x81, 0x03, 0x01, 0x24,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32, 0x8F,
						0x07, 0x03, 0x49, 0x74, 0x65,
						0x6D, 0x20, 0x33 };

static unsigned char select_item_711[] = { 0xD0, 0x2B, 0x81, 0x03, 0x01, 0x24,
						0x04, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0E, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x8F, 0x07, 0x01, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x31,
						0x8F, 0x07, 0x02, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x32 };

static unsigned char select_item_811[] = { 0xD0, 0x30, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0A, 0x3C, 0x54, 0x49,
						0x4D, 0x45, 0x2D, 0x4F, 0x55,
						0x54, 0x3E, 0x8F, 0x07, 0x01,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x31, 0x8F, 0x07, 0x02, 0x49,
						0x74, 0x65, 0x6D, 0x20, 0x32,
						0x8F, 0x07, 0x03, 0x49, 0x74,
						0x65, 0x6D, 0x20, 0x33 };

static unsigned char select_item_911[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x31, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x31, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x00, 0xB4, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char select_item_912[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x32, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x33, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x34 };

static unsigned char select_item_921[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x31, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x31, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x01, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x01, 0xB4, 0x00, 0x06,
						0x01, 0xB4 };

static unsigned char select_item_922[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x32, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x33, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x34 };

static unsigned char select_item_931[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x31, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x31, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x02, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x02, 0xB4, 0x00, 0x06,
						0x02, 0xB4 };

static unsigned char select_item_932[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x32, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x33, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x34 };

static unsigned char select_item_941[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x31, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x31, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x04, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x04, 0xB4, 0x00, 0x06,
						0x04, 0xB4 };

static unsigned char select_item_942[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x32, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x33, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x34, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x00, 0xB4, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char select_item_943[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x33, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x35, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x36 };

static unsigned char select_item_951[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x31, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x31, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x08, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x08, 0xB4, 0x00, 0x06,
						0x08, 0xB4 };

static unsigned char select_item_952[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x32, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x33, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x34, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x00, 0xB4, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char select_item_953[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x33, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x35, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x36 };

static unsigned char select_item_961[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x31, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x31, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x10, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x10, 0xB4, 0x00, 0x06,
						0x10, 0xB4 };

static unsigned char select_item_962[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x32, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x33, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x34, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x00, 0xB4, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char select_item_963[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x33, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x35, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x36 };

static unsigned char select_item_971[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x31, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x31, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x20, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x20, 0xB4, 0x00, 0x06,
						0x20, 0xB4 };

static unsigned char select_item_972[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x32, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x33, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x34, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x00, 0xB4, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char select_item_973[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x33, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x35, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x36 };

static unsigned char select_item_981[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x31, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x31, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x40, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x40, 0xB4, 0x00, 0x06,
						0x40, 0xB4 };

static unsigned char select_item_982[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x32, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x33, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x34, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x00, 0xB4, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char select_item_983[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x33, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x35, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x36 };

static unsigned char select_item_991[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x31, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x31, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x80, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x80, 0xB4, 0x00, 0x06,
						0x80, 0xB4 };

static unsigned char select_item_992[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x32, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x33, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x34, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x00, 0xB4, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char select_item_993[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x33, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x35, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x36 };

static unsigned char select_item_9101[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x31, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x31, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4, 0xD1, 0x08, 0x00,
						0x06, 0x00, 0xB4, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char select_item_9102[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x54, 0x6F, 0x6F,
						0x6C, 0x6B, 0x69, 0x74, 0x20,
						0x53, 0x65, 0x6C, 0x65, 0x63,
						0x74, 0x20, 0x32, 0x8F, 0x07,
						0x01, 0x49, 0x74, 0x65, 0x6D,
						0x20, 0x33, 0x8F, 0x07, 0x02,
						0x49, 0x74, 0x65, 0x6D, 0x20,
						0x34 };

static unsigned char select_item_1011[] = { 0xD0, 0x7E, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x19, 0x80, 0x04, 0x17,
						0x04, 0x14, 0x04, 0x20, 0x04,
						0x10, 0x04, 0x12, 0x04, 0x21,
						0x04, 0x22, 0x04, 0x12, 0x04,
						0x23, 0x04, 0x19, 0x04, 0x22,
						0x04, 0x15, 0x8F, 0x1C, 0x01,
						0x80, 0x04, 0x17, 0x04, 0x14,
						0x04, 0x20, 0x04, 0x10, 0x04,
						0x12, 0x04, 0x21, 0x04, 0x22,
						0x04, 0x12, 0x04, 0x23, 0x04,
						0x19, 0x04, 0x22, 0x04, 0x15,
						0x00, 0x31, 0x8F, 0x1C, 0x02,
						0x80, 0x04, 0x17, 0x04, 0x14,
						0x04, 0x20, 0x04, 0x10, 0x04,
						0x12, 0x04, 0x21, 0x04, 0x22,
						0x04, 0x12, 0x04, 0x23, 0x04,
						0x19, 0x04, 0x22, 0x04, 0x15,
						0x00, 0x32, 0x8F, 0x1C, 0x03,
						0x80, 0x04, 0x17, 0x04, 0x14,
						0x04, 0x20, 0x04, 0x10, 0x04,
						0x12, 0x04, 0x21, 0x04, 0x22,
						0x04, 0x12, 0x04, 0x23, 0x04,
						0x19, 0x04, 0x22, 0x04, 0x15,
						0x00, 0x33 };

static unsigned char select_item_1021[] = { 0xD0, 0x53, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0F, 0x81, 0x0C, 0x08,
						0x97, 0x94, 0xA0, 0x90, 0x92,
						0xA1, 0xA2, 0x92, 0xA3, 0x99,
						0xA2, 0x95, 0x8F, 0x11, 0x01,
						0x81, 0x0D, 0x08, 0x97, 0x94,
						0xA0, 0x90, 0x92, 0xA1, 0xA2,
						0x92, 0xA3, 0x99, 0xA2, 0x95,
						0x31, 0x8F, 0x11, 0x02, 0x81,
						0x0D, 0x08, 0x97, 0x94, 0xA0,
						0x90, 0x92, 0xA1, 0xA2, 0x92,
						0xA3, 0x99, 0xA2, 0x95, 0x32,
						0x8F, 0x11, 0x03, 0x81, 0x0D,
						0x08, 0x97, 0x94, 0xA0, 0x90,
						0x92, 0xA1, 0xA2, 0x92, 0xA3,
						0x99, 0xA2, 0x95, 0x33 };

static unsigned char select_item_1031[] = { 0xD0, 0x57, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x10, 0x82, 0x0C, 0x04,
						0x10, 0x87, 0x84, 0x90, 0x80,
						0x82, 0x91, 0x92, 0x82, 0x93,
						0x89, 0x92, 0x85, 0x8F, 0x12,
						0x01, 0x82, 0x0D, 0x04, 0x10,
						0x87, 0x84, 0x90, 0x80, 0x82,
						0x91, 0x92, 0x82, 0x93, 0x89,
						0x92, 0x85, 0x31, 0x8F, 0x12,
						0x02, 0x82, 0x0D, 0x04, 0x10,
						0x87, 0x84, 0x90, 0x80, 0x82,
						0x91, 0x92, 0x82, 0x93, 0x89,
						0x92, 0x85, 0x32, 0x8F, 0x12,
						0x03, 0x82, 0x0D, 0x04, 0x10,
						0x87, 0x84, 0x90, 0x80, 0x82,
						0x91, 0x92, 0x82, 0x93, 0x89,
						0x92, 0x85, 0x33 };

static unsigned char select_item_1111[] = { 0xD0, 0x3E, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x0B, 0x80, 0x5D, 0xE5,
						0x51, 0x77, 0x7B, 0xB1, 0x90,
						0x09, 0x62, 0xE9, 0x8F, 0x08,
						0x01, 0x80, 0x98, 0x79, 0x76,
						0xEE, 0x4E, 0x00, 0x8F, 0x08,
						0x02, 0x80, 0x98, 0x79, 0x76,
						0xEE, 0x4E, 0x8C, 0x8F, 0x08,
						0x03, 0x80, 0x98, 0x79, 0x76,
						0xEE, 0x4E, 0x09, 0x8F, 0x08,
						0x04, 0x80, 0x98, 0x79, 0x76,
						0xEE, 0x56, 0xDB };

static unsigned char select_item_1211[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x09, 0x80, 0x00, 0x38,
						0x00, 0x30, 0x30, 0xEB, 0x00,
						0x30, 0x8F, 0x0A, 0x01, 0x80,
						0x00, 0x38, 0x00, 0x30, 0x30,
						0xEB, 0x00, 0x31, 0x8F, 0x0A,
						0x02, 0x80, 0x00, 0x38, 0x00,
						0x30, 0x30, 0xEB, 0x00, 0x32,
						0x8F, 0x0A, 0x03, 0x80, 0x00,
						0x38, 0x00, 0x30, 0x30, 0xEB,
						0x00, 0x33 };

static unsigned char select_item_1221[] = { 0xD0, 0x30, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x07, 0x81, 0x04, 0x61,
						0x38, 0x31, 0xEB, 0x30, 0x8F,
						0x08, 0x01, 0x81, 0x04, 0x61,
						0x38, 0x31, 0xEB, 0x31, 0x8F,
						0x08, 0x02, 0x81, 0x04, 0x61,
						0x38, 0x31, 0xEB, 0x32, 0x8F,
						0x08, 0x03, 0x81, 0x04, 0x61,
						0x38, 0x31, 0xEB, 0x33 };

static unsigned char select_item_1231[] = { 0xD0, 0x34, 0x81, 0x03, 0x01, 0x24,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0x85, 0x08, 0x82, 0x04, 0x30,
						0xA0, 0x38, 0x32, 0xCB, 0x30,
						0x8F, 0x09, 0x01, 0x82, 0x04,
						0x30, 0xA0, 0x38, 0x32, 0xCB,
						0x31, 0x8F, 0x09, 0x02, 0x82,
						0x04, 0x30, 0xA0, 0x38, 0x32,
						0xCB, 0x32, 0x8F, 0x09, 0x03,
						0x82, 0x04, 0x30, 0xA0, 0x38,
						0x32, 0xCB, 0x33 };

static struct select_item_test select_item_data_111 = {
	.pdu = select_item_111,
	.pdu_len = sizeof(select_item_111),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
		{ .id = 4, .text = "Item 4" },
	}
};

static struct select_item_test select_item_data_121 = {
	.pdu = select_item_121,
	.pdu_len = sizeof(select_item_121),
	.qualifier = 0x00,
	.alpha_id = "LargeMenu1",
	.items = {
		{ .id = 0x50, .text = "Zero" },
		{ .id = 0x4F, .text = "One" },
		{ .id = 0x4E, .text = "Two" },
		{ .id = 0x4D, .text = "Three" },
		{ .id = 0x4C, .text = "Four" },
		{ .id = 0x4B, .text = "Five" },
		{ .id = 0x4A, .text = "Six" },
		{ .id = 0x49, .text = "Seven" },
		{ .id = 0x48, .text = "Eight" },
		{ .id = 0x47, .text = "Nine" },
		{ .id = 0x46, .text = "Alpha" },
		{ .id = 0x45, .text = "Bravo" },
		{ .id = 0x44, .text = "Charlie" },
		{ .id = 0x43, .text = "Delta" },
		{ .id = 0x42, .text = "Echo" },
		{ .id = 0x41, .text = "Fox-trot" },
		{ .id = 0x40, .text = "Black" },
		{ .id = 0x3F, .text = "Brown" },
		{ .id = 0x3E, .text = "Red" },
		{ .id = 0x3D, .text = "Orange" },
		{ .id = 0x3C, .text = "Yellow" },
		{ .id = 0x3B, .text = "Green" },
		{ .id = 0x3A, .text = "Blue" },
		{ .id = 0x39, .text = "Violet" },
		{ .id = 0x38, .text = "Grey" },
		{ .id = 0x37, .text = "White" },
		{ .id = 0x36, .text = "milli" },
		{ .id = 0x35, .text = "micro" },
		{ .id = 0x34, .text = "nano" },
		{ .id = 0x33, .text = "pico" },
	}
};

static struct select_item_test select_item_data_131 = {
	.pdu = select_item_131,
	.pdu_len = sizeof(select_item_131),
	.qualifier = 0x00,
	.alpha_id = "LargeMenu2",
	.items = {
		{ .id = 0xFF, .text = "Call Forwarding Unconditional" },
		{ .id = 0xFE, .text = "Call Forwarding On User Busy" },
		{ .id = 0xFD, .text = "Call Forwarding On No Reply" },
		{ .id = 0xFC, .text = "Call Forwarding On User Not Reachable" },
		{ .id = 0xFB, .text = "Barring Of All Outgoing Calls" },
		{ .id = 0xFA,
			.text = "Barring Of All Outgoing International Calls" },
		{ .id = 0xF9, .text = "CLI Presentation" },
	}
};

static struct select_item_test select_item_data_141 = {
	.pdu = select_item_141,
	.pdu_len = sizeof(select_item_141),
	.qualifier = 0x00,
	.alpha_id = "Select Item",
	.items = {
		{ .id = 0x11, .text = "One" },
		{ .id = 0x12, .text = "Two" },
	}
};

static struct select_item_test select_item_data_151 = {
	.pdu = select_item_151,
	.pdu_len = sizeof(select_item_151),
	.qualifier = 0x00,
	.alpha_id = "The SIM shall supply a set of items from which the user "
		"may choose one. Each item comprises a short identifier (used "
		"to indicate the selection) and a text string. Optionally the "
		"SIM may include an alpha identifier. The alpha identifier i",
	.items = {
		{ .id = 0x01, .text = "Y" },
	}
};

static struct select_item_test select_item_data_161 = {
	.pdu = select_item_161,
	.pdu_len = sizeof(select_item_161),
	.qualifier = 0x00,
	.alpha_id = "0LargeMenu",
	.items = {
		{ .id = 0xFF, .text = "1 Call Forward Unconditional" },
		{ .id = 0xFE, .text = "2 Call Forward On User Busy" },
		{ .id = 0xFD, .text = "3 Call Forward On No Reply" },
		{ .id = 0xFC, .text = "4 Call Forward On User Not Reachable" },
		{ .id = 0xFB, .text = "5 Barring Of All Outgoing Calls" },
		{ .id = 0xFA, .text = "6 Barring Of All Outgoing Int Calls" },
		{ .id = 0xF9, .text = "7 CLI Presentation" },
	}
};

static struct select_item_test select_item_data_211 = {
	.pdu = select_item_211,
	.pdu_len = sizeof(select_item_211),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.next_act = {
		.list = { STK_COMMAND_TYPE_SEND_SMS,
				STK_COMMAND_TYPE_SETUP_CALL,
				STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO},
		.len = 3
	}
};

static struct select_item_test select_item_data_311 = {
	.pdu = select_item_311,
	.pdu_len = sizeof(select_item_311),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.item_id = 0x02
};

static struct select_item_test select_item_data_411 = {
	.pdu = select_item_411,
	.pdu_len = sizeof(select_item_411),
	.qualifier = 0x80,
	.alpha_id = "Toolkit Select",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	}
};

static struct select_item_test select_item_data_511 = {
	.pdu = select_item_511,
	.pdu_len = sizeof(select_item_511),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 1
	},
	.item_icon_id_list = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.list = { 5, 5, 5 },
		.len = 3
	}
};

static struct select_item_test select_item_data_521 = {
	.pdu = select_item_521,
	.pdu_len = sizeof(select_item_521),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 1
	},
	.item_icon_id_list = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.list = { 5, 5, 5 },
		.len = 3
	}
};

static struct select_item_test select_item_data_611 = {
	.pdu = select_item_611,
	.pdu_len = sizeof(select_item_611),
	.qualifier = 0x03,
	.alpha_id = "Toolkit Select",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	}
};

static struct select_item_test select_item_data_621 = {
	.pdu = select_item_621,
	.pdu_len = sizeof(select_item_621),
	.qualifier = 0x01,
	.alpha_id = "Toolkit Select",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	}
};

static struct select_item_test select_item_data_711 = {
	.pdu = select_item_711,
	.pdu_len = sizeof(select_item_711),
	.qualifier = 0x04,
	.alpha_id = "Toolkit Select",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	}
};

static struct select_item_test select_item_data_811 = {
	.pdu = select_item_811,
	.pdu_len = sizeof(select_item_811),
	.qualifier = 0x00,
	.alpha_id = "<TIME-OUT>",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
		{ .id = 3, .text = "Item 3" },
	}
};

static struct select_item_test select_item_data_911 = {
	.pdu = select_item_911,
	.pdu_len = sizeof(select_item_911),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Select 1</span>"
		"</div>",
};

static struct select_item_test select_item_data_912 = {
	.pdu = select_item_912,
	.pdu_len = sizeof(select_item_912),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 2",
	.items = {
		{ .id = 1, .text = "Item 3" },
		{ .id = 2, .text = "Item 4" },
	}
};

static struct select_item_test select_item_data_921 = {
	.pdu = select_item_921,
	.pdu_len = sizeof(select_item_921),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x01, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x01, 0xB4, 0x00, 0x06, 0x01, 0xB4 }
	},
	.html = "<div style=\"text-align: center;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Select 1</span>"
		"</div>",
};

static struct select_item_test select_item_data_922 = {
	.pdu = select_item_922,
	.pdu_len = sizeof(select_item_922),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 2",
	.items = {
		{ .id = 1, .text = "Item 3" },
		{ .id = 2, .text = "Item 4" },
	}
};

static struct select_item_test select_item_data_931 = {
	.pdu = select_item_931,
	.pdu_len = sizeof(select_item_931),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x02, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x02, 0xB4, 0x00, 0x06, 0x02, 0xB4 }
	},
	.html = "<div style=\"text-align: right;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Select 1</span>"
		"</div>"
};

static struct select_item_test select_item_data_932 = {
	.pdu = select_item_932,
	.pdu_len = sizeof(select_item_932),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 2",
	.items = {
		{ .id = 1, .text = "Item 3" },
		{ .id = 2, .text = "Item 4" },
	}
};

static struct select_item_test select_item_data_941 = {
	.pdu = select_item_941,
	.pdu_len = sizeof(select_item_941),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x04, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x04, 0xB4, 0x00, 0x06, 0x04, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"big;color: #347235;background-color: #FFFF00;\">"
		"Toolkit Select 1</span></div>",
};

static struct select_item_test select_item_data_942 = {
	.pdu = select_item_942,
	.pdu_len = sizeof(select_item_942),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 2",
	.items = {
		{ .id = 1, .text = "Item 3" },
		{ .id = 2, .text = "Item 4" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Select 2</span>"
		"</div>",
};

static struct select_item_test select_item_data_943 = {
	.pdu = select_item_943,
	.pdu_len = sizeof(select_item_943),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 3",
	.items = {
		{ .id = 1, .text = "Item 5" },
		{ .id = 2, .text = "Item 6" },
	}
};

static struct select_item_test select_item_data_951 = {
	.pdu = select_item_951,
	.pdu_len = sizeof(select_item_951),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x08, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x08, 0xB4, 0x00, 0x06, 0x08, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"small;color: #347235;background-color: #FFFF00;\">"
		"Toolkit Select 1</span></div>",
};

static struct select_item_test select_item_data_952 = {
	.pdu = select_item_952,
	.pdu_len = sizeof(select_item_952),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 2",
	.items = {
		{ .id = 1, .text = "Item 3" },
		{ .id = 2, .text = "Item 4" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Select 2</span>"
		"</div>",
};

static struct select_item_test select_item_data_953 = {
	.pdu = select_item_953,
	.pdu_len = sizeof(select_item_953),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 3",
	.items = {
		{ .id = 1, .text = "Item 5" },
		{ .id = 2, .text = "Item 6" },
	}
};

static struct select_item_test select_item_data_961 = {
	.pdu = select_item_961,
	.pdu_len = sizeof(select_item_961),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x10, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x10, 0xB4, 0x00, 0x06, 0x10, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-weight: "
		"bold;color: #347235;background-color: #FFFF00;\">"
		"Toolkit Select 1</span></div>",
};

static struct select_item_test select_item_data_962 = {
	.pdu = select_item_962,
	.pdu_len = sizeof(select_item_962),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 2",
	.items = {
		{ .id = 1, .text = "Item 3" },
		{ .id = 2, .text = "Item 4" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Select 2</span>"
		"</div>",
};

static struct select_item_test select_item_data_963 = {
	.pdu = select_item_963,
	.pdu_len = sizeof(select_item_963),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 3",
	.items = {
		{ .id = 1, .text = "Item 5" },
		{ .id = 2, .text = "Item 6" },
	}
};

static struct select_item_test select_item_data_971 = {
	.pdu = select_item_971,
	.pdu_len = sizeof(select_item_971),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x20, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x20, 0xB4, 0x00, 0x06, 0x20, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-style: "
		"italic;color: #347235;background-color: #FFFF00;\">"
		"Toolkit Select 1</span></div>"
};

static struct select_item_test select_item_data_972 = {
	.pdu = select_item_972,
	.pdu_len = sizeof(select_item_972),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 2",
	.items = {
		{ .id = 1, .text = "Item 3" },
		{ .id = 2, .text = "Item 4" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Select 2</span>"
		"</div>",
};

static struct select_item_test select_item_data_973 = {
	.pdu = select_item_973,
	.pdu_len = sizeof(select_item_973),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 3",
	.items = {
		{ .id = 1, .text = "Item 5" },
		{ .id = 2, .text = "Item 6" },
	}
};

static struct select_item_test select_item_data_981 = {
	.pdu = select_item_981,
	.pdu_len = sizeof(select_item_981),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x40, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x40, 0xB4, 0x00, 0x06, 0x40, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span "
		"style=\"text-decoration: underline;color: #347235;"
		"background-color: #FFFF00;\">Toolkit Select 1</span></div>",
};

static struct select_item_test select_item_data_982 = {
	.pdu = select_item_982,
	.pdu_len = sizeof(select_item_982),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 2",
	.items = {
		{ .id = 1, .text = "Item 3" },
		{ .id = 2, .text = "Item 4" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Select 2</span>"
		"</div>",
};

static struct select_item_test select_item_data_983 = {
	.pdu = select_item_983,
	.pdu_len = sizeof(select_item_983),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 3",
	.items = {
		{ .id = 1, .text = "Item 5" },
		{ .id = 2, .text = "Item 6" },
	}
};

static struct select_item_test select_item_data_991 = {
	.pdu = select_item_991,
	.pdu_len = sizeof(select_item_991),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x80, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x80, 0xB4, 0x00, 0x06, 0x80, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span "
		"style=\"text-decoration: line-through;color: #347235;"
		"background-color: #FFFF00;\">Toolkit Select 1</span></div>",
};

static struct select_item_test select_item_data_992 = {
	.pdu = select_item_992,
	.pdu_len = sizeof(select_item_992),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 2",
	.items = {
		{ .id = 1, .text = "Item 3" },
		{ .id = 2, .text = "Item 4" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Select 2</span>"
		"</div>",
};

static struct select_item_test select_item_data_993 = {
	.pdu = select_item_993,
	.pdu_len = sizeof(select_item_993),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 3",
	.items = {
		{ .id = 1, .text = "Item 5" },
		{ .id = 2, .text = "Item 6" },
	}
};

static struct select_item_test select_item_data_9101 = {
	.pdu = select_item_9101,
	.pdu_len = sizeof(select_item_9101),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 1",
	.items = {
		{ .id = 1, .text = "Item 1" },
		{ .id = 2, .text = "Item 2" },
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.item_text_attr_list = {
		.len = 8,
		.list = { 0x00, 0x06, 0x00, 0xB4, 0x00, 0x06, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Toolkit Select 1</span>"
		"</div>",
};

static struct select_item_test select_item_data_9102 = {
	.pdu = select_item_9102,
	.pdu_len = sizeof(select_item_9102),
	.qualifier = 0x00,
	.alpha_id = "Toolkit Select 2",
	.items = {
		{ .id = 1, .text = "Item 3" },
		{ .id = 2, .text = "Item 4" },
	}
};

static struct select_item_test select_item_data_1011 = {
	.pdu = select_item_1011,
	.pdu_len = sizeof(select_item_1011),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.items = {
		{ .id = 1, .text = "ЗДРАВСТВУЙТЕ1" },
		{ .id = 2, .text = "ЗДРАВСТВУЙТЕ2" },
		{ .id = 3, .text = "ЗДРАВСТВУЙТЕ3" },
	}
};

static struct select_item_test select_item_data_1021 = {
	.pdu = select_item_1021,
	.pdu_len = sizeof(select_item_1021),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.items = {
		{ .id = 1, .text = "ЗДРАВСТВУЙТЕ1" },
		{ .id = 2, .text = "ЗДРАВСТВУЙТЕ2" },
		{ .id = 3, .text = "ЗДРАВСТВУЙТЕ3" },
	}
};

static struct select_item_test select_item_data_1031 = {
	.pdu = select_item_1031,
	.pdu_len = sizeof(select_item_1031),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.items = {
		{ .id = 1, .text = "ЗДРАВСТВУЙТЕ1" },
		{ .id = 2, .text = "ЗДРАВСТВУЙТЕ2" },
		{ .id = 3, .text = "ЗДРАВСТВУЙТЕ3" },
	}
};

static struct select_item_test select_item_data_1111 = {
	.pdu = select_item_1111,
	.pdu_len = sizeof(select_item_1111),
	.qualifier = 0x00,
	.alpha_id = "工具箱选择",
	.items = {
		{ .id = 1, .text = "项目一" },
		{ .id = 2, .text = "项目二" },
		{ .id = 3, .text = "项目三" },
		{ .id = 4, .text = "项目四" },
	}
};

static struct select_item_test select_item_data_1211 = {
	.pdu = select_item_1211,
	.pdu_len = sizeof(select_item_1211),
	.qualifier = 0x00,
	.alpha_id = "80ル0",
	.items = {
		{ .id = 1, .text = "80ル1" },
		{ .id = 2, .text = "80ル2" },
		{ .id = 3, .text = "80ル3" },
	}
};

static struct select_item_test select_item_data_1221 = {
	.pdu = select_item_1221,
	.pdu_len = sizeof(select_item_1221),
	.qualifier = 0x00,
	.alpha_id = "81ル0",
	.items = {
		{ .id = 1, .text = "81ル1" },
		{ .id = 2, .text = "81ル2" },
		{ .id = 3, .text = "81ル3" },
	}
};

static struct select_item_test select_item_data_1231 = {
	.pdu = select_item_1231,
	.pdu_len = sizeof(select_item_1231),
	.qualifier = 0x00,
	.alpha_id = "82ル0",
	.items = {
		{ .id = 1, .text = "82ル1" },
		{ .id = 2, .text = "82ル2" },
		{ .id = 3, .text = "82ル3" },
	}
};

static void test_select_item(gconstpointer data)
{
	const struct select_item_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_SELECT_ITEM);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_alpha_id(command->select_item.alpha_id, test->alpha_id);
	check_items(command->select_item.items, test->items);
	check_items_next_action_indicator(&command->select_item.next_act,
						&test->next_act);
	check_item_id(command->select_item.item_id, test->item_id);
	check_icon_id(&command->select_item.icon_id, &test->icon_id);
	check_item_icon_id_list(&command->select_item.item_icon_id_list,
					&test->item_icon_id_list);
	check_text_attr(&command->select_item.text_attr, &test->text_attr);
	check_item_text_attribute_list(
				&command->select_item.item_text_attr_list,
				&test->item_text_attr_list);
	check_text_attr_html(&command->select_item.text_attr,
				command->select_item.alpha_id, test->html);
	check_frame_id(&command->select_item.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct send_sms_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *alpha_id;
	struct sms_test gsm_sms;
	struct stk_common_byte_array cdma_sms;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
};

/* 3GPP TS 31.124 Section 27.22.4.10.1.4.2 */
static unsigned char send_sms_111[] = { 0xD0, 0x37, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x07, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x53, 0x4D, 0x86,
						0x09, 0x91, 0x11, 0x22, 0x33,
						0x44, 0x55, 0x66, 0x77, 0xF8,
						0x8B, 0x18, 0x01, 0x00, 0x09,
						0x91, 0x10, 0x32, 0x54, 0x76,
						0xF8, 0x40, 0xF4, 0x0C, 0x54,
						0x65, 0x73, 0x74, 0x20, 0x4D,
						0x65, 0x73, 0x73, 0x61, 0x67,
						0x65 };

static unsigned char send_sms_121[] = { 0xD0, 0x32, 0x81, 0x03, 0x01, 0x13,
						0x01, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x07, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x53, 0x4D, 0x86,
						0x09, 0x91, 0x11, 0x22, 0x33,
						0x44, 0x55, 0x66, 0x77, 0xF8,
						0x8B, 0x13, 0x01, 0x00, 0x09,
						0x91, 0x10, 0x32, 0x54, 0x76,
						0xF8, 0x40, 0xF4, 0x07, 0x53,
						0x65, 0x6E, 0x64, 0x20, 0x53,
						0x4D };

static unsigned char send_sms_131[] = { 0xD0, 0x3D, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0D, 0x53, 0x68, 0x6F,
						0x72, 0x74, 0x20, 0x4D, 0x65,
						0x73, 0x73, 0x61, 0x67, 0x65,
						0x86, 0x09, 0x91, 0x11, 0x22,
						0x33, 0x44, 0x55, 0x66, 0x77,
						0xF8, 0x8B, 0x18, 0x01, 0x00,
						0x09, 0x91, 0x10, 0x32, 0x54,
						0x76, 0xF8, 0x40, 0xF0, 0x0D,
						0x53, 0xF4, 0x5B, 0x4E, 0x07,
						0x35, 0xCB, 0xF3, 0x79, 0xF8,
						0x5C, 0x06 };

static unsigned char send_sms_141[] = { 0xD0, 0x81, 0xFD, 0x81, 0x03, 0x01,
						0x13, 0x01, 0x82, 0x02, 0x81,
						0x83, 0x85, 0x38, 0x54, 0x68,
						0x65, 0x20, 0x61, 0x64, 0x64,
						0x72, 0x65, 0x73, 0x73, 0x20,
						0x64, 0x61, 0x74, 0x61, 0x20,
						0x6F, 0x62, 0x6A, 0x65, 0x63,
						0x74, 0x20, 0x68, 0x6F, 0x6C,
						0x64, 0x73, 0x20, 0x74, 0x68,
						0x65, 0x20, 0x52, 0x50, 0x11,
						0x44, 0x65, 0x73, 0x74, 0x69,
						0x6E, 0x61, 0x74, 0x69, 0x6F,
						0x6E, 0x11, 0x41, 0x64, 0x64,
						0x72, 0x65, 0x73, 0x73, 0x86,
						0x09, 0x91, 0x11, 0x22, 0x33,
						0x44, 0x55, 0x66, 0x77, 0xF8,
						0x8B, 0x81, 0xAC, 0x01, 0x00,
						0x09, 0x91, 0x10, 0x32, 0x54,
						0x76, 0xF8, 0x40, 0xF4, 0xA0,
						0x54, 0x77, 0x6F, 0x20, 0x74,
						0x79, 0x70, 0x65, 0x73, 0x20,
						0x61, 0x72, 0x65, 0x20, 0x64,
						0x65, 0x66, 0x69, 0x6E, 0x65,
						0x64, 0x3A, 0x20, 0x2D, 0x20,
						0x41, 0x20, 0x73, 0x68, 0x6F,
						0x72, 0x74, 0x20, 0x6D, 0x65,
						0x73, 0x73, 0x61, 0x67, 0x65,
						0x20, 0x74, 0x6F, 0x20, 0x62,
						0x65, 0x20, 0x73, 0x65, 0x6E,
						0x74, 0x20, 0x74, 0x6F, 0x20,
						0x74, 0x68, 0x65, 0x20, 0x6E,
						0x65, 0x74, 0x77, 0x6F, 0x72,
						0x6B, 0x20, 0x69, 0x6E, 0x20,
						0x61, 0x6E, 0x20, 0x53, 0x4D,
						0x53, 0x2D, 0x53, 0x55, 0x42,
						0x4D, 0x49, 0x54, 0x20, 0x6D,
						0x65, 0x73, 0x73, 0x61, 0x67,
						0x65, 0x2C, 0x20, 0x6F, 0x72,
						0x20, 0x61, 0x6E, 0x20, 0x53,
						0x4D, 0x53, 0x2D, 0x43, 0x4F,
						0x4D, 0x4D, 0x41, 0x4E, 0x44,
						0x20, 0x6D, 0x65, 0x73, 0x73,
						0x61, 0x67, 0x65, 0x2C, 0x20,
						0x77, 0x68, 0x65, 0x72, 0x65,
						0x20, 0x74, 0x68, 0x65, 0x20,
						0x75, 0x73, 0x65, 0x72, 0x20,
						0x64, 0x61, 0x74, 0x61, 0x20,
						0x63, 0x61, 0x6E, 0x20, 0x62,
						0x65, 0x20, 0x70, 0x61, 0x73,
						0x73, 0x65, 0x64, 0x20, 0x74,
						0x72, 0x61, 0x6E, 0x73, 0x70 };

static unsigned char send_sms_151[] = { 0xD0, 0x81, 0xE9, 0x81, 0x03, 0x01,
						0x13, 0x00, 0x82, 0x02, 0x81,
						0x83, 0x85, 0x38, 0x54, 0x68,
						0x65, 0x20, 0x61, 0x64, 0x64,
						0x72, 0x65, 0x73, 0x73, 0x20,
						0x64, 0x61, 0x74, 0x61, 0x20,
						0x6F, 0x62, 0x6A, 0x65, 0x63,
						0x74, 0x20, 0x68, 0x6F, 0x6C,
						0x64, 0x73, 0x20, 0x74, 0x68,
						0x65, 0x20, 0x52, 0x50, 0x20,
						0x44, 0x65, 0x73, 0x74, 0x69,
						0x6E, 0x61, 0x74, 0x69, 0x6F,
						0x6E, 0x20, 0x41, 0x64, 0x64,
						0x72, 0x65, 0x73, 0x73, 0x86,
						0x09, 0x91, 0x11, 0x22, 0x33,
						0x44, 0x55, 0x66, 0x77, 0xF8,
						0x8B, 0x81, 0x98, 0x01, 0x00,
						0x09, 0x91, 0x10, 0x32, 0x54,
						0x76, 0xF8, 0x40, 0xF0, 0xA0,
						0xD4, 0xFB, 0x1B, 0x44, 0xCF,
						0xC3, 0xCB, 0x73, 0x50, 0x58,
						0x5E, 0x06, 0x91, 0xCB, 0xE6,
						0xB4, 0xBB, 0x4C, 0xD6, 0x81,
						0x5A, 0xA0, 0x20, 0x68, 0x8E,
						0x7E, 0xCB, 0xE9, 0xA0, 0x76,
						0x79, 0x3E, 0x0F, 0x9F, 0xCB,
						0x20, 0xFA, 0x1B, 0x24, 0x2E,
						0x83, 0xE6, 0x65, 0x37, 0x1D,
						0x44, 0x7F, 0x83, 0xE8, 0xE8,
						0x32, 0xC8, 0x5D, 0xA6, 0xDF,
						0xDF, 0xF2, 0x35, 0x28, 0xED,
						0x06, 0x85, 0xDD, 0xA0, 0x69,
						0x73, 0xDA, 0x9A, 0x56, 0x85,
						0xCD, 0x24, 0x15, 0xD4, 0x2E,
						0xCF, 0xE7, 0xE1, 0x73, 0x99,
						0x05, 0x7A, 0xCB, 0x41, 0x61,
						0x37, 0x68, 0xDA, 0x9C, 0xB6,
						0x86, 0xCF, 0x66, 0x33, 0xE8,
						0x24, 0x82, 0xDA, 0xE5, 0xF9,
						0x3C, 0x7C, 0x2E, 0xB3, 0x40,
						0x77, 0x74, 0x59, 0x5E, 0x06,
						0xD1, 0xD1, 0x65, 0x50, 0x7D,
						0x5E, 0x96, 0x83, 0xC8, 0x61,
						0x7A, 0x18, 0x34, 0x0E, 0xBB,
						0x41, 0xE2, 0x32, 0x08, 0x1E,
						0x9E, 0xCF, 0xCB, 0x64, 0x10,
						0x5D, 0x1E, 0x76, 0xCF, 0xE1 };

static unsigned char send_sms_161[] = { 0xD0, 0x81, 0xFD, 0x81, 0x03, 0x01,
						0x13, 0x00, 0x82, 0x02, 0x81,
						0x83, 0x85, 0x81, 0xE6, 0x54,
						0x77, 0x6F, 0x20, 0x74, 0x79,
						0x70, 0x65, 0x73, 0x20, 0x61,
						0x72, 0x65, 0x20, 0x64, 0x65,
						0x66, 0x69, 0x6E, 0x65, 0x64,
						0x3A, 0x20, 0x2D, 0x20, 0x41,
						0x20, 0x73, 0x68, 0x6F, 0x72,
						0x74, 0x20, 0x6D, 0x65, 0x73,
						0x73, 0x61, 0x67, 0x65, 0x20,
						0x74, 0x6F, 0x20, 0x62, 0x65,
						0x20, 0x73, 0x65, 0x6E, 0x74,
						0x20, 0x74, 0x6F, 0x20, 0x74,
						0x68, 0x65, 0x20, 0x6E, 0x65,
						0x74, 0x77, 0x6F, 0x72, 0x6B,
						0x20, 0x69, 0x6E, 0x20, 0x61,
						0x6E, 0x20, 0x53, 0x4D, 0x53,
						0x2D, 0x53, 0x55, 0x42, 0x4D,
						0x49, 0x54, 0x20, 0x6D, 0x65,
						0x73, 0x73, 0x61, 0x67, 0x65,
						0x2C, 0x20, 0x6F, 0x72, 0x20,
						0x61, 0x6E, 0x20, 0x53, 0x4D,
						0x53, 0x2D, 0x43, 0x4F, 0x4D,
						0x4D, 0x41, 0x4E, 0x44, 0x20,
						0x6D, 0x65, 0x73, 0x73, 0x61,
						0x67, 0x65, 0x2C, 0x20, 0x77,
						0x68, 0x65, 0x72, 0x65, 0x20,
						0x74, 0x68, 0x65, 0x20, 0x75,
						0x73, 0x65, 0x72, 0x20, 0x64,
						0x61, 0x74, 0x61, 0x20, 0x63,
						0x61, 0x6E, 0x20, 0x62, 0x65,
						0x20, 0x70, 0x61, 0x73, 0x73,
						0x65, 0x64, 0x20, 0x74, 0x72,
						0x61, 0x6E, 0x73, 0x70, 0x61,
						0x72, 0x65, 0x6E, 0x74, 0x6C,
						0x79, 0x3B, 0x20, 0x2D, 0x20,
						0x41, 0x20, 0x73, 0x68, 0x6F,
						0x72, 0x74, 0x20, 0x6D, 0x65,
						0x73, 0x73, 0x61, 0x67, 0x65,
						0x20, 0x74, 0x6F, 0x20, 0x62,
						0x65, 0x20, 0x73, 0x65, 0x6E,
						0x74, 0x20, 0x74, 0x6F, 0x20,
						0x74, 0x68, 0x65, 0x20, 0x6E,
						0x65, 0x74, 0x77, 0x6F, 0x72,
						0x6B, 0x20, 0x69, 0x6E, 0x20,
						0x61, 0x6E, 0x20, 0x53, 0x4D,
						0x53, 0x2D, 0x53, 0x55, 0x42,
						0x4D, 0x49, 0x54, 0x20, 0x8B,
						0x09, 0x01, 0x00, 0x02, 0x91,
						0x10, 0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_171[] = { 0xD0, 0x30, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x00, 0x86, 0x09, 0x91,
						0x11, 0x22, 0x33, 0x44, 0x55,
						0x66, 0x77, 0xF8, 0x8B, 0x18,
						0x01, 0x00, 0x09, 0x91, 0x10,
						0x32, 0x54, 0x76, 0xF8, 0x40,
						0xF4, 0x0C, 0x54, 0x65, 0x73,
						0x74, 0x20, 0x4D, 0x65, 0x73,
						0x73, 0x61, 0x67, 0x65 };

static unsigned char send_sms_181[] = { 0xD0, 0x2E, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x86, 0x09, 0x91, 0x11, 0x22,
						0x33, 0x44, 0x55, 0x66, 0x77,
						0xF8, 0x8B, 0x18, 0x01, 0x00,
						0x09, 0x91, 0x10, 0x32, 0x54,
						0x76, 0xF8, 0x40, 0xF4, 0x0C,
						0x54, 0x65, 0x73, 0x74, 0x20,
						0x4D, 0x65, 0x73, 0x73, 0x61,
						0x67, 0x65 };

static unsigned char send_sms_211[] = { 0xD0, 0x55, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x19, 0x80, 0x04, 0x17,
						0x04, 0x14, 0x04, 0x20, 0x04,
						0x10, 0x04, 0x12, 0x04, 0x21,
						0x04, 0x22, 0x04, 0x12, 0x04,
						0x23, 0x04, 0x19, 0x04, 0x22,
						0x04, 0x15, 0x86, 0x09, 0x91,
						0x11, 0x22, 0x33, 0x44, 0x55,
						0x66, 0x77, 0xF8, 0x8B, 0x24,
						0x01, 0x00, 0x09, 0x91, 0x10,
						0x32, 0x54, 0x76, 0xF8, 0x40,
						0x08, 0x18, 0x04, 0x17, 0x04,
						0x14, 0x04, 0x20, 0x04, 0x10,
						0x04, 0x12, 0x04, 0x21, 0x04,
						0x22, 0x04, 0x12, 0x04, 0x23,
						0x04, 0x19, 0x04, 0x22, 0x04,
						0x15 };

static unsigned char send_sms_212[] = { 0xD0, 0x4B, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0F, 0x81, 0x0C, 0x08,
						0x97, 0x94, 0xA0, 0x90, 0x92,
						0xA1, 0xA2, 0x92, 0xA3, 0x99,
						0xA2, 0x95, 0x86, 0x09, 0x91,
						0x11, 0x22, 0x33, 0x44, 0x55,
						0x66, 0x77, 0xF8, 0x8B, 0x24,
						0x01, 0x00, 0x09, 0x91, 0x10,
						0x32, 0x54, 0x76, 0xF8, 0x40,
						0x08, 0x18, 0x04, 0x17, 0x04,
						0x14, 0x04, 0x20, 0x04, 0x10,
						0x04, 0x12, 0x04, 0x21, 0x04,
						0x22, 0x04, 0x12, 0x04, 0x23,
						0x04, 0x19, 0x04, 0x22, 0x04,
						0x15 };

static unsigned char send_sms_213[] = { 0xD0, 0x4C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x82, 0x0C, 0x04,
						0x10, 0x87, 0x84, 0x90, 0x80,
						0x82, 0x91, 0x92, 0x82, 0x93,
						0x89, 0x92, 0x85, 0x86, 0x09,
						0x91, 0x11, 0x22, 0x33, 0x44,
						0x55, 0x66, 0x77, 0xF8, 0x8B,
						0x24, 0x01, 0x00, 0x09, 0x91,
						0x10, 0x32, 0x54, 0x76, 0xF8,
						0x40, 0x08, 0x18, 0x04, 0x17,
						0x04, 0x14, 0x04, 0x20, 0x04,
						0x10, 0x04, 0x12, 0x04, 0x21,
						0x04, 0x22, 0x04, 0x12, 0x04,
						0x23, 0x04, 0x19, 0x04, 0x22,
						0x04, 0x15 };

static unsigned char send_sms_311[] = { 0xD0, 0x3B, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x07, 0x4E, 0x4F, 0x20,
						0x49, 0x43, 0x4F, 0x4E, 0x86,
						0x09, 0x91, 0x11, 0x22, 0x33,
						0x44, 0x55, 0x66, 0x77, 0xF8,
						0x8B, 0x18, 0x01, 0x00, 0x09,
						0x91, 0x10, 0x32, 0x54, 0x76,
						0xF8, 0x40, 0xF4, 0x0C, 0x54,
						0x65, 0x73, 0x74, 0x20, 0x4D,
						0x65, 0x73, 0x73, 0x61, 0x67,
						0x65, 0x9E, 0x02, 0x00, 0x01 };

static unsigned char send_sms_321[] = { 0xD0, 0x3B, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x07, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x53, 0x4D, 0x86,
						0x09, 0x91, 0x11, 0x22, 0x33,
						0x44, 0x55, 0x66, 0x77, 0xF8,
						0x8B, 0x18, 0x01, 0x00, 0x09,
						0x91, 0x10, 0x32, 0x54, 0x76,
						0xF8, 0x40, 0xF4, 0x0C, 0x54,
						0x65, 0x73, 0x74, 0x20, 0x4D,
						0x65, 0x73, 0x73, 0x61, 0x67,
						0x65, 0x1E, 0x02, 0x01, 0x01 };

static unsigned char send_sms_411[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x00, 0xB4 };

static unsigned char send_sms_412[] = { 0xD0, 0x26, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_421[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x01, 0xB4 };

static unsigned char send_sms_422[] = { 0xD0, 0x26, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_431[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x02, 0xB4 };

static unsigned char send_sms_432[] = { 0xD0, 0x26, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_441[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x04, 0xB4 };

static unsigned char send_sms_442[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x00, 0xB4 };

static unsigned char send_sms_443[] = { 0xD0, 0x26, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_451[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x08, 0xB4 };

static unsigned char send_sms_452[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x00, 0xB4 };

static unsigned char send_sms_453[] = { 0xD0, 0x26, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_461[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x10, 0xB4 };

static unsigned char send_sms_462[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x00, 0xB4 };

static unsigned char send_sms_463[] = { 0xD0, 0x26, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_471[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x20, 0xB4 };

static unsigned char send_sms_472[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x00, 0xB4 };

static unsigned char send_sms_473[] = { 0xD0, 0x26, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_481[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x40, 0xB4 };

static unsigned char send_sms_482[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x00, 0xB4 };

static unsigned char send_sms_483[] = { 0xD0, 0x26, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_491[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x80, 0xB4 };

static unsigned char send_sms_492[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x00, 0xB4 };

static unsigned char send_sms_493[] = { 0xD0, 0x26, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_4101[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20, 0xD0,
						0x04, 0x00, 0x10, 0x00, 0xB4 };

static unsigned char send_sms_4102[] = { 0xD0, 0x26, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8B, 0x09,
						0x01, 0x00, 0x02, 0x91, 0x10,
						0x40, 0xF0, 0x01, 0x20 };

static unsigned char send_sms_511[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x05, 0x80, 0x4E, 0x2D,
						0x4E, 0x00, 0x86, 0x09, 0x91,
						0x11, 0x22, 0x33, 0x44, 0x55,
						0x66, 0x77, 0xF8, 0x8B, 0x10,
						0x01, 0x00, 0x09, 0x91, 0x10,
						0x32, 0x54, 0x76, 0xF8, 0x40,
						0x08, 0x04, 0x4E, 0x2D, 0x4E,
						0x00 };

static unsigned char send_sms_512[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x05, 0x81, 0x02, 0x9C,
						0xAD, 0x80, 0x86, 0x09, 0x91,
						0x11, 0x22, 0x33, 0x44, 0x55,
						0x66, 0x77, 0xF8, 0x8B, 0x10,
						0x01, 0x00, 0x09, 0x91, 0x10,
						0x32, 0x54, 0x76, 0xF8, 0x40,
						0x08, 0x04, 0x4E, 0x2D, 0x4E,
						0x00 };

static unsigned char send_sms_513[] = { 0xD0, 0x2E, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x06, 0x82, 0x02, 0x4E,
						0x00, 0xAD, 0x80, 0x86, 0x09,
						0x91, 0x11, 0x22, 0x33, 0x44,
						0x55, 0x66, 0x77, 0xF8, 0x8B,
						0x10, 0x01, 0x00, 0x09, 0x91,
						0x10, 0x32, 0x54, 0x76, 0xF8,
						0x40, 0x08, 0x04, 0x4E, 0x2D,
						0x4E, 0x00 };

static unsigned char send_sms_611[] = { 0xD0, 0x35, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x09, 0x80, 0x00, 0x38,
						0x00, 0x30, 0x30, 0xEB, 0x00,
						0x30, 0x86, 0x09, 0x91, 0x11,
						0x22, 0x33, 0x44, 0x55, 0x66,
						0x77, 0xF8, 0x8B, 0x14, 0x01,
						0x00, 0x09, 0x91, 0x10, 0x32,
						0x54, 0x76, 0xF8, 0x40, 0x08,
						0x08, 0x00, 0x38, 0x00, 0x30,
						0x30, 0xEB, 0x00, 0x31 };

static unsigned char send_sms_612[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x07, 0x81, 0x04, 0x61,
						0x38, 0x31, 0xEB, 0x31, 0x86,
						0x09, 0x91, 0x11, 0x22, 0x33,
						0x44, 0x55, 0x66, 0x77, 0xF8,
						0x8B, 0x14, 0x01, 0x00, 0x09,
						0x91, 0x10, 0x32, 0x54, 0x76,
						0xF8, 0x40, 0x08, 0x08, 0x00,
						0x38, 0x00, 0x30, 0x30, 0xEB,
						0x00, 0x32 };

static unsigned char send_sms_613[] = { 0xD0, 0x34, 0x81, 0x03, 0x01, 0x13,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x08, 0x82, 0x04, 0x30,
						0xA0, 0x38, 0x32, 0xCB, 0x32,
						0x86, 0x09, 0x91, 0x11, 0x22,
						0x33, 0x44, 0x55, 0x66, 0x77,
						0xF8, 0x8B, 0x14, 0x01, 0x00,
						0x09, 0x91, 0x10, 0x32, 0x54,
						0x76, 0xF8, 0x40, 0x08, 0x08,
						0x00, 0x38, 0x00, 0x30, 0x30,
						0xEB, 0x00, 0x33 };

static struct send_sms_test send_sms_data_111 = {
	.pdu = send_sms_111,
	.pdu_len = sizeof(send_sms_111),
	.qualifier = 0x00,
	.alpha_id = "Send SM",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0xF4,
			.udl = 12,
			.ud = "Test Message"
		} }
	}
};

static struct send_sms_test send_sms_data_121 = {
	.pdu = send_sms_121,
	.pdu_len = sizeof(send_sms_121),
	.qualifier = 0x01,
	.alpha_id = "Send SM",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 7,
			.ud = "Send SM"
		} }
	}
};

static struct send_sms_test send_sms_data_131 = {
	.pdu = send_sms_131,
	.pdu_len = sizeof(send_sms_131),
	.qualifier = 0x00,
	.alpha_id = "Short Message",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 13,
			.ud = "Short Message"
		} }
	},
};

static struct send_sms_test send_sms_data_141 = {
	.pdu = send_sms_141,
	.pdu_len = sizeof(send_sms_141),
	.qualifier = 0x01,
	.alpha_id = "The address data object holds the RP_Destination_Address",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 160,
			.ud = "Two types are defined: - A short message to be "
				"sent to the network in an SMS-SUBMIT message, "
				"or an SMS-COMMAND message, where the user "
				"data can be passed transp"
		} }
	}
};

static struct send_sms_test send_sms_data_151 = {
	.pdu = send_sms_151,
	.pdu_len = sizeof(send_sms_151),
	.qualifier = 0x00,
	.alpha_id = "The address data object holds the RP Destination Address",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 160,
			.ud = "Two types are defined: - A short message to be "
				"sent to the network in an SMS-SUBMIT message, "
				"or an SMS-COMMAND message, where the user "
				"data can be passed transp"
		} }
	}
};

/* There should be a space after alpha_id */
static struct send_sms_test send_sms_data_161 = {
	.pdu = send_sms_161,
	.pdu_len = sizeof(send_sms_161),
	.qualifier = 0x00,
	.alpha_id = "Two types are defined: - A short message to be sent to "
			"the network in an SMS-SUBMIT message, or an "
			"SMS-COMMAND message, where the user data can be "
			"passed transparently; - A short message to be sent "
			"to the network in an SMS-SUBMIT ",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

static struct send_sms_test send_sms_data_171 = {
	.pdu = send_sms_171,
	.pdu_len = sizeof(send_sms_171),
	.qualifier = 0x00,
	.alpha_id = "",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0xF4,
			.udl = 12,
			.ud = "Test Message"
		} }
	}
};

static struct send_sms_test send_sms_data_181 = {
	.pdu = send_sms_181,
	.pdu_len = sizeof(send_sms_181),
	.qualifier = 0x00,
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0xF4,
			.udl = 12,
			.ud = "Test Message"
		} }
	}
};

static struct send_sms_test send_sms_data_211 = {
	.pdu = send_sms_211,
	.pdu_len = sizeof(send_sms_211),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0x08,
			.udl = 24,
			.ud = "ЗДРАВСТВУЙТЕ"
		} }
	}
};

static struct send_sms_test send_sms_data_212 = {
	.pdu = send_sms_212,
	.pdu_len = sizeof(send_sms_212),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0x08,
			.udl = 24,
			.ud = "ЗДРАВСТВУЙТЕ"
		} }
	}
};

static struct send_sms_test send_sms_data_213 = {
	.pdu = send_sms_213,
	.pdu_len = sizeof(send_sms_213),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0x08,
			.udl = 24,
			.ud = "ЗДРАВСТВУЙТЕ"
		} }
	}
};

static struct send_sms_test send_sms_data_311 = {
	.pdu = send_sms_311,
	.pdu_len = sizeof(send_sms_311),
	.qualifier = 0x00,
	.alpha_id = "NO ICON",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0xF4,
			.udl = 12,
			.ud = "Test Message"
		} }
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct send_sms_test send_sms_data_321 = {
	.pdu = send_sms_321,
	.pdu_len = sizeof(send_sms_321),
	.qualifier = 0x00,
	.alpha_id = "Send SM",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0xF4,
			.udl = 12,
			.ud = "Test Message"
		} }
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct send_sms_test send_sms_data_411 = {
	.pdu = send_sms_411,
	.pdu_len = sizeof(send_sms_411),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_412 = {
	.pdu = send_sms_412,
	.pdu_len = sizeof(send_sms_412),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

static struct send_sms_test send_sms_data_421 = {
	.pdu = send_sms_421,
	.pdu_len = sizeof(send_sms_421),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x01, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_422 = {
	.pdu = send_sms_422,
	.pdu_len = sizeof(send_sms_422),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

static struct send_sms_test send_sms_data_431 = {
	.pdu = send_sms_431,
	.pdu_len = sizeof(send_sms_431),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x02, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_432 = {
	.pdu = send_sms_432,
	.pdu_len = sizeof(send_sms_432),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

static struct send_sms_test send_sms_data_441 = {
	.pdu = send_sms_441,
	.pdu_len = sizeof(send_sms_441),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x04, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_442 = {
	.pdu = send_sms_442,
	.pdu_len = sizeof(send_sms_442),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_443 = {
	.pdu = send_sms_443,
	.pdu_len = sizeof(send_sms_443),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

static struct send_sms_test send_sms_data_451 = {
	.pdu = send_sms_451,
	.pdu_len = sizeof(send_sms_451),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x08, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_452 = {
	.pdu = send_sms_452,
	.pdu_len = sizeof(send_sms_452),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_453 = {
	.pdu = send_sms_453,
	.pdu_len = sizeof(send_sms_453),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

static struct send_sms_test send_sms_data_461 = {
	.pdu = send_sms_461,
	.pdu_len = sizeof(send_sms_461),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x10, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_462 = {
	.pdu = send_sms_462,
	.pdu_len = sizeof(send_sms_462),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_463 = {
	.pdu = send_sms_463,
	.pdu_len = sizeof(send_sms_463),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

static struct send_sms_test send_sms_data_471 = {
	.pdu = send_sms_471,
	.pdu_len = sizeof(send_sms_471),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x20, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_472 = {
	.pdu = send_sms_472,
	.pdu_len = sizeof(send_sms_472),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_473 = {
	.pdu = send_sms_473,
	.pdu_len = sizeof(send_sms_473),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

static struct send_sms_test send_sms_data_481 = {
	.pdu = send_sms_481,
	.pdu_len = sizeof(send_sms_481),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x40, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_482 = {
	.pdu = send_sms_482,
	.pdu_len = sizeof(send_sms_482),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_483 = {
	.pdu = send_sms_483,
	.pdu_len = sizeof(send_sms_483),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

static struct send_sms_test send_sms_data_491 = {
	.pdu = send_sms_491,
	.pdu_len = sizeof(send_sms_491),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x80, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_492 = {
	.pdu = send_sms_492,
	.pdu_len = sizeof(send_sms_492),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_493 = {
	.pdu = send_sms_493,
	.pdu_len = sizeof(send_sms_493),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

static struct send_sms_test send_sms_data_4101 = {
	.pdu = send_sms_4101,
	.pdu_len = sizeof(send_sms_4101),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_sms_test send_sms_data_4102 = {
	.pdu = send_sms_4102,
	.pdu_len = sizeof(send_sms_4102),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.gsm_sms = {
		{}, SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "01",
			},
			.pid = 0x40,
			.dcs = 0xF0,
			.udl = 1,
			.ud = " "
		} }
	}
};

/* The TP-UDL should be 4, instead of 24 */
static struct send_sms_test send_sms_data_511 = {
	.pdu = send_sms_511,
	.pdu_len = sizeof(send_sms_511),
	.qualifier = 0x00,
	.alpha_id = "中一",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0x08,
			.udl = 4,
			.ud = "中一"
		} }
	}
};

/* The TP-UDL should be 4, instead of 24 */
static struct send_sms_test send_sms_data_512 = {
	.pdu = send_sms_512,
	.pdu_len = sizeof(send_sms_512),
	.qualifier = 0x00,
	.alpha_id = "中一",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0x08,
			.udl = 4,
			.ud = "中一"
		} }
	}
};

/* The TP-UDL should be 4, instead of 24 */
static struct send_sms_test send_sms_data_513 = {
	.pdu = send_sms_513,
	.pdu_len = sizeof(send_sms_513),
	.qualifier = 0x00,
	.alpha_id = "中一",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0x08,
			.udl = 4,
			.ud = "中一"
		} }
	}
};

static struct send_sms_test send_sms_data_611 = {
	.pdu = send_sms_611,
	.pdu_len = sizeof(send_sms_611),
	.qualifier = 0x00,
	.alpha_id = "80ル0",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0x08,
			.udl = 8,
			.ud = "80ル1"
		} }
	}
};

static struct send_sms_test send_sms_data_612 = {
	.pdu = send_sms_612,
	.pdu_len = sizeof(send_sms_612),
	.qualifier = 0x00,
	.alpha_id = "81ル1",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0x08,
			.udl = 8,
			.ud = "80ル2"
		} }
	}
};

static struct send_sms_test send_sms_data_613 = {
	.pdu = send_sms_613,
	.pdu_len = sizeof(send_sms_613),
	.qualifier = 0x00,
	.alpha_id = "82ル2",
	.gsm_sms = {
		{
			.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
			.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
			.address = "112233445566778",
		},
		SMS_TYPE_SUBMIT,
		{.submit = {
			.mr = 0x00,
			.daddr = {
				.number_type = SMS_NUMBER_TYPE_INTERNATIONAL,
				.numbering_plan = SMS_NUMBERING_PLAN_ISDN,
				.address = "012345678",
			},
			.pid = 0x40,
			.dcs = 0x08,
			.udl = 8,
			.ud = "80ル3"
		} }
	}
};

static void test_send_sms(gconstpointer data)
{
	const struct send_sms_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_SEND_SMS);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_NETWORK);

	check_alpha_id(command->send_sms.alpha_id, test->alpha_id);
	check_gsm_sms(&command->send_sms.gsm_sms, &test->gsm_sms);
	check_cdma_sms_tpdu(&command->send_sms.cdma_sms, &test->cdma_sms);
	check_icon_id(&command->send_sms.icon_id, &test->icon_id);
	check_text_attr(&command->send_sms.text_attr, &test->text_attr);
	check_frame_id(&command->send_sms.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct send_ss_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *alpha_id;
	struct stk_ss ss;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
};

static unsigned char send_ss_111[] = { 0xD0, 0x29, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0C, 0x43, 0x61, 0x6C,
						0x6C, 0x20, 0x46, 0x6F, 0x72,
						0x77, 0x61, 0x72, 0x64, 0x89,
						0x10, 0x91, 0xAA, 0x12, 0x0A,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0x21, 0x43, 0x65, 0x87, 0xA9,
						0x01, 0xFB };

static unsigned char send_ss_141[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0C, 0x43, 0x61, 0x6C,
						0x6C, 0x20, 0x46, 0x6F, 0x72,
						0x77, 0x61, 0x72, 0x64, 0x89,
						0x14, 0x91, 0xAA, 0x12, 0x0A,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0x21, 0x43, 0x65, 0xA7, 0x11,
						0xFB };

static unsigned char send_ss_151[] = { 0xD0, 0x81, 0xFD, 0x81, 0x03, 0x01,
						0x11, 0x00, 0x82, 0x02, 0x81,
						0x83, 0x85, 0x81, 0xEB, 0x45,
						0x76, 0x65, 0x6E, 0x20, 0x69,
						0x66, 0x20, 0x74, 0x68, 0x65,
						0x20, 0x46, 0x69, 0x78, 0x65,
						0x64, 0x20, 0x44, 0x69, 0x61,
						0x6C, 0x6C, 0x69, 0x6E, 0x67,
						0x20, 0x4E, 0x75, 0x6D, 0x62,
						0x65, 0x72, 0x20, 0x73, 0x65,
						0x72, 0x76, 0x69, 0x63, 0x65,
						0x20, 0x69, 0x73, 0x20, 0x65,
						0x6E, 0x61, 0x62, 0x6C, 0x65,
						0x64, 0x2C, 0x20, 0x74, 0x68,
						0x65, 0x20, 0x73, 0x75, 0x70,
						0x70, 0x6C, 0x65, 0x6D, 0x65,
						0x6E, 0x74, 0x61, 0x72, 0x79,
						0x20, 0x73, 0x65, 0x72, 0x76,
						0x69, 0x63, 0x65, 0x20, 0x63,
						0x6F, 0x6E, 0x74, 0x72, 0x6F,
						0x6C, 0x20, 0x73, 0x74, 0x72,
						0x69, 0x6E, 0x67, 0x20, 0x69,
						0x6E, 0x63, 0x6C, 0x75, 0x64,
						0x65, 0x64, 0x20, 0x69, 0x6E,
						0x20, 0x74, 0x68, 0x65, 0x20,
						0x53, 0x45, 0x4E, 0x44, 0x20,
						0x53, 0x53, 0x20, 0x70, 0x72,
						0x6F, 0x61, 0x63, 0x74, 0x69,
						0x76, 0x65, 0x20, 0x63, 0x6F,
						0x6D, 0x6D, 0x61, 0x6E, 0x64,
						0x20, 0x73, 0x68, 0x61, 0x6C,
						0x6C, 0x20, 0x6E, 0x6F, 0x74,
						0x20, 0x62, 0x65, 0x20, 0x63,
						0x68, 0x65, 0x63, 0x6B, 0x65,
						0x64, 0x20, 0x61, 0x67, 0x61,
						0x69, 0x6E, 0x73, 0x74, 0x20,
						0x74, 0x68, 0x6F, 0x73, 0x65,
						0x20, 0x6F, 0x66, 0x20, 0x74,
						0x68, 0x65, 0x20, 0x46, 0x44,
						0x4E, 0x20, 0x6C, 0x69, 0x73,
						0x74, 0x2E, 0x20, 0x55, 0x70,
						0x6F, 0x6E, 0x20, 0x72, 0x65,
						0x63, 0x65, 0x69, 0x76, 0x69,
						0x6E, 0x67, 0x20, 0x74, 0x68,
						0x69, 0x73, 0x20, 0x63, 0x6F,
						0x6D, 0x6D, 0x61, 0x6E, 0x64,
						0x2C, 0x20, 0x74, 0x68, 0x65,
						0x20, 0x4D, 0x45, 0x20, 0x73,
						0x68, 0x61, 0x6C, 0x6C, 0x20,
						0x64, 0x65, 0x63, 0x69, 0x89,
						0x04, 0xFF, 0xBA, 0x13, 0xFB };

static unsigned char send_ss_161[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x00, 0x89, 0x10, 0x91,
						0xAA, 0x12, 0x0A, 0x21, 0x43,
						0x65, 0x87, 0x09, 0x21, 0x43,
						0x65, 0x87, 0xA9, 0x01, 0xFB };

static unsigned char send_ss_211[] = { 0xD0, 0x2B, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0A, 0x42, 0x61, 0x73,
						0x69, 0x63, 0x20, 0x49, 0x63,
						0x6F, 0x6E, 0x89, 0x10, 0x91,
						0xAA, 0x12, 0x0A, 0x21, 0x43,
						0x65, 0x87, 0x09, 0x21, 0x43,
						0x65, 0x87, 0xA9, 0x01, 0xFB,
						0x9E, 0x02, 0x00, 0x01 };

static unsigned char send_ss_221[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x43, 0x6F, 0x6C,
						0x6F, 0x75, 0x72, 0x20, 0x49,
						0x63, 0x6F, 0x6E, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0x9E, 0x02, 0x00, 0x02 };

static unsigned char send_ss_231[] = { 0xD0, 0x2B, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0A, 0x42, 0x61, 0x73,
						0x69, 0x63, 0x20, 0x49, 0x63,
						0x6F, 0x6E, 0x89, 0x10, 0x91,
						0xAA, 0x12, 0x0A, 0x21, 0x43,
						0x65, 0x87, 0x09, 0x21, 0x43,
						0x65, 0x87, 0xA9, 0x01, 0xFB,
						0x9E, 0x02, 0x01, 0x01 };

static unsigned char send_ss_241[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x89, 0x0E, 0x91, 0xAA, 0x12,
						0x0A, 0x21, 0x43, 0x65, 0x87,
						0x09, 0x21, 0x43, 0x65, 0x87,
						0xB9, 0x9E, 0x02, 0x01, 0x01 };

static unsigned char send_ss_311[] = { 0xD0, 0x36, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x19, 0x80, 0x04, 0x17,
						0x04, 0x14, 0x04, 0x20, 0x04,
						0x10, 0x04, 0x12, 0x04, 0x21,
						0x04, 0x22, 0x04, 0x12, 0x04,
						0x23, 0x04, 0x19, 0x04, 0x22,
						0x04, 0x15, 0x89, 0x10, 0x91,
						0xAA, 0x12, 0x0A, 0x21, 0x43,
						0x65, 0x87, 0x09, 0x21, 0x43,
						0x65, 0x87, 0xA9, 0x01, 0xFB };

static unsigned char send_ss_411[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char send_ss_412[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB };

static unsigned char send_ss_421[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x01, 0xB4 };

static unsigned char send_ss_422[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB };

static unsigned char send_ss_431[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x02, 0xB4 };

static unsigned char send_ss_432[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB };

static unsigned char send_ss_441[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x04, 0xB4 };

static unsigned char send_ss_442[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char send_ss_443[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB };

static unsigned char send_ss_451[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x08, 0xB4 };

static unsigned char send_ss_452[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char send_ss_453[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB };

static unsigned char send_ss_461[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x10, 0xB4 };

static unsigned char send_ss_462[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char send_ss_463[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB };

static unsigned char send_ss_471[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x20, 0xB4 };

static unsigned char send_ss_472[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char send_ss_473[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB };

static unsigned char send_ss_481[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x40, 0xB4 };

static unsigned char send_ss_482[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char send_ss_483[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB };

static unsigned char send_ss_491[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x80, 0xB4 };

static unsigned char send_ss_492[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char send_ss_493[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB };

static unsigned char send_ss_4101[] = { 0xD0, 0x33, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char send_ss_4102[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x89, 0x10,
						0x91, 0xAA, 0x12, 0x0A, 0x21,
						0x43, 0x65, 0x87, 0x09, 0x21,
						0x43, 0x65, 0x87, 0xA9, 0x01,
						0xFB };

static unsigned char send_ss_511[] = { 0xD0, 0x22, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x05, 0x80, 0x4F, 0x60,
						0x59, 0x7D, 0x89, 0x10, 0x91,
						0xAA, 0x12, 0x0A, 0x21, 0x43,
						0x65, 0x87, 0x09, 0x21, 0x43,
						0x65, 0x87, 0xA9, 0x01, 0xFB };

static unsigned char send_ss_611[] = { 0xD0, 0x20, 0x81, 0x03, 0x01, 0x11,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x03, 0x80, 0x30, 0xEB,
						0x89, 0x10, 0x91, 0xAA, 0x12,
						0x0A, 0x21, 0x43, 0x65, 0x87,
						0x09, 0x21, 0x43, 0x65, 0x87,
						0xA9, 0x01, 0xFB };

static struct send_ss_test send_ss_data_111 = {
	.pdu = send_ss_111,
	.pdu_len = sizeof(send_ss_111),
	.qualifier = 0x00,
	.alpha_id = "Call Forward",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_141 = {
	.pdu = send_ss_141,
	.pdu_len = sizeof(send_ss_141),
	.qualifier = 0x00,
	.alpha_id = "Call Forward",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*0123456789012345678901234567*11#"
	}
};

static struct send_ss_test send_ss_data_151 = {
	.pdu = send_ss_151,
	.pdu_len = sizeof(send_ss_151),
	.qualifier = 0x00,
	.alpha_id = "Even if the Fixed Dialling Number service is enabled, the "
		"supplementary service control string included in the SEND SS "
		"proactive command shall not be checked against those of the "
		"FDN list. Upon receiving this command, the ME shall deci",
	.ss = {
		.ton_npi = 0xFF,
		.ss = "*#31#"
	}
};

static struct send_ss_test send_ss_data_161 = {
	.pdu = send_ss_161,
	.pdu_len = sizeof(send_ss_161),
	.qualifier = 0x00,
	.alpha_id = "",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_211 = {
	.pdu = send_ss_211,
	.pdu_len = sizeof(send_ss_211),
	.qualifier = 0x00,
	.alpha_id = "Basic Icon",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct send_ss_test send_ss_data_221 = {
	.pdu = send_ss_221,
	.pdu_len = sizeof(send_ss_221),
	.qualifier = 0x00,
	.alpha_id = "Colour Icon",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct send_ss_test send_ss_data_231 = {
	.pdu = send_ss_231,
	.pdu_len = sizeof(send_ss_231),
	.qualifier = 0x00,
	.alpha_id = "Basic Icon",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct send_ss_test send_ss_data_241 = {
	.pdu = send_ss_241,
	.pdu_len = sizeof(send_ss_241),
	.qualifier = 0x00,
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789#"
	},
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct send_ss_test send_ss_data_311 = {
	.pdu = send_ss_311,
	.pdu_len = sizeof(send_ss_311),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_411 = {
	.pdu = send_ss_411,
	.pdu_len = sizeof(send_ss_411),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_412 = {
	.pdu = send_ss_412,
	.pdu_len = sizeof(send_ss_412),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_421 = {
	.pdu = send_ss_421,
	.pdu_len = sizeof(send_ss_421),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x01, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_422 = {
	.pdu = send_ss_422,
	.pdu_len = sizeof(send_ss_422),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_431 = {
	.pdu = send_ss_431,
	.pdu_len = sizeof(send_ss_431),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x02, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_432 = {
	.pdu = send_ss_432,
	.pdu_len = sizeof(send_ss_432),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_441 = {
	.pdu = send_ss_441,
	.pdu_len = sizeof(send_ss_441),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x04, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_442 = {
	.pdu = send_ss_442,
	.pdu_len = sizeof(send_ss_442),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_443 = {
	.pdu = send_ss_443,
	.pdu_len = sizeof(send_ss_443),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_451 = {
	.pdu = send_ss_451,
	.pdu_len = sizeof(send_ss_451),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x08, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_452 = {
	.pdu = send_ss_452,
	.pdu_len = sizeof(send_ss_452),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_453 = {
	.pdu = send_ss_453,
	.pdu_len = sizeof(send_ss_453),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_461 = {
	.pdu = send_ss_461,
	.pdu_len = sizeof(send_ss_461),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x10, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_462 = {
	.pdu = send_ss_462,
	.pdu_len = sizeof(send_ss_462),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_463 = {
	.pdu = send_ss_463,
	.pdu_len = sizeof(send_ss_463),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_471 = {
	.pdu = send_ss_471,
	.pdu_len = sizeof(send_ss_471),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x20, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_472 = {
	.pdu = send_ss_472,
	.pdu_len = sizeof(send_ss_472),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_473 = {
	.pdu = send_ss_473,
	.pdu_len = sizeof(send_ss_473),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_481 = {
	.pdu = send_ss_481,
	.pdu_len = sizeof(send_ss_481),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x40, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_482 = {
	.pdu = send_ss_482,
	.pdu_len = sizeof(send_ss_482),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_483 = {
	.pdu = send_ss_483,
	.pdu_len = sizeof(send_ss_483),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_491 = {
	.pdu = send_ss_491,
	.pdu_len = sizeof(send_ss_491),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x80, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_492 = {
	.pdu = send_ss_492,
	.pdu_len = sizeof(send_ss_492),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_493 = {
	.pdu = send_ss_493,
	.pdu_len = sizeof(send_ss_493),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_4101 = {
	.pdu = send_ss_4101,
	.pdu_len = sizeof(send_ss_4101),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ss_test send_ss_data_4102 = {
	.pdu = send_ss_4102,
	.pdu_len = sizeof(send_ss_4102),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_511 = {
	.pdu = send_ss_511,
	.pdu_len = sizeof(send_ss_511),
	.qualifier = 0x00,
	.alpha_id = "你好",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static struct send_ss_test send_ss_data_611 = {
	.pdu = send_ss_611,
	.pdu_len = sizeof(send_ss_611),
	.qualifier = 0x00,
	.alpha_id = "ル",
	.ss = {
		.ton_npi = 0x91,
		.ss = "**21*01234567890123456789*10#"
	}
};

static void test_send_ss(gconstpointer data)
{
	const struct send_ss_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_SEND_SS);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_NETWORK);

	check_alpha_id(command->send_ss.alpha_id, test->alpha_id);
	check_ss(&command->send_ss.ss, &test->ss);
	check_icon_id(&command->send_ss.icon_id, &test->icon_id);
	check_text_attr(&command->send_ss.text_attr, &test->text_attr);
	check_frame_id(&command->send_ss.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct send_ussd_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *alpha_id;
	char *ussd;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
};

static unsigned char send_ussd_111[] = { 0xD0, 0x50, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0A, 0x37, 0x2D, 0x62,
						0x69, 0x74, 0x20, 0x55, 0x53,
						0x53, 0x44, 0x8A, 0x39, 0xF0,
						0x41, 0xE1, 0x90, 0x58, 0x34,
						0x1E, 0x91, 0x49, 0xE5, 0x92,
						0xD9, 0x74, 0x3E, 0xA1, 0x51,
						0xE9, 0x94, 0x5A, 0xB5, 0x5E,
						0xB1, 0x59, 0x6D, 0x2B, 0x2C,
						0x1E, 0x93, 0xCB, 0xE6, 0x33,
						0x3A, 0xAD, 0x5E, 0xB3, 0xDB,
						0xEE, 0x37, 0x3C, 0x2E, 0x9F,
						0xD3, 0xEB, 0xF6, 0x3B, 0x3E,
						0xAF, 0x6F, 0xC5, 0x64, 0x33,
						0x5A, 0xCD, 0x76, 0xC3, 0xE5,
						0x60 };

static unsigned char send_ussd_121[] = { 0xD0, 0x58, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0A, 0x38, 0x2D, 0x62,
						0x69, 0x74, 0x20, 0x55, 0x53,
						0x53, 0x44, 0x8A, 0x41, 0x44,
						0x41, 0x42, 0x43, 0x44, 0x45,
						0x46, 0x47, 0x48, 0x49, 0x4A,
						0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
						0x50, 0x51, 0x52, 0x53, 0x54,
						0x55, 0x56, 0x57, 0x58, 0x59,
						0x5A, 0x2D, 0x61, 0x62, 0x63,
						0x64, 0x65, 0x66, 0x67, 0x68,
						0x69, 0x6A, 0x6B, 0x6C, 0x6D,
						0x6E, 0x6F, 0x70, 0x71, 0x72,
						0x73, 0x74, 0x75, 0x76, 0x77,
						0x78, 0x79, 0x7A, 0x2D, 0x31,
						0x32, 0x33, 0x34, 0x35, 0x36,
						0x37, 0x38, 0x39, 0x30 };

static unsigned char send_ussd_131[] = { 0xD0, 0x2F, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x09, 0x55, 0x43, 0x53,
						0x32, 0x20, 0x55, 0x53, 0x53,
						0x44, 0x8A, 0x19, 0x48, 0x04,
						0x17, 0x04, 0x14, 0x04, 0x20,
						0x04, 0x10, 0x04, 0x12, 0x04,
						0x21, 0x04, 0x22, 0x04, 0x12,
						0x04, 0x23, 0x04, 0x19, 0x04,
						0x22, 0x04, 0x15 };

static unsigned char send_ussd_161[] = { 0xD0, 0x81, 0xFD, 0x81, 0x03, 0x01,
						0x12, 0x00, 0x82, 0x02, 0x81,
						0x83, 0x85, 0x81, 0xB6, 0x6F,
						0x6E, 0x63, 0x65, 0x20, 0x61,
						0x20, 0x52, 0x45, 0x4C, 0x45,
						0x41, 0x53, 0x45, 0x20, 0x43,
						0x4F, 0x4D, 0x50, 0x4C, 0x45,
						0x54, 0x45, 0x20, 0x6D, 0x65,
						0x73, 0x73, 0x61, 0x67, 0x65,
						0x20, 0x63, 0x6F, 0x6E, 0x74,
						0x61, 0x69, 0x6E, 0x69, 0x6E,
						0x67, 0x20, 0x74, 0x68, 0x65,
						0x20, 0x55, 0x53, 0x53, 0x44,
						0x20, 0x52, 0x65, 0x74, 0x75,
						0x72, 0x6E, 0x20, 0x52, 0x65,
						0x73, 0x75, 0x6C, 0x74, 0x20,
						0x6D, 0x65, 0x73, 0x73, 0x61,
						0x67, 0x65, 0x20, 0x6E, 0x6F,
						0x74, 0x20, 0x63, 0x6F, 0x6E,
						0x74, 0x61, 0x69, 0x6E, 0x69,
						0x6E, 0x67, 0x20, 0x61, 0x6E,
						0x20, 0x65, 0x72, 0x72, 0x6F,
						0x72, 0x20, 0x68, 0x61, 0x73,
						0x20, 0x62, 0x65, 0x65, 0x6E,
						0x20, 0x72, 0x65, 0x63, 0x65,
						0x69, 0x76, 0x65, 0x64, 0x20,
						0x66, 0x72, 0x6F, 0x6D, 0x20,
						0x74, 0x68, 0x65, 0x20, 0x6E,
						0x65, 0x74, 0x77, 0x6F, 0x72,
						0x6B, 0x2C, 0x20, 0x74, 0x68,
						0x65, 0x20, 0x4D, 0x45, 0x20,
						0x73, 0x68, 0x61, 0x6C, 0x6C,
						0x20, 0x69, 0x6E, 0x66, 0x6F,
						0x72, 0x6D, 0x20, 0x74, 0x68,
						0x65, 0x20, 0x53, 0x49, 0x4D,
						0x20, 0x74, 0x68, 0x61, 0x74,
						0x20, 0x74, 0x68, 0x65, 0x20,
						0x63, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x68, 0x61,
						0x73, 0x8A, 0x39, 0xF0, 0x41,
						0xE1, 0x90, 0x58, 0x34, 0x1E,
						0x91, 0x49, 0xE5, 0x92, 0xD9,
						0x74, 0x3E, 0xA1, 0x51, 0xE9,
						0x94, 0x5A, 0xB5, 0x5E, 0xB1,
						0x59, 0x6D, 0x2B, 0x2C, 0x1E,
						0x93, 0xCB, 0xE6, 0x33, 0x3A,
						0xAD, 0x5E, 0xB3, 0xDB, 0xEE,
						0x37, 0x3C, 0x2E, 0x9F, 0xD3,
						0xEB, 0xF6, 0x3B, 0x3E, 0xAF,
						0x6F, 0xC5, 0x64, 0x33, 0x5A,
						0xCD, 0x76, 0xC3, 0xE5, 0x60 };

static unsigned char send_ussd_171[] = { 0xD0, 0x44, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x8A, 0x39, 0xF0, 0x41, 0xE1,
						0x90, 0x58, 0x34, 0x1E, 0x91,
						0x49, 0xE5, 0x92, 0xD9, 0x74,
						0x3E, 0xA1, 0x51, 0xE9, 0x94,
						0x5A, 0xB5, 0x5E, 0xB1, 0x59,
						0x6D, 0x2B, 0x2C, 0x1E, 0x93,
						0xCB, 0xE6, 0x33, 0x3A, 0xAD,
						0x5E, 0xB3, 0xDB, 0xEE, 0x37,
						0x3C, 0x2E, 0x9F, 0xD3, 0xEB,
						0xF6, 0x3B, 0x3E, 0xAF, 0x6F,
						0xC5, 0x64, 0x33, 0x5A, 0xCD,
						0x76, 0xC3, 0xE5, 0x60 };

static unsigned char send_ussd_181[] = { 0xD0, 0x46, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x00, 0x8A, 0x39, 0xF0,
						0x41, 0xE1, 0x90, 0x58, 0x34,
						0x1E, 0x91, 0x49, 0xE5, 0x92,
						0xD9, 0x74, 0x3E, 0xA1, 0x51,
						0xE9, 0x94, 0x5A, 0xB5, 0x5E,
						0xB1, 0x59, 0x6D, 0x2B, 0x2C,
						0x1E, 0x93, 0xCB, 0xE6, 0x33,
						0x3A, 0xAD, 0x5E, 0xB3, 0xDB,
						0xEE, 0x37, 0x3C, 0x2E, 0x9F,
						0xD3, 0xEB, 0xF6, 0x3B, 0x3E,
						0xAF, 0x6F, 0xC5, 0x64, 0x33,
						0x5A, 0xCD, 0x76, 0xC3, 0xE5,
						0x60 };

static unsigned char send_ussd_211[] = { 0xD0, 0x54, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0A, 0x42, 0x61, 0x73,
						0x69, 0x63, 0x20, 0x49, 0x63,
						0x6F, 0x6E, 0x8A, 0x39, 0xF0,
						0x41, 0xE1, 0x90, 0x58, 0x34,
						0x1E, 0x91, 0x49, 0xE5, 0x92,
						0xD9, 0x74, 0x3E, 0xA1, 0x51,
						0xE9, 0x94, 0x5A, 0xB5, 0x5E,
						0xB1, 0x59, 0x6D, 0x2B, 0x2C,
						0x1E, 0x93, 0xCB, 0xE6, 0x33,
						0x3A, 0xAD, 0x5E, 0xB3, 0xDB,
						0xEE, 0x37, 0x3C, 0x2E, 0x9F,
						0xD3, 0xEB, 0xF6, 0x3B, 0x3E,
						0xAF, 0x6F, 0xC5, 0x64, 0x33,
						0x5A, 0xCD, 0x76, 0xC3, 0xE5,
						0x60, 0x9E, 0x02, 0x00, 0x01 };

static unsigned char send_ussd_221[] = { 0xD0, 0x54, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0A, 0x43, 0x6F, 0x6C,
						0x6F, 0x72, 0x20, 0x49, 0x63,
						0x6F, 0x6E, 0x8A, 0x39, 0xF0,
						0x41, 0xE1, 0x90, 0x58, 0x34,
						0x1E, 0x91, 0x49, 0xE5, 0x92,
						0xD9, 0x74, 0x3E, 0xA1, 0x51,
						0xE9, 0x94, 0x5A, 0xB5, 0x5E,
						0xB1, 0x59, 0x6D, 0x2B, 0x2C,
						0x1E, 0x93, 0xCB, 0xE6, 0x33,
						0x3A, 0xAD, 0x5E, 0xB3, 0xDB,
						0xEE, 0x37, 0x3C, 0x2E, 0x9F,
						0xD3, 0xEB, 0xF6, 0x3B, 0x3E,
						0xAF, 0x6F, 0xC5, 0x64, 0x33,
						0x5A, 0xCD, 0x76, 0xC3, 0xE5,
						0x60, 0x9E, 0x02, 0x00, 0x02 };

static unsigned char send_ussd_231[] = { 0xD0, 0x54, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0A, 0x42, 0x61, 0x73,
						0x69, 0x63, 0x20, 0x49, 0x63,
						0x6F, 0x6E, 0x8A, 0x39, 0xF0,
						0x41, 0xE1, 0x90, 0x58, 0x34,
						0x1E, 0x91, 0x49, 0xE5, 0x92,
						0xD9, 0x74, 0x3E, 0xA1, 0x51,
						0xE9, 0x94, 0x5A, 0xB5, 0x5E,
						0xB1, 0x59, 0x6D, 0x2B, 0x2C,
						0x1E, 0x93, 0xCB, 0xE6, 0x33,
						0x3A, 0xAD, 0x5E, 0xB3, 0xDB,
						0xEE, 0x37, 0x3C, 0x2E, 0x9F,
						0xD3, 0xEB, 0xF6, 0x3B, 0x3E,
						0xAF, 0x6F, 0xC5, 0x64, 0x33,
						0x5A, 0xCD, 0x76, 0xC3, 0xE5,
						0x60, 0x9E, 0x02, 0x01, 0x01 };

static unsigned char send_ussd_241[] = { 0xD0, 0x48, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x8A, 0x39, 0xF0, 0x41, 0xE1,
						0x90, 0x58, 0x34, 0x1E, 0x91,
						0x49, 0xE5, 0x92, 0xD9, 0x74,
						0x3E, 0xA1, 0x51, 0xE9, 0x94,
						0x5A, 0xB5, 0x5E, 0xB1, 0x59,
						0x6D, 0x2B, 0x2C, 0x1E, 0x93,
						0xCB, 0xE6, 0x33, 0x3A, 0xAD,
						0x5E, 0xB3, 0xDB, 0xEE, 0x37,
						0x3C, 0x2E, 0x9F, 0xD3, 0xEB,
						0xF6, 0x3B, 0x3E, 0xAF, 0x6F,
						0xC5, 0x64, 0x33, 0x5A, 0xCD,
						0x76, 0xC3, 0xE5, 0x60, 0x9E,
						0x02, 0x01, 0x01 };

static unsigned char send_ussd_311[] = { 0xD0, 0x5F, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x19, 0x80, 0x04, 0x17,
						0x04, 0x14, 0x04, 0x20, 0x04,
						0x10, 0x04, 0x12, 0x04, 0x21,
						0x04, 0x22, 0x04, 0x12, 0x04,
						0x23, 0x04, 0x19, 0x04, 0x22,
						0x04, 0x15, 0x8A, 0x39, 0xF0,
						0x41, 0xE1, 0x90, 0x58, 0x34,
						0x1E, 0x91, 0x49, 0xE5, 0x92,
						0xD9, 0x74, 0x3E, 0xA1, 0x51,
						0xE9, 0x94, 0x5A, 0xB5, 0x5E,
						0xB1, 0x59, 0x6D, 0x2B, 0x2C,
						0x1E, 0x93, 0xCB, 0xE6, 0x33,
						0x3A, 0xAD, 0x5E, 0xB3, 0xDB,
						0xEE, 0x37, 0x3C, 0x2E, 0x9F,
						0xD3, 0xEB, 0xF6, 0x3B, 0x3E,
						0xAF, 0x6F, 0xC5, 0x64, 0x33,
						0x5A, 0xCD, 0x76, 0xC3, 0xE5,
						0x60 };

static unsigned char send_ussd_411[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x00, 0xB4 };

static unsigned char send_ussd_412[] = { 0xD0, 0x56, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60 };

static unsigned char send_ussd_421[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x01, 0xB4 };

static unsigned char send_ussd_422[] = { 0xD0, 0x56, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60 };

static unsigned char send_ussd_431[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x02, 0xB4 };

static unsigned char send_ussd_432[] = { 0xD0, 0x56, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60 };

static unsigned char send_ussd_441[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x04, 0xB4 };

static unsigned char send_ussd_442[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x00, 0xB4 };

static unsigned char send_ussd_443[] = { 0xD0, 0x56, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60 };

static unsigned char send_ussd_451[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x08, 0xB4 };

static unsigned char send_ussd_452[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x00, 0xB4 };

static unsigned char send_ussd_453[] = { 0xD0, 0x56, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60 };

static unsigned char send_ussd_461[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x10, 0xB4 };

static unsigned char send_ussd_462[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x00, 0xB4 };

static unsigned char send_ussd_463[] = { 0xD0, 0x56, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60 };

static unsigned char send_ussd_471[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x20, 0xB4 };

static unsigned char send_ussd_472[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x00, 0xB4 };

static unsigned char send_ussd_473[] = { 0xD0, 0x56, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60 };

static unsigned char send_ussd_481[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x40, 0xB4 };

static unsigned char send_ussd_482[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x00, 0xB4 };

static unsigned char send_ussd_483[] = { 0xD0, 0x56, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60 };

static unsigned char send_ussd_491[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x80, 0xB4 };

static unsigned char send_ussd_492[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x00, 0xB4 };

static unsigned char send_ussd_493[] = { 0xD0, 0x56, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x33, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60 };

static unsigned char send_ussd_4101[] = { 0xD0, 0x5C, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x31, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60, 0xD0, 0x04, 0x00,
						0x10, 0x00, 0xB4 };

static unsigned char send_ussd_4102[] = { 0xD0, 0x56, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x10, 0x54, 0x65, 0x78,
						0x74, 0x20, 0x41, 0x74, 0x74,
						0x72, 0x69, 0x62, 0x75, 0x74,
						0x65, 0x20, 0x32, 0x8A, 0x39,
						0xF0, 0x41, 0xE1, 0x90, 0x58,
						0x34, 0x1E, 0x91, 0x49, 0xE5,
						0x92, 0xD9, 0x74, 0x3E, 0xA1,
						0x51, 0xE9, 0x94, 0x5A, 0xB5,
						0x5E, 0xB1, 0x59, 0x6D, 0x2B,
						0x2C, 0x1E, 0x93, 0xCB, 0xE6,
						0x33, 0x3A, 0xAD, 0x5E, 0xB3,
						0xDB, 0xEE, 0x37, 0x3C, 0x2E,
						0x9F, 0xD3, 0xEB, 0xF6, 0x3B,
						0x3E, 0xAF, 0x6F, 0xC5, 0x64,
						0x33, 0x5A, 0xCD, 0x76, 0xC3,
						0xE5, 0x60 };

static unsigned char send_ussd_511[] = { 0xD0, 0x4B, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x05, 0x80, 0x4F, 0x60,
						0x59, 0x7D, 0x8A, 0x39, 0xF0,
						0x41, 0xE1, 0x90, 0x58, 0x34,
						0x1E, 0x91, 0x49, 0xE5, 0x92,
						0xD9, 0x74, 0x3E, 0xA1, 0x51,
						0xE9, 0x94, 0x5A, 0xB5, 0x5E,
						0xB1, 0x59, 0x6D, 0x2B, 0x2C,
						0x1E, 0x93, 0xCB, 0xE6, 0x33,
						0x3A, 0xAD, 0x5E, 0xB3, 0xDB,
						0xEE, 0x37, 0x3C, 0x2E, 0x9F,
						0xD3, 0xEB, 0xF6, 0x3B, 0x3E,
						0xAF, 0x6F, 0xC5, 0x64, 0x33,
						0x5A, 0xCD, 0x76, 0xC3, 0xE5,
						0x60 };

static unsigned char send_ussd_611[] = { 0xD0, 0x49, 0x81, 0x03, 0x01, 0x12,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x03, 0x80, 0x30, 0xEB,
						0x8A, 0x39, 0xF0, 0x41, 0xE1,
						0x90, 0x58, 0x34, 0x1E, 0x91,
						0x49, 0xE5, 0x92, 0xD9, 0x74,
						0x3E, 0xA1, 0x51, 0xE9, 0x94,
						0x5A, 0xB5, 0x5E, 0xB1, 0x59,
						0x6D, 0x2B, 0x2C, 0x1E, 0x93,
						0xCB, 0xE6, 0x33, 0x3A, 0xAD,
						0x5E, 0xB3, 0xDB, 0xEE, 0x37,
						0x3C, 0x2E, 0x9F, 0xD3, 0xEB,
						0xF6, 0x3B, 0x3E, 0xAF, 0x6F,
						0xC5, 0x64, 0x33, 0x5A, 0xCD,
						0x76, 0xC3, 0xE5, 0x60 };

static struct send_ussd_test send_ussd_data_111 = {
	.pdu = send_ussd_111,
	.pdu_len = sizeof(send_ussd_111),
	.qualifier = 0x00,
	.alpha_id = "7-bit USSD",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_121 = {
	.pdu = send_ussd_121,
	.pdu_len = sizeof(send_ussd_121),
	.qualifier = 0x00,
	.alpha_id = "8-bit USSD",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_131 = {
	.pdu = send_ussd_131,
	.pdu_len = sizeof(send_ussd_131),
	.qualifier = 0x00,
	.alpha_id = "UCS2 USSD",
	.ussd = "ЗДРАВСТВУЙТЕ"
};

static struct send_ussd_test send_ussd_data_161 = {
	.pdu = send_ussd_161,
	.pdu_len = sizeof(send_ussd_161),
	.qualifier = 0x00,
	.alpha_id = "once a RELEASE COMPLETE message containing the USSD "
		"Return Result message not containing an error has been "
		"received from the network, the ME shall inform the SIM "
		"that the command has",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_171 = {
	.pdu = send_ussd_171,
	.pdu_len = sizeof(send_ussd_171),
	.qualifier = 0x00,
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_181 = {
	.pdu = send_ussd_181,
	.pdu_len = sizeof(send_ussd_181),
	.qualifier = 0x00,
	.alpha_id = "",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_211 = {
	.pdu = send_ussd_211,
	.pdu_len = sizeof(send_ussd_211),
	.qualifier = 0x00,
	.alpha_id = "Basic Icon",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct send_ussd_test send_ussd_data_221 = {
	.pdu = send_ussd_221,
	.pdu_len = sizeof(send_ussd_221),
	.qualifier = 0x00,
	.alpha_id = "Color Icon",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct send_ussd_test send_ussd_data_231 = {
	.pdu = send_ussd_231,
	.pdu_len = sizeof(send_ussd_231),
	.qualifier = 0x00,
	.alpha_id = "Basic Icon",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct send_ussd_test send_ussd_data_241 = {
	.pdu = send_ussd_241,
	.pdu_len = sizeof(send_ussd_241),
	.qualifier = 0x00,
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

/* The ussd is not complete in spec */
static struct send_ussd_test send_ussd_data_311 = {
	.pdu = send_ussd_311,
	.pdu_len = sizeof(send_ussd_311),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_411 = {
	.pdu = send_ussd_411,
	.pdu_len = sizeof(send_ussd_411),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_412 = {
	.pdu = send_ussd_412,
	.pdu_len = sizeof(send_ussd_412),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_421 = {
	.pdu = send_ussd_421,
	.pdu_len = sizeof(send_ussd_421),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x01, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_422 = {
	.pdu = send_ussd_422,
	.pdu_len = sizeof(send_ussd_422),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_431 = {
	.pdu = send_ussd_431,
	.pdu_len = sizeof(send_ussd_431),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x02, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_432 = {
	.pdu = send_ussd_432,
	.pdu_len = sizeof(send_ussd_432),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_441 = {
	.pdu = send_ussd_441,
	.pdu_len = sizeof(send_ussd_441),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x04, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_442 = {
	.pdu = send_ussd_442,
	.pdu_len = sizeof(send_ussd_442),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_443 = {
	.pdu = send_ussd_443,
	.pdu_len = sizeof(send_ussd_443),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_451 = {
	.pdu = send_ussd_451,
	.pdu_len = sizeof(send_ussd_451),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x08, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_452 = {
	.pdu = send_ussd_452,
	.pdu_len = sizeof(send_ussd_452),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_453 = {
	.pdu = send_ussd_453,
	.pdu_len = sizeof(send_ussd_453),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_461 = {
	.pdu = send_ussd_461,
	.pdu_len = sizeof(send_ussd_461),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x10, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_462 = {
	.pdu = send_ussd_462,
	.pdu_len = sizeof(send_ussd_462),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_463 = {
	.pdu = send_ussd_463,
	.pdu_len = sizeof(send_ussd_463),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_471 = {
	.pdu = send_ussd_471,
	.pdu_len = sizeof(send_ussd_471),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x20, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_472 = {
	.pdu = send_ussd_472,
	.pdu_len = sizeof(send_ussd_472),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_473 = {
	.pdu = send_ussd_473,
	.pdu_len = sizeof(send_ussd_473),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_481 = {
	.pdu = send_ussd_481,
	.pdu_len = sizeof(send_ussd_481),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x40, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_482 = {
	.pdu = send_ussd_482,
	.pdu_len = sizeof(send_ussd_482),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_483 = {
	.pdu = send_ussd_483,
	.pdu_len = sizeof(send_ussd_483),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_491 = {
	.pdu = send_ussd_491,
	.pdu_len = sizeof(send_ussd_491),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x80, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_492 = {
	.pdu = send_ussd_492,
	.pdu_len = sizeof(send_ussd_492),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_493 = {
	.pdu = send_ussd_493,
	.pdu_len = sizeof(send_ussd_493),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 3",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_4101 = {
	.pdu = send_ussd_4101,
	.pdu_len = sizeof(send_ussd_4101),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 1",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct send_ussd_test send_ussd_data_4102 = {
	.pdu = send_ussd_4102,
	.pdu_len = sizeof(send_ussd_4102),
	.qualifier = 0x00,
	.alpha_id = "Text Attribute 2",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_511 = {
	.pdu = send_ussd_511,
	.pdu_len = sizeof(send_ussd_511),
	.qualifier = 0x00,
	.alpha_id = "你好",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static struct send_ussd_test send_ussd_data_611 = {
	.pdu = send_ussd_611,
	.pdu_len = sizeof(send_ussd_611),
	.qualifier = 0x00,
	.alpha_id = "ル",
	.ussd = "ABCDEFGHIJKLMNOPQRSTUVWXYZ-abcdefghijklmnopqrstuvwxyz-"
		"1234567890"
};

static void test_send_ussd(gconstpointer data)
{
	const struct send_ussd_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_SEND_USSD);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_NETWORK);

	check_alpha_id(command->send_ussd.alpha_id, test->alpha_id);
	check_ussd(&command->send_ussd.ussd_string, test->ussd);
	check_icon_id(&command->send_ussd.icon_id, &test->icon_id);
	check_text_attr(&command->send_ussd.text_attr, &test->text_attr);
	check_frame_id(&command->send_ussd.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct setup_call_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *alpha_id_usr_cfm;
	struct stk_address addr;
	struct stk_ccp ccp;
	struct stk_subaddress subaddr;
	struct stk_duration duration;
	struct stk_icon_id icon_id_usr_cfm;
	char *alpha_id_call_setup;
	struct stk_icon_id icon_id_call_setup;
	struct stk_text_attribute text_attr_usr_cfm;
	struct stk_text_attribute text_attr_call_setup;
	struct stk_frame_id frame_id;
};

static unsigned char setup_call_111[] = { 0xD0, 0x1E, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x08, 0x4E, 0x6F, 0x74,
						0x20, 0x62, 0x75, 0x73, 0x79,
						0x86, 0x09, 0x91, 0x10, 0x32,
						0x04, 0x21, 0x43, 0x65, 0x1C,
						0x2C };

static unsigned char setup_call_141[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x10,
						0x02, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x07, 0x4F, 0x6E, 0x20,
						0x68, 0x6F, 0x6C, 0x64, 0x86,
						0x09, 0x91, 0x10, 0x32, 0x04,
						0x21, 0x43, 0x65, 0x1C, 0x2C };

static unsigned char setup_call_151[] = { 0xD0, 0x20, 0x81, 0x03, 0x01, 0x10,
						0x04, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0A, 0x44, 0x69, 0x73,
						0x63, 0x6F, 0x6E, 0x6E, 0x65,
						0x63, 0x74, 0x86, 0x09, 0x91,
						0x10, 0x32, 0x04, 0x21, 0x43,
						0x65, 0x1C, 0x2C };

static unsigned char setup_call_181[] = { 0xD0, 0x2B, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x11, 0x43, 0x61, 0x70,
						0x61, 0x62, 0x69, 0x6C, 0x69,
						0x74, 0x79, 0x20, 0x63, 0x6F,
						0x6E, 0x66, 0x69, 0x67, 0x86,
						0x09, 0x91, 0x10, 0x32, 0x04,
						0x21, 0x43, 0x65, 0x1C, 0x2C,
						0x87, 0x02, 0x01, 0xA0 };

static unsigned char setup_call_191[] = { 0xD0, 0x1C, 0x81, 0x03, 0x01, 0x10,
						0x01, 0x82, 0x02, 0x81, 0x83,
						0x86, 0x11, 0x91, 0x10, 0x32,
						0x54, 0x76, 0x98, 0x10, 0x32,
						0x54, 0x76, 0x98, 0x10, 0x32,
						0x54, 0x76, 0x98, 0x10 };

static unsigned char setup_call_1101[] = { 0xD0, 0x81, 0xFD, 0x81, 0x03, 0x01,
						0x10, 0x01, 0x82, 0x02, 0x81,
						0x83, 0x85, 0x81, 0xED, 0x54,
						0x68, 0x72, 0x65, 0x65, 0x20,
						0x74, 0x79, 0x70, 0x65, 0x73,
						0x20, 0x61, 0x72, 0x65, 0x20,
						0x64, 0x65, 0x66, 0x69, 0x6E,
						0x65, 0x64, 0x3A, 0x20, 0x2D,
						0x20, 0x73, 0x65, 0x74, 0x20,
						0x75, 0x70, 0x20, 0x61, 0x20,
						0x63, 0x61, 0x6C, 0x6C, 0x2C,
						0x20, 0x62, 0x75, 0x74, 0x20,
						0x6F, 0x6E, 0x6C, 0x79, 0x20,
						0x69, 0x66, 0x20, 0x6E, 0x6F,
						0x74, 0x20, 0x63, 0x75, 0x72,
						0x72, 0x65, 0x6E, 0x74, 0x6C,
						0x79, 0x20, 0x62, 0x75, 0x73,
						0x79, 0x20, 0x6F, 0x6E, 0x20,
						0x61, 0x6E, 0x6F, 0x74, 0x68,
						0x65, 0x72, 0x20, 0x63, 0x61,
						0x6C, 0x6C, 0x3B, 0x20, 0x2D,
						0x20, 0x73, 0x65, 0x74, 0x20,
						0x75, 0x70, 0x20, 0x61, 0x20,
						0x63, 0x61, 0x6C, 0x6C, 0x2C,
						0x20, 0x70, 0x75, 0x74, 0x74,
						0x69, 0x6E, 0x67, 0x20, 0x61,
						0x6C, 0x6C, 0x20, 0x6F, 0x74,
						0x68, 0x65, 0x72, 0x20, 0x63,
						0x61, 0x6C, 0x6C, 0x73, 0x20,
						0x28, 0x69, 0x66, 0x20, 0x61,
						0x6E, 0x79, 0x29, 0x20, 0x6F,
						0x6E, 0x20, 0x68, 0x6F, 0x6C,
						0x64, 0x3B, 0x20, 0x2D, 0x20,
						0x73, 0x65, 0x74, 0x20, 0x75,
						0x70, 0x20, 0x61, 0x20, 0x63,
						0x61, 0x6C, 0x6C, 0x2C, 0x20,
						0x64, 0x69, 0x73, 0x63, 0x6F,
						0x6E, 0x6E, 0x65, 0x63, 0x74,
						0x69, 0x6E, 0x67, 0x20, 0x61,
						0x6C, 0x6C, 0x20, 0x6F, 0x74,
						0x68, 0x65, 0x72, 0x20, 0x63,
						0x61, 0x6C, 0x6C, 0x73, 0x20,
						0x28, 0x69, 0x66, 0x20, 0x61,
						0x6E, 0x79, 0x29, 0x20, 0x66,
						0x69, 0x72, 0x73, 0x74, 0x2E,
						0x20, 0x46, 0x6F, 0x72, 0x20,
						0x65, 0x61, 0x63, 0x68, 0x20,
						0x6F, 0x66, 0x20, 0x74, 0x68,
						0x65, 0x73, 0x65, 0x20, 0x74,
						0x79, 0x70, 0x65, 0x73, 0x2C,
						0x20, 0x86, 0x02, 0x91, 0x10 };

static unsigned char setup_call_1111[] = { 0xD0, 0x2B, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0C, 0x43, 0x61, 0x6C,
						0x6C, 0x65, 0x64, 0x20, 0x70,
						0x61, 0x72, 0x74, 0x79, 0x86,
						0x09, 0x91, 0x10, 0x32, 0x04,
						0x21, 0x43, 0x65, 0x1C, 0x2C,
						0x88, 0x07, 0x80, 0x50, 0x95,
						0x95, 0x95, 0x95, 0x95 };

static unsigned char setup_call_1121[] = { 0xD0, 0x22, 0x81, 0x03, 0x01, 0x10,
						0x01, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x08, 0x44, 0x75, 0x72,
						0x61, 0x74, 0x69, 0x6F, 0x6E,
						0x86, 0x09, 0x91, 0x10, 0x32,
						0x04, 0x21, 0x43, 0x65, 0x1C,
						0x2C, 0x84, 0x02, 0x01, 0x0A };

static unsigned char setup_call_211[] = { 0xD0, 0x28, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0C, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x86,
						0x09, 0x91, 0x10, 0x32, 0x04,
						0x21, 0x43, 0x65, 0x1C, 0x2C,
						0x85, 0x04, 0x43, 0x41, 0x4C,
						0x4C };

static unsigned char setup_call_311[] = { 0xD0, 0x30, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x16, 0x53, 0x65, 0x74,
						0x20, 0x75, 0x70, 0x20, 0x63,
						0x61, 0x6C, 0x6C, 0x20, 0x49,
						0x63, 0x6F, 0x6E, 0x20, 0x33,
						0x2E, 0x31, 0x2E, 0x31, 0x86,
						0x09, 0x91, 0x10, 0x32, 0x04,
						0x21, 0x43, 0x65, 0x1C, 0x2C,
						0x9E, 0x02, 0x01, 0x01 };

static unsigned char setup_call_321[] = { 0xD0, 0x30, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x16, 0x53, 0x65, 0x74,
						0x20, 0x75, 0x70, 0x20, 0x63,
						0x61, 0x6C, 0x6C, 0x20, 0x49,
						0x63, 0x6F, 0x6E, 0x20, 0x33,
						0x2E, 0x32, 0x2E, 0x31, 0x86,
						0x09, 0x91, 0x10, 0x32, 0x04,
						0x21, 0x43, 0x65, 0x1C, 0x2C,
						0x9E, 0x02, 0x00, 0x01 };

static unsigned char setup_call_331[] = { 0xD0, 0x30, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x16, 0x53, 0x65, 0x74,
						0x20, 0x75, 0x70, 0x20, 0x63,
						0x61, 0x6C, 0x6C, 0x20, 0x49,
						0x63, 0x6F, 0x6E, 0x20, 0x33,
						0x2E, 0x33, 0x2E, 0x31, 0x86,
						0x09, 0x91, 0x10, 0x32, 0x04,
						0x21, 0x43, 0x65, 0x1C, 0x2C,
						0x9E, 0x02, 0x01, 0x02 };

static unsigned char setup_call_341[] = { 0xD0, 0x4C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x16, 0x53, 0x65, 0x74,
						0x20, 0x75, 0x70, 0x20, 0x63,
						0x61, 0x6C, 0x6C, 0x20, 0x49,
						0x63, 0x6F, 0x6E, 0x20, 0x33,
						0x2E, 0x34, 0x2E, 0x31, 0x86,
						0x09, 0x91, 0x10, 0x32, 0x04,
						0x21, 0x43, 0x65, 0x1C, 0x2C,
						0x9E, 0x02, 0x00, 0x01, 0x85,
						0x16, 0x53, 0x65, 0x74, 0x20,
						0x75, 0x70, 0x20, 0x63, 0x61,
						0x6C, 0x6C, 0x20, 0x49, 0x63,
						0x6F, 0x6E, 0x20, 0x33, 0x2E,
						0x34, 0x2E, 0x32, 0x9E, 0x02,
						0x00, 0x01 };

static unsigned char setup_call_411[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x31, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x31,
						0xD0, 0x04, 0x00, 0x0E, 0x00,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char setup_call_412[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x32, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x32 };

static unsigned char setup_call_421[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x31, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x31,
						0xD0, 0x04, 0x00, 0x0E, 0x01,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x01, 0xB4 };

static unsigned char setup_call_422[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x32, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x32 };

static unsigned char setup_call_431[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x31, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x31,
						0xD0, 0x04, 0x00, 0x0E, 0x02,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x02, 0xB4 };

static unsigned char setup_call_432[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x32, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x32 };

static unsigned char setup_call_441[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x31, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x31,
						0xD0, 0x04, 0x00, 0x0E, 0x04,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x04, 0xB4 };

static unsigned char setup_call_442[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x32, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x32,
						0xD0, 0x04, 0x00, 0x0E, 0x00,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char setup_call_443[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x33, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x33 };

static unsigned char setup_call_451[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x31, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x31,
						0xD0, 0x04, 0x00, 0x0E, 0x08,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x08, 0xB4 };

static unsigned char setup_call_452[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x32, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x32,
						0xD0, 0x04, 0x00, 0x0E, 0x00,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char setup_call_453[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x33, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x33 };

static unsigned char setup_call_461[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x31, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x31,
						0xD0, 0x04, 0x00, 0x0E, 0x10,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x10, 0xB4 };

static unsigned char setup_call_462[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x32, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x32,
						0xD0, 0x04, 0x00, 0x0E, 0x00,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char setup_call_463[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x33, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x33 };

static unsigned char setup_call_471[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x31, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x31,
						0xD0, 0x04, 0x00, 0x0E, 0x20,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x20, 0xB4 };

static unsigned char setup_call_472[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x32, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x32,
						0xD0, 0x04, 0x00, 0x0E, 0x00,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char setup_call_473[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x33, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x33 };

static unsigned char setup_call_481[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x31, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x31,
						0xD0, 0x04, 0x00, 0x0E, 0x40,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x40, 0xB4 };

static unsigned char setup_call_482[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x32, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x32,
						0xD0, 0x04, 0x00, 0x0E, 0x00,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char setup_call_483[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x33, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x33 };

static unsigned char setup_call_491[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x31, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x31,
						0xD0, 0x04, 0x00, 0x0E, 0x80,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x80, 0xB4 };

static unsigned char setup_call_492[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x32, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x32,
						0xD0, 0x04, 0x00, 0x0E, 0x00,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x00, 0xB4 };

static unsigned char setup_call_493[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x33, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x33 };

static unsigned char setup_call_4101[] = { 0xD0, 0x38, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x31, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x31,
						0xD0, 0x04, 0x00, 0x0E, 0x00,
						0xB4, 0xD0, 0x04, 0x00, 0x06,
						0x00, 0x4B };

static unsigned char setup_call_4102[] = { 0xD0, 0x2C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0E, 0x43, 0x4F, 0x4E,
						0x46, 0x49, 0x52, 0x4D, 0x41,
						0x54, 0x49, 0x4F, 0x4E, 0x20,
						0x32, 0x86, 0x09, 0x91, 0x10,
						0x32, 0x04, 0x21, 0x43, 0x65,
						0x1C, 0x2C, 0x85, 0x06, 0x43,
						0x41, 0x4C, 0x4C, 0x20, 0x32 };

static unsigned char setup_call_511[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x19, 0x80, 0x04, 0x17,
						0x04, 0x14, 0x04, 0x20, 0x04,
						0x10, 0x04, 0x12, 0x04, 0x21,
						0x04, 0x22, 0x04, 0x12, 0x04,
						0x23, 0x04, 0x19, 0x04, 0x22,
						0x04, 0x15, 0x86, 0x07, 0x91,
						0x10, 0x32, 0x04, 0x21, 0x43,
						0x65 };

static unsigned char setup_call_521[] = { 0xD0, 0x4C, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x1B, 0x80, 0x04, 0x17,
						0x04, 0x14, 0x04, 0x20, 0x04,
						0x10, 0x04, 0x12, 0x04, 0x21,
						0x04, 0x22, 0x04, 0x12, 0x04,
						0x23, 0x04, 0x19, 0x04, 0x22,
						0x04, 0x15, 0x00, 0x31, 0x86,
						0x07, 0x91, 0x10, 0x32, 0x04,
						0x21, 0x43, 0x65, 0x85, 0x1B,
						0x80, 0x04, 0x17, 0x04, 0x14,
						0x04, 0x20, 0x04, 0x10, 0x04,
						0x12, 0x04, 0x21, 0x04, 0x22,
						0x04, 0x12, 0x04, 0x23, 0x04,
						0x19, 0x04, 0x22, 0x04, 0x15,
						0x00, 0x32 };

static unsigned char setup_call_611[] = { 0xD0, 0x19, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x05, 0x80, 0x4E, 0x0D,
						0x5F, 0xD9, 0x86, 0x07, 0x91,
						0x10, 0x32, 0x04, 0x21, 0x43,
						0x65 };

static unsigned char setup_call_621[] = { 0xD0, 0x22, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x05, 0x80, 0x78, 0x6E,
						0x5B, 0x9A, 0x86, 0x07, 0x91,
						0x10, 0x32, 0x04, 0x21, 0x43,
						0x65, 0x85, 0x07, 0x80, 0x62,
						0x53, 0x75, 0x35, 0x8B, 0xDD };

static unsigned char setup_call_711[] = { 0xD0, 0x17, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x03, 0x80, 0x30, 0xEB,
						0x86, 0x07, 0x91, 0x10, 0x32,
						0x04, 0x21, 0x43, 0x65 };

static unsigned char setup_call_721[] = { 0xD0, 0x20, 0x81, 0x03, 0x01, 0x10,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x05, 0x80, 0x30, 0xEB,
						0x00, 0x31, 0x86, 0x07, 0x91,
						0x10, 0x32, 0x04, 0x21, 0x43,
						0x65, 0x85, 0x05, 0x80, 0x30,
						0xEB, 0x00, 0x32 };

static struct setup_call_test setup_call_data_111 = {
	.pdu = setup_call_111,
	.pdu_len = sizeof(setup_call_111),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "Not busy",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	}
};

static struct setup_call_test setup_call_data_141 = {
	.pdu = setup_call_141,
	.pdu_len = sizeof(setup_call_141),
	.qualifier = 0x02,
	.alpha_id_usr_cfm = "On hold",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	}
};

static struct setup_call_test setup_call_data_151 = {
	.pdu = setup_call_151,
	.pdu_len = sizeof(setup_call_151),
	.qualifier = 0x04,
	.alpha_id_usr_cfm = "Disconnect",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	}
};

static struct setup_call_test setup_call_data_181 = {
	.pdu = setup_call_181,
	.pdu_len = sizeof(setup_call_181),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "Capability config",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.ccp = {
		.len = 0x02,
		.ccp = { 0x01, 0xA0 }
	}
};

static struct setup_call_test setup_call_data_191 = {
	.pdu = setup_call_191,
	.pdu_len = sizeof(setup_call_191),
	.qualifier = 0x01,
	.addr = {
		.ton_npi = 0x91,
		.number = "01234567890123456789012345678901"
	}
};

static struct setup_call_test setup_call_data_1101 = {
	.pdu = setup_call_1101,
	.pdu_len = sizeof(setup_call_1101),
	.qualifier = 0x01,
	.alpha_id_usr_cfm = "Three types are defined: - set up a call, but "
			"only if not currently busy on another call; - set "
			"up a call, putting all other calls (if any) on hold; "
			"- set up a call, disconnecting all other calls (if "
			"any) first. For each of these types, ",
	.addr = {
		.ton_npi = 0x91,
		.number = "01"
	}
};

static struct setup_call_test setup_call_data_1111 = {
	.pdu = setup_call_1111,
	.pdu_len = sizeof(setup_call_1111),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "Called party",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.subaddr = {
		.len = 0x07,
		.subaddr = { 0x80, 0x50, 0x95, 0x95, 0x95, 0x95, 0x95 }
	}
};

static struct setup_call_test setup_call_data_1121 = {
	.pdu = setup_call_1121,
	.pdu_len = sizeof(setup_call_1121),
	.qualifier = 0x01,
	.alpha_id_usr_cfm = "Duration",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.duration = {
		.unit = STK_DURATION_TYPE_SECONDS,
		.interval = 10,
	}
};

static struct setup_call_test setup_call_data_211 = {
	.pdu = setup_call_211,
	.pdu_len = sizeof(setup_call_211),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL"
};

static struct setup_call_test setup_call_data_311 = {
	.pdu = setup_call_311,
	.pdu_len = sizeof(setup_call_311),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "Set up call Icon 3.1.1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.icon_id_usr_cfm = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct setup_call_test setup_call_data_321 = {
	.pdu = setup_call_321,
	.pdu_len = sizeof(setup_call_321),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "Set up call Icon 3.2.1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.icon_id_usr_cfm = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct setup_call_test setup_call_data_331 = {
	.pdu = setup_call_331,
	.pdu_len = sizeof(setup_call_331),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "Set up call Icon 3.3.1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.icon_id_usr_cfm = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct setup_call_test setup_call_data_341 = {
	.pdu = setup_call_341,
	.pdu_len = sizeof(setup_call_341),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "Set up call Icon 3.4.1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.icon_id_usr_cfm = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	},
	.alpha_id_call_setup = "Set up call Icon 3.4.2",
	.icon_id_call_setup = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct setup_call_test setup_call_data_411 = {
	.pdu = setup_call_411,
	.pdu_len = sizeof(setup_call_411),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 1",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x00, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_412 = {
	.pdu = setup_call_412,
	.pdu_len = sizeof(setup_call_412),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 2",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 2"
};

static struct setup_call_test setup_call_data_421 = {
	.pdu = setup_call_421,
	.pdu_len = sizeof(setup_call_421),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 1",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x01, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x01, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_422 = {
	.pdu = setup_call_422,
	.pdu_len = sizeof(setup_call_422),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 2",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 2"
};

static struct setup_call_test setup_call_data_431 = {
	.pdu = setup_call_431,
	.pdu_len = sizeof(setup_call_431),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 1",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x02, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x02, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_432 = {
	.pdu = setup_call_432,
	.pdu_len = sizeof(setup_call_432),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 2",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 2"
};

static struct setup_call_test setup_call_data_441 = {
	.pdu = setup_call_441,
	.pdu_len = sizeof(setup_call_441),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 1",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x04, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x04, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_442 = {
	.pdu = setup_call_442,
	.pdu_len = sizeof(setup_call_442),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 2",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 2",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x00, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_443 = {
	.pdu = setup_call_443,
	.pdu_len = sizeof(setup_call_443),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 3",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 3"
};

static struct setup_call_test setup_call_data_451 = {
	.pdu = setup_call_451,
	.pdu_len = sizeof(setup_call_451),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 1",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x08, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x08, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_452 = {
	.pdu = setup_call_452,
	.pdu_len = sizeof(setup_call_452),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 2",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 2",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x00, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_453 = {
	.pdu = setup_call_453,
	.pdu_len = sizeof(setup_call_453),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 3",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 3"
};

static struct setup_call_test setup_call_data_461 = {
	.pdu = setup_call_461,
	.pdu_len = sizeof(setup_call_461),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 1",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x10, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x10, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_462 = {
	.pdu = setup_call_462,
	.pdu_len = sizeof(setup_call_462),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 2",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 2",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x00, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_463 = {
	.pdu = setup_call_463,
	.pdu_len = sizeof(setup_call_463),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 3",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 3"
};

static struct setup_call_test setup_call_data_471 = {
	.pdu = setup_call_471,
	.pdu_len = sizeof(setup_call_471),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 1",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x20, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x20, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_472 = {
	.pdu = setup_call_472,
	.pdu_len = sizeof(setup_call_472),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 2",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 2",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x00, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_473 = {
	.pdu = setup_call_473,
	.pdu_len = sizeof(setup_call_473),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 3",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 3"
};

static struct setup_call_test setup_call_data_481 = {
	.pdu = setup_call_481,
	.pdu_len = sizeof(setup_call_481),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 1",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x40, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x40, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_482 = {
	.pdu = setup_call_482,
	.pdu_len = sizeof(setup_call_482),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 2",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 2",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x00, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_483 = {
	.pdu = setup_call_483,
	.pdu_len = sizeof(setup_call_483),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 3",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 3"
};

static struct setup_call_test setup_call_data_491 = {
	.pdu = setup_call_491,
	.pdu_len = sizeof(setup_call_491),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 1",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x80, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x80, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_492 = {
	.pdu = setup_call_492,
	.pdu_len = sizeof(setup_call_492),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 2",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 2",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x00, 0xB4 }
	}
};

static struct setup_call_test setup_call_data_493 = {
	.pdu = setup_call_493,
	.pdu_len = sizeof(setup_call_493),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 3",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 3"
};

static struct setup_call_test setup_call_data_4101 = {
	.pdu = setup_call_4101,
	.pdu_len = sizeof(setup_call_4101),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 1",
	.text_attr_usr_cfm = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
	.text_attr_call_setup = {
		.len = 4,
		.attributes = { 0x00, 0x06, 0x00, 0x4B }
	}
};

static struct setup_call_test setup_call_data_4102 = {
	.pdu = setup_call_4102,
	.pdu_len = sizeof(setup_call_4102),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "CONFIRMATION 2",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456c1c2"
	},
	.alpha_id_call_setup = "CALL 2"
};

static struct setup_call_test setup_call_data_511 = {
	.pdu = setup_call_511,
	.pdu_len = sizeof(setup_call_511),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "ЗДРАВСТВУЙТЕ",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456"
	}
};

static struct setup_call_test setup_call_data_521 = {
	.pdu = setup_call_521,
	.pdu_len = sizeof(setup_call_521),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "ЗДРАВСТВУЙТЕ1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456"
	},
	.alpha_id_call_setup = "ЗДРАВСТВУЙТЕ2"
};

static struct setup_call_test setup_call_data_611 = {
	.pdu = setup_call_611,
	.pdu_len = sizeof(setup_call_611),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "不忙",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456"
	}
};

static struct setup_call_test setup_call_data_621 = {
	.pdu = setup_call_621,
	.pdu_len = sizeof(setup_call_621),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "确定",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456"
	},
	.alpha_id_call_setup = "打电话"
};

static struct setup_call_test setup_call_data_711 = {
	.pdu = setup_call_711,
	.pdu_len = sizeof(setup_call_711),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "ル",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456"
	}
};

static struct setup_call_test setup_call_data_721 = {
	.pdu = setup_call_721,
	.pdu_len = sizeof(setup_call_721),
	.qualifier = 0x00,
	.alpha_id_usr_cfm = "ル1",
	.addr = {
		.ton_npi = 0x91,
		.number = "012340123456"
	},
	.alpha_id_call_setup = "ル2"
};

static void test_setup_call(gconstpointer data)
{
	const struct setup_call_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_SETUP_CALL);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_NETWORK);

	check_alpha_id(command->setup_call.alpha_id_usr_cfm,
					test->alpha_id_usr_cfm);
	check_address(&command->setup_call.addr, &test->addr);
	check_ccp(&command->setup_call.ccp, &test->ccp);
	check_subaddress(&command->setup_call.subaddr, &test->subaddr);
	check_duration(&command->setup_call.duration, &test->duration);
	check_icon_id(&command->setup_call.icon_id_usr_cfm,
					&test->icon_id_usr_cfm);
	check_alpha_id(command->setup_call.alpha_id_call_setup,
					test->alpha_id_call_setup);
	check_icon_id(&command->setup_call.icon_id_call_setup,
					&test->icon_id_call_setup);
	check_text_attr(&command->setup_call.text_attr_usr_cfm,
					&test->text_attr_usr_cfm);
	check_text_attr(&command->setup_call.text_attr_call_setup,
					&test->text_attr_call_setup);
	check_frame_id(&command->setup_call.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct refresh_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	struct stk_file file_list[MAX_ITEM];
	struct stk_aid aid;
	char *alpha_id;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
};

static unsigned char refresh_121[] = { 0xD0, 0x10, 0x81, 0x03, 0x01, 0x01,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0x92, 0x05, 0x01, 0x3F, 0x00,
						0x2F, 0xE2 };

static unsigned char refresh_151[] = { 0xD0, 0x09, 0x81, 0x03, 0x01, 0x01,
						0x04, 0x82, 0x02, 0x81, 0x82 };

static struct refresh_test refresh_data_121 = {
	.pdu = refresh_121,
	.pdu_len = sizeof(refresh_121),
	.qualifier = 0x01,
	.file_list = {{
		.len = 4,
		.file = { 0x3F, 0x00, 0x2F, 0xE2 }
	}}
};

static struct refresh_test refresh_data_151 = {
	.pdu = refresh_151,
	.pdu_len = sizeof(refresh_151),
	.qualifier = 0x04
};

/* Defined in TS 102.384 Section 27.22.4.7 */
static void test_refresh(gconstpointer data)
{
	const struct refresh_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_REFRESH);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_file_list(command->refresh.file_list, test->file_list);
	check_aid(&command->refresh.aid, &test->aid);
	check_alpha_id(command->refresh.alpha_id, test->alpha_id);
	check_icon_id(&command->refresh.icon_id, &test->icon_id);
	check_text_attr(&command->refresh.text_attr, &test->text_attr);
	check_frame_id(&command->refresh.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct polling_off_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
};

static unsigned char polling_off_112[] = { 0xD0, 0x09, 0x81, 0x03, 0x01, 0x04,
						0x00, 0x82, 0x02, 0x81, 0x82 };

static struct polling_off_test polling_off_data_112 = {
	.pdu = polling_off_112,
	.pdu_len = sizeof(polling_off_112),
	.qualifier = 0x00,
};

static void test_polling_off(gconstpointer data)
{
	const struct polling_off_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_POLLING_OFF);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	stk_command_free(command);
}

struct provide_local_info_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
};

static unsigned char provide_local_info_121[] = { 0xD0, 0x09, 0x81, 0x03, 0x01,
						0x26, 0x01, 0x82, 0x02, 0x81,
						0x82 };

static unsigned char provide_local_info_141[] = { 0xD0, 0x09, 0x81, 0x03, 0x01,
						0x26, 0x03, 0x82, 0x02, 0x81,
						0x82 };

static unsigned char provide_local_info_151[] = { 0xD0, 0x09, 0x81, 0x03, 0x01,
						0x26, 0x04, 0x82, 0x02, 0x81,
						0x82 };

static unsigned char provide_local_info_181[] = { 0xD0, 0x09, 0x81, 0x03, 0x01,
						0x26, 0x07, 0x82, 0x02, 0x81,
						0x82 };

static unsigned char provide_local_info_191[] = { 0xD0, 0x09, 0x81, 0x03, 0x01,
						0x26, 0x08, 0x82, 0x02, 0x81,
						0x82 };

static unsigned char provide_local_info_1111[] = { 0xD0, 0x09, 0x81, 0x03, 0x01,
						0x26, 0x0A, 0x82, 0x02, 0x81,
						0x82 };

static struct provide_local_info_test provide_local_info_data_121 = {
	.pdu = provide_local_info_121,
	.pdu_len = sizeof(provide_local_info_121),
	.qualifier = 0x01
};

static struct provide_local_info_test provide_local_info_data_141 = {
	.pdu = provide_local_info_141,
	.pdu_len = sizeof(provide_local_info_141),
	.qualifier = 0x03
};

static struct provide_local_info_test provide_local_info_data_151 = {
	.pdu = provide_local_info_151,
	.pdu_len = sizeof(provide_local_info_151),
	.qualifier = 0x04
};

static struct provide_local_info_test provide_local_info_data_181 = {
	.pdu = provide_local_info_181,
	.pdu_len = sizeof(provide_local_info_181),
	.qualifier = 0x07
};

static struct provide_local_info_test provide_local_info_data_191 = {
	.pdu = provide_local_info_191,
	.pdu_len = sizeof(provide_local_info_191),
	.qualifier = 0x08
};

static struct provide_local_info_test provide_local_info_data_1111 = {
	.pdu = provide_local_info_1111,
	.pdu_len = sizeof(provide_local_info_1111),
	.qualifier = 0x0A
};

static void test_provide_local_info(gconstpointer data)
{
	const struct provide_local_info_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	stk_command_free(command);
}

struct setup_event_list_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	struct stk_event_list event_list;
};

static unsigned char setup_event_list_111[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01,
						0x05, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x99, 0x01, 0x04 };

static unsigned char setup_event_list_121[] = { 0xD0, 0x0D, 0x81, 0x03, 0x01,
						0x05, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x99, 0x02, 0x05, 0x07 };

static unsigned char setup_event_list_122[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01,
						0x05, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x99, 0x01, 0x07 };

static unsigned char setup_event_list_131[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01,
						0x05, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x99, 0x01, 0x07 };

static unsigned char setup_event_list_132[] = { 0xD0, 0x0B, 0x81, 0x03, 0x01,
						0x05, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x99, 0x00 };

static unsigned char setup_event_list_141[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01,
						0x05, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x99, 0x01, 0x07 };

static struct setup_event_list_test setup_event_list_data_111 = {
	.pdu = setup_event_list_111,
	.pdu_len = sizeof(setup_event_list_111),
	.qualifier = 0x00,
	.event_list = {
		.len = 1,
		.list = { STK_EVENT_TYPE_USER_ACTIVITY }
	}
};

static struct setup_event_list_test setup_event_list_data_121 = {
	.pdu = setup_event_list_121,
	.pdu_len = sizeof(setup_event_list_121),
	.qualifier = 0x00,
	.event_list = {
		.len = 2,
		.list = { STK_EVENT_TYPE_IDLE_SCREEN_AVAILABLE,
				STK_EVENT_TYPE_LANGUAGE_SELECTION }
	}
};

static struct setup_event_list_test setup_event_list_data_122 = {
	.pdu = setup_event_list_122,
	.pdu_len = sizeof(setup_event_list_122),
	.qualifier = 0x00,
	.event_list = {
		.len = 1,
		.list = { STK_EVENT_TYPE_LANGUAGE_SELECTION }
	}
};

static struct setup_event_list_test setup_event_list_data_131 = {
	.pdu = setup_event_list_131,
	.pdu_len = sizeof(setup_event_list_131),
	.qualifier = 0x00,
	.event_list = {
		.len = 1,
		.list = { STK_EVENT_TYPE_LANGUAGE_SELECTION }
	}
};

static struct setup_event_list_test setup_event_list_data_132 = {
	.pdu = setup_event_list_132,
	.pdu_len = sizeof(setup_event_list_132),
	.qualifier = 0x00
};

static struct setup_event_list_test setup_event_list_data_141 = {
	.pdu = setup_event_list_141,
	.pdu_len = sizeof(setup_event_list_141),
	.qualifier = 0x00,
	.event_list = {
		.len = 1,
		.list = { STK_EVENT_TYPE_LANGUAGE_SELECTION }
	}
};

static void test_setup_event_list(gconstpointer data)
{
	const struct setup_event_list_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_SETUP_EVENT_LIST);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_event_list(&command->setup_event_list.event_list,
						&test->event_list);

	stk_command_free(command);
}

struct perform_card_apdu_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	unsigned char dst;
	struct stk_c_apdu c_apdu;
};

static unsigned char perform_card_apdu_111[] = { 0xD0, 0x12, 0x81, 0x03, 0x01,
						0x30, 0x00, 0x82, 0x02, 0x81,
						0x11, 0xA2, 0x07, 0xA0, 0xA4,
						0x00, 0x00, 0x02, 0x3F, 0x00 };

static unsigned char perform_card_apdu_112[] = { 0xD0, 0x10, 0x81, 0x03, 0x01,
						0x30, 0x00, 0x82, 0x02, 0x81,
						0x11, 0xA2, 0x05, 0xA0, 0xC0,
						0x00, 0x00, 0x1B };

static unsigned char perform_card_apdu_121[] = { 0xD0, 0x12, 0x81, 0x03, 0x01,
						0x30, 0x00, 0x82, 0x02, 0x81,
						0x11, 0xA2, 0x07, 0xA0, 0xA4,
						0x00, 0x00, 0x02, 0x7F, 0x20 };

static unsigned char perform_card_apdu_122[] = { 0xD0, 0x12, 0x81, 0x03, 0x01,
						0x30, 0x00, 0x82, 0x02, 0x81,
						0x11, 0xA2, 0x07, 0xA0, 0xA4,
						0x00, 0x00, 0x02, 0x6F, 0x30 };

static unsigned char perform_card_apdu_123[] = { 0xD0, 0x28, 0x81, 0x03, 0x01,
						0x30, 0x00, 0x82, 0x02, 0x81,
						0x11, 0xA2, 0x1D, 0xA0, 0xD6,
						0x00, 0x00, 0x18, 0x00, 0x01,
						0x02, 0x03, 0x04, 0x05, 0x06,
						0x07, 0x08, 0x09, 0x0A, 0x0B,
						0x0C, 0x0D, 0x0E, 0x0F, 0x10,
						0x11, 0x12, 0x13, 0x14, 0x15,
						0x16, 0x17 };

static unsigned char perform_card_apdu_124[] = { 0xD0, 0x10, 0x81, 0x03, 0x01,
						0x30, 0x00, 0x82, 0x02, 0x81,
						0x11, 0xA2, 0x05, 0xA0, 0xB0,
						0x00, 0x00, 0x18 };

static unsigned char perform_card_apdu_125[] = { 0xD0, 0x28, 0x81, 0x03, 0x01,
						0x30, 0x00, 0x82, 0x02, 0x81,
						0x11, 0xA2, 0x1D, 0xA0, 0xD6,
						0x00, 0x00, 0x18, 0xFF, 0xFF,
						0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
						0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
						0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
						0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
						0xFF, 0xFF };

static unsigned char perform_card_apdu_151[] = { 0xD0, 0x12, 0x81, 0x03, 0x01,
						0x30, 0x00, 0x82, 0x02, 0x81,
						0x17, 0xA2, 0x07, 0xA0, 0xA4,
						0x00, 0x00, 0x02, 0x3F, 0x00 };

static unsigned char perform_card_apdu_211[] = { 0xD0, 0x12, 0x81, 0x03, 0x01,
						0x30, 0x00, 0x82, 0x02, 0x81,
						0x11, 0xA2, 0x07, 0xA0, 0xA4,
						0x00, 0x00, 0x02, 0x3F, 0x00 };

static struct perform_card_apdu_test perform_card_apdu_data_111 = {
	.pdu = perform_card_apdu_111,
	.pdu_len = sizeof(perform_card_apdu_111),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CARD_READER_1,
	.c_apdu = {
		.cla = 0xA0,
		.ins = STK_INS_SELECT,
		.p1 = 0x00,
		.p2 = 0x00,
		.lc = 0x02,
		.data = { 0x3F, 0x00 }
	}
};

static struct perform_card_apdu_test perform_card_apdu_data_112 = {
	.pdu = perform_card_apdu_112,
	.pdu_len = sizeof(perform_card_apdu_112),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CARD_READER_1,
	.c_apdu = {
		.cla = 0xA0,
		.ins = STK_INS_GET_RESPONSE,
		.p1 = 0x00,
		.p2 = 0x00,
		.has_le = 1,
		.le = 0x1B
	}
};

static struct perform_card_apdu_test perform_card_apdu_data_121 = {
	.pdu = perform_card_apdu_121,
	.pdu_len = sizeof(perform_card_apdu_121),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CARD_READER_1,
	.c_apdu = {
		.cla = 0xA0,
		.ins = STK_INS_SELECT,
		.p1 = 0x00,
		.p2 = 0x00,
		.lc = 0x02,
		.data = { 0x7F, 0x20 }
	}
};

static struct perform_card_apdu_test perform_card_apdu_data_122 = {
	.pdu = perform_card_apdu_122,
	.pdu_len = sizeof(perform_card_apdu_122),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CARD_READER_1,
	.c_apdu = {
		.cla = 0xA0,
		.ins = STK_INS_SELECT,
		.p1 = 0x00,
		.p2 = 0x00,
		.lc = 0x02,
		.data = { 0x6F, 0x30 }
	}
};

/* Byte 14 of Data is not correct in spec. */
static struct perform_card_apdu_test perform_card_apdu_data_123 = {
	.pdu = perform_card_apdu_123,
	.pdu_len = sizeof(perform_card_apdu_123),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CARD_READER_1,
	.c_apdu = {
		.cla = 0xA0,
		.ins = STK_INS_UPDATE_BINARY_D6,
		.p1 = 0x00,
		.p2 = 0x00,
		.lc = 0x18,
		.data = { 0x00, 0x01, 0x02, 0x03, 0x04,	0x05, 0x06, 0x07, 0x08,
				0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
				0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 }
	}
};

static struct perform_card_apdu_test perform_card_apdu_data_124 = {
	.pdu = perform_card_apdu_124,
	.pdu_len = sizeof(perform_card_apdu_124),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CARD_READER_1,
	.c_apdu = {
		.cla = 0xA0,
		.ins = STK_INS_READ_BINARY_B0,
		.p1 = 0x00,
		.p2 = 0x00,
		.has_le = 1,
		.le = 0x18
	}
};

static struct perform_card_apdu_test perform_card_apdu_data_125 = {
	.pdu = perform_card_apdu_125,
	.pdu_len = sizeof(perform_card_apdu_125),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CARD_READER_1,
	.c_apdu = {
		.cla = 0xA0,
		.ins = STK_INS_UPDATE_BINARY_D6,
		.p1 = 0x00,
		.p2 = 0x00,
		.lc = 0x18,
		.data = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
				0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
				0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
	}
};

static struct perform_card_apdu_test perform_card_apdu_data_151 = {
	.pdu = perform_card_apdu_151,
	.pdu_len = sizeof(perform_card_apdu_151),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CARD_READER_7,
	.c_apdu = {
		.cla = 0xA0,
		.ins = STK_INS_SELECT,
		.p1 = 0x00,
		.p2 = 0x00,
		.lc = 0x02,
		.data = { 0x3F, 0x00 }
	}
};

static struct perform_card_apdu_test perform_card_apdu_data_211 = {
	.pdu = perform_card_apdu_211,
	.pdu_len = sizeof(perform_card_apdu_211),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CARD_READER_1,
	.c_apdu = {
		.cla = 0xA0,
		.ins = STK_INS_SELECT,
		.p1 = 0x00,
		.p2 = 0x00,
		.lc = 0x02,
		.data = { 0x3F, 0x00 }
	}
};

static void test_perform_card_apdu(gconstpointer data)
{
	const struct perform_card_apdu_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_PERFORM_CARD_APDU);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == test->dst);

	check_c_apdu(&command->perform_card_apdu.c_apdu, &test->c_apdu);

	stk_command_free(command);
}

struct get_reader_status_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
};

static unsigned char get_reader_status_111[] = { 0xD0, 0x09, 0x81, 0x03, 0x01,
						0x33, 0x00, 0x82, 0x02, 0x81,
						0x82 };

static struct get_reader_status_test get_reader_status_data_111 = {
	.pdu = get_reader_status_111,
	.pdu_len = sizeof(get_reader_status_111),
	.qualifier = STK_QUALIFIER_TYPE_CARD_READER_STATUS,
};

static void test_get_reader_status(gconstpointer data)
{
	const struct get_reader_status_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_GET_READER_STATUS);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);

	if (command->qualifier == STK_QUALIFIER_TYPE_CARD_READER_STATUS)
		g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);
	else
		g_assert(command->dst <
				STK_DEVICE_IDENTITY_TYPE_CARD_READER_0 ||
			command->dst >
				STK_DEVICE_IDENTITY_TYPE_CARD_READER_7);

	stk_command_free(command);
}

struct timer_mgmt_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	unsigned char timer_id;
	struct stk_timer_value timer_value;
};

static unsigned char timer_mgmt_111[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x01, 0xA5, 0x03,
						0x00, 0x50, 0x00 };

static unsigned char timer_mgmt_112[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x01 };

static unsigned char timer_mgmt_113[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x01, 0xA5, 0x03,
						0x00, 0x10, 0x03 };

static unsigned char timer_mgmt_114[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x01 };

static unsigned char timer_mgmt_121[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x02, 0xA5, 0x03,
						0x32, 0x95, 0x95 };

static unsigned char timer_mgmt_122[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x02 };

static unsigned char timer_mgmt_123[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x02, 0xA5, 0x03,
						0x00, 0x10, 0x01 };

static unsigned char timer_mgmt_124[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x02 };

static unsigned char timer_mgmt_131[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x08, 0xA5, 0x03,
						0x00, 0x02, 0x00 };

static unsigned char timer_mgmt_132[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x08 };

static unsigned char timer_mgmt_133[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x08, 0xA5, 0x03,
						0x10, 0x00, 0x00 };

static unsigned char timer_mgmt_134[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x08 };

static unsigned char timer_mgmt_141[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x01 };

static unsigned char timer_mgmt_142[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x02 };

static unsigned char timer_mgmt_143[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x03 };

static unsigned char timer_mgmt_144[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x04 };

static unsigned char timer_mgmt_145[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x05 };

static unsigned char timer_mgmt_146[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x06 };

static unsigned char timer_mgmt_147[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x07 };

static unsigned char timer_mgmt_148[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x02, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x08 };

static unsigned char timer_mgmt_151[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x01 };

static unsigned char timer_mgmt_152[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x02 };

static unsigned char timer_mgmt_153[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x03 };

static unsigned char timer_mgmt_154[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x04 };

static unsigned char timer_mgmt_155[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x05 };

static unsigned char timer_mgmt_156[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x06 };

static unsigned char timer_mgmt_157[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x07 };

static unsigned char timer_mgmt_158[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x27,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x08 };

static unsigned char timer_mgmt_161[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x01, 0xA5, 0x03,
						0x00, 0x00, 0x50 };

static unsigned char timer_mgmt_162[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x02, 0xA5, 0x03,
						0x00, 0x00, 0x50 };

static unsigned char timer_mgmt_163[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x03, 0xA5, 0x03,
						0x00, 0x00, 0x50 };

static unsigned char timer_mgmt_164[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x04, 0xA5, 0x03,
						0x00, 0x00, 0x50 };

static unsigned char timer_mgmt_165[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x05, 0xA5, 0x03,
						0x00, 0x00, 0x50 };

static unsigned char timer_mgmt_166[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x06, 0xA5, 0x03,
						0x00, 0x00, 0x50 };

static unsigned char timer_mgmt_167[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x07, 0xA5, 0x03,
						0x00, 0x00, 0x50 };

static unsigned char timer_mgmt_168[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x08, 0xA5, 0x03,
						0x00, 0x00, 0x50 };

static unsigned char timer_mgmt_211[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x01, 0xA5, 0x03,
						0x00, 0x00, 0x01 };

static unsigned char timer_mgmt_221[] = { 0xD0, 0x11, 0x81, 0x03, 0x01, 0x27,
						0x00, 0x82, 0x02, 0x81, 0x82,
						0xA4, 0x01, 0x01, 0xA5, 0x03,
						0x00, 0x00, 0x03 };

static struct timer_mgmt_test timer_mgmt_data_111 = {
	.pdu = timer_mgmt_111,
	.pdu_len = sizeof(timer_mgmt_111),
	.qualifier = 0x00,
	.timer_id = 1,
	.timer_value = {
		.minute = 5
	}
};

static struct timer_mgmt_test timer_mgmt_data_112 = {
	.pdu = timer_mgmt_112,
	.pdu_len = sizeof(timer_mgmt_112),
	.qualifier = 0x02,
	.timer_id = 1
};

static struct timer_mgmt_test timer_mgmt_data_113 = {
	.pdu = timer_mgmt_113,
	.pdu_len = sizeof(timer_mgmt_113),
	.qualifier = 0x00,
	.timer_id = 1,
	.timer_value = {
		.minute = 1,
		.second = 30
	}
};

static struct timer_mgmt_test timer_mgmt_data_114 = {
	.pdu = timer_mgmt_114,
	.pdu_len = sizeof(timer_mgmt_114),
	.qualifier = 0x01,
	.timer_id = 1
};

static struct timer_mgmt_test timer_mgmt_data_121 = {
	.pdu = timer_mgmt_121,
	.pdu_len = sizeof(timer_mgmt_121),
	.qualifier = 0x00,
	.timer_id = 2,
	.timer_value = {
		.hour = 23,
		.minute = 59,
		.second = 59
	}
};

static struct timer_mgmt_test timer_mgmt_data_122 = {
	.pdu = timer_mgmt_122,
	.pdu_len = sizeof(timer_mgmt_122),
	.qualifier = 0x02,
	.timer_id = 2
};

static struct timer_mgmt_test timer_mgmt_data_123 = {
	.pdu = timer_mgmt_123,
	.pdu_len = sizeof(timer_mgmt_123),
	.qualifier = 0x00,
	.timer_id = 2,
	.timer_value = {
		.minute = 1,
		.second = 10
	}
};

static struct timer_mgmt_test timer_mgmt_data_124 = {
	.pdu = timer_mgmt_124,
	.pdu_len = sizeof(timer_mgmt_124),
	.qualifier = 0x01,
	.timer_id = 2
};

static struct timer_mgmt_test timer_mgmt_data_131 = {
	.pdu = timer_mgmt_131,
	.pdu_len = sizeof(timer_mgmt_131),
	.qualifier = 0x00,
	.timer_id = 8,
	.timer_value = {
		.minute = 20
	}
};

static struct timer_mgmt_test timer_mgmt_data_132 = {
	.pdu = timer_mgmt_132,
	.pdu_len = sizeof(timer_mgmt_132),
	.qualifier = 0x02,
	.timer_id = 8
};

static struct timer_mgmt_test timer_mgmt_data_133 = {
	.pdu = timer_mgmt_133,
	.pdu_len = sizeof(timer_mgmt_133),
	.qualifier = 0x00,
	.timer_id = 8,
	.timer_value = {
		.hour = 1
	}
};

static struct timer_mgmt_test timer_mgmt_data_134 = {
	.pdu = timer_mgmt_134,
	.pdu_len = sizeof(timer_mgmt_134),
	.qualifier = 0x01,
	.timer_id = 8
};

static struct timer_mgmt_test timer_mgmt_data_141 = {
	.pdu = timer_mgmt_141,
	.pdu_len = sizeof(timer_mgmt_141),
	.qualifier = 0x02,
	.timer_id = 1
};

static struct timer_mgmt_test timer_mgmt_data_142 = {
	.pdu = timer_mgmt_142,
	.pdu_len = sizeof(timer_mgmt_142),
	.qualifier = 0x02,
	.timer_id = 2
};

static struct timer_mgmt_test timer_mgmt_data_143 = {
	.pdu = timer_mgmt_143,
	.pdu_len = sizeof(timer_mgmt_143),
	.qualifier = 0x02,
	.timer_id = 3
};

static struct timer_mgmt_test timer_mgmt_data_144 = {
	.pdu = timer_mgmt_144,
	.pdu_len = sizeof(timer_mgmt_144),
	.qualifier = 0x02,
	.timer_id = 4
};

static struct timer_mgmt_test timer_mgmt_data_145 = {
	.pdu = timer_mgmt_145,
	.pdu_len = sizeof(timer_mgmt_145),
	.qualifier = 0x02,
	.timer_id = 5
};

static struct timer_mgmt_test timer_mgmt_data_146 = {
	.pdu = timer_mgmt_146,
	.pdu_len = sizeof(timer_mgmt_146),
	.qualifier = 0x02,
	.timer_id = 6
};

static struct timer_mgmt_test timer_mgmt_data_147 = {
	.pdu = timer_mgmt_147,
	.pdu_len = sizeof(timer_mgmt_147),
	.qualifier = 0x02,
	.timer_id = 7
};

static struct timer_mgmt_test timer_mgmt_data_148 = {
	.pdu = timer_mgmt_148,
	.pdu_len = sizeof(timer_mgmt_148),
	.qualifier = 0x02,
	.timer_id = 8
};

static struct timer_mgmt_test timer_mgmt_data_151 = {
	.pdu = timer_mgmt_151,
	.pdu_len = sizeof(timer_mgmt_151),
	.qualifier = 0x01,
	.timer_id = 1
};

static struct timer_mgmt_test timer_mgmt_data_152 = {
	.pdu = timer_mgmt_152,
	.pdu_len = sizeof(timer_mgmt_152),
	.qualifier = 0x01,
	.timer_id = 2
};

static struct timer_mgmt_test timer_mgmt_data_153 = {
	.pdu = timer_mgmt_153,
	.pdu_len = sizeof(timer_mgmt_153),
	.qualifier = 0x01,
	.timer_id = 3
};

static struct timer_mgmt_test timer_mgmt_data_154 = {
	.pdu = timer_mgmt_154,
	.pdu_len = sizeof(timer_mgmt_154),
	.qualifier = 0x01,
	.timer_id = 4
};

static struct timer_mgmt_test timer_mgmt_data_155 = {
	.pdu = timer_mgmt_155,
	.pdu_len = sizeof(timer_mgmt_155),
	.qualifier = 0x01,
	.timer_id = 5
};

static struct timer_mgmt_test timer_mgmt_data_156 = {
	.pdu = timer_mgmt_156,
	.pdu_len = sizeof(timer_mgmt_156),
	.qualifier = 0x01,
	.timer_id = 6
};

static struct timer_mgmt_test timer_mgmt_data_157 = {
	.pdu = timer_mgmt_157,
	.pdu_len = sizeof(timer_mgmt_157),
	.qualifier = 0x01,
	.timer_id = 7
};

static struct timer_mgmt_test timer_mgmt_data_158 = {
	.pdu = timer_mgmt_158,
	.pdu_len = sizeof(timer_mgmt_158),
	.qualifier = 0x01,
	.timer_id = 8
};

static struct timer_mgmt_test timer_mgmt_data_161 = {
	.pdu = timer_mgmt_161,
	.pdu_len = sizeof(timer_mgmt_161),
	.qualifier = 0x00,
	.timer_id = 1,
	.timer_value = {
		.second = 5
	}
};

static struct timer_mgmt_test timer_mgmt_data_162 = {
	.pdu = timer_mgmt_162,
	.pdu_len = sizeof(timer_mgmt_162),
	.qualifier = 0x00,
	.timer_id = 2,
	.timer_value = {
		.second = 5
	}
};

static struct timer_mgmt_test timer_mgmt_data_163 = {
	.pdu = timer_mgmt_163,
	.pdu_len = sizeof(timer_mgmt_163),
	.qualifier = 0x00,
	.timer_id = 3,
	.timer_value = {
		.second = 5
	}
};

static struct timer_mgmt_test timer_mgmt_data_164 = {
	.pdu = timer_mgmt_164,
	.pdu_len = sizeof(timer_mgmt_164),
	.qualifier = 0x00,
	.timer_id = 4,
	.timer_value = {
		.second = 5
	}
};

static struct timer_mgmt_test timer_mgmt_data_165 = {
	.pdu = timer_mgmt_165,
	.pdu_len = sizeof(timer_mgmt_165),
	.qualifier = 0x00,
	.timer_id = 5,
	.timer_value = {
		.second = 5
	}
};

static struct timer_mgmt_test timer_mgmt_data_166 = {
	.pdu = timer_mgmt_166,
	.pdu_len = sizeof(timer_mgmt_166),
	.qualifier = 0x00,
	.timer_id = 6,
	.timer_value = {
		.second = 5
	}
};

static struct timer_mgmt_test timer_mgmt_data_167 = {
	.pdu = timer_mgmt_167,
	.pdu_len = sizeof(timer_mgmt_167),
	.qualifier = 0x00,
	.timer_id = 7,
	.timer_value = {
		.second = 5
	}
};

static struct timer_mgmt_test timer_mgmt_data_168 = {
	.pdu = timer_mgmt_168,
	.pdu_len = sizeof(timer_mgmt_168),
	.qualifier = 0x00,
	.timer_id = 8,
	.timer_value = {
		.second = 5
	}
};

static struct timer_mgmt_test timer_mgmt_data_211 = {
	.pdu = timer_mgmt_211,
	.pdu_len = sizeof(timer_mgmt_211),
	.qualifier = 0x00,
	.timer_id = 1,
	.timer_value = {
		.second = 10
	}
};

static struct timer_mgmt_test timer_mgmt_data_221 = {
	.pdu = timer_mgmt_221,
	.pdu_len = sizeof(timer_mgmt_221),
	.qualifier = 0x00,
	.timer_id = 1,
	.timer_value = {
		.second = 30
	}
};

static void test_timer_mgmt(gconstpointer data)
{
	const struct timer_mgmt_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_TIMER_MANAGEMENT);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_timer_id(command->timer_mgmt.timer_id, test->timer_id);
	check_timer_value(&command->timer_mgmt.timer_value, &test->timer_value);

	stk_command_free(command);
}

struct setup_idle_mode_text_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *text;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
	char *html;
	enum stk_command_parse_result status;
};

static unsigned char setup_idle_mode_text_111[] = { 0xD0, 0x1A, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x0F, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74 };

static unsigned char setup_idle_mode_text_121[] = { 0xD0, 0x18, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x0D, 0x04,
						0x54, 0x6F, 0x6F, 0x6C, 0x6B,
						0x69, 0x74, 0x20, 0x54, 0x65,
						0x73, 0x74 };

static unsigned char setup_idle_mode_text_131[] = { 0xD0, 0x0B, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x00 };

static unsigned char setup_idle_mode_text_171[] = { 0xD0, 0x81, 0xFD, 0x81,
						0x03, 0x01, 0x28, 0x00, 0x82,
						0x02, 0x81, 0x82, 0x8D, 0x81,
						0xF1, 0x00, 0x54, 0x74, 0x19,
						0x34, 0x4D, 0x36, 0x41, 0x73,
						0x74, 0x98, 0xCD, 0x06, 0xCD,
						0xEB, 0x70, 0x38, 0x3B, 0x0F,
						0x0A, 0x83, 0xE8, 0x65, 0x3C,
						0x1D, 0x34, 0xA7, 0xCB, 0xD3,
						0xEE, 0x33, 0x0B, 0x74, 0x47,
						0xA7, 0xC7, 0x68, 0xD0, 0x1C,
						0x1D, 0x66, 0xB3, 0x41, 0xE2,
						0x32, 0x88, 0x9C, 0x9E, 0xC3,
						0xD9, 0xE1, 0x7C, 0x99, 0x0C,
						0x12, 0xE7, 0x41, 0x74, 0x74,
						0x19, 0xD4, 0x2C, 0x82, 0xC2,
						0x73, 0x50, 0xD8, 0x0D, 0x4A,
						0x93, 0xD9, 0x65, 0x50, 0xFB,
						0x4D, 0x2E, 0x83, 0xE8, 0x65,
						0x3C, 0x1D, 0x94, 0x36, 0x83,
						0xE8, 0xE8, 0x32, 0xA8, 0x59,
						0x04, 0xA5, 0xE7, 0xA0, 0xB0,
						0x98, 0x5D, 0x06, 0xD1, 0xDF,
						0x20, 0xF2, 0x1B, 0x94, 0xA6,
						0xBB, 0xA8, 0xE8, 0x32, 0x08,
						0x2E, 0x2F, 0xCF, 0xCB, 0x6E,
						0x7A, 0x98, 0x9E, 0x7E, 0xBB,
						0x41, 0x73, 0x7A, 0x9E, 0x5D,
						0x06, 0xA5, 0xE7, 0x20, 0x76,
						0xD9, 0x4C, 0x07, 0x85, 0xE7,
						0xA0, 0xB0, 0x1B, 0x94, 0x6E,
						0xC3, 0xD9, 0xE5, 0x76, 0xD9,
						0x4D, 0x0F, 0xD3, 0xD3, 0x6F,
						0x37, 0x88, 0x5C, 0x1E, 0xA7,
						0xE7, 0xE9, 0xB7, 0x1B, 0x44,
						0x7F, 0x83, 0xE8, 0xE8, 0x32,
						0xA8, 0x59, 0x04, 0xB5, 0xC3,
						0xEE, 0xBA, 0x39, 0x3C, 0xA6,
						0xD7, 0xE5, 0x65, 0xB9, 0x0B,
						0x44, 0x45, 0x97, 0x41, 0x69,
						0x32, 0xBB, 0x0C, 0x6A, 0xBF,
						0xC9, 0x65, 0x10, 0xBD, 0x8C,
						0xA7, 0x83, 0xE6, 0xE8, 0x30,
						0x9B, 0x0D, 0x12, 0x97, 0x41,
						0xE4, 0xF4, 0x1C, 0xCE, 0x0E,
						0xE7, 0xCB, 0x64, 0x50, 0xDA,
						0x0D, 0x0A, 0x83, 0xDA, 0x61,
						0xB7, 0xBB, 0x2C, 0x07, 0xD1,
						0xD1, 0x61, 0x3A, 0xA8, 0xEC,
						0x9E, 0xD7, 0xE5, 0xE5, 0x39,
						0x88, 0x8E, 0x0E, 0xD3, 0x41,
						0xEE, 0x32 };

static unsigned char setup_idle_mode_text_211[] = { 0xD0, 0x19, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x0A, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x74, 0x65, 0x78, 0x74, 0x9E,
						0x02, 0x00, 0x01 };

static unsigned char setup_idle_mode_text_221[] = { 0xD0, 0x19, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x0A, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x74, 0x65, 0x78, 0x74, 0x9E,
						0x02, 0x01, 0x01 };

static unsigned char setup_idle_mode_text_231[] = { 0xD0, 0x19, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x0A, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x74, 0x65, 0x78, 0x74, 0x9E,
						0x02, 0x00, 0x02 };

static unsigned char setup_idle_mode_text_241[] = { 0xD0, 0x0F, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x00, 0x9E,
						0x02, 0x01, 0x01 };

static unsigned char setup_idle_mode_text_311[] = { 0xD0, 0x24, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x19, 0x08,
						0x04, 0x17, 0x04, 0x14, 0x04,
						0x20, 0x04, 0x10, 0x04, 0x12,
						0x04, 0x21, 0x04, 0x22, 0x04,
						0x12, 0x04, 0x23, 0x04, 0x19,
						0x04, 0x22, 0x04, 0x15 };

static unsigned char setup_idle_mode_text_411[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x31, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char setup_idle_mode_text_412[] = { 0xD0, 0x1C, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x32 };

static unsigned char setup_idle_mode_text_421[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x31, 0xD0, 0x04, 0x00, 0x10,
						0x01, 0xB4 };

static unsigned char setup_idle_mode_text_422[] = { 0xD0, 0x1C, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x32 };

static unsigned char setup_idle_mode_text_431[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x31, 0xD0, 0x04, 0x00, 0x10,
						0x02, 0xB4 };

static unsigned char setup_idle_mode_text_432[] = { 0xD0, 0x1C, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x32 };

static unsigned char setup_idle_mode_text_441[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x31, 0xD0, 0x04, 0x00, 0x10,
						0x04, 0xB4 };

static unsigned char setup_idle_mode_text_442[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char setup_idle_mode_text_443[] = { 0xD0, 0x1C, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x33 };

static unsigned char setup_idle_mode_text_451[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x31, 0xD0, 0x04, 0x00, 0x10,
						0x08, 0xB4 };

static unsigned char setup_idle_mode_text_452[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char setup_idle_mode_text_453[] = { 0xD0, 0x1C, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x33 };

static unsigned char setup_idle_mode_text_461[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x31, 0xD0, 0x04, 0x00, 0x10,
						0x10, 0xB4 };

static unsigned char setup_idle_mode_text_462[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char setup_idle_mode_text_463[] = { 0xD0, 0x1C, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x33 };

static unsigned char setup_idle_mode_text_471[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x31, 0xD0, 0x04, 0x00, 0x10,
						0x20, 0xB4 };

static unsigned char setup_idle_mode_text_472[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char setup_idle_mode_text_473[] = { 0xD0, 0x1C, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x33 };

static unsigned char setup_idle_mode_text_481[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x31, 0xD0, 0x04, 0x00, 0x10,
						0x40, 0xB4 };

static unsigned char setup_idle_mode_text_482[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char setup_idle_mode_text_483[] = { 0xD0, 0x1C, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x33 };

static unsigned char setup_idle_mode_text_491[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x31, 0xD0, 0x04, 0x00, 0x10,
						0x80, 0xB4 };

static unsigned char setup_idle_mode_text_492[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x32, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char setup_idle_mode_text_493[] = { 0xD0, 0x1C, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x33 };

static unsigned char setup_idle_mode_text_4101[] = { 0xD0, 0x22, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x31, 0xD0, 0x04, 0x00, 0x10,
						0x00, 0xB4 };

static unsigned char setup_idle_mode_text_4102[] = { 0xD0, 0x1C, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x11, 0x04,
						0x49, 0x64, 0x6C, 0x65, 0x20,
						0x4D, 0x6F, 0x64, 0x65, 0x20,
						0x54, 0x65, 0x78, 0x74, 0x20,
						0x32 };

static unsigned char setup_idle_mode_text_511[] = { 0xD0, 0x10, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x05, 0x08,
						0x4F, 0x60, 0x59, 0x7D };

static unsigned char setup_idle_mode_text_611[] = { 0xD0, 0x14, 0x81, 0x03,
						0x01, 0x28, 0x00, 0x82, 0x02,
						0x81, 0x82, 0x8D, 0x09, 0x08,
						0x00, 0x38, 0x00, 0x30, 0x30,
						0xEB, 0x00, 0x30 };

static struct setup_idle_mode_text_test setup_idle_mode_text_data_111 = {
	.pdu = setup_idle_mode_text_111,
	.pdu_len = sizeof(setup_idle_mode_text_111),
	.qualifier = 0x00,
	.text = "Idle Mode Text"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_121 = {
	.pdu = setup_idle_mode_text_121,
	.pdu_len = sizeof(setup_idle_mode_text_121),
	.qualifier = 0x00,
	.text = "Toolkit Test"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_131 = {
	.pdu = setup_idle_mode_text_131,
	.pdu_len = sizeof(setup_idle_mode_text_131),
	.qualifier = 0x00,
	.text = ""
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_171 = {
	.pdu = setup_idle_mode_text_171,
	.pdu_len = sizeof(setup_idle_mode_text_171),
	.qualifier = 0x00,
	.text = "The SIM shall supply a text string, which shall be displayed "
		"by the ME as an idle mode text if the ME is able to do it."
		"The presentation style is left as an implementation decision "
		"to the ME manufacturer. The idle mode text shall be displayed "
		"in a manner that ensures that ne"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_211 = {
	.pdu = setup_idle_mode_text_211,
	.pdu_len = sizeof(setup_idle_mode_text_211),
	.qualifier = 0x00,
	.text = "Idle text",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_221 = {
	.pdu = setup_idle_mode_text_221,
	.pdu_len = sizeof(setup_idle_mode_text_221),
	.qualifier = 0x00,
	.text = "Idle text",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_231 = {
	.pdu = setup_idle_mode_text_231,
	.pdu_len = sizeof(setup_idle_mode_text_231),
	.qualifier = 0x00,
	.text = "Idle text",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_241 = {
	.pdu = setup_idle_mode_text_241,
	.pdu_len = sizeof(setup_idle_mode_text_241),
	.qualifier = 0x00,
	.text = "",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	},
	.status = STK_PARSE_RESULT_DATA_NOT_UNDERSTOOD
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_311 = {
	.pdu = setup_idle_mode_text_311,
	.pdu_len = sizeof(setup_idle_mode_text_311),
	.qualifier = 0x00,
	.text = "ЗДРАВСТВУЙТЕ"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_411 = {
	.pdu = setup_idle_mode_text_411,
	.pdu_len = sizeof(setup_idle_mode_text_411),
	.qualifier = 0x00,
	.text = "Idle Mode Text 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 1</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_412 = {
	.pdu = setup_idle_mode_text_412,
	.pdu_len = sizeof(setup_idle_mode_text_412),
	.qualifier = 0x00,
	.text = "Idle Mode Text 2"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_421 = {
	.pdu = setup_idle_mode_text_421,
	.pdu_len = sizeof(setup_idle_mode_text_421),
	.qualifier = 0x00,
	.text = "Idle Mode Text 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x01, 0xB4 }
	},
	.html = "<div style=\"text-align: center;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 1</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_422 = {
	.pdu = setup_idle_mode_text_422,
	.pdu_len = sizeof(setup_idle_mode_text_422),
	.qualifier = 0x00,
	.text = "Idle Mode Text 2"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_431 = {
	.pdu = setup_idle_mode_text_431,
	.pdu_len = sizeof(setup_idle_mode_text_431),
	.qualifier = 0x00,
	.text = "Idle Mode Text 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x02, 0xB4 }
	},
	.html = "<div style=\"text-align: right;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 1</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_432 = {
	.pdu = setup_idle_mode_text_432,
	.pdu_len = sizeof(setup_idle_mode_text_432),
	.qualifier = 0x00,
	.text = "Idle Mode Text 2"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_441 = {
	.pdu = setup_idle_mode_text_441,
	.pdu_len = sizeof(setup_idle_mode_text_441),
	.qualifier = 0x00,
	.text = "Idle Mode Text 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x04, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"big;color: #347235;background-color: #FFFF00;\">"
		"Idle Mode Text 1</span></div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_442 = {
	.pdu = setup_idle_mode_text_442,
	.pdu_len = sizeof(setup_idle_mode_text_442),
	.qualifier = 0x00,
	.text = "Idle Mode Text 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 2</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_443 = {
	.pdu = setup_idle_mode_text_443,
	.pdu_len = sizeof(setup_idle_mode_text_443),
	.qualifier = 0x00,
	.text = "Idle Mode Text 3"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_451 = {
	.pdu = setup_idle_mode_text_451,
	.pdu_len = sizeof(setup_idle_mode_text_451),
	.qualifier = 0x00,
	.text = "Idle Mode Text 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x08, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-size: "
		"small;color: #347235;background-color: #FFFF00;\">"
		"Idle Mode Text 1</span></div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_452 = {
	.pdu = setup_idle_mode_text_452,
	.pdu_len = sizeof(setup_idle_mode_text_452),
	.qualifier = 0x00,
	.text = "Idle Mode Text 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 2</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_453 = {
	.pdu = setup_idle_mode_text_453,
	.pdu_len = sizeof(setup_idle_mode_text_453),
	.qualifier = 0x00,
	.text = "Idle Mode Text 3"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_461 = {
	.pdu = setup_idle_mode_text_461,
	.pdu_len = sizeof(setup_idle_mode_text_461),
	.qualifier = 0x00,
	.text = "Idle Mode Text 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x10, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-weight: "
		"bold;color: #347235;background-color: #FFFF00;\">"
		"Idle Mode Text 1</span></div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_462 = {
	.pdu = setup_idle_mode_text_462,
	.pdu_len = sizeof(setup_idle_mode_text_462),
	.qualifier = 0x00,
	.text = "Idle Mode Text 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 2</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_463 = {
	.pdu = setup_idle_mode_text_463,
	.pdu_len = sizeof(setup_idle_mode_text_463),
	.qualifier = 0x00,
	.text = "Idle Mode Text 3"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_471 = {
	.pdu = setup_idle_mode_text_471,
	.pdu_len = sizeof(setup_idle_mode_text_471),
	.qualifier = 0x00,
	.text = "Idle Mode Text 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x20, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"font-style: "
		"italic;color: #347235;background-color: #FFFF00;\">"
		"Idle Mode Text 1</span></div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_472 = {
	.pdu = setup_idle_mode_text_472,
	.pdu_len = sizeof(setup_idle_mode_text_472),
	.qualifier = 0x00,
	.text = "Idle Mode Text 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 2</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_473 = {
	.pdu = setup_idle_mode_text_473,
	.pdu_len = sizeof(setup_idle_mode_text_473),
	.qualifier = 0x00,
	.text = "Idle Mode Text 3"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_481 = {
	.pdu = setup_idle_mode_text_481,
	.pdu_len = sizeof(setup_idle_mode_text_481),
	.qualifier = 0x00,
	.text = "Idle Mode Text 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x40, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span "
		"style=\"text-decoration: underline;color: #347235;"
		"background-color: #FFFF00;\">Idle Mode Text 1</span></div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_482 = {
	.pdu = setup_idle_mode_text_482,
	.pdu_len = sizeof(setup_idle_mode_text_482),
	.qualifier = 0x00,
	.text = "Idle Mode Text 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 2</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_483 = {
	.pdu = setup_idle_mode_text_483,
	.pdu_len = sizeof(setup_idle_mode_text_483),
	.qualifier = 0x00,
	.text = "Idle Mode Text 3"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_491 = {
	.pdu = setup_idle_mode_text_491,
	.pdu_len = sizeof(setup_idle_mode_text_491),
	.qualifier = 0x00,
	.text = "Idle Mode Text 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x80, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span "
		"style=\"text-decoration: line-through;color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 1</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_492 = {
	.pdu = setup_idle_mode_text_492,
	.pdu_len = sizeof(setup_idle_mode_text_492),
	.qualifier = 0x00,
	.text = "Idle Mode Text 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 2</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_493 = {
	.pdu = setup_idle_mode_text_493,
	.pdu_len = sizeof(setup_idle_mode_text_493),
	.qualifier = 0x00,
	.text = "Idle Mode Text 3"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_4101 = {
	.pdu = setup_idle_mode_text_4101,
	.pdu_len = sizeof(setup_idle_mode_text_4101),
	.qualifier = 0x00,
	.text = "Idle Mode Text 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	},
	.html = "<div style=\"text-align: left;\"><span style=\"color: "
		"#347235;background-color: #FFFF00;\">Idle Mode Text 1</span>"
		"</div>",
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_4102 = {
	.pdu = setup_idle_mode_text_4102,
	.pdu_len = sizeof(setup_idle_mode_text_4102),
	.qualifier = 0x00,
	.text = "Idle Mode Text 2"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_511 = {
	.pdu = setup_idle_mode_text_511,
	.pdu_len = sizeof(setup_idle_mode_text_511),
	.qualifier = 0x00,
	.text = "你好"
};

static struct setup_idle_mode_text_test setup_idle_mode_text_data_611 = {
	.pdu = setup_idle_mode_text_611,
	.pdu_len = sizeof(setup_idle_mode_text_611),
	.qualifier = 0x00,
	.text = "80ル0"
};

static void test_setup_idle_mode_text(gconstpointer data)
{
	const struct setup_idle_mode_text_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == test->status);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_SETUP_IDLE_MODE_TEXT);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_text(command->setup_idle_mode_text.text, test->text);
	check_icon_id(&command->setup_idle_mode_text.icon_id, &test->icon_id);
	check_text_attr(&command->setup_idle_mode_text.text_attr,
							&test->text_attr);
	check_text_attr_html(&command->setup_idle_mode_text.text_attr,
				command->setup_idle_mode_text.text, test->html);
	check_frame_id(&command->setup_idle_mode_text.frame_id,
							&test->frame_id);

	stk_command_free(command);
}

struct run_at_command_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *alpha_id;
	char *at_command;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
	enum stk_command_parse_result status;
};

static unsigned char run_at_command_111[] = { 0xD0, 0x12, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0xA8, 0x07, 0x41, 0x54,
						0x2B, 0x43, 0x47, 0x4D, 0x49 };

static unsigned char run_at_command_121[] = { 0xD0, 0x14, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x00, 0xA8, 0x07,
						0x41, 0x54, 0x2B, 0x43, 0x47,
						0x4D, 0x49 };

static unsigned char run_at_command_131[] = { 0xD0, 0x22, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x0E, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0xA8, 0x07, 0x41,
						0x54, 0x2B, 0x43, 0x47, 0x4D,
						0x49 };

static unsigned char run_at_command_211[] = { 0xD0, 0x22, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x0A, 0x42, 0x61,
						0x73, 0x69, 0x63, 0x20, 0x49,
						0x63, 0x6F, 0x6E, 0xA8, 0x07,
						0x41, 0x54, 0x2B, 0x43, 0x47,
						0x4D, 0x49, 0x9E, 0x02, 0x00,
						0x01 };

/* The 12th byte should be 0x85, instead of 0xA8 */
static unsigned char run_at_command_221[] = { 0xD0, 0x23, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x0B, 0x43, 0x6F,
						0x6C, 0x6F, 0x75, 0x72, 0x20,
						0x49, 0x63, 0x6F, 0x6E, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0x9E, 0x02,
						0x00, 0x02 };

static unsigned char run_at_command_231[] = { 0xD0, 0x22, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x0A, 0x42, 0x61,
						0x73, 0x69, 0x63, 0x20, 0x49,
						0x63, 0x6F, 0x6E, 0xA8, 0x07,
						0x41, 0x54, 0x2B, 0x43, 0x47,
						0x4D, 0x49, 0x9E, 0x02, 0x01,
						0x01 };

static unsigned char run_at_command_241[] = { 0xD0, 0x23, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x0B, 0x43, 0x6F,
						0x6C, 0x6F, 0x75, 0x72, 0x20,
						0x49, 0x63, 0x6F, 0x6E, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0x9E, 0x02,
						0x01, 0x02 };

static unsigned char run_at_command_251[] = { 0xD0, 0x16, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0xA8, 0x07, 0x41, 0x54,
						0x2B, 0x43, 0x47, 0x4D, 0x49,
						0x9E, 0x02, 0x01, 0x01 };

static unsigned char run_at_command_311[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x31, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x00, 0xB4 };

static unsigned char run_at_command_312[] = { 0xD0, 0x24, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x32, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49 };

static unsigned char run_at_command_321[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x31, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x01, 0xB4 };

static unsigned char run_at_command_322[] = { 0xD0, 0x24, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x32, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49 };

static unsigned char run_at_command_331[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x31, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x02, 0xB4 };

static unsigned char run_at_command_332[] = { 0xD0, 0x24, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x32, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49 };

static unsigned char run_at_command_341[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x31, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x04, 0xB4 };

static unsigned char run_at_command_342[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x32, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x00, 0xB4 };

static unsigned char run_at_command_343[] = { 0xD0, 0x24, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x33, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49 };

static unsigned char run_at_command_351[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x31, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x08, 0xB4 };

static unsigned char run_at_command_352[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x32, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x00, 0xB4 };

static unsigned char run_at_command_353[] = { 0xD0, 0x24, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x33, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49 };

static unsigned char run_at_command_361[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x31, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x10, 0xB4 };

static unsigned char run_at_command_362[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x32, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x00, 0xB4 };

static unsigned char run_at_command_363[] = { 0xD0, 0x24, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x33, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49 };

static unsigned char run_at_command_371[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x31, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x20, 0xB4 };

static unsigned char run_at_command_372[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x32, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x00, 0xB4 };

static unsigned char run_at_command_373[] = { 0xD0, 0x24, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x33, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49 };

static unsigned char run_at_command_381[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x31, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x40, 0xB4 };

static unsigned char run_at_command_382[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x32, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x00, 0xB4 };

static unsigned char run_at_command_383[] = { 0xD0, 0x24, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x33, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49 };

static unsigned char run_at_command_391[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x31, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x80, 0xB4 };

static unsigned char run_at_command_392[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x32, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x00, 0xB4 };

static unsigned char run_at_command_393[] = { 0xD0, 0x24, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x33, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49 };

static unsigned char run_at_command_3101[] = { 0xD0, 0x2A, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x31, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49, 0xD0, 0x04,
						0x00, 0x10, 0x00, 0xB4 };

static unsigned char run_at_command_3102[] = { 0xD0, 0x24, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x10, 0x52, 0x75,
						0x6E, 0x20, 0x41, 0x54, 0x20,
						0x43, 0x6F, 0x6D, 0x6D, 0x61,
						0x6E, 0x64, 0x20, 0x32, 0xA8,
						0x07, 0x41, 0x54, 0x2B, 0x43,
						0x47, 0x4D, 0x49 };

/* The 2nd byte (total size) should be 0x2D, instead of 0x21 */
static unsigned char run_at_command_411[] = { 0xD0, 0x2D, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x19, 0x80, 0x04,
						0x17, 0x04, 0x14, 0x04, 0x20,
						0x04, 0x10, 0x04, 0x12, 0x04,
						0x21, 0x04, 0x22, 0x04, 0x12,
						0x04, 0x23, 0x04, 0x19, 0x04,
						0x22, 0x04, 0x15, 0xA8, 0x07,
						0x41, 0x54, 0x2B, 0x43, 0x47,
						0x4D, 0x49 };

static unsigned char run_at_command_511[] = { 0xD0, 0x19, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x05, 0x80, 0x4F,
						0x60, 0x59, 0x7D, 0xA8, 0x07,
						0x41, 0x54, 0x2B, 0x43, 0x47,
						0x4D, 0x49 };

static unsigned char run_at_command_611[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01,
						0x34, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x85, 0x07, 0x80, 0x00,
						0x38, 0x00, 0x30, 0x30, 0xEB,
						0xA8, 0x07, 0x41, 0x54, 0x2B,
						0x43, 0x47, 0x4D, 0x49 };

static struct run_at_command_test run_at_command_data_111 = {
	.pdu = run_at_command_111,
	.pdu_len = sizeof(run_at_command_111),
	.qualifier = 0x00,
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_121 = {
	.pdu = run_at_command_121,
	.pdu_len = sizeof(run_at_command_121),
	.qualifier = 0x00,
	.alpha_id = "",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_131 = {
	.pdu = run_at_command_131,
	.pdu_len = sizeof(run_at_command_131),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_211 = {
	.pdu = run_at_command_211,
	.pdu_len = sizeof(run_at_command_211),
	.qualifier = 0x00,
	.alpha_id = "Basic Icon",
	.at_command = "AT+CGMI",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct run_at_command_test run_at_command_data_221 = {
	.pdu = run_at_command_221,
	.pdu_len = sizeof(run_at_command_221),
	.qualifier = 0x00,
	.alpha_id = "Colour Icon",
	.at_command = "AT+CGMI",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct run_at_command_test run_at_command_data_231 = {
	.pdu = run_at_command_231,
	.pdu_len = sizeof(run_at_command_231),
	.qualifier = 0x00,
	.alpha_id = "Basic Icon",
	.at_command = "AT+CGMI",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

/* The qualifier of icon_id should be non self-explanatory */
static struct run_at_command_test run_at_command_data_241 = {
	.pdu = run_at_command_241,
	.pdu_len = sizeof(run_at_command_241),
	.qualifier = 0x00,
	.alpha_id = "Colour Icon",
	.at_command = "AT+CGMI",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct run_at_command_test run_at_command_data_251 = {
	.pdu = run_at_command_251,
	.pdu_len = sizeof(run_at_command_251),
	.qualifier = 0x00,
	.at_command = "AT+CGMI",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	},
	.status = STK_PARSE_RESULT_DATA_NOT_UNDERSTOOD
};

static struct run_at_command_test run_at_command_data_311 = {
	.pdu = run_at_command_311,
	.pdu_len = sizeof(run_at_command_311),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 1",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_312 = {
	.pdu = run_at_command_312,
	.pdu_len = sizeof(run_at_command_312),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 2",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_321 = {
	.pdu = run_at_command_321,
	.pdu_len = sizeof(run_at_command_321),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 1",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x01, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_322 = {
	.pdu = run_at_command_322,
	.pdu_len = sizeof(run_at_command_322),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 2",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_331 = {
	.pdu = run_at_command_331,
	.pdu_len = sizeof(run_at_command_331),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 1",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x02, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_332 = {
	.pdu = run_at_command_332,
	.pdu_len = sizeof(run_at_command_332),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 2",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_341 = {
	.pdu = run_at_command_341,
	.pdu_len = sizeof(run_at_command_341),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 1",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x04, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_342 = {
	.pdu = run_at_command_342,
	.pdu_len = sizeof(run_at_command_342),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 2",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_343 = {
	.pdu = run_at_command_343,
	.pdu_len = sizeof(run_at_command_343),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 3",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_351 = {
	.pdu = run_at_command_351,
	.pdu_len = sizeof(run_at_command_351),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 1",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x08, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_352 = {
	.pdu = run_at_command_352,
	.pdu_len = sizeof(run_at_command_352),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 2",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_353 = {
	.pdu = run_at_command_353,
	.pdu_len = sizeof(run_at_command_353),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 3",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_361 = {
	.pdu = run_at_command_361,
	.pdu_len = sizeof(run_at_command_361),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 1",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x10, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_362 = {
	.pdu = run_at_command_362,
	.pdu_len = sizeof(run_at_command_362),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 2",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_363 = {
	.pdu = run_at_command_363,
	.pdu_len = sizeof(run_at_command_363),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 3",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_371 = {
	.pdu = run_at_command_371,
	.pdu_len = sizeof(run_at_command_371),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 1",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x20, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_372 = {
	.pdu = run_at_command_372,
	.pdu_len = sizeof(run_at_command_372),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 2",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_373 = {
	.pdu = run_at_command_373,
	.pdu_len = sizeof(run_at_command_373),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 3",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_381 = {
	.pdu = run_at_command_381,
	.pdu_len = sizeof(run_at_command_381),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 1",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x40, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_382 = {
	.pdu = run_at_command_382,
	.pdu_len = sizeof(run_at_command_382),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 2",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_383 = {
	.pdu = run_at_command_383,
	.pdu_len = sizeof(run_at_command_383),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 3",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_391 = {
	.pdu = run_at_command_391,
	.pdu_len = sizeof(run_at_command_391),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 1",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x80, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_392 = {
	.pdu = run_at_command_392,
	.pdu_len = sizeof(run_at_command_392),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 2",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_393 = {
	.pdu = run_at_command_393,
	.pdu_len = sizeof(run_at_command_393),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 3",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_3101 = {
	.pdu = run_at_command_3101,
	.pdu_len = sizeof(run_at_command_3101),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 1",
	.at_command = "AT+CGMI",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x10, 0x00, 0xB4 }
	}
};

static struct run_at_command_test run_at_command_data_3102 = {
	.pdu = run_at_command_3102,
	.pdu_len = sizeof(run_at_command_3102),
	.qualifier = 0x00,
	.alpha_id = "Run AT Command 2",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_411 = {
	.pdu = run_at_command_411,
	.pdu_len = sizeof(run_at_command_411),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_511 = {
	.pdu = run_at_command_511,
	.pdu_len = sizeof(run_at_command_511),
	.qualifier = 0x00,
	.alpha_id = "你好",
	.at_command = "AT+CGMI"
};

static struct run_at_command_test run_at_command_data_611 = {
	.pdu = run_at_command_611,
	.pdu_len = sizeof(run_at_command_611),
	.qualifier = 0x00,
	.alpha_id = "80ル",
	.at_command = "AT+CGMI"
};

static void test_run_at_command(gconstpointer data)
{
	const struct run_at_command_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == test->status);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_RUN_AT_COMMAND);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_alpha_id(command->run_at_command.alpha_id, test->alpha_id);
	check_at_command(command->run_at_command.at_command, test->at_command);
	check_icon_id(&command->run_at_command.icon_id, &test->icon_id);
	check_text_attr(&command->run_at_command.text_attr, &test->text_attr);
	check_frame_id(&command->run_at_command.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct send_dtmf_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *alpha_id;
	char *dtmf;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
};

static unsigned char send_dtmf_111[] = { 0xD0, 0x0D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0xAC, 0x02, 0xC1, 0xF2 };

static unsigned char send_dtmf_121[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x09, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0xAC, 0x05, 0x21, 0x43,
						0x65, 0x87, 0x09 };

static unsigned char send_dtmf_131[] = { 0xD0, 0x13, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x00, 0xAC, 0x06, 0xC1,
						0xCC, 0xCC, 0xCC, 0xCC, 0x2C };

static unsigned char send_dtmf_211[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0A, 0x42, 0x61, 0x73,
						0x69, 0x63, 0x20, 0x49, 0x63,
						0x6F, 0x6E, 0xAC, 0x02, 0xC1,
						0xF2, 0x9E, 0x02, 0x00, 0x01 };

static unsigned char send_dtmf_221[] = { 0xD0, 0x1E, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x43, 0x6F, 0x6C,
						0x6F, 0x75, 0x72, 0x20, 0x49,
						0x63, 0x6F, 0x6E, 0xAC, 0x02,
						0xC1, 0xF2, 0x9E, 0x02, 0x00,
						0x02 };

static unsigned char send_dtmf_231[] = { 0xD0, 0x1C, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x09, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0xAC, 0x02, 0xC1, 0xF2,
						0x9E, 0x02, 0x01, 0x01 };

static unsigned char send_dtmf_311[] = { 0xD0, 0x28, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x19, 0x80, 0x04, 0x17,
						0x04, 0x14, 0x04, 0x20, 0x04,
						0x10, 0x04, 0x12, 0x04, 0x21,
						0x04, 0x22, 0x04, 0x12, 0x04,
						0x23, 0x04, 0x19, 0x04, 0x22,
						0x04, 0x15, 0xAC, 0x02, 0xC1,
						0xF2 };

static unsigned char send_dtmf_411[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x31, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x00,
						0xB4 };

static unsigned char send_dtmf_412[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x32, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09 };

static unsigned char send_dtmf_421[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x31, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x01,
						0xB4 };

static unsigned char send_dtmf_422[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x32, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09 };

static unsigned char send_dtmf_431[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x31, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0xB0, 0x02,
						0xB4 };

static unsigned char send_dtmf_432[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x32, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09 };

static unsigned char send_dtmf_441[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x31, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x04,
						0xB4 };

static unsigned char send_dtmf_442[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x32, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x00,
						0xB4 };

static unsigned char send_dtmf_443[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x33, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09 };

static unsigned char send_dtmf_451[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x31, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x08,
						0xB4 };

static unsigned char send_dtmf_452[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x32, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x00,
						0xB4 };

static unsigned char send_dtmf_453[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x33, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09 };

/* The last 0x00 in spec should be removed. */
static unsigned char send_dtmf_461[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x31, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x10,
						0xB4 };

static unsigned char send_dtmf_462[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x32, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x00,
						0xB4 };

static unsigned char send_dtmf_463[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x33, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09 };

static unsigned char send_dtmf_471[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x31, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x20,
						0xB4 };

static unsigned char send_dtmf_472[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x32, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x00,
						0xB4 };

static unsigned char send_dtmf_473[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x33, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09 };

static unsigned char send_dtmf_481[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x31, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x40,
						0xB4 };

static unsigned char send_dtmf_482[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x32, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x00,
						0xB4 };

static unsigned char send_dtmf_483[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x33, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09 };

/* The second to the last should be 0x80 */
static unsigned char send_dtmf_491[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x31, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x80,
						0xB4 };

static unsigned char send_dtmf_492[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x32, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x00,
						0xB4 };

static unsigned char send_dtmf_493[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x33, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09 };

static unsigned char send_dtmf_4101[] = { 0xD0, 0x23, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x31, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09,
						0xD0, 0x04, 0x00, 0x0B, 0x00,
						0xB4 };

static unsigned char send_dtmf_4102[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x0B, 0x53, 0x65, 0x6E,
						0x64, 0x20, 0x44, 0x54, 0x4D,
						0x46, 0x20, 0x32, 0xAC, 0x05,
						0x21, 0x43, 0x65, 0x87, 0x09 };

static unsigned char send_dtmf_511[] = { 0xD0, 0x14, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x05, 0x80, 0x4F, 0x60,
						0x59, 0x7D, 0xAC, 0x02, 0xC1,
						0xF2 };

static unsigned char send_dtmf_611[] = { 0xD0, 0x12, 0x81, 0x03, 0x01, 0x14,
						0x00, 0x82, 0x02, 0x81, 0x83,
						0x85, 0x03, 0x80, 0x30, 0xEB,
						0xAC, 0x02, 0xC1, 0xF2 };

static struct send_dtmf_test send_dtmf_data_111 = {
	.pdu = send_dtmf_111,
	.pdu_len = sizeof(send_dtmf_111),
	.qualifier = 0x00,
	.dtmf = "1c2"
};

static struct send_dtmf_test send_dtmf_data_121 = {
	.pdu = send_dtmf_121,
	.pdu_len = sizeof(send_dtmf_121),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_131 = {
	.pdu = send_dtmf_131,
	.pdu_len = sizeof(send_dtmf_131),
	.qualifier = 0x00,
	.alpha_id = "",
	.dtmf = "1cccccccccc2"
};

static struct send_dtmf_test send_dtmf_data_211 = {
	.pdu = send_dtmf_211,
	.pdu_len = sizeof(send_dtmf_211),
	.qualifier = 0x00,
	.alpha_id = "Basic Icon",
	.dtmf = "1c2",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct send_dtmf_test send_dtmf_data_221 = {
	.pdu = send_dtmf_221,
	.pdu_len = sizeof(send_dtmf_221),
	.qualifier = 0x00,
	.alpha_id = "Colour Icon",
	.dtmf = "1c2",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x02
	}
};

static struct send_dtmf_test send_dtmf_data_231 = {
	.pdu = send_dtmf_231,
	.pdu_len = sizeof(send_dtmf_231),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF",
	.dtmf = "1c2",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct send_dtmf_test send_dtmf_data_311 = {
	.pdu = send_dtmf_311,
	.pdu_len = sizeof(send_dtmf_311),
	.qualifier = 0x00,
	.alpha_id = "ЗДРАВСТВУЙТЕ",
	.dtmf = "1c2"
};

static struct send_dtmf_test send_dtmf_data_411 = {
	.pdu = send_dtmf_411,
	.pdu_len = sizeof(send_dtmf_411),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 1",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_412 = {
	.pdu = send_dtmf_412,
	.pdu_len = sizeof(send_dtmf_412),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 2",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_421 = {
	.pdu = send_dtmf_421,
	.pdu_len = sizeof(send_dtmf_421),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 1",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x01, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_422 = {
	.pdu = send_dtmf_422,
	.pdu_len = sizeof(send_dtmf_422),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 2",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_431 = {
	.pdu = send_dtmf_431,
	.pdu_len = sizeof(send_dtmf_431),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 1",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0xB0, 0x02, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_432 = {
	.pdu = send_dtmf_432,
	.pdu_len = sizeof(send_dtmf_432),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 2",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_441 = {
	.pdu = send_dtmf_441,
	.pdu_len = sizeof(send_dtmf_441),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 1",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x04, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_442 = {
	.pdu = send_dtmf_442,
	.pdu_len = sizeof(send_dtmf_442),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 2",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_443 = {
	.pdu = send_dtmf_443,
	.pdu_len = sizeof(send_dtmf_443),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 3",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_451 = {
	.pdu = send_dtmf_451,
	.pdu_len = sizeof(send_dtmf_451),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 1",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x08, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_452 = {
	.pdu = send_dtmf_452,
	.pdu_len = sizeof(send_dtmf_452),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 2",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_453 = {
	.pdu = send_dtmf_453,
	.pdu_len = sizeof(send_dtmf_453),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 3",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_461 = {
	.pdu = send_dtmf_461,
	.pdu_len = sizeof(send_dtmf_461),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 1",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x10, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_462 = {
	.pdu = send_dtmf_462,
	.pdu_len = sizeof(send_dtmf_462),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 2",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_463 = {
	.pdu = send_dtmf_463,
	.pdu_len = sizeof(send_dtmf_463),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 3",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_471 = {
	.pdu = send_dtmf_471,
	.pdu_len = sizeof(send_dtmf_471),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 1",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x20, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_472 = {
	.pdu = send_dtmf_472,
	.pdu_len = sizeof(send_dtmf_472),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 2",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_473 = {
	.pdu = send_dtmf_473,
	.pdu_len = sizeof(send_dtmf_473),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 3",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_481 = {
	.pdu = send_dtmf_481,
	.pdu_len = sizeof(send_dtmf_481),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 1",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x40, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_482 = {
	.pdu = send_dtmf_482,
	.pdu_len = sizeof(send_dtmf_482),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 2",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_483 = {
	.pdu = send_dtmf_483,
	.pdu_len = sizeof(send_dtmf_483),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 3",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_491 = {
	.pdu = send_dtmf_491,
	.pdu_len = sizeof(send_dtmf_491),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 1",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x80, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_492 = {
	.pdu = send_dtmf_492,
	.pdu_len = sizeof(send_dtmf_492),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 2",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_493 = {
	.pdu = send_dtmf_493,
	.pdu_len = sizeof(send_dtmf_493),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 3",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_4101 = {
	.pdu = send_dtmf_4101,
	.pdu_len = sizeof(send_dtmf_4101),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 1",
	.dtmf = "1234567890",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	}
};

static struct send_dtmf_test send_dtmf_data_4102 = {
	.pdu = send_dtmf_4102,
	.pdu_len = sizeof(send_dtmf_4102),
	.qualifier = 0x00,
	.alpha_id = "Send DTMF 2",
	.dtmf = "1234567890"
};

static struct send_dtmf_test send_dtmf_data_511 = {
	.pdu = send_dtmf_511,
	.pdu_len = sizeof(send_dtmf_511),
	.qualifier = 0x00,
	.alpha_id = "你好",
	.dtmf = "1c2"
};

static struct send_dtmf_test send_dtmf_data_611 = {
	.pdu = send_dtmf_611,
	.pdu_len = sizeof(send_dtmf_611),
	.qualifier = 0x00,
	.alpha_id = "ル",
	.dtmf = "1c2"
};

static void test_send_dtmf(gconstpointer data)
{
	const struct send_dtmf_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_SEND_DTMF);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_NETWORK);

	check_alpha_id(command->send_dtmf.alpha_id, test->alpha_id);
	check_dtmf_string(command->send_dtmf.dtmf, test->dtmf);
	check_icon_id(&command->send_dtmf.icon_id, &test->icon_id);
	check_text_attr(&command->send_dtmf.text_attr, &test->text_attr);
	check_frame_id(&command->send_dtmf.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct language_notification_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char language[3];
};

static unsigned char language_notification_111[] = { 0xD0, 0x0D, 0x81, 0x03,
						0x01, 0x35, 0x01, 0x82, 0x02,
						0x81, 0x82, 0xAD, 0x02, 0x73,
						0x65 };

static unsigned char language_notification_121[] = { 0xD0, 0x09, 0x81, 0x03,
						0x01, 0x35, 0x00, 0x82, 0x02,
						0x81, 0x82 };

static struct language_notification_test language_notification_data_111 = {
	.pdu = language_notification_111,
	.pdu_len = sizeof(language_notification_111),
	.qualifier = 0x01,
	.language = "se"
};

static struct language_notification_test language_notification_data_121 = {
	.pdu = language_notification_121,
	.pdu_len = sizeof(language_notification_121),
	.qualifier = 0x00
};

static void test_language_notification(gconstpointer data)
{
	const struct language_notification_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_LANGUAGE_NOTIFICATION);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_language(command->language_notification.language, test->language);

	stk_command_free(command);
}

struct launch_browser_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	unsigned char browser_id;
	char *url;
	struct stk_common_byte_array bearer;
	struct stk_file prov_file_refs[MAX_ITEM];
	char *text_gateway_proxy_id;
	char *alpha_id;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
	struct stk_common_byte_array network_name;
	char *text_usr;
	char *text_passwd;
};

static unsigned char launch_browser_111[] = { 0xD0, 0x18, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0B,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C };

static unsigned char launch_browser_121[] = { 0xD0, 0x1F, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x12, 0x68, 0x74,
						0x74, 0x70, 0x3A, 0x2F, 0x2F,
						0x78, 0x78, 0x78, 0x2E, 0x79,
						0x79, 0x79, 0x2E, 0x7A, 0x7A,
						0x7A, 0x05, 0x00 };

static unsigned char launch_browser_131[] = { 0xD0, 0x0E, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x30, 0x01, 0x00, 0x31,
						0x00 };

static unsigned char launch_browser_141[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x32, 0x01,
						0x03, 0x0D, 0x10, 0x04, 0x61,
						0x62, 0x63, 0x2E, 0x64, 0x65,
						0x66, 0x2E, 0x67, 0x68, 0x69,
						0x2E, 0x6A, 0x6B, 0x6C };

static unsigned char launch_browser_211[] = { 0xD0, 0x18, 0x81, 0x03, 0x01,
						0x15, 0x02, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0B,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C };

static unsigned char launch_browser_221[] = { 0xD0, 0x18, 0x81, 0x03, 0x01,
						0x15, 0x03, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0B,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C };

static unsigned char launch_browser_231[] = { 0xD0, 0x0B, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00 };

static unsigned char launch_browser_311[] = { 0xD0, 0x26, 0x81, 0x03, 0x01,
						0x15, 0x02, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x19,
						0x80, 0x04, 0x17, 0x04, 0x14,
						0x04, 0x20, 0x04, 0x10, 0x04,
						0x12, 0x04, 0x21, 0x04, 0x22,
						0x04, 0x12, 0x04, 0x23, 0x04,
						0x19, 0x04, 0x22, 0x04, 0x15 };

static unsigned char launch_browser_411[] = { 0xD0, 0x21, 0x81, 0x03, 0x01,
						0x15, 0x02, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x10,
						0x4E, 0x6F, 0x74, 0x20, 0x73,
						0x65, 0x6C, 0x66, 0x20, 0x65,
						0x78, 0x70, 0x6C, 0x61, 0x6E,
						0x2E, 0x1E, 0x02, 0x01, 0x01 };

static unsigned char launch_browser_421[] = { 0xD0, 0x1D, 0x81, 0x03, 0x01,
						0x15, 0x02, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0C,
						0x53, 0x65, 0x6C, 0x66, 0x20,
						0x65, 0x78, 0x70, 0x6C, 0x61,
						0x6E, 0x2E, 0x1E, 0x02, 0x00,
						0x01 };

static unsigned char launch_browser_511[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x31, 0xD0, 0x04,
						0x00, 0x0D, 0x00, 0xB4 };

static unsigned char launch_browser_512[] = { 0xD0, 0x1A, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x32 };

static unsigned char launch_browser_521[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x31, 0xD0, 0x04,
						0x00, 0x0D, 0x01, 0xB4 };

static unsigned char launch_browser_522[] = { 0xD0, 0x1A, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x32 };

static unsigned char launch_browser_531[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x31, 0xD0, 0x04,
						0x00, 0x0D, 0x02, 0xB4 };

static unsigned char launch_browser_532[] = { 0xD0, 0x1A, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x32 };

static unsigned char launch_browser_541[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x31, 0xD0, 0x04,
						0x00, 0x0D, 0x04, 0xB4 };

static unsigned char launch_browser_542[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x32, 0xD0, 0x04,
						0x00, 0x0D, 0x00, 0xB4 };

static unsigned char launch_browser_543[] = { 0xD0, 0x1A, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x33 };

static unsigned char launch_browser_551[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x31, 0xD0, 0x04,
						0x00, 0x0D, 0x08, 0xB4 };

static unsigned char launch_browser_552[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x32, 0xD0, 0x04,
						0x00, 0x0D, 0x00, 0xB4 };

static unsigned char launch_browser_553[] = { 0xD0, 0x1A, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x33 };

static unsigned char launch_browser_561[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x31, 0xD0, 0x04,
						0x00, 0x0D, 0x10, 0xB4 };

static unsigned char launch_browser_562[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x32, 0xD0, 0x04,
						0x00, 0x0D, 0x00, 0xB4 };

static unsigned char launch_browser_563[] = { 0xD0, 0x1A, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x33 };

static unsigned char launch_browser_571[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x31, 0xD0, 0x04,
						0x00, 0x0D, 0x20, 0xB4 };

static unsigned char launch_browser_572[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x32, 0xD0, 0x04,
						0x00, 0x0D, 0x00, 0xB4 };

static unsigned char launch_browser_573[] = { 0xD0, 0x1A, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x33 };

static unsigned char launch_browser_581[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x31, 0xD0, 0x04,
						0x00, 0x0D, 0x40, 0xB4 };

static unsigned char launch_browser_582[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x32, 0xD0, 0x04,
						0x00, 0x0D, 0x00, 0xB4 };

static unsigned char launch_browser_583[] = { 0xD0, 0x1A, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x33 };

static unsigned char launch_browser_591[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x31, 0xD0, 0x04,
						0x00, 0x0D, 0x80, 0xB4 };

static unsigned char launch_browser_592[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x32, 0xD0, 0x04,
						0x00, 0x0D, 0x00, 0xB4 };

static unsigned char launch_browser_593[] = { 0xD0, 0x1A, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x33 };

static unsigned char launch_browser_5101[] = { 0xD0, 0x20, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x31, 0xD0, 0x04,
						0x00, 0x0D, 0x00, 0xB4 };

static unsigned char launch_browser_5102[] = { 0xD0, 0x1A, 0x81, 0x03, 0x01,
						0x15, 0x00, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x0D,
						0x44, 0x65, 0x66, 0x61, 0x75,
						0x6C, 0x74, 0x20, 0x55, 0x52,
						0x4C, 0x20, 0x32 };

static unsigned char launch_browser_611[] = { 0xD0, 0x12, 0x81, 0x03, 0x01,
						0x15, 0x02, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x05,
						0x80, 0x4F, 0x60, 0x59, 0x7D };

static unsigned char launch_browser_711[] = { 0xD0, 0x10, 0x81, 0x03, 0x01,
						0x15, 0x02, 0x82, 0x02, 0x81,
						0x82, 0x31, 0x00, 0x05, 0x03,
						0x80, 0x30, 0xEB };

static struct launch_browser_test launch_browser_data_111 = {
	.pdu = launch_browser_111,
	.pdu_len = sizeof(launch_browser_111),
	.qualifier = 0x00,
	.alpha_id = "Default URL"
};

static struct launch_browser_test launch_browser_data_121 = {
	.pdu = launch_browser_121,
	.pdu_len = sizeof(launch_browser_121),
	.qualifier = 0x00,
	.alpha_id = "",
	.url = "http://xxx.yyy.zzz"
};

static struct launch_browser_test launch_browser_data_131 = {
	.pdu = launch_browser_131,
	.pdu_len = sizeof(launch_browser_131),
	.qualifier = 0x00
};

static struct launch_browser_test launch_browser_data_141 = {
	.pdu = launch_browser_141,
	.pdu_len = sizeof(launch_browser_141),
	.qualifier = 0x00,
	.bearer = {
		.len = 1,
		.array = (unsigned char *) "\x03"
	},
	.text_gateway_proxy_id = "abc.def.ghi.jkl"
};

static struct launch_browser_test launch_browser_data_211 = {
	.pdu = launch_browser_211,
	.pdu_len = sizeof(launch_browser_211),
	.qualifier = 0x02,
	.alpha_id = "Default URL"
};

static struct launch_browser_test launch_browser_data_221 = {
	.pdu = launch_browser_221,
	.pdu_len = sizeof(launch_browser_221),
	.qualifier = 0x03,
	.alpha_id = "Default URL"
};

static struct launch_browser_test launch_browser_data_231 = {
	.pdu = launch_browser_231,
	.pdu_len = sizeof(launch_browser_231),
	.qualifier = 0x00
};

static struct launch_browser_test launch_browser_data_311 = {
	.pdu = launch_browser_311,
	.pdu_len = sizeof(launch_browser_311),
	.qualifier = 0x02,
	.alpha_id = "ЗДРАВСТВУЙТЕ"
};

static struct launch_browser_test launch_browser_data_411 = {
	.pdu = launch_browser_411,
	.pdu_len = sizeof(launch_browser_411),
	.qualifier = 0x02,
	.alpha_id = "Not self explan.",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_NON_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct launch_browser_test launch_browser_data_421 = {
	.pdu = launch_browser_421,
	.pdu_len = sizeof(launch_browser_421),
	.qualifier = 0x02,
	.alpha_id = "Self explan.",
	.icon_id = {
		.qualifier = STK_ICON_QUALIFIER_TYPE_SELF_EXPLANATORY,
		.id = 0x01
	}
};

static struct launch_browser_test launch_browser_data_511 = {
	.pdu = launch_browser_511,
	.pdu_len = sizeof(launch_browser_511),
	.qualifier = 0x00,
	.alpha_id = "Default URL 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x00, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_512 = {
	.pdu = launch_browser_512,
	.pdu_len = sizeof(launch_browser_512),
	.qualifier = 0x00,
	.alpha_id = "Default URL 2"
};

static struct launch_browser_test launch_browser_data_521 = {
	.pdu = launch_browser_521,
	.pdu_len = sizeof(launch_browser_521),
	.qualifier = 0x00,
	.alpha_id = "Default URL 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x01, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_522 = {
	.pdu = launch_browser_522,
	.pdu_len = sizeof(launch_browser_522),
	.qualifier = 0x00,
	.alpha_id = "Default URL 2"
};

static struct launch_browser_test launch_browser_data_531 = {
	.pdu = launch_browser_531,
	.pdu_len = sizeof(launch_browser_531),
	.qualifier = 0x00,
	.alpha_id = "Default URL 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x02, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_532 = {
	.pdu = launch_browser_532,
	.pdu_len = sizeof(launch_browser_532),
	.qualifier = 0x00,
	.alpha_id = "Default URL 2"
};

static struct launch_browser_test launch_browser_data_541 = {
	.pdu = launch_browser_541,
	.pdu_len = sizeof(launch_browser_541),
	.qualifier = 0x00,
	.alpha_id = "Default URL 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x04, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_542 = {
	.pdu = launch_browser_542,
	.pdu_len = sizeof(launch_browser_542),
	.qualifier = 0x00,
	.alpha_id = "Default URL 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x00, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_543 = {
	.pdu = launch_browser_543,
	.pdu_len = sizeof(launch_browser_543),
	.qualifier = 0x00,
	.alpha_id = "Default URL 3"
};

static struct launch_browser_test launch_browser_data_551 = {
	.pdu = launch_browser_551,
	.pdu_len = sizeof(launch_browser_551),
	.qualifier = 0x00,
	.alpha_id = "Default URL 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x08, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_552 = {
	.pdu = launch_browser_552,
	.pdu_len = sizeof(launch_browser_552),
	.qualifier = 0x00,
	.alpha_id = "Default URL 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x00, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_553 = {
	.pdu = launch_browser_553,
	.pdu_len = sizeof(launch_browser_553),
	.qualifier = 0x00,
	.alpha_id = "Default URL 3"
};

static struct launch_browser_test launch_browser_data_561 = {
	.pdu = launch_browser_561,
	.pdu_len = sizeof(launch_browser_561),
	.qualifier = 0x00,
	.alpha_id = "Default URL 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x10, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_562 = {
	.pdu = launch_browser_562,
	.pdu_len = sizeof(launch_browser_562),
	.qualifier = 0x00,
	.alpha_id = "Default URL 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x00, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_563 = {
	.pdu = launch_browser_563,
	.pdu_len = sizeof(launch_browser_563),
	.qualifier = 0x00,
	.alpha_id = "Default URL 3"
};

static struct launch_browser_test launch_browser_data_571 = {
	.pdu = launch_browser_571,
	.pdu_len = sizeof(launch_browser_571),
	.qualifier = 0x00,
	.alpha_id = "Default URL 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x20, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_572 = {
	.pdu = launch_browser_572,
	.pdu_len = sizeof(launch_browser_572),
	.qualifier = 0x00,
	.alpha_id = "Default URL 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x00, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_573 = {
	.pdu = launch_browser_573,
	.pdu_len = sizeof(launch_browser_573),
	.qualifier = 0x00,
	.alpha_id = "Default URL 3"
};

static struct launch_browser_test launch_browser_data_581 = {
	.pdu = launch_browser_581,
	.pdu_len = sizeof(launch_browser_581),
	.qualifier = 0x00,
	.alpha_id = "Default URL 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x40, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_582 = {
	.pdu = launch_browser_582,
	.pdu_len = sizeof(launch_browser_582),
	.qualifier = 0x00,
	.alpha_id = "Default URL 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x00, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_583 = {
	.pdu = launch_browser_583,
	.pdu_len = sizeof(launch_browser_583),
	.qualifier = 0x00,
	.alpha_id = "Default URL 3"
};

static struct launch_browser_test launch_browser_data_591 = {
	.pdu = launch_browser_591,
	.pdu_len = sizeof(launch_browser_591),
	.qualifier = 0x00,
	.alpha_id = "Default URL 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x80, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_592 = {
	.pdu = launch_browser_592,
	.pdu_len = sizeof(launch_browser_592),
	.qualifier = 0x00,
	.alpha_id = "Default URL 2",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x00, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_593 = {
	.pdu = launch_browser_593,
	.pdu_len = sizeof(launch_browser_593),
	.qualifier = 0x00,
	.alpha_id = "Default URL 3"
};

static struct launch_browser_test launch_browser_data_5101 = {
	.pdu = launch_browser_5101,
	.pdu_len = sizeof(launch_browser_5101),
	.qualifier = 0x00,
	.alpha_id = "Default URL 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0D, 0x00, 0xB4 }
	}
};

static struct launch_browser_test launch_browser_data_5102 = {
	.pdu = launch_browser_5102,
	.pdu_len = sizeof(launch_browser_5102),
	.qualifier = 0x00,
	.alpha_id = "Default URL 2"
};

static struct launch_browser_test launch_browser_data_611 = {
	.pdu = launch_browser_611,
	.pdu_len = sizeof(launch_browser_611),
	.qualifier = 0x02,
	.alpha_id = "你好"
};

static struct launch_browser_test launch_browser_data_711 = {
	.pdu = launch_browser_711,
	.pdu_len = sizeof(launch_browser_711),
	.qualifier = 0x02,
	.alpha_id = "ル"
};

static void test_launch_browser(gconstpointer data)
{
	const struct launch_browser_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_LAUNCH_BROWSER);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_browser_id(command->launch_browser.browser_id, test->browser_id);
	check_url(command->launch_browser.url, test->url);
	check_bearer(&command->launch_browser.bearer, &test->bearer);
	check_provisioning_file_references(
		command->launch_browser.prov_file_refs,	test->prov_file_refs);
	check_text(command->launch_browser.text_gateway_proxy_id,
						test->text_gateway_proxy_id);
	check_alpha_id(command->launch_browser.alpha_id, test->alpha_id);
	check_icon_id(&command->launch_browser.icon_id, &test->icon_id);
	check_text_attr(&command->launch_browser.text_attr, &test->text_attr);
	check_frame_id(&command->launch_browser.frame_id, &test->frame_id);
	check_text(command->launch_browser.text_usr, test->text_usr);
	check_text(command->launch_browser.text_passwd, test->text_passwd);

	stk_command_free(command);
}

struct open_channel_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	char *alpha_id;
	struct stk_icon_id icon_id;
	struct stk_bearer_description bearer_desc;
	unsigned short buf_size;
	char *apn;
	struct stk_other_address local_addr;
	char *text_usr;
	char *text_passwd;
	struct stk_uicc_te_interface uti;
	struct stk_other_address data_dest_addr;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
};

static unsigned char open_channel_211[] = { 0xD0, 0x36, 0x81, 0x03, 0x01, 0x40,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0x35, 0x07, 0x02, 0x03, 0x04,
						0x03, 0x04, 0x1F, 0x02, 0x39,
						0x02, 0x05, 0x78, 0x0D, 0x08,
						0xF4, 0x55, 0x73, 0x65, 0x72,
						0x4C, 0x6F, 0x67, 0x0D, 0x08,
						0xF4, 0x55, 0x73, 0x65, 0x72,
						0x50, 0x77, 0x64, 0x3C, 0x03,
						0x01, 0xAD, 0x9C, 0x3E, 0x05,
						0x21, 0x01, 0x01, 0x01, 0x01 };

static unsigned char open_channel_221[] = { 0xD0, 0x42, 0x81, 0x03, 0x01, 0x40,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0x35, 0x07, 0x02, 0x03, 0x04,
						0x03, 0x04, 0x1F, 0x02, 0x39,
						0x02, 0x05, 0x78, 0x47, 0x0A,
						0x06, 0x54, 0x65, 0x73, 0x74,
						0x47, 0x70, 0x02, 0x72, 0x73,
						0x0D, 0x08, 0xF4, 0x55, 0x73,
						0x65, 0x72, 0x4C, 0x6F, 0x67,
						0x0D, 0x08, 0xF4, 0x55, 0x73,
						0x65, 0x72, 0x50, 0x77, 0x64,
						0x3C, 0x03, 0x01, 0xAD, 0x9C,
						0x3E, 0x05, 0x21, 0x01, 0x01,
						0x01, 0x01 };

static unsigned char open_channel_231[] = { 0xD0, 0x4B, 0x81, 0x03, 0x01, 0x40,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0x05, 0x07, 0x4F, 0x70, 0x65,
						0x6E, 0x20, 0x49, 0x44, 0x35,
						0x07, 0x02, 0x03, 0x04, 0x03,
						0x04, 0x1F, 0x02, 0x39, 0x02,
						0x05, 0x78, 0x47, 0x0A, 0x06,
						0x54, 0x65, 0x73, 0x74, 0x47,
						0x70, 0x02, 0x72, 0x73, 0x0D,
						0x08, 0xF4, 0x55, 0x73, 0x65,
						0x72, 0x4C, 0x6F, 0x67, 0x0D,
						0x08, 0xF4, 0x55, 0x73, 0x65,
						0x72, 0x50, 0x77, 0x64, 0x3C,
						0x03, 0x01, 0xAD, 0x9C, 0x3E,
						0x05, 0x21, 0x01, 0x01, 0x01,
						0x01 };

static unsigned char open_channel_241[] = { 0xD0, 0x44, 0x81, 0x03, 0x01, 0x40,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0x05, 0x00, 0x35, 0x07, 0x02,
						0x03, 0x04, 0x03, 0x04, 0x1F,
						0x02, 0x39, 0x02, 0x05, 0x78,
						0x47, 0x0A, 0x06, 0x54, 0x65,
						0x73, 0x74, 0x47, 0x70, 0x02,
						0x72, 0x73, 0x0D, 0x08, 0xF4,
						0x55, 0x73, 0x65, 0x72, 0x4C,
						0x6F, 0x67, 0x0D, 0x08, 0xF4,
						0x55, 0x73, 0x65, 0x72, 0x50,
						0x77, 0x64, 0x3C, 0x03, 0x01,
						0xAD, 0x9C, 0x3E, 0x05, 0x21,
						0x01, 0x01, 0x01, 0x01 };

static unsigned char open_channel_511[] = { 0xD0, 0x53, 0x81, 0x03, 0x01, 0x40,
						0x01, 0x82, 0x02, 0x81, 0x82,
						0x05, 0x09, 0x4F, 0x70, 0x65,
						0x6E, 0x20, 0x49, 0x44, 0x20,
						0x31, 0x35, 0x07, 0x02, 0x03,
						0x04, 0x03, 0x04, 0x1F, 0x02,
						0x39, 0x02, 0x05, 0x78, 0x47,
						0x0A, 0x06, 0x54, 0x65, 0x73,
						0x74, 0x47, 0x70, 0x02, 0x72,
						0x73, 0x0D, 0x08, 0xF4, 0x55,
						0x73, 0x65, 0x72, 0x4C, 0x6F,
						0x67, 0x0D, 0x08, 0xF4, 0x55,
						0x73, 0x65, 0x72, 0x50, 0x77,
						0x64, 0x3C, 0x03, 0x01, 0xAD,
						0x9C, 0x3E, 0x05, 0x21, 0x01,
						0x01, 0x01, 0x01, 0xD0, 0x04,
						0x00, 0x09, 0x00, 0xB4 };

static struct open_channel_test open_channel_data_211 = {
	/*
	 * OPEN CHANNEL, immediate link establishment, GPRS, no local address
	 * no alpha identifier, no network access name
	 */
	.pdu = open_channel_211,
	.pdu_len = sizeof(open_channel_211),
	.qualifier = STK_OPEN_CHANNEL_FLAG_IMMEDIATE,
	.bearer_desc = {
			.type = STK_BEARER_TYPE_GPRS_UTRAN,
			.gprs = {
				.precedence = 3,
				.delay = 4,
				.reliability = 3,
				.peak = 4,
				.mean = 31,
				.pdp_type = 2,
			},
	},
	.buf_size = 1400,
	.text_usr = "UserLog",
	.text_passwd = "UserPwd",
	.uti = {
		.protocol = STK_TRANSPORT_PROTOCOL_UDP_CLIENT_REMOTE,
		.port = 44444,
	},
	.data_dest_addr = {
			.type = STK_ADDRESS_IPV4,
			.addr = {
				.ipv4 = 0x01010101,
			},
	},
};

static struct open_channel_test open_channel_data_221 = {
	/*
	 * OPEN CHANNEL, immediate link establishment GPRS,
	 * no alpha identifier, with network access name
	 */
	.pdu = open_channel_221,
	.pdu_len = sizeof(open_channel_221),
	.qualifier = STK_OPEN_CHANNEL_FLAG_IMMEDIATE,
	.bearer_desc = {
			.type = STK_BEARER_TYPE_GPRS_UTRAN,
			.gprs = {
				.precedence = 3,
				.delay = 4,
				.reliability = 3,
				.peak = 4,
				.mean = 31,
				.pdp_type = 2,
			},
	},
	.buf_size = 1400,
	.apn = "TestGp.rs",
	.text_usr = "UserLog",
	.text_passwd = "UserPwd",
	.uti = {
		.protocol = STK_TRANSPORT_PROTOCOL_UDP_CLIENT_REMOTE,
		.port = 44444,
	},
	.data_dest_addr = {
			.type = STK_ADDRESS_IPV4,
			.addr = {
				.ipv4 = 0x01010101,
			},
	},
};

static struct open_channel_test open_channel_data_231 = {
	/*
	 * OPEN CHANNEL, immediate link establishment, GPRS
	 * with alpha identifier
	 */
	.pdu = open_channel_231,
	.pdu_len = sizeof(open_channel_231),
	.qualifier = STK_OPEN_CHANNEL_FLAG_IMMEDIATE,
	.alpha_id = "Open ID",
	.bearer_desc = {
			.type = STK_BEARER_TYPE_GPRS_UTRAN,
			.gprs = {
				.precedence = 3,
				.delay = 4,
				.reliability = 3,
				.peak = 4,
				.mean = 31,
				.pdp_type = 2,
			},
	},
	.buf_size = 1400,
	.apn = "TestGp.rs",
	.text_usr = "UserLog",
	.text_passwd = "UserPwd",
	.uti = {
		.protocol = STK_TRANSPORT_PROTOCOL_UDP_CLIENT_REMOTE,
		.port = 44444,
	},
	.data_dest_addr = {
			.type = STK_ADDRESS_IPV4,
			.addr = {
				.ipv4 = 0x01010101,
			},
	},
};

static struct open_channel_test open_channel_data_241 = {
	/*
	 * OPEN CHANNEL, immediate link establishment, GPRS,
	 * with null alpha identifier
	 */
	.pdu = open_channel_241,
	.pdu_len = sizeof(open_channel_241),
	.qualifier = STK_OPEN_CHANNEL_FLAG_IMMEDIATE,
	.alpha_id = "",
	.bearer_desc = {
			.type = STK_BEARER_TYPE_GPRS_UTRAN,
			.gprs = {
				.precedence = 3,
				.delay = 4,
				.reliability = 3,
				.peak = 4,
				.mean = 31,
				.pdp_type = 2,
			},
	},
	.buf_size = 1400,
	.apn = "TestGp.rs",
	.text_usr = "UserLog",
	.text_passwd = "UserPwd",
	.uti = {
		.protocol = STK_TRANSPORT_PROTOCOL_UDP_CLIENT_REMOTE,
		.port = 44444,
	},
	.data_dest_addr = {
			.type = STK_ADDRESS_IPV4,
			.addr = {
				.ipv4 = 0x01010101,
			},
	},
};

static struct open_channel_test open_channel_data_511 = {
	/*
	 * OPEN CHANNEL, immediate link establishment, GPRS
	 * Text Attribute – Left Alignment
	 */
	.pdu = open_channel_511,
	.pdu_len = sizeof(open_channel_511),
	.qualifier = STK_OPEN_CHANNEL_FLAG_IMMEDIATE,
	.alpha_id = "Open ID 1",
	.bearer_desc = {
			.type = STK_BEARER_TYPE_GPRS_UTRAN,
			.gprs = {
				.precedence = 3,
				.delay = 4,
				.reliability = 3,
				.peak = 4,
				.mean = 31,
				.pdp_type = 2,
			},
	},
	.buf_size = 1400,
	.apn = "TestGp.rs",
	.text_usr = "UserLog",
	.text_passwd = "UserPwd",
	.uti = {
		.protocol = STK_TRANSPORT_PROTOCOL_UDP_CLIENT_REMOTE,
		.port = 44444,
	},
	.data_dest_addr = {
			.type = STK_ADDRESS_IPV4,
			.addr = {
				.ipv4 = 0x01010101,
			},
	},
	.text_attr = {
			.len = 4,
			.attributes = { 0x00, 0x09, 0x00, 0xB4 }
	},
};

static void test_open_channel(gconstpointer data)
{
	const struct open_channel_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_OPEN_CHANNEL);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	check_alpha_id(command->open_channel.alpha_id, test->alpha_id);
	check_icon_id(&command->open_channel.icon_id, &test->icon_id);
	check_bearer_desc(&command->open_channel.bearer_desc,
					&test->bearer_desc);
	g_assert(command->open_channel.buf_size == test->buf_size);
	check_network_access_name(command->open_channel.apn, test->apn);
	check_other_address(&command->open_channel.local_addr,
					&test->local_addr);
	check_text(command->open_channel.text_usr, test->text_usr);
	check_text(command->open_channel.text_passwd, test->text_passwd);
	check_uicc_te_interface(&command->open_channel.uti, &test->uti);
	check_other_address(&command->open_channel.data_dest_addr,
					&test->data_dest_addr);
	check_text_attr(&command->open_channel.text_attr, &test->text_attr);
	check_frame_id(&command->open_channel.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct close_channel_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	enum stk_device_identity_type dst;
	char *alpha_id;
	struct stk_icon_id icon_id;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
};

static unsigned char close_channel_111[] = { 0xD0, 0x09, 0x81, 0x03, 0x01, 0x41,
						0x00, 0x82, 0x02, 0x81, 0x21 };

static struct close_channel_test close_channel_data_111 = {
	.pdu = close_channel_111,
	.pdu_len = sizeof(close_channel_111),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CHANNEL_1,
};

static unsigned char close_channel_211[] = { 0xD0, 0x1B, 0x81, 0x03, 0x01, 0x41,
						0x00, 0x82, 0x02, 0x81, 0x21,
						0x85, 0x0A, 0x43, 0x6C, 0x6F,
						0x73, 0x65, 0x20, 0x49, 0x44,
						0x20, 0x31, 0xD0, 0x04, 0x00,
						0x0A, 0x00, 0xB4,
 };

static struct close_channel_test close_channel_data_211 = {
	.pdu = close_channel_211,
	.pdu_len = sizeof(close_channel_211),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CHANNEL_1,
	.alpha_id = "Close ID 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0A, 0x00, 0xB4 }
	},
};

static void test_close_channel(gconstpointer data)
{
	const struct close_channel_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_CLOSE_CHANNEL);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == test->dst);

	check_alpha_id(command->close_channel.alpha_id, test->alpha_id);
	check_icon_id(&command->close_channel.icon_id, &test->icon_id);
	check_text_attr(&command->close_channel.text_attr, &test->text_attr);
	check_frame_id(&command->close_channel.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct receive_data_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	enum stk_device_identity_type dst;
	char *alpha_id;
	struct stk_icon_id icon_id;
	unsigned char data_len;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
};

static unsigned char receive_data_111[] = { 0xD0, 0x0C, 0x81, 0x03, 0x01, 0x42,
						0x00, 0x82, 0x02, 0x81, 0x21,
						0xB7, 0x01, 0xC8 };

static struct receive_data_test receive_data_data_111 = {
	.pdu = receive_data_111,
	.pdu_len = sizeof(receive_data_111),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CHANNEL_1,
	.data_len = 200,
};

static unsigned char receive_data_211[] = { 0xD0, 0x22, 0x81, 0x03, 0x01, 0x42,
						0x00, 0x82, 0x02, 0x81, 0x21,
						0x85, 0x0E, 0x52, 0x65, 0x63,
						0x65, 0x69, 0x76, 0x65, 0x20,
						0x44, 0x61, 0x74, 0x61, 0x20,
						0x31, 0xB7, 0x01, 0xC8, 0xD0,
						0x04, 0x00, 0x0E, 0x00, 0xB4 };

static struct receive_data_test receive_data_data_211 = {
	.pdu = receive_data_211,
	.pdu_len = sizeof(receive_data_211),
	.qualifier = 0x00,
	.dst = STK_DEVICE_IDENTITY_TYPE_CHANNEL_1,
	.data_len = 200,
	.alpha_id = "Receive Data 1",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0E, 0x00, 0xB4 }
	},
};


static void test_receive_data(gconstpointer data)
{
	const struct receive_data_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_RECEIVE_DATA);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == test->dst);

	check_alpha_id(command->receive_data.alpha_id, test->alpha_id);
	check_icon_id(&command->receive_data.icon_id, &test->icon_id);
	check_common_byte(command->receive_data.data_len, test->data_len);
	check_text_attr(&command->receive_data.text_attr, &test->text_attr);
	check_frame_id(&command->receive_data.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct send_data_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
	enum stk_device_identity_type dst;
	char *alpha_id;
	struct stk_icon_id icon_id;
	struct stk_common_byte_array data;
	struct stk_text_attribute text_attr;
	struct stk_frame_id frame_id;
};

static unsigned char send_data_111[] = { 0xD0, 0x13, 0x81, 0x03, 0x01, 0x43,
						0x01, 0x82, 0x02, 0x81, 0x21,
						0xB6, 0x08, 0x00, 0x01, 0x02,
						0x03, 0x04, 0x05, 0x06, 0x07 };

static struct send_data_test send_data_data_111 = {
	.pdu = send_data_111,
	.pdu_len = sizeof(send_data_111),
	.qualifier = STK_SEND_DATA_IMMEDIATELY,
	.dst = STK_DEVICE_IDENTITY_TYPE_CHANNEL_1,
	.data = {
		.array = (unsigned char[8]) {
				0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
		},
		.len = 8,
	},
};

static unsigned char send_data_121[] = {
				0xD0, 0x81, 0xD4, 0x81, 0x03, 0x01, 0x43, 0x00,
				0x82, 0x02, 0x81, 0x21, 0xB6, 0x81, 0xC8, 0x00,
				0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
				0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
				0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
				0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
				0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
				0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
				0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
				0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40,
				0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
				0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50,
				0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
				0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60,
				0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
				0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70,
				0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
				0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80,
				0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
				0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90,
				0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
				0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0,
				0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
				0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0,
				0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8,
				0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0,
				0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7 };

static struct send_data_test send_data_data_121 = {
	.pdu = send_data_121,
	.pdu_len = sizeof(send_data_121),
	.qualifier = STK_SEND_DATA_STORE_DATA,
	.dst = STK_DEVICE_IDENTITY_TYPE_CHANNEL_1,
	.data = {
		.array = (unsigned char[200]) {
				0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
				0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
				0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
				0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
				0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
				0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
				0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
				0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
				0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
				0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
				0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
				0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
				0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
				0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
				0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
				0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
				0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
				0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
				0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
				0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
				0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
				0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
				0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
				0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
				0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
		},
		.len = 200,
	},
};
static unsigned char send_data_211[] = {
				0xD0, 0x26, 0x81, 0x03, 0x01, 0x43, 0x01, 0x82,
				0x02, 0x81, 0x21, 0x85, 0x0B, 0x53, 0x65, 0x6E,
				0x64, 0x20, 0x44, 0x61, 0x74, 0x61, 0x20, 0x31,
				0xB6, 0x08, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
				0x06, 0x07, 0xD0, 0x04, 0x00, 0x0B, 0x00, 0xB4,
 };

static struct send_data_test send_data_data_211 = {
	.pdu = send_data_211,
	.pdu_len = sizeof(send_data_211),
	.qualifier = STK_SEND_DATA_IMMEDIATELY,
	.dst = STK_DEVICE_IDENTITY_TYPE_CHANNEL_1,
	.alpha_id = "Send Data 1",
	.data = {
		.array = (unsigned char[8]) {
				0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
		},
		.len = 8,
	},
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x0B, 0x00, 0xB4 }
	},
};

static void test_send_data(gconstpointer data)
{
	const struct send_data_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_SEND_DATA);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == test->dst);

	check_alpha_id(command->send_data.alpha_id, test->alpha_id);
	check_icon_id(&command->send_data.icon_id, &test->icon_id);
	check_channel_data(&command->send_data.data, &test->data);
	check_text_attr(&command->send_data.text_attr, &test->text_attr);
	check_frame_id(&command->send_data.frame_id, &test->frame_id);

	stk_command_free(command);
}

struct get_channel_status_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	unsigned char qualifier;
};

static unsigned char get_channel_status_111[] = { 0xD0, 0x09, 0x81, 0x03, 0x01,
							0x44, 0x00, 0x82, 0x02,
							0x81, 0x82 };

static struct get_channel_status_test get_channel_status_data_111 = {
	.pdu = get_channel_status_111,
	.pdu_len = sizeof(get_channel_status_111),
	.qualifier = 0x00,
};

static void test_get_channel_status(gconstpointer data)
{
	const struct get_channel_status_test *test = data;
	struct stk_command *command;

	command = stk_command_new_from_pdu(test->pdu, test->pdu_len);

	g_assert(command);
	g_assert(command->status == STK_PARSE_RESULT_OK);

	g_assert(command->number == 1);
	g_assert(command->type == STK_COMMAND_TYPE_GET_CHANNEL_STATUS);
	g_assert(command->qualifier == test->qualifier);

	g_assert(command->src == STK_DEVICE_IDENTITY_TYPE_UICC);
	g_assert(command->dst == STK_DEVICE_IDENTITY_TYPE_TERMINAL);

	stk_command_free(command);
}

struct terminal_response_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	struct stk_response response;
};

static void test_terminal_response_encoding(gconstpointer data)
{
	const struct terminal_response_test *test = data;
	const unsigned char *pdu;
	unsigned int pdu_len;

	pdu = stk_pdu_from_response(&test->response, &pdu_len);

	if (test->pdu)
		g_assert(pdu);
	else
		g_assert(pdu == NULL);

	g_assert(pdu_len == test->pdu_len);
	g_assert(memcmp(pdu, test->pdu, pdu_len) == 0);
}

static const struct terminal_response_test display_text_response_data_111 = {
	.pdu = display_text_response_111,
	.pdu_len = sizeof(display_text_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_DISPLAY_TEXT,
		.qualifier = 0x80, /* Wait for user to clear */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const struct terminal_response_test display_text_response_data_121 = {
	.pdu = display_text_response_121,
	.pdu_len = sizeof(display_text_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_DISPLAY_TEXT,
		.qualifier = 0x80, /* Wait for user to clear */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TERMINAL_BUSY,
			.additional_len = 1, /* Screen is busy */
			.additional = (unsigned char *) "\1",
		},
	},
};

static const struct terminal_response_test display_text_response_data_131 = {
	.pdu = display_text_response_131,
	.pdu_len = sizeof(display_text_response_131),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_DISPLAY_TEXT,
		.qualifier = 0x81, /* Wait for user to clear, High priority */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const struct terminal_response_test display_text_response_data_151 = {
	.pdu = display_text_response_151,
	.pdu_len = sizeof(display_text_response_151),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_DISPLAY_TEXT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const struct terminal_response_test display_text_response_data_171 = {
	.pdu = display_text_response_171,
	.pdu_len = sizeof(display_text_response_171),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_DISPLAY_TEXT,
		.qualifier = 0x80, /* Wait for user to clear */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_GO_BACK,
		},
	},
};

static const struct terminal_response_test display_text_response_data_181 = {
	.pdu = display_text_response_181,
	.pdu_len = sizeof(display_text_response_181),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_DISPLAY_TEXT,
		.qualifier = 0x80, /* Wait for user to clear */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_USER_TERMINATED,
		},
	},
};

static const struct terminal_response_test display_text_response_data_191 = {
	.pdu = display_text_response_191,
	.pdu_len = sizeof(display_text_response_191),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_DISPLAY_TEXT,
		.qualifier = 0x80, /* Wait for user to clear */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_DATA_NOT_UNDERSTOOD,
		},
	},
};

static const struct terminal_response_test display_text_response_data_211 = {
	.pdu = display_text_response_211,
	.pdu_len = sizeof(display_text_response_211),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_DISPLAY_TEXT,
		.qualifier = 0x80, /* Wait for user to clear */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_RESPONSE,
		},
	},
};

static const struct terminal_response_test display_text_response_data_511b = {
	.pdu = display_text_response_511b,
	.pdu_len = sizeof(display_text_response_511b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_DISPLAY_TEXT,
		.qualifier = 0x80, /* Wait for user to clear */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
	},
};

static const struct terminal_response_test get_inkey_response_data_111 = {
	.pdu = get_inkey_response_111,
	.pdu_len = sizeof(get_inkey_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = "+",
			},
		}},
	},
};

static const struct terminal_response_test get_inkey_response_data_121 = {
	.pdu = get_inkey_response_121,
	.pdu_len = sizeof(get_inkey_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = "0",
			},
		}},
	},
};

static const struct terminal_response_test get_inkey_response_data_131 = {
	.pdu = get_inkey_response_131,
	.pdu_len = sizeof(get_inkey_response_131),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_GO_BACK,
		},
	},
};

static const struct terminal_response_test get_inkey_response_data_141 = {
	.pdu = get_inkey_response_141,
	.pdu_len = sizeof(get_inkey_response_141),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_USER_TERMINATED,
		},
	},
};

static const struct terminal_response_test get_inkey_response_data_151 = {
	.pdu = get_inkey_response_151,
	.pdu_len = sizeof(get_inkey_response_151),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x01, /* SMS alphabet */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = "q",
			},
		}},
	},
};

static const struct terminal_response_test get_inkey_response_data_161 = {
	.pdu = get_inkey_response_161,
	.pdu_len = sizeof(get_inkey_response_161),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x01, /* SMS alphabet */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = "x",
			},
		}},
	},
};

static const struct terminal_response_test get_inkey_response_data_211 = {
	.pdu = get_inkey_response_211,
	.pdu_len = sizeof(get_inkey_response_211),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_RESPONSE,
		},
	},
};

static const struct terminal_response_test get_inkey_response_data_411 = {
	.pdu = get_inkey_response_411,
	.pdu_len = sizeof(get_inkey_response_411),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x03, /* UCS2 alphabet */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = "Д",
			},
		}},
	},
};

static const struct terminal_response_test get_inkey_response_data_511 = {
	.pdu = get_inkey_response_511,
	.pdu_len = sizeof(get_inkey_response_511),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x04, /* Yes/No response */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = "Yes",
				.yesno = TRUE,
			},
		}},
	},
};

static const struct terminal_response_test get_inkey_response_data_512 = {
	.pdu = get_inkey_response_512,
	.pdu_len = sizeof(get_inkey_response_512),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x04, /* Yes/No response */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = NULL,
				.yesno = TRUE,
			},
		}},
	},
};

static const unsigned char get_inkey_response_611b[] = {
	0x81, 0x03, 0x01, 0x22, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04, 0x8d, 0x02, 0x04, 0x2b,
};

static const struct terminal_response_test get_inkey_response_data_611b = {
	.pdu = get_inkey_response_611b,
	.pdu_len = sizeof(get_inkey_response_611b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
		{ .get_inkey = {
			.text = {
				.text = "+",
			},
		}},
	},
};

static const unsigned char get_inkey_response_711[] = {
	0x81, 0x03, 0x01, 0x22, 0x80, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x13,
};

static const struct terminal_response_test get_inkey_response_data_711 = {
	.pdu = get_inkey_response_711,
	.pdu_len = sizeof(get_inkey_response_711),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x80, /* Help information available */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_HELP_REQUESTED,
		},
	},
};

static const unsigned char get_inkey_response_712[] = {
	0x81, 0x03, 0x01, 0x22, 0x80, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x8d, 0x02, 0x04, 0x2b,
};

static const struct terminal_response_test get_inkey_response_data_712 = {
	.pdu = get_inkey_response_712,
	.pdu_len = sizeof(get_inkey_response_712),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x80, /* Help information available */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = "+",
			},
		}},
	},
};

static const struct terminal_response_test get_inkey_response_data_811 = {
	.pdu = get_inkey_response_811,
	.pdu_len = sizeof(get_inkey_response_811),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_RESPONSE,
		},
		{ .get_inkey = {
			.duration = {
				.unit = STK_DURATION_TYPE_SECONDS,
				.interval = 11,
			},
		}},
	},
};

static const unsigned char get_inkey_response_912[] = {
	0x81, 0x03, 0x01, 0x22, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x8d, 0x02, 0x04, 0x23,
};

static const struct terminal_response_test get_inkey_response_data_912 = {
	.pdu = get_inkey_response_912,
	.pdu_len = sizeof(get_inkey_response_912),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = "#",
			},
		}},
	},
};

static const struct terminal_response_test get_inkey_response_data_1111 = {
	.pdu = get_inkey_response_1111,
	.pdu_len = sizeof(get_inkey_response_1111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x03, /* UCS2 alphabet */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = "好",
			},
		}},
	},
};

static const struct terminal_response_test get_inkey_response_data_1311 = {
	.pdu = get_inkey_response_1311,
	.pdu_len = sizeof(get_inkey_response_1311),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INKEY,
		.qualifier = 0x03, /* UCS2 alphabet */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_inkey = {
			.text = {
				.text = "ル",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_111 = {
	.pdu = get_input_response_111,
	.pdu_len = sizeof(get_input_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "12345",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_121 = {
	.pdu = get_input_response_121,
	.pdu_len = sizeof(get_input_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x08, /* Input is packed */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "67*#+",
				.packed = TRUE,
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_131 = {
	.pdu = get_input_response_131,
	.pdu_len = sizeof(get_input_response_131),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x01, /* Allow all SMS characters */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "AbCdE",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_141 = {
	.pdu = get_input_response_141,
	.pdu_len = sizeof(get_input_response_141),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x04, /* Hide text */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "2345678",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_151 = {
	.pdu = get_input_response_151,
	.pdu_len = sizeof(get_input_response_151),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "12345678901234567890",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_161 = {
	.pdu = get_input_response_161,
	.pdu_len = sizeof(get_input_response_161),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_GO_BACK,
		},
	},
};

static const struct terminal_response_test get_input_response_data_171 = {
	.pdu = get_input_response_171,
	.pdu_len = sizeof(get_input_response_171),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_USER_TERMINATED,
		},
	},
};

static const struct terminal_response_test get_input_response_data_181 = {
	.pdu = get_input_response_181,
	.pdu_len = sizeof(get_input_response_181),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "***1111111111###***2222222222###"
					"***3333333333###***4444444444###"
					"***5555555555###***6666666666###"
					"***7777777777###***8888888888###"
					"***9999999999###***0000000000###",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_191 = {
	/* Either get_input_response_191a or get_input_response_191b is ok */
	.pdu = get_input_response_191a,
	.pdu_len = sizeof(get_input_response_191a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00, /* Allow all SMS characters */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_211 = {
	.pdu = get_input_response_211,
	.pdu_len = sizeof(get_input_response_211),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_RESPONSE,
		},
	},
};

static const struct terminal_response_test get_input_response_data_311 = {
	.pdu = get_input_response_311,
	.pdu_len = sizeof(get_input_response_311),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x01, /* Allow all SMS characters */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "HELLO",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_411 = {
	.pdu = get_input_response_411,
	.pdu_len = sizeof(get_input_response_411),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x03, /* Allow all UCS2 characters */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "ЗДРАВСТВУЙТЕ",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_421 = {
	.pdu = get_input_response_421,
	.pdu_len = sizeof(get_input_response_421),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x03, /* Allow all UCS2 characters */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "ЗДРАВСТВУЙТЕЗДРАВСТВУЙТЕ"
					"ЗДРАВСТВУЙТЕЗДРАВСТВУЙТЕ"
					"ЗДРАВСТВУЙТЕЗДРАВСТВУЙ",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_611a = {
	.pdu = get_input_response_611a,
	.pdu_len = sizeof(get_input_response_611a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "+",
			},
		}},
	},
};

static const unsigned char get_input_response_611b[] = {
	0x81, 0x03, 0x01, 0x23, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04, 0x8d, 0x02, 0x04, 0x2b,
};

static const struct terminal_response_test get_input_response_data_611b = {
	.pdu = get_input_response_611b,
	.pdu_len = sizeof(get_input_response_611b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
		{ .get_input = {
			.text = {
				.text = "+",
			},
		}},
	},
};

static const unsigned char get_input_response_711[] = {
	0x81, 0x03, 0x01, 0x23, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x13,
};

static const struct terminal_response_test get_input_response_data_711 = {
	.pdu = get_input_response_711,
	.pdu_len = sizeof(get_input_response_711),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_HELP_REQUESTED,
		},
	},
};

static const unsigned char get_input_response_812[] = {
	0x81, 0x03, 0x01, 0x23, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x8d, 0x06, 0x04, 0x32,
	0x32, 0x32, 0x32, 0x32,
};

static const struct terminal_response_test get_input_response_data_812 = {
	.pdu = get_input_response_812,
	.pdu_len = sizeof(get_input_response_812),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "22222",
			},
		}},
	},
};

static const unsigned char get_input_response_843[] = {
	0x81, 0x03, 0x01, 0x23, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x8d, 0x06, 0x04, 0x33,
	0x33, 0x33, 0x33, 0x33,
};

static const struct terminal_response_test get_input_response_data_843 = {
	.pdu = get_input_response_843,
	.pdu_len = sizeof(get_input_response_843),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "33333",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_1011 = {
	.pdu = get_input_response_1011,
	.pdu_len = sizeof(get_input_response_1011),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x03, /* Allow all UCS2 characters */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "你好",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_1021 = {
	.pdu = get_input_response_1021,
	.pdu_len = sizeof(get_input_response_1021),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x03, /* Allow all UCS2 characters */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "你好你好你好你好你好你好"
					"你好你好你好你好你好你好"
					"你好你好你好你好你好你好"
					"你好你好你好你好你好你好"
					"你好你好你好你好你好你好"
					"你好你好你好你好你好",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_1211 = {
	.pdu = get_input_response_1211,
	.pdu_len = sizeof(get_input_response_1211),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x03, /* Allow all UCS2 characters */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "ルル",
			},
		}},
	},
};

static const struct terminal_response_test get_input_response_data_1221 = {
	.pdu = get_input_response_1221,
	.pdu_len = sizeof(get_input_response_1221),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_INPUT,
		.qualifier = 0x03, /* Allow all UCS2 characters */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .get_input = {
			.text = {
				.text = "ルルルルルルルルルルルル"
					"ルルルルルルルルルルルル"
					"ルルルルルルルルルルルル"
					"ルルルルルルルルルルルル"
					"ルルルルルルルルルルルル"
					"ルルルルルルルルルル",
			},
		}},
	},
};

static const struct terminal_response_test more_time_response_data_111 = {
	.pdu = more_time_response_111,
	.pdu_len = sizeof(more_time_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_MORE_TIME,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char send_sms_response_111[] = {
	0x81, 0x03, 0x01, 0x13, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test send_sms_response_data_111 = {
	.pdu = send_sms_response_111,
	.pdu_len = sizeof(send_sms_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SEND_SMS,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char send_sms_response_121[] = {
	0x81, 0x03, 0x01, 0x13, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test send_sms_response_data_121 = {
	.pdu = send_sms_response_121,
	.pdu_len = sizeof(send_sms_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SEND_SMS,
		.qualifier = 0x01, /* Packing required */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char send_sms_response_311b[] = {
	0x81, 0x03, 0x01, 0x13, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04,
};

static const struct terminal_response_test send_sms_response_data_311b = {
	.pdu = send_sms_response_311b,
	.pdu_len = sizeof(send_sms_response_311b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SEND_SMS,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
	},
};

static const struct terminal_response_test play_tone_response_data_111 = {
	.pdu = play_tone_response_111,
	.pdu_len = sizeof(play_tone_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PLAY_TONE,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char play_tone_response_119b[] = {
	0x81, 0x03, 0x01, 0x20, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x30,
};

static const struct terminal_response_test play_tone_response_data_119b = {
	.pdu = play_tone_response_119b,
	.pdu_len = sizeof(play_tone_response_119b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PLAY_TONE,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NOT_CAPABLE,
		},
	},
};

static const struct terminal_response_test play_tone_response_data_1114 = {
	.pdu = play_tone_response_1114,
	.pdu_len = sizeof(play_tone_response_1114),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PLAY_TONE,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_USER_TERMINATED,
		},
	},
};

static const unsigned char play_tone_response_311b[] = {
	0x81, 0x03, 0x01, 0x20, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04,
};

static const struct terminal_response_test play_tone_response_data_311b = {
	.pdu = play_tone_response_311b,
	.pdu_len = sizeof(play_tone_response_311b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PLAY_TONE,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
	},
};

static const struct terminal_response_test poll_interval_response_data_111 = {
	.pdu = poll_interval_response_111,
	.pdu_len = sizeof(poll_interval_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_POLL_INTERVAL,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .poll_interval = {
			.max_interval = {
				.unit = STK_DURATION_TYPE_SECONDS,
				.interval = 20,
			},
		}},
	},
};

/* 3GPP TS 31.124 */
static const unsigned char poll_interval_response_111a[] = {
	0x81, 0x03, 0x01, 0x03, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x84, 0x02, 0x00, 0x01,
};

static const struct terminal_response_test poll_interval_response_data_111a = {
	/* Either poll_interval_response_111a or b is ok */
	.pdu = poll_interval_response_111a,
	.pdu_len = sizeof(poll_interval_response_111a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_POLL_INTERVAL,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .poll_interval = {
			.max_interval = {
				.unit = STK_DURATION_TYPE_MINUTES,
				.interval = 1,
			},
		}},
	},
};

static const unsigned char refresh_response_111a[] = {
	0x81, 0x03, 0x01, 0x01, 0x03, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test refresh_response_data_111a = {
	.pdu = refresh_response_111a,
	.pdu_len = sizeof(refresh_response_111a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x03, /* USIM Initialization */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char refresh_response_111b[] = {
	0x81, 0x03, 0x01, 0x01, 0x03, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x03,
};

static const struct terminal_response_test refresh_response_data_111b = {
	.pdu = refresh_response_111b,
	.pdu_len = sizeof(refresh_response_111b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x03, /* USIM Initialization */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_REFRESH_WITH_EFS,
		},
	},
};

static const unsigned char refresh_response_121a[] = {
	0x81, 0x03, 0x01, 0x01, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test refresh_response_data_121a = {
	.pdu = refresh_response_121a,
	.pdu_len = sizeof(refresh_response_121a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x01, /* File Change Notification */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char refresh_response_121b[] = {
	0x81, 0x03, 0x01, 0x01, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x03,
};

static const struct terminal_response_test refresh_response_data_121b = {
	.pdu = refresh_response_121b,
	.pdu_len = sizeof(refresh_response_121b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x01, /* File Change Notification */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_REFRESH_WITH_EFS,
		},
	},
};

static const unsigned char refresh_response_131a[] = {
	0x81, 0x03, 0x01, 0x01, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test refresh_response_data_131a = {
	.pdu = refresh_response_131a,
	.pdu_len = sizeof(refresh_response_131a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x02, /* USIM Initialization & File Change */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char refresh_response_141a[] = {
	0x81, 0x03, 0x01, 0x01, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test refresh_response_data_141a = {
	.pdu = refresh_response_141a,
	.pdu_len = sizeof(refresh_response_141a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x00, /* USIM Initialization & Full File Change */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char refresh_response_141b[] = {
	0x81, 0x03, 0x01, 0x01, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x03,
};

static const struct terminal_response_test refresh_response_data_141b = {
	.pdu = refresh_response_141b,
	.pdu_len = sizeof(refresh_response_141b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x00, /* USIM Initialization & Full File Change */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_REFRESH_WITH_EFS,
		},
	},
};

static const unsigned char refresh_response_171[] = {
	0x81, 0x03, 0x01, 0x01, 0x05, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test refresh_response_data_171 = {
	.pdu = refresh_response_171,
	.pdu_len = sizeof(refresh_response_171),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x05, /* USIM Application Reset */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char refresh_response_241a[] = {
	0x81, 0x03, 0x01, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x02, 0x20, 0x02,
};

static const struct terminal_response_test refresh_response_data_241a = {
	.pdu = refresh_response_241a,
	.pdu_len = sizeof(refresh_response_241a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x06, /* 3G Session Reset */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TERMINAL_BUSY,
			.additional_len = 1, /* ME currently busy on call */
			.additional = (unsigned char *) "\2",
		},
	},
};

static const unsigned char refresh_response_241b[] = {
	0x81, 0x03, 0x01, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x02, 0x20, 0x01,
};

static const struct terminal_response_test refresh_response_data_241b = {
	.pdu = refresh_response_241b,
	.pdu_len = sizeof(refresh_response_241b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x06, /* 3G Session Reset */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TERMINAL_BUSY,
			.additional_len = 1, /* Screen is busy */
			.additional = (unsigned char *) "\1",
		},
	},
};

static const unsigned char refresh_response_311[] = {
	0x81, 0x03, 0x01, 0x01, 0x07, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x20,
};

static const struct terminal_response_test refresh_response_data_311 = {
	.pdu = refresh_response_311,
	.pdu_len = sizeof(refresh_response_311),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x07, /* Steering of roaming */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TERMINAL_BUSY,
		},
	},
};

static const unsigned char refresh_response_312[] = {
	0x81, 0x03, 0x01, 0x01, 0x07, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test refresh_response_data_312 = {
	.pdu = refresh_response_312,
	.pdu_len = sizeof(refresh_response_312),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_REFRESH,
		.qualifier = 0x07, /* Steering of roaming */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char set_up_menu_response_111[] = {
	0x81, 0x03, 0x01, 0x25, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test set_up_menu_response_data_111 = {
	.pdu = set_up_menu_response_111,
	.pdu_len = sizeof(set_up_menu_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_MENU,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char set_up_menu_response_411b[] = {
	0x81, 0x03, 0x01, 0x25, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04,
};

static const struct terminal_response_test set_up_menu_response_data_411b = {
	.pdu = set_up_menu_response_411b,
	.pdu_len = sizeof(set_up_menu_response_411b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_MENU,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
	},
};

static const unsigned char set_up_menu_response_511[] = {
	0x81, 0x03, 0x01, 0x25, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test set_up_menu_response_data_511 = {
	.pdu = set_up_menu_response_511,
	.pdu_len = sizeof(set_up_menu_response_511),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_MENU,
		.qualifier = 0x01, /* Soft key selection preferred */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char select_item_response_111[] = {
	0x81, 0x03, 0x01, 0x24, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x90, 0x01, 0x02,
};

static const struct terminal_response_test select_item_response_data_111 = {
	.pdu = select_item_response_111,
	.pdu_len = sizeof(select_item_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .select_item = {
			.item_id = 2,
		}},
	},
};

static const unsigned char select_item_response_121[] = {
	0x81, 0x03, 0x01, 0x24, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x90, 0x01, 0x3d,
};

static const struct terminal_response_test select_item_response_data_121 = {
	.pdu = select_item_response_121,
	.pdu_len = sizeof(select_item_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .select_item = {
			.item_id = 61,
		}},
	},
};

static const unsigned char select_item_response_131[] = {
	0x81, 0x03, 0x01, 0x24, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x90, 0x01, 0xfb,
};

static const struct terminal_response_test select_item_response_data_131 = {
	.pdu = select_item_response_131,
	.pdu_len = sizeof(select_item_response_131),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .select_item = {
			.item_id = 251,
		}},
	},
};

static const unsigned char select_item_response_141[] = {
	0x81, 0x03, 0x01, 0x24, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x11,
};

static const struct terminal_response_test select_item_response_data_141 = {
	/* The response can be select_item_response_141 or it can optionally
	 * have an ITEM_ID data object appended with any id (90 01 XX).  */
	.pdu = select_item_response_141,
	.pdu_len = sizeof(select_item_response_141),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_GO_BACK,
		},
	},
};

static const unsigned char select_item_response_142[] = {
	0x81, 0x03, 0x01, 0x24, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x10,
};

static const struct terminal_response_test select_item_response_data_142 = {
	/* The response can be select_item_response_142 or it can optionally
	 * have an ITEM_ID data object appended with any id (90 01 XX).  */
	.pdu = select_item_response_142,
	.pdu_len = sizeof(select_item_response_142),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_USER_TERMINATED,
		},
	},
};

static const unsigned char select_item_response_151[] = {
	0x81, 0x03, 0x01, 0x24, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x90, 0x01, 0x01,
};

static const struct terminal_response_test select_item_response_data_151 = {
	.pdu = select_item_response_151,
	.pdu_len = sizeof(select_item_response_151),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .select_item = {
			.item_id = 1,
		}},
	},
};

static const unsigned char select_item_response_311[] = {
	0x81, 0x03, 0x01, 0x24, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x90, 0x01, 0x03,
};

static const struct terminal_response_test select_item_response_data_311 = {
	.pdu = select_item_response_311,
	.pdu_len = sizeof(select_item_response_311),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .select_item = {
			.item_id = 3,
		}},
	},
};

static const unsigned char select_item_response_411[] = {
	0x81, 0x03, 0x01, 0x24, 0x80, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x13, 0x90, 0x01, 0x01,
};

static const struct terminal_response_test select_item_response_data_411 = {
	.pdu = select_item_response_411,
	.pdu_len = sizeof(select_item_response_411),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x80, /* Help information available */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_HELP_REQUESTED,
		},
		{ .select_item = {
			.item_id = 1,
		}},
	},
};

static const unsigned char select_item_response_511b[] = {
	0x81, 0x03, 0x01, 0x24, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04, 0x90, 0x01, 0x01,
};

static const struct terminal_response_test select_item_response_data_511b = {
	.pdu = select_item_response_511b,
	.pdu_len = sizeof(select_item_response_511b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
		{ .select_item = {
			.item_id = 1,
		}},
	},
};

static const unsigned char select_item_response_611[] = {
	0x81, 0x03, 0x01, 0x24, 0x03, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x90, 0x01, 0x01,
};

static const struct terminal_response_test select_item_response_data_611 = {
	.pdu = select_item_response_611,
	.pdu_len = sizeof(select_item_response_611),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x03, /* Choice of navigation options */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .select_item = {
			.item_id = 1,
		}},
	},
};

static const unsigned char select_item_response_621[] = {
	0x81, 0x03, 0x01, 0x24, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x90, 0x01, 0x01,
};

static const struct terminal_response_test select_item_response_data_621 = {
	.pdu = select_item_response_621,
	.pdu_len = sizeof(select_item_response_621),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x01, /* Choice of data values presentation */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .select_item = {
			.item_id = 1,
		}},
	},
};

static const unsigned char select_item_response_711[] = {
	0x81, 0x03, 0x01, 0x24, 0x04, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x90, 0x01, 0x01,
};

static const struct terminal_response_test select_item_response_data_711 = {
	.pdu = select_item_response_711,
	.pdu_len = sizeof(select_item_response_711),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x04, /* Selection using soft keys preferred */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .select_item = {
			.item_id = 1,
		}},
	},
};

static const unsigned char select_item_response_811[] = {
	0x81, 0x03, 0x01, 0x24, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x12,
};

static const struct terminal_response_test select_item_response_data_811 = {
	.pdu = select_item_response_811,
	.pdu_len = sizeof(select_item_response_811),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SELECT_ITEM,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_RESPONSE,
		},
	},
};

static const unsigned char set_up_call_response_111[] = {
	0x81, 0x03, 0x01, 0x10, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test set_up_call_response_data_111 = {
	.pdu = set_up_call_response_111,
	.pdu_len = sizeof(set_up_call_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x00, /* Only if not busy on another call */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char set_up_call_response_121[] = {
	0x81, 0x03, 0x01, 0x10, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x22,
};

static const struct terminal_response_test set_up_call_response_data_121 = {
	.pdu = set_up_call_response_121,
	.pdu_len = sizeof(set_up_call_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x00, /* Only if not busy on another call */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_USER_REJECT,
		},
	},
};

static const unsigned char set_up_call_response_141[] = {
	0x81, 0x03, 0x01, 0x10, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test set_up_call_response_data_141 = {
	.pdu = set_up_call_response_141,
	.pdu_len = sizeof(set_up_call_response_141),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x02, /* Put all other calls on hold */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char set_up_call_response_151[] = {
	0x81, 0x03, 0x01, 0x10, 0x04, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test set_up_call_response_data_151 = {
	.pdu = set_up_call_response_151,
	.pdu_len = sizeof(set_up_call_response_151),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x04, /* Disconnect all other calls */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char set_up_call_response_161[] = {
	0x81, 0x03, 0x01, 0x10, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x02, 0x20, 0x02,
};

static const struct terminal_response_test set_up_call_response_data_161 = {
	.pdu = set_up_call_response_161,
	.pdu_len = sizeof(set_up_call_response_161),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x00, /* Only if not busy on another call */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TERMINAL_BUSY,
			.additional_len = 1, /* ME currently busy on call */
			.additional = (unsigned char[1]) { 0x02 },
		},
	},
};

static const unsigned char set_up_call_response_171a[] = {
	0x81, 0x03, 0x01, 0x10, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x02, 0x21, 0x00,
};

static const struct terminal_response_test set_up_call_response_data_171a = {
	.pdu = set_up_call_response_171a,
	.pdu_len = sizeof(set_up_call_response_171a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x02, /* Put all other calls on hold */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NETWORK_UNAVAILABLE,
			.additional_len = 1, /* No specific cause given */
			.additional = (unsigned char[1]) { 0x00 },
		},
	},
};

static const unsigned char set_up_call_response_171b[] = {
	0x81, 0x03, 0x01, 0x10, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x02, 0x21, 0x9d,
};

static const struct terminal_response_test set_up_call_response_data_171b = {
	.pdu = set_up_call_response_171b,
	.pdu_len = sizeof(set_up_call_response_171b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x02, /* Put all other calls on hold */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NETWORK_UNAVAILABLE,
			.additional_len = 1, /* Facility rejected */
			.additional = (unsigned char[1]) { 0x9d },
		},
	},
};

static const unsigned char set_up_call_response_1101[] = {
	0x81, 0x03, 0x01, 0x10, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test set_up_call_response_data_1101 = {
	.pdu = set_up_call_response_1101,
	.pdu_len = sizeof(set_up_call_response_1101),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x01, /* Only if not busy, with redial */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char set_up_call_response_1111b[] = {
	0x81, 0x03, 0x01, 0x10, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x30,
};

static const struct terminal_response_test set_up_call_response_data_1111b = {
	.pdu = set_up_call_response_1111b,
	.pdu_len = sizeof(set_up_call_response_1111b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x00, /* Only if not busy on another call */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NOT_CAPABLE,
		},
	},
};

static const unsigned char set_up_call_response_1121[] = {
	0x81, 0x03, 0x01, 0x10, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x02, 0x21, 0x91,
};

static const struct terminal_response_test set_up_call_response_data_1121 = {
	.pdu = set_up_call_response_1121,
	.pdu_len = sizeof(set_up_call_response_1121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x01, /* Only if not busy, with redial */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NETWORK_UNAVAILABLE,
			.additional_len = 1, /* User busy */
			.additional = (unsigned char[1]) { 0x91 },
		},
	},
};

static const unsigned char set_up_call_response_311b[] = {
	0x81, 0x03, 0x01, 0x10, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04,
};

static const struct terminal_response_test set_up_call_response_data_311b = {
	.pdu = set_up_call_response_311b,
	.pdu_len = sizeof(set_up_call_response_311b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_CALL,
		.qualifier = 0x00, /* Only if not busy on another call */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
	},
};

static const unsigned char polling_off_response_112[] = {
	0x81, 0x03, 0x01, 0x04, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test polling_off_response_data_112 = {
	.pdu = polling_off_response_112,
	.pdu_len = sizeof(polling_off_response_112),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_POLLING_OFF,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char provide_local_info_response_111a[] = {
	0x81, 0x03, 0x01, 0x26, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x93, 0x07, 0x00, 0xf1,
	0x10, 0x00, 0x01, 0x00, 0x01,
};

static const struct terminal_response_test
		provide_local_info_response_data_111a = {
	.pdu = provide_local_info_response_111a,
	.pdu_len = sizeof(provide_local_info_response_111a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x00, /* Location information (MCC MNC LAC CI) */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .location = {
				.mcc = "001",
				.mnc = "01",
				.lac_tac = 0x0001,
				.has_ci = TRUE,
				.ci = 0x0001,
			}},
		}},
	},
};

static const unsigned char provide_local_info_response_111b[] = {
	0x81, 0x03, 0x01, 0x26, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x93, 0x07, 0x00, 0x11,
	0x10, 0x00, 0x01, 0x00, 0x01,
};

static const struct terminal_response_test
		provide_local_info_response_data_111b = {
	.pdu = provide_local_info_response_111b,
	.pdu_len = sizeof(provide_local_info_response_111b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x00, /* Location information (MCC MNC LAC CI) */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .location = {
				.mcc = "001",
				.mnc = "011",
				.lac_tac = 0x0001,
				.has_ci = TRUE,
				.ci = 0x0001,
			}},
		}},
	},
};

static const unsigned char provide_local_info_response_121[] = {
	0x81, 0x03, 0x01, 0x26, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x94, 0x08, 0x1a, 0x32,
	0x54, 0x76, 0x98, 0x10, 0x32, 0x54, /* Typo in TS 102 384? */
};

static const struct terminal_response_test
		provide_local_info_response_data_121 = {
	.pdu = provide_local_info_response_121,
	.pdu_len = sizeof(provide_local_info_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x01, /* IMEI of the Terminal */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .imei = "123456789012345", }
		}},
	},
};

static const unsigned char provide_local_info_response_131[] = {
	0x81, 0x03, 0x01, 0x26, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x96, 0x10, 0x34, 0x34,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9d, 0x0d,
	0x8c, 0x63, 0x58, 0xe2, 0x39, 0x8f, 0x63, 0xf9,
	0x06, 0x45, 0x91, 0xa4, 0x90,
};

static const struct terminal_response_test
		provide_local_info_response_data_131 = {
	.pdu = provide_local_info_response_131,
	.pdu_len = sizeof(provide_local_info_response_131),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x02, /* Network Measurement Results */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .nmr = {
				.nmr = {
					/* RXLEV-FULL-SERVING-CELL=52, no BA,
					 * no DTX */
					.array = (unsigned char[16]) {
						0x34, 0x34, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00,
					},
					.len = 16,
				},
				.bcch_ch_list = {
					.channels = {
						561, 565, 568, 569, 573,
						575, 577, 581, 582, 585,
					},
					.num = 10,
					.has_list = TRUE,
				},
			}},
		}},
	},
};

static const unsigned char provide_local_info_response_141[] = {
	0x81, 0x03, 0x01, 0x26, 0x03, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa6, 0x07, 0x20, 0x50,
	0x70, 0x41, 0x80, 0x71, 0xff,
};

static const struct terminal_response_test
		provide_local_info_response_data_141 = {
	.pdu = provide_local_info_response_141,
	.pdu_len = sizeof(provide_local_info_response_141),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x03, /* Date Time and Time Zone */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .datetime = {
				.year = 2, /* 2002 - 1900 - 100 */
				.month = 5,
				.day = 7,
				.hour = 14,
				.minute = 8,
				.second = 17,
				.timezone = 0xff, /* No information */
			}},
		}},
	},
};

static const unsigned char provide_local_info_response_151[] = {
	0x81, 0x03, 0x01, 0x26, 0x04, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xad, 0x02, 0x65, 0x6e,
};

static const struct terminal_response_test
		provide_local_info_response_data_151 = {
	.pdu = provide_local_info_response_151,
	.pdu_len = sizeof(provide_local_info_response_151),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x04, /* Language setting */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .language = "en", }
		}},
	},
};

static const unsigned char provide_local_info_response_161[] = {
	0x81, 0x03, 0x01, 0x26, 0x05, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xae, 0x02, 0x00, 0x00,
};

static const struct terminal_response_test
		provide_local_info_response_data_161 = {
	.pdu = provide_local_info_response_161,
	.pdu_len = sizeof(provide_local_info_response_161),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x05, /* Timing Advance */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .tadv = {
				.status = STK_ME_STATUS_IDLE,
				.advance = 0,
			}},
		}},
	},
};

static const unsigned char provide_local_info_response_171[] = {
	0x81, 0x03, 0x01, 0x26, 0x06, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x3f, 0x01, 0x03,
};

static const struct terminal_response_test
		provide_local_info_response_data_171 = {
	.pdu = provide_local_info_response_171,
	.pdu_len = sizeof(provide_local_info_response_171),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x06, /* Access technology */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .access_technology = STK_ACCESS_TECHNOLOGY_UTRAN, }
		}},
	},
};

static const unsigned char provide_local_info_response_181[] = {
	0x81, 0x03, 0x01, 0x26, 0x07, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xc6, 0x04, 0x01, 0x02,
	0x03, 0x04,
};

static const struct terminal_response_test
		provide_local_info_response_data_181 = {
	.pdu = provide_local_info_response_181,
	.pdu_len = sizeof(provide_local_info_response_181),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x07, /* ESN of the terminal */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .esn = 0x01020304, }
		}},
	},
};

static const unsigned char provide_local_info_response_191[] = {
	0x81, 0x03, 0x01, 0x26, 0x08, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xe2, 0x09, 0x13, 0x32,
	0x54, 0x76, 0x98, 0x10, 0x32, 0x54, 0xf6,
};

static const struct terminal_response_test
		provide_local_info_response_data_191 = {
	.pdu = provide_local_info_response_191,
	.pdu_len = sizeof(provide_local_info_response_191),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x08, /* IMEISV of the terminal */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .imeisv = "1234567890123456", }
		}},
	},
};

static const unsigned char provide_local_info_response_1111[] = {
	0x81, 0x03, 0x01, 0x26, 0x0a, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xe3, 0x01, 0x04,
};

static const struct terminal_response_test
		provide_local_info_response_data_1111 = {
	.pdu = provide_local_info_response_1111,
	.pdu_len = sizeof(provide_local_info_response_1111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x0a, /* Charge state of the battery */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .battery_charge = STK_BATTERY_FULL, }
		}},
	},
};

static const unsigned char provide_local_info_response_1121[] = {
	0x81, 0x03, 0x01, 0x26, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x96, 0x02, 0x80, 0x00,
	/* Intra-frequency UTRAN Measurement report in ASN.1 goes here */
	/* "The remaining bytes shall not be verified" */
};

static const struct terminal_response_test
		provide_local_info_response_data_1121 = {
	.pdu = provide_local_info_response_1121,
	.pdu_len = sizeof(provide_local_info_response_1121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x02, /* Network Measurement Results */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .nmr = {
				.nmr = {
					.array = (unsigned char[2])
						{ 0x80, 0x00 },
					.len = 2,
				},
			}},
		}},
	},
};

static const unsigned char provide_local_info_response_1131[] = {
	0x81, 0x03, 0x01, 0x26, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x96, 0x02, 0x80, 0x11,
	/* Inter-frequency UTRAN Measurement report in ASN.1 goes here */
	/* "The remaining bytes shall not be verified" */
};

static const struct terminal_response_test
		provide_local_info_response_data_1131 = {
	.pdu = provide_local_info_response_1131,
	.pdu_len = sizeof(provide_local_info_response_1131),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x02, /* Network Measurement Results */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .nmr = {
				.nmr = {
					.array = (unsigned char[2])
						{ 0x80, 0x11},
					.len = 2,
				},
			}},
		}},
	},
};

static const unsigned char provide_local_info_response_1141[] = {
	0x81, 0x03, 0x01, 0x26, 0x06, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x3f, 0x01, 0x08,
};

static const struct terminal_response_test
		provide_local_info_response_data_1141 = {
	.pdu = provide_local_info_response_1141,
	.pdu_len = sizeof(provide_local_info_response_1141),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x06, /* Access technology */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .access_technology = STK_ACCESS_TECHNOLOGY_EUTRAN, }
		}},
	},
};

static const unsigned char provide_local_info_response_1151[] = {
	0x81, 0x03, 0x01, 0x26, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x96, 0x02, 0x80, 0x00,
	/* Intra-frequency E-UTRAN Measurement report in ASN.1 goes here */
	/* "The remaining bytes shall not be verified" */
};

static const struct terminal_response_test
		provide_local_info_response_data_1151 = {
	.pdu = provide_local_info_response_1151,
	.pdu_len = sizeof(provide_local_info_response_1151),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x02, /* Network Measurement Results */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .nmr = {
				.nmr = {
					.array = (unsigned char[2])
						{ 0x80, 0x00},
					.len = 2,
				},
			}},
		}},
	},
};

static const unsigned char provide_local_info_response_1161[] = {
	0x81, 0x03, 0x01, 0x26, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x96, 0x02, 0x80, 0x11,
	/* Inter-frequency E-UTRAN Measurement report in ASN.1 goes here */
	/* "The remaining bytes shall not be verified" */
};

static const struct terminal_response_test
		provide_local_info_response_data_1161 = {
	.pdu = provide_local_info_response_1161,
	.pdu_len = sizeof(provide_local_info_response_1161),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x02, /* Network Measurement Results */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .nmr = {
				.nmr = {
					.array = (unsigned char[2])
						{ 0x80, 0x11},
					.len = 2,
				},
			}},
		}},
	},
};

static const unsigned char provide_local_info_response_1171[] = {
	0x81, 0x03, 0x01, 0x26, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0x93, 0x09, 0x00, 0xf1,
	0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1f,
	/* Typo in TS 102 223?  Byte 18 changed to 01 here */
};

static const struct terminal_response_test
		provide_local_info_response_data_1171 = {
	.pdu = provide_local_info_response_1171,
	.pdu_len = sizeof(provide_local_info_response_1171),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_PROVIDE_LOCAL_INFO,
		.qualifier = 0x00, /* Location information (MCC MNC LAC CI) */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .provide_local_info = {
			{ .location = {
				.mcc = "001",
				.mnc = "01",
				.lac_tac = 0x0001,
				.has_eutran_ci = TRUE,
				.eutran_ci = 0x0000001,
			}},
		}},
	},
};

static const unsigned char set_up_event_list_response_111[] = {
	0x81, 0x03, 0x01, 0x05, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test
		set_up_event_list_response_data_111 = {
	.pdu = set_up_event_list_response_111,
	.pdu_len = sizeof(set_up_event_list_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_EVENT_LIST,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char timer_mgmt_response_111[] = {
	0x81, 0x03, 0x01, 0x27, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x01,
};

static const struct terminal_response_test timer_mgmt_response_data_111 = {
	.pdu = timer_mgmt_response_111,
	.pdu_len = sizeof(timer_mgmt_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x00, /* Start the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 1,
		}},
	},
};

static const unsigned char timer_mgmt_response_112[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x01, 0xa5,
	0x03, 0x00, 0x30, 0x95,
};

static const struct terminal_response_test timer_mgmt_response_data_112 = {
	.pdu = timer_mgmt_response_112,
	.pdu_len = sizeof(timer_mgmt_response_112),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 1,
			.value = {
				.minute = 3,
				.second = 59,
				.has_value = TRUE,
			},
		}},
	},
};

static const unsigned char timer_mgmt_response_114[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x01, 0xa5,
	0x03, 0x00, 0x00, 0x95,
};

static const struct terminal_response_test timer_mgmt_response_data_114 = {
	.pdu = timer_mgmt_response_114,
	.pdu_len = sizeof(timer_mgmt_response_114),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 1,
			.value = {
				.second = 59,
				.has_value = TRUE,
			},
		}},
	},
};

static const unsigned char timer_mgmt_response_121[] = {
	0x81, 0x03, 0x01, 0x27, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x02,
};

static const struct terminal_response_test timer_mgmt_response_data_121 = {
	.pdu = timer_mgmt_response_121,
	.pdu_len = sizeof(timer_mgmt_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x00, /* Start the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 2,
		}},
	},
};

static const unsigned char timer_mgmt_response_122[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x02, 0xa5,
	0x03, 0x32, 0x85, 0x85,
};

static const struct terminal_response_test timer_mgmt_response_data_122 = {
	.pdu = timer_mgmt_response_122,
	.pdu_len = sizeof(timer_mgmt_response_122),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 2,
			.value = {
				.hour = 23,
				.minute = 58,
				.second = 58,
				.has_value = TRUE,
			},
		}},
	},
};

static const unsigned char timer_mgmt_response_124[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x02, 0xa5,
	0x03, 0x00, 0x00, 0x95,
};

static const struct terminal_response_test timer_mgmt_response_data_124 = {
	.pdu = timer_mgmt_response_124,
	.pdu_len = sizeof(timer_mgmt_response_124),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 2,
			.value = {
				.second = 59,
				.has_value = TRUE,
			},
		}},
	},
};

static const unsigned char timer_mgmt_response_131[] = {
	0x81, 0x03, 0x01, 0x27, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x08,
};

static const struct terminal_response_test timer_mgmt_response_data_131 = {
	.pdu = timer_mgmt_response_131,
	.pdu_len = sizeof(timer_mgmt_response_131),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x00, /* Start the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 8,
		}},
	},
};

static const unsigned char timer_mgmt_response_132[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x08, 0xa5,
	0x03, 0x00, 0x81, 0x95,
};

static const struct terminal_response_test timer_mgmt_response_data_132 = {
	.pdu = timer_mgmt_response_132,
	.pdu_len = sizeof(timer_mgmt_response_132),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 8,
			.value = {
				.minute = 18,
				.second = 59,
				.has_value = TRUE,
			},
		}},
	},
};

static const unsigned char timer_mgmt_response_134[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x08, 0xa5,
	0x03, 0x00, 0x95, 0x92,
};

static const struct terminal_response_test timer_mgmt_response_data_134 = {
	.pdu = timer_mgmt_response_134,
	.pdu_len = sizeof(timer_mgmt_response_134),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 8,
			.value = {
				.minute = 59,
				.second = 29,
				.has_value = TRUE,
			},
		}},
	},
};

static const unsigned char timer_mgmt_response_141a[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x01,
};

static const struct terminal_response_test timer_mgmt_response_data_141a = {
	.pdu = timer_mgmt_response_141a,
	.pdu_len = sizeof(timer_mgmt_response_141a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 1,
		}},
	},
};

static const unsigned char timer_mgmt_response_141b[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24,
};

static const struct terminal_response_test timer_mgmt_response_data_141b = {
	.pdu = timer_mgmt_response_141b,
	.pdu_len = sizeof(timer_mgmt_response_141b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
	},
};

static const unsigned char timer_mgmt_response_142a[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x02,
};

static const struct terminal_response_test timer_mgmt_response_data_142a = {
	.pdu = timer_mgmt_response_142a,
	.pdu_len = sizeof(timer_mgmt_response_142a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 2,
		}},
	},
};

static const unsigned char timer_mgmt_response_143a[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x03,
};

static const struct terminal_response_test timer_mgmt_response_data_143a = {
	.pdu = timer_mgmt_response_143a,
	.pdu_len = sizeof(timer_mgmt_response_143a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 3,
		}},
	},
};

static const unsigned char timer_mgmt_response_144a[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x04,
};

static const struct terminal_response_test timer_mgmt_response_data_144a = {
	.pdu = timer_mgmt_response_144a,
	.pdu_len = sizeof(timer_mgmt_response_144a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 4,
		}},
	},
};

static const unsigned char timer_mgmt_response_145a[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x05,
};

static const struct terminal_response_test timer_mgmt_response_data_145a = {
	.pdu = timer_mgmt_response_145a,
	.pdu_len = sizeof(timer_mgmt_response_145a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 5,
		}},
	},
};

static const unsigned char timer_mgmt_response_146a[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x06,
};

static const struct terminal_response_test timer_mgmt_response_data_146a = {
	.pdu = timer_mgmt_response_146a,
	.pdu_len = sizeof(timer_mgmt_response_146a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 6,
		}},
	},
};

static const unsigned char timer_mgmt_response_147a[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x07,
};

static const struct terminal_response_test timer_mgmt_response_data_147a = {
	.pdu = timer_mgmt_response_147a,
	.pdu_len = sizeof(timer_mgmt_response_147a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 7,
		}},
	},
};

static const unsigned char timer_mgmt_response_148a[] = {
	0x81, 0x03, 0x01, 0x27, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x08,
};

static const struct terminal_response_test timer_mgmt_response_data_148a = {
	.pdu = timer_mgmt_response_148a,
	.pdu_len = sizeof(timer_mgmt_response_148a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x02, /* Get the current value of the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 8,
		}},
	},
};

static const unsigned char timer_mgmt_response_151a[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x01,
};

static const struct terminal_response_test timer_mgmt_response_data_151a = {
	.pdu = timer_mgmt_response_151a,
	.pdu_len = sizeof(timer_mgmt_response_151a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 1,
		}},
	},
};

static const unsigned char timer_mgmt_response_151b[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24,
};

static const struct terminal_response_test timer_mgmt_response_data_151b = {
	.pdu = timer_mgmt_response_151b,
	.pdu_len = sizeof(timer_mgmt_response_151b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
	},
};

static const unsigned char timer_mgmt_response_152a[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x02,
};

static const struct terminal_response_test timer_mgmt_response_data_152a = {
	.pdu = timer_mgmt_response_152a,
	.pdu_len = sizeof(timer_mgmt_response_152a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 2,
		}},
	},
};

static const unsigned char timer_mgmt_response_153a[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x03,
};

static const struct terminal_response_test timer_mgmt_response_data_153a = {
	.pdu = timer_mgmt_response_153a,
	.pdu_len = sizeof(timer_mgmt_response_153a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 3,
		}},
	},
};

static const unsigned char timer_mgmt_response_154a[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x04,
};

static const struct terminal_response_test timer_mgmt_response_data_154a = {
	.pdu = timer_mgmt_response_154a,
	.pdu_len = sizeof(timer_mgmt_response_154a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 4,
		}},
	},
};

static const unsigned char timer_mgmt_response_155a[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x05,
};

static const struct terminal_response_test timer_mgmt_response_data_155a = {
	.pdu = timer_mgmt_response_155a,
	.pdu_len = sizeof(timer_mgmt_response_155a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 5,
		}},
	},
};

static const unsigned char timer_mgmt_response_156a[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x06,
};

static const struct terminal_response_test timer_mgmt_response_data_156a = {
	.pdu = timer_mgmt_response_156a,
	.pdu_len = sizeof(timer_mgmt_response_156a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 6,
		}},
	},
};

static const unsigned char timer_mgmt_response_157a[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x07,
};

static const struct terminal_response_test timer_mgmt_response_data_157a = {
	.pdu = timer_mgmt_response_157a,
	.pdu_len = sizeof(timer_mgmt_response_157a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 7,
		}},
	},
};

static const unsigned char timer_mgmt_response_158a[] = {
	0x81, 0x03, 0x01, 0x27, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x24, 0xa4, 0x01, 0x08,
};

static const struct terminal_response_test timer_mgmt_response_data_158a = {
	.pdu = timer_mgmt_response_158a,
	.pdu_len = sizeof(timer_mgmt_response_158a),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x01, /* Deactivate the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TIMER_CONFLICT,
		},
		{ .timer_mgmt = {
			.id = 8,
		}},
	},
};

static const unsigned char timer_mgmt_response_163[] = {
	0x81, 0x03, 0x01, 0x27, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x03,
};

static const struct terminal_response_test timer_mgmt_response_data_163 = {
	.pdu = timer_mgmt_response_163,
	.pdu_len = sizeof(timer_mgmt_response_163),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x00, /* Start the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 3,
		}},
	},
};

static const unsigned char timer_mgmt_response_164[] = {
	0x81, 0x03, 0x01, 0x27, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x04,
};

static const struct terminal_response_test timer_mgmt_response_data_164 = {
	.pdu = timer_mgmt_response_164,
	.pdu_len = sizeof(timer_mgmt_response_164),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x00, /* Start the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 4,
		}},
	},
};

static const unsigned char timer_mgmt_response_165[] = {
	0x81, 0x03, 0x01, 0x27, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x05,
};

static const struct terminal_response_test timer_mgmt_response_data_165 = {
	.pdu = timer_mgmt_response_165,
	.pdu_len = sizeof(timer_mgmt_response_165),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x00, /* Start the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 5,
		}},
	},
};

static const unsigned char timer_mgmt_response_166[] = {
	0x81, 0x03, 0x01, 0x27, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x06,
};

static const struct terminal_response_test timer_mgmt_response_data_166 = {
	.pdu = timer_mgmt_response_166,
	.pdu_len = sizeof(timer_mgmt_response_166),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x00, /* Start the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 6,
		}},
	},
};

static const unsigned char timer_mgmt_response_167[] = {
	0x81, 0x03, 0x01, 0x27, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xa4, 0x01, 0x07,
};

static const struct terminal_response_test timer_mgmt_response_data_167 = {
	.pdu = timer_mgmt_response_167,
	.pdu_len = sizeof(timer_mgmt_response_167),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_TIMER_MANAGEMENT,
		.qualifier = 0x00, /* Start the Timer */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .timer_mgmt = {
			.id = 7,
		}},
	},
};

static const unsigned char set_up_idle_mode_text_response_111[] = {
	0x81, 0x03, 0x01, 0x28, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test
		set_up_idle_mode_text_response_data_111 = {
	.pdu = set_up_idle_mode_text_response_111,
	.pdu_len = sizeof(set_up_idle_mode_text_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_IDLE_MODE_TEXT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char set_up_idle_mode_text_response_211b[] = {
	0x81, 0x03, 0x01, 0x28, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04,
};

static const struct terminal_response_test
		set_up_idle_mode_text_response_data_211b = {
	.pdu = set_up_idle_mode_text_response_211b,
	.pdu_len = sizeof(set_up_idle_mode_text_response_211b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_IDLE_MODE_TEXT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
	},
};

static const unsigned char set_up_idle_mode_text_response_241[] = {
	0x81, 0x03, 0x01, 0x28, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x32,
};

static const struct terminal_response_test
		set_up_idle_mode_text_response_data_241 = {
	.pdu = set_up_idle_mode_text_response_241,
	.pdu_len = sizeof(set_up_idle_mode_text_response_241),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SETUP_IDLE_MODE_TEXT,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_DATA_NOT_UNDERSTOOD,
		},
	},
};

static const unsigned char run_at_command_response_111[] = {
	0x81, 0x03, 0x01, 0x34, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00, 0xA9, 0x05, 0x2b, 0x43,
	0x47, 0x4d, 0x49,
};

static const struct terminal_response_test run_at_command_response_data_111 = {
	.pdu = run_at_command_response_111,
	.pdu_len = sizeof(run_at_command_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_RUN_AT_COMMAND,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .run_at_command = {
			.at_response = "+CGMI",
		}},
	},
};

static const unsigned char run_at_command_response_211b[] = {
	0x81, 0x03, 0x01, 0x34, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04, 0xA9, 0x05, 0x2b, 0x43,
	0x47, 0x4d, 0x49,
};

static const struct terminal_response_test run_at_command_response_data_211b = {
	.pdu = run_at_command_response_211b,
	.pdu_len = sizeof(run_at_command_response_211b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_RUN_AT_COMMAND,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
		{ .run_at_command = {
			.at_response = "+CGMI",
		}},
	},
};

static const unsigned char run_at_command_response_251[] = {
	0x81, 0x03, 0x01, 0x34, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x32,
};

static const struct terminal_response_test run_at_command_response_data_251 = {
	.pdu = run_at_command_response_251,
	.pdu_len = sizeof(run_at_command_response_251),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_RUN_AT_COMMAND,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_DATA_NOT_UNDERSTOOD,
		},
	},
};

static const unsigned char send_dtmf_response_111[] = {
	0x81, 0x03, 0x01, 0x14, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test send_dtmf_response_data_111 = {
	.pdu = send_dtmf_response_111,
	.pdu_len = sizeof(send_dtmf_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SEND_DTMF,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char send_dtmf_response_141[] = {
	0x81, 0x03, 0x01, 0x14, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x02, 0x20, 0x07,
};

static const struct terminal_response_test send_dtmf_response_data_141 = {
	.pdu = send_dtmf_response_141,
	.pdu_len = sizeof(send_dtmf_response_141),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SEND_DTMF,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_TERMINAL_BUSY,
			.additional_len = 1, /* Not in speech call */
			.additional = (unsigned char[1]) { 0x07 },
		},
	},
};

static const unsigned char send_dtmf_response_211b[] = {
	0x81, 0x03, 0x01, 0x14, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04,
};

static const struct terminal_response_test send_dtmf_response_data_211b = {
	.pdu = send_dtmf_response_211b,
	.pdu_len = sizeof(send_dtmf_response_211b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SEND_DTMF,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
	},
};

static const unsigned char language_notification_response_111[] = {
	0x81, 0x03, 0x01, 0x35, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test
		language_notification_response_data_111 = {
	.pdu = language_notification_response_111,
	.pdu_len = sizeof(language_notification_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_LANGUAGE_NOTIFICATION,
		.qualifier = 0x01,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char language_notification_response_121[] = {
	0x81, 0x03, 0x01, 0x35, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test
		language_notification_response_data_121 = {
	.pdu = language_notification_response_121,
	.pdu_len = sizeof(language_notification_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_LANGUAGE_NOTIFICATION,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char launch_browser_response_111[] = {
	0x81, 0x03, 0x01, 0x15, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test launch_browser_response_data_111 = {
	.pdu = launch_browser_response_111,
	.pdu_len = sizeof(launch_browser_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_LAUNCH_BROWSER,
		.qualifier = 0x00, /* Launch browser, if not running */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char launch_browser_response_211[] = {
	0x81, 0x03, 0x01, 0x15, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test launch_browser_response_data_211 = {
	.pdu = launch_browser_response_211,
	.pdu_len = sizeof(launch_browser_response_211),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_LAUNCH_BROWSER,
		.qualifier = 0x02, /* Use the existing browser */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char launch_browser_response_221[] = {
	0x81, 0x03, 0x01, 0x15, 0x03, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x00,
};

static const struct terminal_response_test launch_browser_response_data_221 = {
	.pdu = launch_browser_response_221,
	.pdu_len = sizeof(launch_browser_response_221),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_LAUNCH_BROWSER,
		.qualifier = 0x03, /* Re-start browser session */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
	},
};

static const unsigned char launch_browser_response_231[] = {
	0x81, 0x03, 0x01, 0x15, 0x00, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x02, 0x26, 0x02,
};

static const struct terminal_response_test launch_browser_response_data_231 = {
	.pdu = launch_browser_response_231,
	.pdu_len = sizeof(launch_browser_response_231),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_LAUNCH_BROWSER,
		.qualifier = 0x00, /* Launch browser, if not running */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_BROWSER_TEMPORARY,
			.additional_len = 1, /* Browser unavailable */
			.additional = (unsigned char[1]) { 0x02 },
		},
	},
};

static const unsigned char launch_browser_response_411b[] = {
	0x81, 0x03, 0x01, 0x15, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x83, 0x01, 0x04,
};

static const struct terminal_response_test launch_browser_response_data_411b = {
	.pdu = launch_browser_response_411b,
	.pdu_len = sizeof(launch_browser_response_411b),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_LAUNCH_BROWSER,
		.qualifier = 0x02, /* Use the existing browser */
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_NO_ICON,
		},
	},
};

static const unsigned char open_channel_response_211[] = {
		0x81, 0x03, 0x01, 0x40, 0x01, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x01, 0x00, 0x38, 0x02, 0x81, 0x00, 0x35, 0x07, 0x02, 0x03,
		0x04, 0x03, 0x04, 0x1F, 0x02, 0x39, 0x02, 0x05, 0x78,
};

static const struct terminal_response_test open_channel_response_data_211 = {
	.pdu = open_channel_response_211,
	.pdu_len = sizeof(open_channel_response_211),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_OPEN_CHANNEL,
		.qualifier = STK_OPEN_CHANNEL_FLAG_IMMEDIATE,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .open_channel = {
			.channel = {
			.id = 1,
			.status = STK_CHANNEL_PACKET_DATA_SERVICE_ACTIVATED,
			},
			.bearer_desc = {
					.type = STK_BEARER_TYPE_GPRS_UTRAN,
					.gprs = {
						.precedence = 3,
						.delay = 4,
						.reliability = 3,
						.peak = 4,
						.mean = 31,
						.pdp_type = 2,
					},
			},
			.buf_size = 1400,
		} },
	},
};

static const unsigned char open_channel_response_271[] = {
		0x81, 0x03, 0x01, 0x40, 0x01, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x01, 0x22, 0x35, 0x07, 0x02, 0x03, 0x04, 0x03, 0x04, 0x1F,
		0x02, 0x39, 0x02, 0x05, 0x78,
};

static const struct terminal_response_test open_channel_response_data_271 = {
	.pdu = open_channel_response_271,
	.pdu_len = sizeof(open_channel_response_271),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_OPEN_CHANNEL,
		.qualifier = STK_OPEN_CHANNEL_FLAG_IMMEDIATE,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_USER_REJECT,
		},
		{ .open_channel = {
			.bearer_desc = {
					.type = STK_BEARER_TYPE_GPRS_UTRAN,
					.gprs = {
						.precedence = 3,
						.delay = 4,
						.reliability = 3,
						.peak = 4,
						.mean = 31,
						.pdp_type = 2,
					},
			},
			.buf_size = 1400,
		} },
	},
};

static const unsigned char close_channel_response_121[] = {
		0x81, 0x03, 0x01, 0x41, 0x00, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x02, 0x3A, 0x03,
};

static const struct terminal_response_test close_channel_response_data_121 = {
	.pdu = close_channel_response_121,
	.pdu_len = sizeof(close_channel_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_CLOSE_CHANNEL,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_BIP_ERROR,
			.additional_len = 1, /* Channel identifier not valid */
			.additional = (unsigned char[1]) { 0x03 },
		},
	},
};

static const unsigned char close_channel_response_131[] = {
		0x81, 0x03, 0x01, 0x41, 0x00, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x02, 0x3A, 0x02,
};

static const struct terminal_response_test close_channel_response_data_131 = {
	.pdu = close_channel_response_131,
	.pdu_len = sizeof(close_channel_response_131),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_CLOSE_CHANNEL,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_BIP_ERROR,
			.additional_len = 1, /* Channel already closed */
			.additional = (unsigned char[1]) { 0x02 },
		},
	},
};

static const unsigned char receive_data_response_111[] = {
		0x81, 0x03, 0x01, 0x42, 0x00, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x01, 0x00, 0xB6, 0x81, 0xC8, 0xc8, 0xc9, 0xca, 0xcb, 0xcc,
		0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
		0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0,
		0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
		0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4,
		0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe,
		0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
		0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
		0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
		0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
		0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
		0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44,
		0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e,
		0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
		0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62,
		0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c,
		0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76,
		0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80,
		0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
		0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0xB7, 0x01, 0xFF,
};

static const struct terminal_response_test receive_data_response_data_111 = {
	.pdu = receive_data_response_111,
	.pdu_len = sizeof(receive_data_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_RECEIVE_DATA,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .receive_data = {
				.rx_data = {
					.array = (unsigned char[200]) {
					0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd,
					0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3,
					0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
					0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
					0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5,
					0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb,
					0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1,
					0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
					0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd,
					0xfe, 0xff, 0x00, 0x01, 0x02, 0x03,
					0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
					0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
					0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
					0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
					0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21,
					0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
					0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d,
					0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33,
					0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
					0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
					0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
					0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b,
					0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51,
					0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
					0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d,
					0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63,
					0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
					0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
					0x70, 0x71, 0x72, 0x73, 0x74, 0x75,
					0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b,
					0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81,
					0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
					0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d,
					0x8e, 0x8f,
					},
					.len = 200,
				},
				.rx_remaining = 0xFF,
		} },
	},
};

static const unsigned char send_data_response_111[] = {
		0x81, 0x03, 0x01, 0x43, 0x01, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x01, 0x00, 0xB7, 0x01, 0xFF,
};

static const struct terminal_response_test send_data_response_data_111 = {
	.pdu = send_data_response_111,
	.pdu_len = sizeof(send_data_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SEND_DATA,
		.qualifier = STK_SEND_DATA_IMMEDIATELY,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .send_data = {
				/* More than 255 bytes of space available */
				.tx_avail = 0xFF,
		} },
	},
};

static const unsigned char send_data_response_121[] = {
		0x81, 0x03, 0x01, 0x43, 0x00, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x01, 0x00, 0xB7, 0x01, 0xFF,
};

static const struct terminal_response_test send_data_response_data_121 = {
	.pdu = send_data_response_121,
	.pdu_len = sizeof(send_data_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SEND_DATA,
		.qualifier = STK_SEND_DATA_STORE_DATA,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .send_data = {
				/* More than 255 bytes of space available */
				.tx_avail = 0xFF,
		} },
	},
};

static const unsigned char send_data_response_151[] = {
		0x81, 0x03, 0x01, 0x43, 0x01, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x02, 0x3A, 0x03,
};

static const struct terminal_response_test send_data_response_data_151 = {
	.pdu = send_data_response_151,
	.pdu_len = sizeof(send_data_response_151),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_SEND_DATA,
		.qualifier = STK_SEND_DATA_IMMEDIATELY,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_BIP_ERROR,
			.additional_len = 1, /* Channel identifier not valid */
			.additional = (unsigned char[1]) { 0x03 },
		},
	},
};

static const unsigned char get_channel_status_response_111[] = {
		0x81, 0x03, 0x01, 0x44, 0x00, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x01, 0x00, 0xB8, 0x02, 0x00, 0x00,
};

static const struct terminal_response_test
				get_channel_status_response_data_111 = {
	.pdu = get_channel_status_response_111,
	.pdu_len = sizeof(get_channel_status_response_111),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_CHANNEL_STATUS,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .channel_status = {
			/*
			 * No Channel available, link not established or
			 * PDP context not activated
			 */
			.channel = {
				.id = 0,
				.status =
				STK_CHANNEL_PACKET_DATA_SERVICE_NOT_ACTIVATED,
			}
		} },
	},
};

static const unsigned char get_channel_status_response_121[] = {
		0x81, 0x03, 0x01, 0x44, 0x00, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x01, 0x00, 0xB8, 0x02, 0x81, 0x00,
};

static const struct terminal_response_test
				get_channel_status_response_data_121 = {
	.pdu = get_channel_status_response_121,
	.pdu_len = sizeof(get_channel_status_response_121),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_CHANNEL_STATUS,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .channel_status = {
		/* Channel 1 open, link established or PDP context activated */
			.channel = {
				.id = 1,
				.status =
				STK_CHANNEL_PACKET_DATA_SERVICE_ACTIVATED,
			},
		} },
	},
};

static const unsigned char get_channel_status_response_131[] = {
		0x81, 0x03, 0x01, 0x44, 0x00, 0x82, 0x02, 0x82, 0x81, 0x83,
		0x01, 0x00, 0xB8, 0x02, 0x01, 0x05,
};

static const struct terminal_response_test
				get_channel_status_response_data_131 = {
	.pdu = get_channel_status_response_131,
	.pdu_len = sizeof(get_channel_status_response_131),
	.response = {
		.number = 1,
		.type = STK_COMMAND_TYPE_GET_CHANNEL_STATUS,
		.qualifier = 0x00,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		.result = {
			.type = STK_RESULT_TYPE_SUCCESS,
		},
		{ .channel_status = {
				/* Channel 1, link dropped */
				.channel = {
					.id = 1,
					.status = STK_CHANNEL_LINK_DROPPED,
				},
		} },

	},
};

struct envelope_test {
	const unsigned char *pdu;
	unsigned int pdu_len;
	struct stk_envelope envelope;
};

static void test_envelope_encoding(gconstpointer data)
{
	const struct envelope_test *test = data;
	const unsigned char *pdu;
	unsigned int pdu_len;

	pdu = stk_pdu_from_envelope(&test->envelope, &pdu_len);

	if (test->pdu)
		g_assert(pdu);
	else
		g_assert(pdu == NULL);

	g_assert(pdu_len == test->pdu_len);
	g_assert(memcmp(pdu, test->pdu, pdu_len) == 0);
}

static const unsigned char sms_pp_data_download_161[] = {
	0xd1, 0x2d, 0x82, 0x02, 0x83, 0x81, 0x06, 0x09,
	0x91, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0xf8, 0x8b, 0x1c, 0x04, 0x04, 0x91, 0x21, 0x43,
	0x7f, 0x16, 0x89, 0x10, 0x10, 0x00, 0x00, 0x00,
	0x00, 0x0d, 0x53, 0x68, 0x6f, 0x72, 0x74, 0x20,
	0x4d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65,
};

static const struct envelope_test sms_pp_data_download_data_161 = {
	.pdu = sms_pp_data_download_161,
	.pdu_len = sizeof(sms_pp_data_download_161),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_SMS_PP_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .sms_pp_download = {
			.address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "112233445566778",
			},
			.message = {
				.oaddr = {
					.number_type =
						SMS_NUMBER_TYPE_INTERNATIONAL,
					.numbering_plan =
						SMS_NUMBERING_PLAN_ISDN,
					.address = "1234",
				},
				.pid = SMS_PID_TYPE_USIM_DOWNLOAD,
				.dcs = 0x16, /* Uncompressed, Class 2, 8-bit */
				.scts = {
					.year = 98,
					.month = 1,
					.day = 1,
					.has_timezone = TRUE,
					.timezone = 0,
				},
				.udl = 13,
				.ud = "Short Message",
			},
		}},
	},
};

static const unsigned char sms_pp_data_download_162[] = {
	0xd1, 0x2d, 0x82, 0x02, 0x83, 0x81, 0x06, 0x09,
	0x91, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0xf8, 0x8b, 0x1c, 0x04, 0x04, 0x91, 0x21, 0x43,
	0x7f, 0xf6, 0x89, 0x10, 0x10, 0x00, 0x00, 0x00,
	0x00, 0x0d, 0x53, 0x68, 0x6f, 0x72, 0x74, 0x20,
	0x4d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65,
};

static const struct envelope_test sms_pp_data_download_data_162 = {
	.pdu = sms_pp_data_download_162,
	.pdu_len = sizeof(sms_pp_data_download_162),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_SMS_PP_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .sms_pp_download = {
			.address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "112233445566778",
			},
			.message = {
				.oaddr = {
					.number_type =
						SMS_NUMBER_TYPE_INTERNATIONAL,
					.numbering_plan =
						SMS_NUMBERING_PLAN_ISDN,
					.address = "1234",
				},
				.pid = SMS_PID_TYPE_USIM_DOWNLOAD,
				.dcs = 0xf6, /* Data, Class 2, 8-bit */
				.scts = {
					.year = 98,
					.month = 1,
					.day = 1,
					.has_timezone = TRUE,
					.timezone = 0,
				},
				.udl = 13,
				.ud = "Short Message",
			},
		}},
	},
};

static const unsigned char sms_pp_data_download_182[] = {
	0xd1, 0x3e, 0x82, 0x02, 0x83, 0x81, 0x06, 0x09,
	0x91, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0xf8, 0x8b, 0x2d, 0x44, 0x04, 0x91, 0x21, 0x43,
	0x7f, 0xf6, 0x89, 0x10, 0x10, 0x00, 0x00, 0x00,
	0x00, 0x1e, 0x02, 0x70, 0x00, 0x00, 0x19, 0x00,
	0x0d, 0x00, 0x00, 0x00, 0x00, 0xbf, 0xff, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0xdc, 0xdc,
	0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
};

static const struct envelope_test sms_pp_data_download_data_182 = {
	.pdu = sms_pp_data_download_182,
	.pdu_len = sizeof(sms_pp_data_download_182),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_SMS_PP_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .sms_pp_download = {
			.address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "112233445566778",
			},
			.message = {
				.udhi = TRUE,
				.oaddr = {
					.number_type =
						SMS_NUMBER_TYPE_INTERNATIONAL,
					.numbering_plan =
						SMS_NUMBERING_PLAN_ISDN,
					.address = "1234",
				},
				.pid = SMS_PID_TYPE_USIM_DOWNLOAD,
				.dcs = 0xf6, /* Data, Class 2, 8-bit */
				.scts = {
					.year = 98,
					.month = 1,
					.day = 1,
					.has_timezone = TRUE,
					.timezone = 0,
				},
				.udl = 30,
				.ud = {
					0x02, 0x70, 0x00, 0x00, 0x19, 0x00,
					0x0d, 0x00, 0x00, 0x00, 0x00, 0xbf,
					0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x01, 0x00, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
				},
			},
		}},
	},
};

static const unsigned char cbs_pp_data_download_11[] = {
	0xd2, 0x5e, 0x82, 0x02, 0x83, 0x81, 0x8c, 0x58,
	0xc0, 0x11, 0x10, 0x01, 0x01, 0x11, 0xc3, 0x32,
	0x9b, 0x0d, 0x12, 0xca, 0xdf, 0x61, 0xf2, 0x38,
	0x3c, 0xa7, 0x83, 0x40, 0x20, 0x10, 0x08, 0x04,
	0x02, 0x81, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02,
	0x81, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x81,
	0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x81, 0x40,
	0x20, 0x10, 0x08, 0x04, 0x02, 0x81, 0x40, 0x20,
	0x10, 0x08, 0x04, 0x02, 0x81, 0x40, 0x20, 0x10,
	0x08, 0x04, 0x02, 0x81, 0x40, 0x20, 0x10, 0x08,
	0x04, 0x02, 0x81, 0x40, 0x20, 0x10, 0x08, 0x04,
	0x02, 0x81, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02,
};

static const struct envelope_test cbs_pp_data_download_data_11 = {
	.pdu = cbs_pp_data_download_11,
	.pdu_len = sizeof(cbs_pp_data_download_11),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_CBS_PP_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .cbs_pp_download = {
			.page = {
				.gs = CBS_GEO_SCOPE_CELL_NORMAL,
				.message_code = 1,
				.update_number = 1,
				.message_identifier = 0x1001,
				.dcs = CBS_LANGUAGE_ENGLISH, /* GSM 7-bit */
				.max_pages = 1,
				.page = 1,
				.ud = {
					/* 7-bit "Cell Broadcast " repeated */
					0xc3, 0x32, 0x9b, 0x0d, 0x12, 0xca,
					0xdf, 0x61, 0xf2, 0x38, 0x3c, 0xa7,
					0x83, 0x40, 0x20, 0x10, 0x08, 0x04,
					0x02, 0x81, 0x40, 0x20, 0x10, 0x08,
					0x04, 0x02, 0x81, 0x40, 0x20, 0x10,
					0x08, 0x04, 0x02, 0x81, 0x40, 0x20,
					0x10, 0x08, 0x04, 0x02, 0x81, 0x40,
					0x20, 0x10, 0x08, 0x04, 0x02, 0x81,
					0x40, 0x20, 0x10, 0x08, 0x04, 0x02,
					0x81, 0x40, 0x20, 0x10, 0x08, 0x04,
					0x02, 0x81, 0x40, 0x20, 0x10, 0x08,
					0x04, 0x02, 0x81, 0x40, 0x20, 0x10,
					0x08, 0x04, 0x02, 0x81, 0x40, 0x20,
					0x10, 0x08, 0x04, 0x02,
				},
			},
		}},
	},
};

static const unsigned char cbs_pp_data_download_17[] = {
	0xd2, 0x5e, 0x82, 0x02, 0x83, 0x81, 0x8c, 0x58,
	0xc0, 0x11, 0x10, 0x01, 0x96, 0x11, 0x02, 0x70,
	0x00, 0x00, 0x4d, 0x00, 0x0d, 0x00, 0x00, 0x00,
	0x00, 0xbf, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
	0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
	0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
	0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
	0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
	0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
	0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
	0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
};

static const struct envelope_test cbs_pp_data_download_data_17 = {
	.pdu = cbs_pp_data_download_17,
	.pdu_len = sizeof(cbs_pp_data_download_17),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_CBS_PP_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .cbs_pp_download = {
			.page = {
				.gs = CBS_GEO_SCOPE_CELL_NORMAL,
				.message_code = 1,
				.update_number = 1,
				.message_identifier = 0x1001,
				.dcs = SMS_CLASS_2 | (SMS_CHARSET_8BIT << 2) |
					(9 << 4), /* UDHI present */
				.max_pages = 1,
				.page = 1,
				.ud = {
					/* Secured User Header */
					0x02, 0x70, 0x00, 0x00, 0x4d, 0x00,
					0x0d, 0x00, 0x00, 0x00, 0x00, 0xbf,
					0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
					0x01, 0x00, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc, 0xdc, 0xdc,
					0xdc, 0xdc, 0xdc, 0xdc,
				},
			},
		}},
	},
};

static const unsigned char menu_selection_111[] = {
	0xd3, 0x07, 0x82, 0x02, 0x01, 0x81, 0x90, 0x01,
	0x02,
};

static const struct envelope_test menu_selection_data_111 = {
	.pdu = menu_selection_111,
	.pdu_len = sizeof(menu_selection_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_MENU_SELECTION,
		.src = STK_DEVICE_IDENTITY_TYPE_KEYPAD,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .menu_selection = {
			.item_id = 0x2,
		}},
	},
};

static const unsigned char menu_selection_112[] = {
	0xd3, 0x07, 0x82, 0x02, 0x01, 0x81, 0x90, 0x01,
	0x12,
};

static const struct envelope_test menu_selection_data_112 = {
	.pdu = menu_selection_112,
	.pdu_len = sizeof(menu_selection_112),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_MENU_SELECTION,
		.src = STK_DEVICE_IDENTITY_TYPE_KEYPAD,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .menu_selection = {
			.item_id = 0x12,
		}},
	},
};

static const unsigned char menu_selection_121[] = {
	0xd3, 0x07, 0x82, 0x02, 0x01, 0x81, 0x90, 0x01,
	0x3d,
};

static const struct envelope_test menu_selection_data_121 = {
	.pdu = menu_selection_121,
	.pdu_len = sizeof(menu_selection_121),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_MENU_SELECTION,
		.src = STK_DEVICE_IDENTITY_TYPE_KEYPAD,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .menu_selection = {
			.item_id = 0x3d,
		}},
	},
};

static const unsigned char menu_selection_122[] = {
	0xd3, 0x07, 0x82, 0x02, 0x01, 0x81, 0x90, 0x01,
	0xfb,
};

static const struct envelope_test menu_selection_data_122 = {
	.pdu = menu_selection_122,
	.pdu_len = sizeof(menu_selection_122),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_MENU_SELECTION,
		.src = STK_DEVICE_IDENTITY_TYPE_KEYPAD,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .menu_selection = {
			.item_id = 0xfb,
		}},
	},
};

static const unsigned char menu_selection_123[] = {
	0xd3, 0x07, 0x82, 0x02, 0x01, 0x81, 0x90, 0x01,
	0x01,
};

static const struct envelope_test menu_selection_data_123 = {
	.pdu = menu_selection_123,
	.pdu_len = sizeof(menu_selection_123),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_MENU_SELECTION,
		.src = STK_DEVICE_IDENTITY_TYPE_KEYPAD,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .menu_selection = {
			.item_id = 0x1,
		}},
	},
};

static const unsigned char menu_selection_211[] = {
	0xd3, 0x09, 0x82, 0x02, 0x01, 0x81, 0x90, 0x01,
	0x02, 0x15, 0x00,
};

static const struct envelope_test menu_selection_data_211 = {
	.pdu = menu_selection_211,
	.pdu_len = sizeof(menu_selection_211),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_MENU_SELECTION,
		.src = STK_DEVICE_IDENTITY_TYPE_KEYPAD,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .menu_selection = {
			.item_id = 0x2,
			.help_request = TRUE,
		}},
	},
};

static const unsigned char menu_selection_612[] = {
	0xd3, 0x07, 0x82, 0x02, 0x01, 0x81, 0x90, 0x01,
	0x05,
};

static const struct envelope_test menu_selection_data_612 = {
	.pdu = menu_selection_612,
	.pdu_len = sizeof(menu_selection_612),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_MENU_SELECTION,
		.src = STK_DEVICE_IDENTITY_TYPE_KEYPAD,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .menu_selection = {
			.item_id = 0x5,
		}},
	},
};

static const unsigned char menu_selection_641[] = {
	0xd3, 0x07, 0x82, 0x02, 0x01, 0x81, 0x90, 0x01,
	0x08,
};

static const struct envelope_test menu_selection_data_641 = {
	.pdu = menu_selection_641,
	.pdu_len = sizeof(menu_selection_641),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_MENU_SELECTION,
		.src = STK_DEVICE_IDENTITY_TYPE_KEYPAD,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .menu_selection = {
			.item_id = 0x8,
		}},
	},
};

static const unsigned char call_control_111a[] = {
	0xd4, 0x25, 0x82, 0x02, 0x82, 0x81, 0x86, 0x0b,
	0x91, 0x10, 0x32, 0x54, 0x76, 0x98, 0x10, 0x32,
	0x54, 0x76, 0x98, 0x07, 0x07, 0x06, 0x60, 0x04,
	0x02, 0x00, 0x05, 0x81, 0x13, 0x09, 0x00, 0xf1,
	0x10, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
};

static const struct envelope_test call_control_data_111a = {
	.pdu = call_control_111a,
	.pdu_len = sizeof(call_control_111a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_CALL_CONTROL,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .call_control = {
			.type = STK_CC_TYPE_CALL_SETUP,
			{ .address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "01234567890123456789",
			}},
			.ccp1 = {
				.ccp = {
					0x60, 0x04, 0x02, 0x00, 0x05, 0x81,
				},
				.len = 6,
			},
			.location = {
				.mcc = "001",
				.mnc = "01",
				.lac_tac = 0x0001,
				.has_ci = TRUE,
				.ci = 0x0001,
				.has_ext_ci = TRUE,
				.ext_ci = 0x0001,
			},
		}},
	},
};

static const unsigned char call_control_111b[] = {
	0xd4, 0x23, 0x82, 0x02, 0x82, 0x81, 0x86, 0x0b,
	0x91, 0x10, 0x32, 0x54, 0x76, 0x98, 0x10, 0x32,
	0x54, 0x76, 0x98, 0x07, 0x07, 0x06, 0x60, 0x04,
	0x02, 0x00, 0x05, 0x81, 0x13, 0x07, 0x00, 0x11,
	0x10, 0x00, 0x01, 0x00, 0x01,
};

static const struct envelope_test call_control_data_111b = {
	.pdu = call_control_111b,
	.pdu_len = sizeof(call_control_111b),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_CALL_CONTROL,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .call_control = {
			.type = STK_CC_TYPE_CALL_SETUP,
			{ .address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "01234567890123456789",
			}},
			.ccp1 = {
				.ccp = {
					0x60, 0x04, 0x02, 0x00, 0x05, 0x81,
				},
				.len = 6,
			},
			.location = {
				.mcc = "001",
				.mnc = "011",
				.lac_tac = 0x0001,
				.has_ci = TRUE,
				.ci = 0x0001,
			},
		}},
	},
};

static const unsigned char call_control_131a[] = {
	0xd4, 0x18, 0x82, 0x02, 0x82, 0x81, 0x86, 0x07,
	0x91, 0x10, 0x32, 0x04, 0x21, 0x43, 0x65, 0x13,
	0x09, 0x00, 0xf1, 0x10, 0x00, 0x01, 0x00, 0x01,
	0x00, 0x01,
	/*
	 * Byte 3 changed to 0x82 and byte 7 changed to 0x86 (Comprehension
	 * Required should be set according to TS 102 223 7.3.1.6)
	 */
};

static const struct envelope_test call_control_data_131a = {
	.pdu = call_control_131a,
	.pdu_len = sizeof(call_control_131a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_CALL_CONTROL,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .call_control = {
			.type = STK_CC_TYPE_CALL_SETUP,
			{ .address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "012340123456",
			}},
			.location = {
				.mcc = "001",
				.mnc = "01",
				.lac_tac = 0x0001,
				.has_ci = TRUE,
				.ci = 0x0001,
				.has_ext_ci = TRUE,
				.ext_ci = 0x0001,
			},
		}},
	},
};

static const unsigned char call_control_131b[] = {
	0xd4, 0x16, 0x82, 0x02, 0x82, 0x81, 0x86, 0x07,
	0x91, 0x10, 0x32, 0x04, 0x21, 0x43, 0x65, 0x13,
	0x07, 0x00, 0x11, 0x10, 0x00, 0x01, 0x00, 0x01,
	/*
	 * Byte 3 changed to 0x82 and byte 7 changed to 0x86 (Comprehension
	 * Required should be set according to TS 102 223 7.3.1.6)
	 */
};

static const struct envelope_test call_control_data_131b = {
	.pdu = call_control_131b,
	.pdu_len = sizeof(call_control_131b),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_CALL_CONTROL,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .call_control = {
			.type = STK_CC_TYPE_CALL_SETUP,
			{ .address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "012340123456",
			}},
			.location = {
				.mcc = "001",
				.mnc = "011",
				.lac_tac = 0x0001,
				.has_ci = TRUE,
				.ci = 0x0001,
			},
		}},
	},
};

static const unsigned char mo_short_message_control_111a[] = {
	0xd5, 0x22, 0x02, 0x02, 0x82, 0x81, 0x06, 0x09,
	0x91, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0xf8, 0x06, 0x06, 0x91, 0x10, 0x32, 0x54, 0x76,
	0xf8, 0x13, 0x09, 0x00, 0xf1, 0x10, 0x00, 0x01,
	0x00, 0x01, 0x00, 0x01,
};

static const struct envelope_test mo_short_message_control_data_111a = {
	.pdu = mo_short_message_control_111a,
	.pdu_len = sizeof(mo_short_message_control_111a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_MO_SMS_CONTROL,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .sms_mo_control = {
			.sc_address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "112233445566778",
			},
			.dest_address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "012345678",
			},
			.location = {
				.mcc = "001",
				.mnc = "01",
				.lac_tac = 0x0001,
				.has_ci = TRUE,
				.ci = 0x0001,
				.has_ext_ci = TRUE,
				.ext_ci = 0x0001,
			},
		}},
	},
};

static const unsigned char mo_short_message_control_111b[] = {
	0xd5, 0x20, 0x02, 0x02, 0x82, 0x81, 0x06, 0x09,
	0x91, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
	0xf8, 0x06, 0x06, 0x91, 0x10, 0x32, 0x54, 0x76,
	0xf8, 0x13, 0x07, 0x00, 0x11, 0x10, 0x00, 0x01,
	0x00, 0x01,
};

static const struct envelope_test mo_short_message_control_data_111b = {
	.pdu = mo_short_message_control_111b,
	.pdu_len = sizeof(mo_short_message_control_111b),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_MO_SMS_CONTROL,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .sms_mo_control = {
			.sc_address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "112233445566778",
			},
			.dest_address = {
				.ton_npi = 0x91, /* Intl, ISDN */
				.number = "012345678",
			},
			.location = {
				.mcc = "001",
				.mnc = "011",
				.lac_tac = 0x0001,
				.has_ci = TRUE,
				.ci = 0x0001,
			},
		}},
	},
};

static const unsigned char event_download_mt_call_111[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x00, 0x82, 0x02, 0x83,
	0x81, 0x9c, 0x01, 0x00,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c (Comprehension
	 * Required should be set according to TS 102 223 7.5.1.2)
	 */
};

static const struct envelope_test event_download_mt_call_data_111 = {
	.pdu = event_download_mt_call_111,
	.pdu_len = sizeof(event_download_mt_call_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_MT_CALL,
			{ .mt_call = {
				.transaction_id = 0,
			}},
		}},
	},
};

static const unsigned char event_download_mt_call_112[] = {
	0xd6, 0x0f, 0x99, 0x01, 0x00, 0x82, 0x02, 0x83,
	0x81, 0x9c, 0x01, 0x00, 0x06, 0x03, 0x81, 0x89,
	0x67,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c and byte 13 to
	 * 0x06 (Comprehension Required should be set according to
	 * TS 102 223 7.5.1.2)
	 */
};

static const struct envelope_test event_download_mt_call_data_112 = {
	.pdu = event_download_mt_call_112,
	.pdu_len = sizeof(event_download_mt_call_112),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_MT_CALL,
			{ .mt_call = {
				.transaction_id = 0,
				.caller_address = {
					.ton_npi = 0x81, /* Unknown, ISDN */
					.number = "9876",
				},
			}},
		}},
	},
};

static const unsigned char event_download_call_connected_111[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x01, 0x82, 0x02, 0x82,
	0x81, 0x9c, 0x01, 0x80,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c (Comprehension
	 * Required should be set according to TS 102 223 7.5.2.2)
	 */
};

static const struct envelope_test event_download_call_connected_data_111 = {
	.pdu = event_download_call_connected_111,
	.pdu_len = sizeof(event_download_call_connected_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CALL_CONNECTED,
			{ .call_connected = {
				.transaction_id = 0x80,
			}},
		}},
	},
};

static const unsigned char event_download_call_connected_112[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x01, 0x82, 0x02, 0x83,
	0x81, 0x9c, 0x01, 0x80,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c (Comprehension
	 * Required should be set according to TS 102 223 7.5.2.2)
	 */
};

static const struct envelope_test event_download_call_connected_data_112 = {
	.pdu = event_download_call_connected_112,
	.pdu_len = sizeof(event_download_call_connected_112),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CALL_CONNECTED,
			{ .call_connected = {
				.transaction_id = 0x80,
			}},
		}},
	},
};

static const unsigned char event_download_call_disconnected_111[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x02, 0x82, 0x02, 0x83,
	0x81, 0x9c, 0x01, 0x80,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c (Comprehension
	 * Required should be set according to TS 102 223 7.5.3.2)
	 */
};

static const struct envelope_test event_download_call_disconnected_data_111 = {
	.pdu = event_download_call_disconnected_111,
	.pdu_len = sizeof(event_download_call_disconnected_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CALL_DISCONNECTED,
			{ .call_disconnected = {
				.transaction_ids = {
					.len = 1,
					.list = { 0x80 },
				},
			}},
		}},
	},
};

static const unsigned char event_download_call_disconnected_112a[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x9c, 0x01, 0x80,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c (Comprehension
	 * Required should be set according to TS 102 223 7.5.3.2)
	 */
};

static const struct envelope_test
		event_download_call_disconnected_data_112a = {
	.pdu = event_download_call_disconnected_112a,
	.pdu_len = sizeof(event_download_call_disconnected_112a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CALL_DISCONNECTED,
			{ .call_disconnected = {
				.transaction_ids = {
					.len = 1,
					.list = { 0x80 },
				},
			}},
		}},
	},
};

static const unsigned char event_download_call_disconnected_112b[] = {
	0xd6, 0x0e, 0x99, 0x01, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x9c, 0x01, 0x80, 0x1a, 0x02, 0x60, 0x90,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c and byte 13 to
	 * 1a (Comprehension Required should be set according to TS
	 * 102 223 7.5.3.2)
	 */
};

static const struct envelope_test
		event_download_call_disconnected_data_112b = {
	.pdu = event_download_call_disconnected_112b,
	.pdu_len = sizeof(event_download_call_disconnected_112b),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CALL_DISCONNECTED,
			{ .call_disconnected = {
				.transaction_ids = {
					.len = 1,
					.list = { 0x80 },
				},
				.cause = {
					.has_cause = TRUE,
					.len = 2,
					/* Normal call clearing */
					.cause = { 0x60, 0x90 },
				},
			}},
		}},
	},
};

static const unsigned char event_download_call_disconnected_112c[] = {
	0xd6, 0x0e, 0x99, 0x01, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x9c, 0x01, 0x80, 0x1a, 0x02, 0xe0, 0x90,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c and byte 13 to
	 * 1a (Comprehension Required should be set according to TS
	 * 102 223 7.5.3.2)
	 */
};

static const struct envelope_test
		event_download_call_disconnected_data_112c = {
	.pdu = event_download_call_disconnected_112c,
	.pdu_len = sizeof(event_download_call_disconnected_112c),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CALL_DISCONNECTED,
			{ .call_disconnected = {
				.transaction_ids = {
					.len = 1,
					.list = { 0x80 },
				},
				.cause = {
					.has_cause = TRUE,
					.len = 2,
					/* Normal call clearing */
					.cause = { 0xe0, 0x90 },
				},
			}},
		}},
	},
};

static const unsigned char event_download_call_disconnected_113a[] = {
	0xd6, 0x0e, 0x99, 0x01, 0x02, 0x82, 0x02, 0x83,
	0x81, 0x9c, 0x01, 0x00, 0x1a, 0x02, 0x60, 0x90,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c and byte 13 to
	 * 1a (Comprehension Required should be set according to TS
	 * 102 223 7.5.3.2)
	 */
};

static const struct envelope_test
		event_download_call_disconnected_data_113a = {
	.pdu = event_download_call_disconnected_113a,
	.pdu_len = sizeof(event_download_call_disconnected_113a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CALL_DISCONNECTED,
			{ .call_disconnected = {
				.transaction_ids = {
					.len = 1,
					.list = { 0 },
				},
				.cause = {
					.has_cause = TRUE,
					.len = 2,
					/* Normal call clearing */
					.cause = { 0x60, 0x90 },
				},
			}},
		}},
	},
};

static const unsigned char event_download_call_disconnected_113b[] = {
	0xd6, 0x0e, 0x99, 0x01, 0x02, 0x82, 0x02, 0x83,
	0x81, 0x9c, 0x01, 0x00, 0x1a, 0x02, 0xe0, 0x90,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c and byte 13 to
	 * 1a (Comprehension Required should be set according to TS
	 * 102 223 7.5.3.2)
	 */
};

static const struct envelope_test
		event_download_call_disconnected_data_113b = {
	.pdu = event_download_call_disconnected_113b,
	.pdu_len = sizeof(event_download_call_disconnected_113b),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CALL_DISCONNECTED,
			{ .call_disconnected = {
				.transaction_ids = {
					.len = 1,
					.list = { 0 },
				},
				.cause = {
					.has_cause = TRUE,
					.len = 2,
					/* Normal call clearing */
					.cause = { 0xe0, 0x90 },
				},
			}},
		}},
	},
};

static const unsigned char event_download_call_disconnected_114a[] = {
	0xd6, 0x0c, 0x99, 0x01, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x9c, 0x01, 0x80, 0x1a, 0x00,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c and byte 13 to
	 * 1a (Comprehension Required should be set according to TS
	 * 102 223 7.5.3.2)
	 */
};

static const struct envelope_test
		event_download_call_disconnected_data_114a = {
	.pdu = event_download_call_disconnected_114a,
	.pdu_len = sizeof(event_download_call_disconnected_114a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CALL_DISCONNECTED,
			{ .call_disconnected = {
				.transaction_ids = {
					.len = 1,
					.list = { 0x80 },
				},
				.cause = {
					.has_cause = TRUE,
					/* Radio link failure */
				},
			}},
		}},
	},
};

static const unsigned char event_download_call_disconnected_114b[] = {
	0xd6, 0x0c, 0x99, 0x01, 0x02, 0x82, 0x02, 0x82,
	0x81, 0x9c, 0x01, 0x00, 0x1a, 0x00,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9c and byte 13 to
	 * 1a (Comprehension Required should be set according to TS
	 * 102 223 7.5.3.2)
	 */
};

static const struct envelope_test
		event_download_call_disconnected_data_114b = {
	.pdu = event_download_call_disconnected_114b,
	.pdu_len = sizeof(event_download_call_disconnected_114b),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CALL_DISCONNECTED,
			{ .call_disconnected = {
				.transaction_ids = {
					.len = 1,
					.list = { 0 },
				},
				.cause = {
					.has_cause = TRUE,
					/* Radio link failure */
				},
			}},
		}},
	},
};

static const unsigned char event_download_location_status_111[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x03, 0x82, 0x02, 0x82,
	0x81, 0x9b, 0x01, 0x02,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9b (Comprehension
	 * Required should be set according to TS 102 223 7.5.4.2)
	 */
};

static const struct envelope_test
		event_download_location_status_data_111 = {
	.pdu = event_download_location_status_111,
	.pdu_len = sizeof(event_download_location_status_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_LOCATION_STATUS,
			{ .location_status = {
				.state = STK_NO_SERVICE,
			}},
		}},
	},
};

static const unsigned char event_download_location_status_112a[] = {
	0xd6, 0x15, 0x99, 0x01, 0x03, 0x82, 0x02, 0x82,
	0x81, 0x9b, 0x01, 0x00, 0x13, 0x09, 0x00, 0xf1,
	0x10, 0x00, 0x02, 0x00, 0x02, 0x00, 0x01,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9b (Comprehension
	 * Required should be set according to TS 102 223 7.5.4.2)
	 */
};

static const struct envelope_test
		event_download_location_status_data_112a = {
	.pdu = event_download_location_status_112a,
	.pdu_len = sizeof(event_download_location_status_112a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_LOCATION_STATUS,
			{ .location_status = {
				.state = STK_NORMAL_SERVICE,
				.info = {
					.mcc = "001",
					.mnc = "01",
					.lac_tac = 0x0002,
					.has_ci = TRUE,
					.ci = 0x0002,
					.has_ext_ci = TRUE,
					.ext_ci = 0x0001,
				},
			}},
		}},
	},
};

static const unsigned char event_download_location_status_112b[] = {
	0xd6, 0x13, 0x99, 0x01, 0x03, 0x82, 0x02, 0x82,
	0x81, 0x9b, 0x01, 0x00, 0x13, 0x07, 0x00, 0x11,
	0x10, 0x00, 0x02, 0x00, 0x02,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9b (Comprehension
	 * Required should be set according to TS 102 223 7.5.4.2)
	 */
};

static const struct envelope_test
		event_download_location_status_data_112b = {
	.pdu = event_download_location_status_112b,
	.pdu_len = sizeof(event_download_location_status_112b),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_LOCATION_STATUS,
			{ .location_status = {
				.state = STK_NORMAL_SERVICE,
				.info = {
					.mcc = "001",
					.mnc = "011",
					.lac_tac = 0x0002,
					.has_ci = TRUE,
					.ci = 0x0002,
				},
			}},
		}},
	},
};

static const unsigned char event_download_location_status_122[] = {
	0xd6, 0x15, 0x99, 0x01, 0x03, 0x82, 0x02, 0x82,
	0x81, 0x9b, 0x01, 0x00, 0x13, 0x09, 0x00, 0xf1,
	0x10, 0x00, 0x02, 0x00, 0x00, 0x00, 0x2f,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0x9b (Comprehension
	 * Required should be set according to TS 102 223 7.5.4.2)
	 */
};

static const struct envelope_test
		event_download_location_status_data_122 = {
	.pdu = event_download_location_status_122,
	.pdu_len = sizeof(event_download_location_status_122),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_LOCATION_STATUS,
			{ .location_status = {
				.state = STK_NORMAL_SERVICE,
				.info = {
					.mcc = "001",
					.mnc = "01",
					.lac_tac = 0x0002,
					.has_eutran_ci = TRUE,
					.eutran_ci = 0x0000002,
				},
			}},
		}},
	},
};

/*
 * This is from 27.22.7.5.  The ENVELOPE given in 27.22.4.16.1.1 seems to
 * have invalid length value (2nd byte), but in turn the Comprehension
 * Required bit is set correctly..
 */
static const unsigned char event_download_user_activity_111[] = {
	0xd6, 0x07, 0x99, 0x01, 0x04, 0x82, 0x02, 0x82,
	0x81,
	/*
	 * Byte 3 changed to 0x99 (Comprehension Required should be
	 * set according to TS 102 223 7.5.5.2)
	 */
};

static const struct envelope_test event_download_user_activity_data_111 = {
	.pdu = event_download_user_activity_111,
	.pdu_len = sizeof(event_download_user_activity_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_USER_ACTIVITY,
		}},
	},
};

static const unsigned char event_download_idle_screen_available_111[] = {
	0xd6, 0x07, 0x99, 0x01, 0x05, 0x82, 0x02, 0x02,
	0x81,
	/*
	 * Byte 3 changed to 0x99 (Comprehension Required should be
	 * set according to TS 102 223 7.5.6.2)
	 */
};

static const struct envelope_test
		event_download_idle_screen_available_data_111 = {
	.pdu = event_download_idle_screen_available_111,
	.pdu_len = sizeof(event_download_idle_screen_available_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_DISPLAY,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_IDLE_SCREEN_AVAILABLE,
		}},
	},
};

static const unsigned char event_download_card_reader_status_111a[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0xa0, 0x01, 0x79,
};

static const struct envelope_test
		event_download_card_reader_status_data_111a = {
	.pdu = event_download_card_reader_status_111a,
	.pdu_len = sizeof(event_download_card_reader_status_111a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CARD_READER_STATUS,
			{ .card_reader_status = {
				.id = 1,
				.removable = TRUE,
				.present = TRUE,
				.id1_size = TRUE,
				.card_present = TRUE,
				.card_powered = FALSE,
			}},
		}},
	},
};

static const unsigned char event_download_card_reader_status_111b[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0xa0, 0x01, 0x59,
};

static const struct envelope_test
		event_download_card_reader_status_data_111b = {
	.pdu = event_download_card_reader_status_111b,
	.pdu_len = sizeof(event_download_card_reader_status_111b),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CARD_READER_STATUS,
			{ .card_reader_status = {
				.id = 1,
				.removable = TRUE,
				.present = TRUE,
				.id1_size = FALSE,
				.card_present = TRUE,
				.card_powered = FALSE,
			}},
		}},
	},
};

static const unsigned char event_download_card_reader_status_111c[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0xa0, 0x01, 0x71,
};

static const struct envelope_test
		event_download_card_reader_status_data_111c = {
	.pdu = event_download_card_reader_status_111c,
	.pdu_len = sizeof(event_download_card_reader_status_111c),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CARD_READER_STATUS,
			{ .card_reader_status = {
				.id = 1,
				.removable = FALSE,
				.present = TRUE,
				.id1_size = TRUE,
				.card_present = TRUE,
				.card_powered = FALSE,
			}},
		}},
	},
};

static const unsigned char event_download_card_reader_status_111d[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0xa0, 0x01, 0x51,
};

static const struct envelope_test
		event_download_card_reader_status_data_111d = {
	.pdu = event_download_card_reader_status_111d,
	.pdu_len = sizeof(event_download_card_reader_status_111d),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CARD_READER_STATUS,
			{ .card_reader_status = {
				.id = 1,
				.removable = FALSE,
				.present = TRUE,
				.id1_size = FALSE,
				.card_present = TRUE,
				.card_powered = FALSE,
			}},
		}},
	},
};

static const unsigned char event_download_card_reader_status_112a[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0xa0, 0x01, 0x39,
};

static const struct envelope_test
		event_download_card_reader_status_data_112a = {
	.pdu = event_download_card_reader_status_112a,
	.pdu_len = sizeof(event_download_card_reader_status_112a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CARD_READER_STATUS,
			{ .card_reader_status = {
				.id = 1,
				.removable = TRUE,
				.present = TRUE,
				.id1_size = TRUE,
				.card_present = FALSE,
			}},
		}},
	},
};

static const unsigned char event_download_card_reader_status_112b[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0xa0, 0x01, 0x19,
};

static const struct envelope_test
		event_download_card_reader_status_data_112b = {
	.pdu = event_download_card_reader_status_112b,
	.pdu_len = sizeof(event_download_card_reader_status_112b),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CARD_READER_STATUS,
			{ .card_reader_status = {
				.id = 1,
				.removable = TRUE,
				.present = TRUE,
				.id1_size = FALSE,
				.card_present = FALSE,
			}},
		}},
	},
};

static const unsigned char event_download_card_reader_status_112c[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0xa0, 0x01, 0x31,
};

static const struct envelope_test
		event_download_card_reader_status_data_112c = {
	.pdu = event_download_card_reader_status_112c,
	.pdu_len = sizeof(event_download_card_reader_status_112c),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CARD_READER_STATUS,
			{ .card_reader_status = {
				.id = 1,
				.removable = FALSE,
				.present = TRUE,
				.id1_size = TRUE,
				.card_present = FALSE,
			}},
		}},
	},
};

static const unsigned char event_download_card_reader_status_112d[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0xa0, 0x01, 0x11,
};

static const struct envelope_test
		event_download_card_reader_status_data_112d = {
	.pdu = event_download_card_reader_status_112d,
	.pdu_len = sizeof(event_download_card_reader_status_112d),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CARD_READER_STATUS,
			{ .card_reader_status = {
				.id = 1,
				.removable = FALSE,
				.present = TRUE,
				.id1_size = FALSE,
				.card_present = FALSE,
			}},
		}},
	},
};

static const unsigned char event_download_card_reader_status_212a[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0xa0, 0x01, 0x29,
};

static const struct envelope_test
		event_download_card_reader_status_data_212a = {
	.pdu = event_download_card_reader_status_212a,
	.pdu_len = sizeof(event_download_card_reader_status_212a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CARD_READER_STATUS,
			{ .card_reader_status = {
				.id = 1,
				.removable = TRUE,
				.present = FALSE,
				.id1_size = TRUE,
				.card_present = FALSE,
			}},
		}},
	},
};

static const unsigned char event_download_card_reader_status_212b[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x06, 0x82, 0x02, 0x82,
	0x81, 0xa0, 0x01, 0x09,
};

static const struct envelope_test
		event_download_card_reader_status_data_212b = {
	.pdu = event_download_card_reader_status_212b,
	.pdu_len = sizeof(event_download_card_reader_status_212b),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CARD_READER_STATUS,
			{ .card_reader_status = {
				.id = 1,
				.removable = TRUE,
				.present = FALSE,
				.id1_size = FALSE,
				.card_present = FALSE,
			}},
		}},
	},
};

static const unsigned char event_download_language_selection_111[] = {
	0xd6, 0x0b, 0x99, 0x01, 0x07, 0x82, 0x02, 0x82,
	0x81, 0xad, 0x02, 0x64, 0x65,
	/*
	 * Byte 3 changed to 0x99 and byte 10 to 0xad (Comprehension
	 * Required should be set according to TS 102 223 7.5.8.2)
	 */
};

static const struct envelope_test
		event_download_language_selection_data_111 = {
	.pdu = event_download_language_selection_111,
	.pdu_len = sizeof(event_download_language_selection_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_LANGUAGE_SELECTION,
			{ .language_selection = "de" },
		}},
	},
};

static const unsigned char event_download_language_selection_122[] = {
	0xd6, 0x0b, 0x99, 0x01, 0x07, 0x82, 0x02, 0x82,
	0x81, 0xad, 0x02, 0x73, 0x65,
	/* Byte 5 changed to 0x07 (Event: Language Selection) */
	/* Byte 8 changed to 0x82 (Source device: Terminal) */
	/* Removed the (unexpected?) Transaction ID data object (0x2d) */
};

static const struct envelope_test
		event_download_language_selection_data_122 = {
	.pdu = event_download_language_selection_122,
	.pdu_len = sizeof(event_download_language_selection_122),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_LANGUAGE_SELECTION,
			{ .language_selection = "se" },
		}},
	},
};

static const unsigned char event_download_browser_termination_111[] = {
	0xd6, 0x0a, 0x99, 0x01, 0x08, 0x82, 0x02, 0x82,
	0x81, 0xb4, 0x01, 0x00,
};

static const struct envelope_test
		event_download_browser_termination_data_111 = {
	.pdu = event_download_browser_termination_111,
	.pdu_len = sizeof(event_download_browser_termination_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_BROWSER_TERMINATION,
			{ .browser_termination = {
				.cause = STK_BROWSER_USER_TERMINATION,
			}},
		}},
	},
};

static const unsigned char event_download_data_available_111[] = {
	0xd6, 0x0e, 0x99, 0x01, 0x09, 0x82, 0x02, 0x82,
	0x81, 0xb8, 0x02, 0x81, 0x00, 0xb7, 0x01, 0xff,
};

static const struct envelope_test event_download_data_available_data_111 = {
	.pdu = event_download_data_available_111,
	.pdu_len = sizeof(event_download_data_available_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_DATA_AVAILABLE,
			{ .data_available = {
				/* Channel 1 open, Link established */
				.channel = {
				.id = 1,
				.status =
				STK_CHANNEL_PACKET_DATA_SERVICE_ACTIVATED,
				},
				.channel_data_len = 255,
			} },
		} },
	},
};

static const unsigned char event_download_data_available_211[] = {
	0xd6, 0x0e, 0x99, 0x01, 0x09, 0x82, 0x02, 0x82,
	0x81, 0xb8, 0x02, 0x81, 0x00, 0xb7, 0x01, 0xff,
};

static const struct envelope_test event_download_data_available_data_211 = {
	.pdu = event_download_data_available_211,
	.pdu_len = sizeof(event_download_data_available_211),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_DATA_AVAILABLE,
			{ .data_available = {
				/* Channel 1 open, Link established */
				.channel = {
				.id = 1,
				.status =
				STK_CHANNEL_PACKET_DATA_SERVICE_ACTIVATED,
				},
				.channel_data_len = 255,
			} },
		} },
	},
};

static const unsigned char event_download_channel_status_131[] = {
	0xd6, 0x0b, 0x99, 0x01, 0x0a, 0x82, 0x02, 0x82,
	0x81, 0xb8, 0x02, 0x01, 0x05,
};

static const struct envelope_test event_download_channel_status_data_131 = {
	.pdu = event_download_channel_status_131,
	.pdu_len = sizeof(event_download_channel_status_131),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CHANNEL_STATUS,
			{ .channel_status = {
				/* Channel 1, Link dropped */
				.channel = {
					.id = 1,
					.status = STK_CHANNEL_LINK_DROPPED,
				},
			} },
		} },
	},
};

static const unsigned char event_download_channel_status_211[] = {
	0xd6, 0x0b, 0x99, 0x01, 0x0a, 0x82, 0x02, 0x82,
	0x81, 0xb8, 0x02, 0x41, 0x00,
	/*
	 * Byte 10 changed to 0xb8 (Comprehension Required should be
	 * set according to TS 102 223 7.5.11.2)
	 */
};

static const struct envelope_test event_download_channel_status_data_211 = {
	.pdu = event_download_channel_status_211,
	.pdu_len = sizeof(event_download_channel_status_211),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CHANNEL_STATUS,
			{ .channel_status = {
				/* Channel 1, TCP in LISTEN state */
				.channel = {
				.id = 1,
				.status = STK_CHANNEL_TCP_IN_LISTEN_STATE,
				},
			} },
		} },
	},
};

static const unsigned char event_download_channel_status_221[] = {
	0xd6, 0x0b, 0x99, 0x01, 0x0a, 0x82, 0x02, 0x82,
	0x81, 0xb8, 0x02, 0x81, 0x00,
	/*
	 * Byte 10 changed to 0xb8 (Comprehension Required should be
	 * set according to TS 102 223 7.5.11.2)
	 */
};

static const struct envelope_test event_download_channel_status_data_221 = {
	.pdu = event_download_channel_status_221,
	.pdu_len = sizeof(event_download_channel_status_221),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_CHANNEL_STATUS,
			{ .channel_status = {
				/* Channel 1 open, TCP Link established */
				.channel = {
				.id = 1,
				.status =
				STK_CHANNEL_PACKET_DATA_SERVICE_ACTIVATED,
				},
			} },
		} },
	},
};

static const unsigned char event_download_network_rejection_111[] = {
	0xd6, 0x17, 0x99, 0x01, 0x12, 0x82, 0x02, 0x83,
	0x81, 0x7d, 0x05, 0x00, 0xf1, 0x10, 0x00, 0x01,
	0xbf, 0x01, 0x08, 0xf4, 0x01, 0x09, 0xf5, 0x01,
	0x0b,
	/*
	 * Byte 3 changed to 99, byte 17 changed to bf, byte 19 to f4 and
	 * byte 22 to f5 (Comprehension Required should be set according
	 * to TS 131 111 7.5.2.2)
	 */
};

static const struct envelope_test event_download_network_rejection_data_111 = {
	.pdu = event_download_network_rejection_111,
	.pdu_len = sizeof(event_download_network_rejection_111),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_NETWORK_REJECTION,
			{ .network_rejection = {
				.tai = {
					.mcc = "001",
					.mnc = "01",
					.tac = 0x0001,
				},
				.access_tech = STK_ACCESS_TECHNOLOGY_EUTRAN,
				.update_attach = STK_UPDATE_ATTACH_EPS_ATTACH,
				.cause = STK_CAUSE_EMM_PLMN_NOT_ALLOWED,
			}},
		}},
	},
};

static const unsigned char event_download_network_rejection_121[] = {
	0xd6, 0x17, 0x99, 0x01, 0x12, 0x82, 0x02, 0x83,
	0x81, 0x7d, 0x05, 0x00, 0xf1, 0x10, 0x00, 0x01,
	0xbf, 0x01, 0x08, 0xf4, 0x01, 0x0b, 0xf5, 0x01,
	0x0c,
	/*
	 * Byte 3 changed to 99, byte 17 changed to bf, byte 19 to f4 and
	 * byte 22 to f5 (Comprehension Required should be set according
	 * to TS 131 111 7.5.2.2)
	 */
};

static const struct envelope_test event_download_network_rejection_data_121 = {
	.pdu = event_download_network_rejection_121,
	.pdu_len = sizeof(event_download_network_rejection_121),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_EVENT_DOWNLOAD,
		.src = STK_DEVICE_IDENTITY_TYPE_NETWORK,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .event_download = {
			.type = STK_EVENT_TYPE_NETWORK_REJECTION,
			{ .network_rejection = {
				.tai = {
					.mcc = "001",
					.mnc = "01",
					.tac = 0x0001,
				},
				.access_tech = STK_ACCESS_TECHNOLOGY_EUTRAN,
				.update_attach = STK_UPDATE_ATTACH_TA_UPDATING,
				.cause = STK_CAUSE_EMM_TAC_NOT_ALLOWED,
			}},
		}},
	},
};

static const unsigned char timer_expiration_211[] = {
	0xd7, 0x0c, 0x82, 0x02, 0x82, 0x81, 0xa4, 0x01,
	0x01, 0xa5, 0x03, 0x00, 0x00, 0x01,
};

static const struct envelope_test timer_expiration_data_211 = {
	.pdu = timer_expiration_211,
	.pdu_len = sizeof(timer_expiration_211),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_TIMER_EXPIRATION,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .timer_expiration = {
			.id = 1,
			.value = {
				.second = 10,
				.has_value = TRUE,
			},
		}},
	},
};

static const unsigned char timer_expiration_221a[] = {
	0xd7, 0x0c, 0x82, 0x02, 0x82, 0x81, 0xa4, 0x01,
	0x01, 0xa5, 0x03, 0x00, 0x00, 0x03,
};

static const struct envelope_test timer_expiration_data_221a = {
	.pdu = timer_expiration_221a,
	.pdu_len = sizeof(timer_expiration_221a),
	.envelope = {
		.type = STK_ENVELOPE_TYPE_TIMER_EXPIRATION,
		.src = STK_DEVICE_IDENTITY_TYPE_TERMINAL,
		.dst = STK_DEVICE_IDENTITY_TYPE_UICC,
		{ .timer_expiration = {
			.id = 1,
			.value = {
				.second = 30,
				.has_value = TRUE,
			},
		}},
	},
};

struct html_attr_test {
	char *text;
	struct stk_text_attribute text_attr;
	char *html;
};

static struct html_attr_test html_attr_data_1 = {
	.text = "Blue green green green",
	.text_attr = {
		.len = 8,
		.attributes = {	0x00, 0x00, 0x03, 0x94, 0x00, 0x04, 0x03,
				0x96 },
	},
	.html = "<span style=\"color: #0000A0;background-color: #FFFFFF;\">"
		"Blue</span><span style=\"color: #347235;background-color: "
		"#FFFFFF;\"> green green green</span>",
};

static struct html_attr_test html_attr_data_2 = {
	.text = "abc",
	.text_attr = {
		.len = 8,
		.attributes = { 0x00, 0x02, 0x03, 0x94, 0x01, 0x02, 0x03,
				0x96 },
	},
	.html = "<span style=\"color: #347235;background-color: #FFFFFF;\">"
		"a</span><span style=\"color: #0000A0;background-color: "
		"#FFFFFF;\">bc</span>",
};

static struct html_attr_test html_attr_data_3 = {
	.text = "1 < 2, 2 > 1, 1 & 0 == 0\nSpecial Chars are Fun\r\nTo Write",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x00, 0x03, 0x00 },
	},
	.html = "1 &lt; 2, 2 &gt; 1, 1 &amp; 0 == 0<br/>Special Chars are Fun"
		"<br/>To Write",
};

static struct html_attr_test html_attr_data_4 = {
	.text = "€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€"
		"€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€"
		"€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€"
		"€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€"
		"€€€€€€€€€€€€€€€",
	.text_attr = {
		.len = 4,
		.attributes = { 0x00, 0x00, 0x03, 0x94 },
	},
	.html = "<span style=\"color: #347235;background-color: #FFFFFF;\">"
		"€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€"
		"€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€"
		"€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€"
		"€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€€"
		"€€€€€€€€€€€€€€€</span>",
};

static void test_html_attr(gconstpointer data)
{
	const struct html_attr_test *test = data;
	check_text_attr_html(&test->text_attr, test->text, test->html);
}

struct img_xpm_test {
	const unsigned char *img;
	unsigned int len;
	const unsigned char *clut;
	unsigned short clut_len;
	guint8 scheme;
	char *xpm;
};

const unsigned char img1[] = { 0x05, 0x05, 0xFE, 0xEB, 0xBF, 0xFF, 0xFF, 0xFF };

const unsigned char img2[] = { 0x08, 0x08, 0x02, 0x03, 0x00, 0x16, 0xAA,
					0xAA, 0x80, 0x02, 0x85, 0x42, 0x81,
					0x42, 0x81, 0x42, 0x81, 0x52, 0x80,
					0x02, 0xAA, 0xAA, 0xFF, 0x00, 0x00,
					0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF };

const unsigned char img3[] = { 0x2E, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x01, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x0F,
				0xFF, 0x00, 0x00, 0x00, 0x00, 0x77, 0xFE, 0x00,
				0x00, 0x00, 0x01, 0xBF, 0xF8, 0x00, 0x00, 0x00,
				0x06, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x1A, 0x03,
				0x80, 0x00, 0x00, 0x00, 0x6B, 0xF6, 0xBC, 0x00,
				0x00, 0x01, 0xAF, 0xD8, 0x38, 0x00, 0x00, 0x06,
				0xBF, 0x60, 0x20, 0x00, 0x00, 0x1A, 0xFD, 0x80,
				0x40, 0x00, 0x00, 0x6B, 0xF6, 0x00, 0x80, 0x00,
				0x01, 0xA0, 0x1F, 0x02, 0x00, 0x00, 0x06, 0xFF,
				0xE4, 0x04, 0x00, 0x00, 0x1B, 0xFF, 0x90, 0x10,
				0x00, 0x00, 0x6D, 0xEE, 0x40, 0x40, 0x00, 0x01,
				0xBF, 0xF9, 0x01, 0x00, 0x00, 0x6F, 0xFF, 0xE4,
				0x04, 0x00, 0x00, 0x1B, 0xFF, 0x90, 0x10, 0x00,
				0x00, 0x6F, 0xFE, 0x40, 0x40, 0x00, 0x01, 0xBF,
				0xF9, 0x01, 0x00, 0x00, 0x06, 0xFF, 0xE6, 0x04,
				0x00, 0x00, 0x1B, 0xFF, 0x88, 0x10, 0x00, 0x00,
				0x6F, 0xFE, 0x20, 0x40, 0x00, 0x01, 0xBF, 0xF8,
				0x66, 0x00, 0x00, 0x06, 0xFF, 0xE0, 0xF0, 0x00,
				0x00, 0x1B, 0xFF, 0x80, 0x80, 0x00, 0x00, 0x7F,
				0xFE, 0x00, 0x00, 0x00, 0x03, 0x00, 0x0C, 0x00,
				0x00, 0x00, 0x1F, 0xFF, 0xF8, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x1C, 0x21, 0x08, 0x44, 0xEE, 0x00, 0x48, 0xC4,
				0x31, 0x92, 0x20, 0x01, 0x25, 0x11, 0x45, 0x50,
				0x80, 0x07, 0x14, 0x45, 0x15, 0x43, 0x80, 0x12,
				0x71, 0x1C, 0x4D, 0x08, 0x00, 0x4A, 0x24, 0x89,
				0x32, 0x20, 0x01, 0xC8, 0x9E, 0x24, 0x4E,
				0xE0 };

const unsigned char img4[] = { 0x18, 0x10, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x01,
				0x80, 0x00, 0x01, 0x80, 0x00, 0x01, 0x8F,
				0x3C, 0xF1, 0x89, 0x20, 0x81, 0x89, 0x20,
				0x81, 0x89, 0x20, 0xF1, 0x89, 0x20, 0x11,
				0x89, 0x20, 0x11, 0x89, 0x20, 0x11, 0x8F,
				0x3C, 0xF1, 0x80, 0x00, 0x01, 0x80, 0x00,
				0x01, 0x80, 0x00, 0x01, 0xFF, 0xFF, 0xFF };

const unsigned char img5[] = { 0x08, 0x08, 0xFF, 0x03, 0xA5, 0x99, 0x99,
				0xA5, 0xC3, 0xFF };

static struct img_xpm_test xpm_test_1 = {
	.img = img1,
	.len = sizeof(img1),
	.scheme = STK_IMG_SCHEME_BASIC,
	.xpm = "/* XPM */\n"
		"static char *xpm[] = {\n"
		"\"5 5 2 1\",\n"
		"\"0	c #000000\",\n"
		"\"1	c #FFFFFF\",\n"
		"\"11111\",\n"
		"\"11011\",\n"
		"\"10101\",\n"
		"\"11011\",\n"
		"\"11111\",\n"
		"};",
};

static struct img_xpm_test xpm_test_2 = {
	.img = img2,
	.len = sizeof(img2),
	.clut = img2 + 0x16,
	.clut_len = 0x09,
	.scheme = STK_IMG_SCHEME_COLOR,
	.xpm = "/* XPM */\n"
		"static char *xpm[] = {\n"
		"\"8 8 3 1\",\n"
		"\"0	c #FF0000\",\n"
		"\"1	c #00FF00\",\n"
		"\"2	c #0000FF\",\n"
		"\"22222222\",\n"
		"\"20000002\",\n"
		"\"20111002\",\n"
		"\"20011002\",\n"
		"\"20011002\",\n"
		"\"20011102\",\n"
		"\"20000002\",\n"
		"\"22222222\",\n"
		"};",
};

static struct img_xpm_test xpm_test_3 = {
	.img = img3,
	.len = sizeof(img3),
	.scheme = STK_IMG_SCHEME_BASIC,
	.xpm = "/* XPM */\n"
		"static char *xpm[] = {\n"
		"\"46 40 2 1\",\n"
		"\"0	c #000000\",\n"
		"\"1	c #FFFFFF\",\n"
		"\"0000000000000000000000000000000000000000000000\",\n"
		"\"0000000000000000011111111110000000000000000000\",\n"
		"\"0000000000000000111111111111000000000000000000\",\n"
		"\"0000000000000001110111111111100000000000000000\",\n"
		"\"0000000000000001101111111111100000000000000000\",\n"
		"\"0000000000000001101111111111100000000000000000\",\n"
		"\"0000000000000001101000000011100000000000000000\",\n"
		"\"0000000000000001101011111101101011110000000000\",\n"
		"\"0000000000000001101011111101100000111000000000\",\n"
		"\"0000000000000001101011111101100000001000000000\",\n"
		"\"0000000000000001101011111101100000000100000000\",\n"
		"\"0000000000000001101011111101100000000010000000\",\n"
		"\"0000000000000001101000000001111100000010000000\",\n"
		"\"0000000000000001101111111111100100000001000000\",\n"
		"\"0000000000000001101111111111100100000001000000\",\n"
		"\"0000000000000001101101111011100100000001000000\",\n"
		"\"0000000000000001101111111111100100000001000000\",\n"
		"\"0000000000011011111111111111100100000001000000\",\n"
		"\"0000000000000001101111111111100100000001000000\",\n"
		"\"0000000000000001101111111111100100000001000000\",\n"
		"\"0000000000000001101111111111100100000001000000\",\n"
		"\"0000000000000001101111111111100110000001000000\",\n"
		"\"0000000000000001101111111111100010000001000000\",\n"
		"\"0000000000000001101111111111100010000001000000\",\n"
		"\"0000000000000001101111111111100001100110000000\",\n"
		"\"0000000000000001101111111111100000111100000000\",\n"
		"\"0000000000000001101111111111100000001000000000\",\n"
		"\"0000000000000001111111111111100000000000000000\",\n"
		"\"0000000000000011000000000000110000000000000000\",\n"
		"\"0000000000000111111111111111111000000000000000\",\n"
		"\"0000000000000000000000000000000000000000000000\",\n"
		"\"0000000000000000000000000000000000000000000000\",\n"
		"\"0000000000000000000000000000000000000000000000\",\n"
		"\"0000011100001000010000100001000100111011100000\",\n"
		"\"0000010010001100010000110001100100100010000000\",\n"
		"\"0000010010010100010001010001010101000010000000\",\n"
		"\"0000011100010100010001010001010101000011100000\",\n"
		"\"0000010010011100010001110001001101000010000000\",\n"
		"\"0000010010100010010010001001001100100010000000\",\n"
		"\"0000011100100010011110001001000100111011100000\",\n"
		"};",
};

static struct img_xpm_test xpm_test_4 = {
	.img = img4,
	.len = sizeof(img4),
	.scheme = STK_IMG_SCHEME_BASIC,
	.xpm = "/* XPM */\n"
		"static char *xpm[] = {\n"
		"\"24 16 2 1\",\n"
		"\"0	c #000000\",\n"
		"\"1	c #FFFFFF\",\n"
		"\"111111111111111111111111\",\n"
		"\"100000000000000000000001\",\n"
		"\"100000000000000000000001\",\n"
		"\"100000000000000000000001\",\n"
		"\"100011110011110011110001\",\n"
		"\"100010010010000010000001\",\n"
		"\"100010010010000010000001\",\n"
		"\"100010010010000011110001\",\n"
		"\"100010010010000000010001\",\n"
		"\"100010010010000000010001\",\n"
		"\"100010010010000000010001\",\n"
		"\"100011110011110011110001\",\n"
		"\"100000000000000000000001\",\n"
		"\"100000000000000000000001\",\n"
		"\"100000000000000000000001\",\n"
		"\"111111111111111111111111\",\n"
		"};",
};

static struct img_xpm_test xpm_test_5 = {
	.img = img5,
	.len = sizeof(img5),
	.scheme = STK_IMG_SCHEME_BASIC,
	.xpm = "/* XPM */\n"
		"static char *xpm[] = {\n"
		"\"8 8 2 1\",\n"
		"\"0	c #000000\",\n"
		"\"1	c #FFFFFF\",\n"
		"\"11111111\",\n"
		"\"00000011\",\n"
		"\"10100101\",\n"
		"\"10011001\",\n"
		"\"10011001\",\n"
		"\"10100101\",\n"
		"\"11000011\",\n"
		"\"11111111\",\n"
		"};",
};

static struct img_xpm_test xpm_test_6 = {
	.img = img2,
	.len = sizeof(img2),
	.clut = img2 + 0x16,
	.clut_len = 0x09,
	.scheme = STK_IMG_SCHEME_TRANSPARENCY,
	.xpm = "/* XPM */\n"
		"static char *xpm[] = {\n"
		"\"8 8 3 1\",\n"
		"\"0	c #FF0000\",\n"
		"\"1	c #00FF00\",\n"
		"\"2	c None\",\n"
		"\"22222222\",\n"
		"\"20000002\",\n"
		"\"20111002\",\n"
		"\"20011002\",\n"
		"\"20011002\",\n"
		"\"20011102\",\n"
		"\"20000002\",\n"
		"\"22222222\",\n"
		"};",
};

static void test_img_to_xpm(gconstpointer data)
{
	const struct img_xpm_test *test = data;
	char *xpm;

	xpm = stk_image_to_xpm(test->img, test->len, test->scheme,
				test->clut, test->clut_len);

	g_assert(memcmp(xpm, test->xpm, strlen(test->xpm)) == 0);
	g_free(xpm);
}

int main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_data_func("/teststk/Display Text 1.1.1",
				&display_text_data_111, test_display_text);
	g_test_add_data_func("/teststk/Display Text 1.3.1",
				&display_text_data_131, test_display_text);
	g_test_add_data_func("/teststk/Display Text 1.4.1",
				&display_text_data_141, test_display_text);
	g_test_add_data_func("/teststk/Display Text 1.5.1",
				&display_text_data_151, test_display_text);
	g_test_add_data_func("/teststk/Display Text 1.6.1",
				&display_text_data_161, test_display_text);
	g_test_add_data_func("/teststk/Display Text 1.7.1",
				&display_text_data_171, test_display_text);
	g_test_add_data_func("/teststk/Display Text 5.1.1",
				&display_text_data_511, test_display_text);
	g_test_add_data_func("/teststk/Display Text 5.2.1",
				&display_text_data_521, test_display_text);
	g_test_add_data_func("/teststk/Display Text 5.3.1",
				&display_text_data_531, test_display_text);
	g_test_add_data_func("/teststk/Display Text 6.1.1",
				&display_text_data_611, test_display_text);
	g_test_add_data_func("/teststk/Display Text 7.1.1",
				&display_text_data_711, test_display_text);
	g_test_add_data_func("/teststk/Display Text 8.1.1",
				&display_text_data_811, test_display_text);
	g_test_add_data_func("/teststk/Display Text 8.2.1",
				&display_text_data_821, test_display_text);
	g_test_add_data_func("/teststk/Display Text 8.3.1",
				&display_text_data_831, test_display_text);
	g_test_add_data_func("/teststk/Display Text 8.4.1",
				&display_text_data_841, test_display_text);
	g_test_add_data_func("/teststk/Display Text 8.5.1",
				&display_text_data_851, test_display_text);
	g_test_add_data_func("/teststk/Display Text 8.6.1",
				&display_text_data_861, test_display_text);
	g_test_add_data_func("/teststk/Display Text 8.7.1",
				&display_text_data_871, test_display_text);
	g_test_add_data_func("/teststk/Display Text 8.8.1",
				&display_text_data_881, test_display_text);
	g_test_add_data_func("/teststk/Display Text 8.9.1",
				&display_text_data_891, test_display_text);
	g_test_add_data_func("/teststk/Display Text 9.1.1",
				&display_text_data_911, test_display_text);
	g_test_add_data_func("/teststk/Display Text 10.1.1",
				&display_text_data_1011, test_display_text);

	g_test_add_data_func("/teststk/Display Text response 1.1.1",
				&display_text_response_data_111,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Display Text response 1.2.1",
				&display_text_response_data_121,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Display Text response 1.3.1",
				&display_text_response_data_131,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Display Text response 1.5.1",
				&display_text_response_data_151,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Display Text response 1.7.1",
				&display_text_response_data_171,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Display Text response 1.8.1",
				&display_text_response_data_181,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Display Text response 1.9.1",
				&display_text_response_data_191,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Display Text response 2.1.1",
				&display_text_response_data_211,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Display Text response 5.1.1B",
				&display_text_response_data_511b,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Get Inkey 1.1.1",
				&get_inkey_data_111, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 1.2.1",
				&get_inkey_data_121, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 1.3.1",
				&get_inkey_data_131, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 1.4.1",
				&get_inkey_data_141, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 1.5.1",
				&get_inkey_data_151, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 1.6.1",
				&get_inkey_data_161, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 2.1.1",
				&get_inkey_data_211, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 3.1.1",
				&get_inkey_data_311, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 3.2.1",
				&get_inkey_data_321, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 4.1.1",
				&get_inkey_data_411, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 5.1.1",
				&get_inkey_data_511, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 5.1.2",
				&get_inkey_data_512, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 6.1.1",
				&get_inkey_data_611, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 6.2.1",
				&get_inkey_data_621, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 6.3.1",
				&get_inkey_data_631, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 6.4.1",
				&get_inkey_data_641, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 7.1.1",
				&get_inkey_data_711, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 7.1.2",
				&get_inkey_data_712, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 8.1.1",
				&get_inkey_data_811, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.1.1",
				&get_inkey_data_911, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.1.2",
				&get_inkey_data_912, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.2.1",
				&get_inkey_data_921, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.2.2",
				&get_inkey_data_922, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.3.1",
				&get_inkey_data_931, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.3.2",
				&get_inkey_data_932, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.4.1",
				&get_inkey_data_941, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.4.2",
				&get_inkey_data_942, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.4.3",
				&get_inkey_data_943, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.5.1",
				&get_inkey_data_951, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.5.2",
				&get_inkey_data_952, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.5.3",
				&get_inkey_data_953, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.6.1",
				&get_inkey_data_961, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.6.2",
				&get_inkey_data_962, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.6.3",
				&get_inkey_data_963, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.7.1",
				&get_inkey_data_971, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.7.2",
				&get_inkey_data_972, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.7.3",
				&get_inkey_data_973, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.8.1",
				&get_inkey_data_981, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.8.2",
				&get_inkey_data_982, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.8.3",
				&get_inkey_data_983, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.9.1",
				&get_inkey_data_991, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.9.2a",
				&get_inkey_data_992a, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.9.2b",
				&get_inkey_data_992b, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.9.3",
				&get_inkey_data_993, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.10.1",
				&get_inkey_data_9101, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 9.10.2",
				&get_inkey_data_9102, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 10.1.1",
				&get_inkey_data_1011, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 10.2.1",
				&get_inkey_data_1021, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 11.1.1",
				&get_inkey_data_1111, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 12.1.1",
				&get_inkey_data_1211, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 12.2.1",
				&get_inkey_data_1221, test_get_inkey);
	g_test_add_data_func("/teststk/Get Inkey 13.1.1",
				&get_inkey_data_1311, test_get_inkey);

	g_test_add_data_func("/teststk/Get Inkey response 1.1.1",
				&get_inkey_response_data_111,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 1.2.1",
				&get_inkey_response_data_121,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 1.3.1",
				&get_inkey_response_data_131,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 1.4.1",
				&get_inkey_response_data_141,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 1.5.1",
				&get_inkey_response_data_151,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 1.6.1",
				&get_inkey_response_data_161,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 2.1.1",
				&get_inkey_response_data_211,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 4.1.1",
				&get_inkey_response_data_411,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 5.1.1",
				&get_inkey_response_data_511,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 5.1.2",
				&get_inkey_response_data_512,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 6.1.1B",
				&get_inkey_response_data_611b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 7.1.1",
				&get_inkey_response_data_711,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 7.1.2",
				&get_inkey_response_data_712,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 8.1.1",
				&get_inkey_response_data_811,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 9.1.2",
				&get_inkey_response_data_912,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 11.1.1",
				&get_inkey_response_data_1111,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Inkey response 13.1.1",
				&get_inkey_response_data_1311,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Get Input 1.1.1",
				&get_input_data_111, test_get_input);
	g_test_add_data_func("/teststk/Get Input 1.2.1",
				&get_input_data_121, test_get_input);
	g_test_add_data_func("/teststk/Get Input 1.3.1",
				&get_input_data_131, test_get_input);
	g_test_add_data_func("/teststk/Get Input 1.4.1",
				&get_input_data_141, test_get_input);
	g_test_add_data_func("/teststk/Get Input 1.5.1",
				&get_input_data_151, test_get_input);
	g_test_add_data_func("/teststk/Get Input 1.6.1",
				&get_input_data_161, test_get_input);
	g_test_add_data_func("/teststk/Get Input 1.7.1",
				&get_input_data_171, test_get_input);
	g_test_add_data_func("/teststk/Get Input 1.8.1",
				&get_input_data_181, test_get_input);
	g_test_add_data_func("/teststk/Get Input 1.9.1",
				&get_input_data_191, test_get_input);
	g_test_add_data_func("/teststk/Get Input 1.10.1",
				&get_input_data_1101, test_get_input);
	g_test_add_data_func("/teststk/Get Input 2.1.1",
				&get_input_data_211, test_get_input);
	g_test_add_data_func("/teststk/Get Input 3.1.1",
				&get_input_data_311, test_get_input);
	g_test_add_data_func("/teststk/Get Input 3.2.1",
				&get_input_data_321, test_get_input);
	g_test_add_data_func("/teststk/Get Input 4.1.1",
				&get_input_data_411, test_get_input);
	g_test_add_data_func("/teststk/Get Input 4.2.1",
				&get_input_data_421, test_get_input);
	g_test_add_data_func("/teststk/Get Input 5.1.1",
				&get_input_data_511, test_get_input);
	g_test_add_data_func("/teststk/Get Input 5.2.1",
				&get_input_data_521, test_get_input);
	g_test_add_data_func("/teststk/Get Input 6.1.1",
				&get_input_data_611, test_get_input);
	g_test_add_data_func("/teststk/Get Input 6.2.1",
				&get_input_data_621, test_get_input);
	g_test_add_data_func("/teststk/Get Input 6.3.1",
				&get_input_data_631, test_get_input);
	g_test_add_data_func("/teststk/Get Input 6.4.1",
				&get_input_data_641, test_get_input);
	g_test_add_data_func("/teststk/Get Input 7.1.1",
				&get_input_data_711, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.1.1",
				&get_input_data_811, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.1.2",
				&get_input_data_812, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.2.1",
				&get_input_data_821, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.2.2",
				&get_input_data_822, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.3.1",
				&get_input_data_831, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.3.2",
				&get_input_data_832, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.4.1",
				&get_input_data_841, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.4.2",
				&get_input_data_842, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.4.3",
				&get_input_data_843, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.5.1",
				&get_input_data_851, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.5.2",
				&get_input_data_852, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.5.3",
				&get_input_data_853, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.6.1",
				&get_input_data_861, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.6.2",
				&get_input_data_862, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.6.3",
				&get_input_data_863, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.7.1",
				&get_input_data_871, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.7.2",
				&get_input_data_872, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.7.3",
				&get_input_data_873, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.8.1",
				&get_input_data_881, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.8.2",
				&get_input_data_882, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.8.3",
				&get_input_data_883, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.9.1",
				&get_input_data_891, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.9.2",
				&get_input_data_892, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.9.3",
				&get_input_data_893, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.10.1",
				&get_input_data_8101, test_get_input);
	g_test_add_data_func("/teststk/Get Input 8.10.2",
				&get_input_data_8102, test_get_input);
	g_test_add_data_func("/teststk/Get Input 9.1.1",
				&get_input_data_911, test_get_input);
	g_test_add_data_func("/teststk/Get Input 9.2.1",
				&get_input_data_921, test_get_input);
	g_test_add_data_func("/teststk/Get Input 10.1.1",
				&get_input_data_1011, test_get_input);
	g_test_add_data_func("/teststk/Get Input 10.2.1",
				&get_input_data_1021, test_get_input);
	g_test_add_data_func("/teststk/Get Input 11.1.1",
				&get_input_data_1111, test_get_input);
	g_test_add_data_func("/teststk/Get Input 11.2.1",
				&get_input_data_1121, test_get_input);
	g_test_add_data_func("/teststk/Get Input 12.1.1",
				&get_input_data_1211, test_get_input);
	g_test_add_data_func("/teststk/Get Input 12.2.1",
				&get_input_data_1221, test_get_input);

	g_test_add_data_func("/teststk/Get Input response 1.1.1",
				&get_input_response_data_111,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 1.2.1",
				&get_input_response_data_121,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 1.3.1",
				&get_input_response_data_131,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 1.4.1",
				&get_input_response_data_141,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 1.5.1",
				&get_input_response_data_151,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 1.6.1",
				&get_input_response_data_161,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 1.7.1",
				&get_input_response_data_171,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 1.8.1",
				&get_input_response_data_181,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 1.9.1",
				&get_input_response_data_191,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 2.1.1",
				&get_input_response_data_211,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 3.1.1",
				&get_input_response_data_311,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 4.1.1",
				&get_input_response_data_411,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 4.2.1",
				&get_input_response_data_421,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 6.1.1A",
				&get_input_response_data_611a,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 6.1.1B",
				&get_input_response_data_611b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 7.1.1",
				&get_input_response_data_711,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 8.1.2",
				&get_input_response_data_812,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 8.4.3",
				&get_input_response_data_843,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 10.1.1",
				&get_input_response_data_1011,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 10.2.1",
				&get_input_response_data_1021,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 12.1.1",
				&get_input_response_data_1211,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Input response 12.2.1",
				&get_input_response_data_1221,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/More Time 1.1.1",
				&more_time_data_111, test_more_time);

	g_test_add_data_func("/teststk/More Time response 1.1.1",
				&more_time_response_data_111,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Play Tone 1.1.1",
				&play_tone_data_111, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.2",
				&play_tone_data_112, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.3",
				&play_tone_data_113, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.4",
				&play_tone_data_114, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.5",
				&play_tone_data_115, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.6",
				&play_tone_data_116, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.7",
				&play_tone_data_117, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.8",
				&play_tone_data_118, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.9",
				&play_tone_data_119, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.10",
				&play_tone_data_1110, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.11",
				&play_tone_data_1111, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.12",
				&play_tone_data_1112, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.13",
				&play_tone_data_1113, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.14",
				&play_tone_data_1114, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 1.1.15",
				&play_tone_data_1115, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 2.1.1",
				&play_tone_data_211, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 2.1.2",
				&play_tone_data_212, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 2.1.3",
				&play_tone_data_213, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 3.1.1",
				&play_tone_data_311, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 3.2.1",
				&play_tone_data_321, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 3.3.1",
				&play_tone_data_331, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 3.4.1",
				&play_tone_data_341, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.1.1",
				&play_tone_data_411, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.1.2",
				&play_tone_data_412, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.2.1",
				&play_tone_data_421, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.2.2",
				&play_tone_data_422, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.3.1",
				&play_tone_data_431, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.3.2",
				&play_tone_data_432, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.4.1",
				&play_tone_data_441, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.4.2",
				&play_tone_data_442, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.4.3",
				&play_tone_data_443, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.5.1",
				&play_tone_data_451, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.5.2",
				&play_tone_data_452, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.5.3",
				&play_tone_data_453, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.6.1",
				&play_tone_data_461, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.6.2",
				&play_tone_data_462, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.6.3",
				&play_tone_data_463, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.7.1",
				&play_tone_data_471, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.7.2",
				&play_tone_data_472, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.7.3",
				&play_tone_data_473, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.8.1",
				&play_tone_data_481, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.8.2",
				&play_tone_data_482, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.8.3",
				&play_tone_data_483, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.9.1",
				&play_tone_data_491, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.9.2",
				&play_tone_data_492, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.9.3",
				&play_tone_data_493, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.10.1",
				&play_tone_data_4101, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 4.10.2",
				&play_tone_data_4102, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 5.1.1",
				&play_tone_data_511, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 5.1.2",
				&play_tone_data_512, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 5.1.3",
				&play_tone_data_513, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 6.1.1",
				&play_tone_data_611, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 6.1.2",
				&play_tone_data_612, test_play_tone);
	g_test_add_data_func("/teststk/Play Tone 6.1.3",
				&play_tone_data_613, test_play_tone);

	g_test_add_data_func("/teststk/Play Tone response 1.1.1",
				&play_tone_response_data_111,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Play Tone response 1.1.9B",
				&play_tone_response_data_119b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Play Tone response 1.1.14",
				&play_tone_response_data_1114,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Play Tone response 3.1.1B",
				&play_tone_response_data_311b,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Poll Interval 1.1.1",
				&poll_interval_data_111, test_poll_interval);

	g_test_add_data_func("/teststk/Poll Interval response 1.1.1",
				&poll_interval_response_data_111,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Poll Interval response 1.1.1A",
				&poll_interval_response_data_111a,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Setup Menu 1.1.1",
				&setup_menu_data_111, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 1.1.2",
				&setup_menu_data_112, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 1.1.3",
				&setup_menu_data_113, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 1.2.1",
				&setup_menu_data_121, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 1.2.2",
				&setup_menu_data_122, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 1.2.3",
				&setup_menu_data_123, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 2.1.1",
				&setup_menu_data_211, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 3.1.1",
				&setup_menu_data_311, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 4.1.1",
				&setup_menu_data_411, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 4.2.1",
				&setup_menu_data_421, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 5.1.1",
				&setup_menu_data_511, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.1.1",
				&setup_menu_data_611, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.1.2",
				&setup_menu_data_612, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.2.1",
				&setup_menu_data_621, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.2.2",
				&setup_menu_data_622, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.3.1",
				&setup_menu_data_631, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.3.2",
				&setup_menu_data_632, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.4.1",
				&setup_menu_data_641, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.4.2",
				&setup_menu_data_642, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.4.3",
				&setup_menu_data_643, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.5.1",
				&setup_menu_data_651, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.6.1",
				&setup_menu_data_661, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.7.1",
				&setup_menu_data_671, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.8.1",
				&setup_menu_data_681, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.9.1",
				&setup_menu_data_691, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 6.10.1",
				&setup_menu_data_6101, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 7.1.1",
				&setup_menu_data_711, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 7.1.2",
				&setup_menu_data_712, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 7.1.3",
				&setup_menu_data_713, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 8.1.1",
				&setup_menu_data_811, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 8.1.2",
				&setup_menu_data_812, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 8.1.3",
				&setup_menu_data_813, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 9.1.1",
				&setup_menu_data_911, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 9.1.2",
				&setup_menu_data_912, test_setup_menu);
	g_test_add_data_func("/teststk/Setup Menu 9.1.3",
				&setup_menu_data_913, test_setup_menu);

	g_test_add_data_func("/teststk/Setup Menu Negative 1",
			&setup_menu_data_neg_1, test_setup_menu_missing_val);
	g_test_add_data_func("/teststk/Setup Menu Negative 2",
			&setup_menu_data_neg_2, test_setup_menu_neg);
	g_test_add_data_func("/teststk/Setup Menu Negative 3",
			&setup_menu_data_neg_3, test_setup_menu_neg);
	g_test_add_data_func("/teststk/Setup Menu Negative 4",
			&setup_menu_data_neg_4, test_setup_menu_neg);

	g_test_add_data_func("/teststk/Set Up Menu response 1.1.1",
				&set_up_menu_response_data_111,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Menu response 4.1.1B",
				&set_up_menu_response_data_411b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Menu response 5.1.1",
				&set_up_menu_response_data_511,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Select Item 1.1.1",
				&select_item_data_111, test_select_item);
	g_test_add_data_func("/teststk/Select Item 1.2.1",
				&select_item_data_121, test_select_item);
	g_test_add_data_func("/teststk/Select Item 1.3.1",
				&select_item_data_131, test_select_item);
	g_test_add_data_func("/teststk/Select Item 1.4.1",
				&select_item_data_141, test_select_item);
	g_test_add_data_func("/teststk/Select Item 1.5.1",
				&select_item_data_151, test_select_item);
	g_test_add_data_func("/teststk/Select Item 1.6.1",
				&select_item_data_161, test_select_item);
	g_test_add_data_func("/teststk/Select Item 2.1.1",
				&select_item_data_211, test_select_item);
	g_test_add_data_func("/teststk/Select Item 3.1.1",
				&select_item_data_311, test_select_item);
	g_test_add_data_func("/teststk/Select Item 4.1.1",
				&select_item_data_411, test_select_item);
	g_test_add_data_func("/teststk/Select Item 5.1.1",
				&select_item_data_511, test_select_item);
	g_test_add_data_func("/teststk/Select Item 5.2.1",
				&select_item_data_521, test_select_item);
	g_test_add_data_func("/teststk/Select Item 6.1.1",
				&select_item_data_611, test_select_item);
	g_test_add_data_func("/teststk/Select Item 6.2.1",
				&select_item_data_621, test_select_item);
	g_test_add_data_func("/teststk/Select Item 7.1.1",
				&select_item_data_711, test_select_item);
	g_test_add_data_func("/teststk/Select Item 8.1.1",
				&select_item_data_811, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.1.1",
				&select_item_data_911, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.1.2",
				&select_item_data_912, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.2.1",
				&select_item_data_921, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.2.2",
				&select_item_data_922, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.3.1",
				&select_item_data_931, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.3.2",
				&select_item_data_932, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.4.1",
				&select_item_data_941, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.4.2",
				&select_item_data_942, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.4.3",
				&select_item_data_943, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.5.1",
				&select_item_data_951, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.5.2",
				&select_item_data_952, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.5.3",
				&select_item_data_953, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.6.1",
				&select_item_data_961, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.6.2",
				&select_item_data_962, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.6.3",
				&select_item_data_963, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.7.1",
				&select_item_data_971, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.7.2",
				&select_item_data_972, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.7.3",
				&select_item_data_973, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.8.1",
				&select_item_data_981, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.8.2",
				&select_item_data_982, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.8.3",
				&select_item_data_983, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.9.1",
				&select_item_data_991, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.9.2",
				&select_item_data_992, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.9.3",
				&select_item_data_993, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.10.1",
				&select_item_data_9101, test_select_item);
	g_test_add_data_func("/teststk/Select Item 9.10.2",
				&select_item_data_9102, test_select_item);
	g_test_add_data_func("/teststk/Select Item 10.1.1",
				&select_item_data_1011, test_select_item);
	g_test_add_data_func("/teststk/Select Item 10.2.1",
				&select_item_data_1021, test_select_item);
	g_test_add_data_func("/teststk/Select Item 10.3.1",
				&select_item_data_1031, test_select_item);
	g_test_add_data_func("/teststk/Select Item 11.1.1",
				&select_item_data_1111, test_select_item);
	g_test_add_data_func("/teststk/Select Item 12.1.1",
				&select_item_data_1211, test_select_item);
	g_test_add_data_func("/teststk/Select Item 12.2.1",
				&select_item_data_1221, test_select_item);
	g_test_add_data_func("/teststk/Select Item 12.3.1",
				&select_item_data_1231, test_select_item);

	g_test_add_data_func("/teststk/Select Item response 1.1.1",
				&select_item_response_data_111,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 1.2.1",
				&select_item_response_data_121,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 1.3.1",
				&select_item_response_data_131,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 1.4.1",
				&select_item_response_data_141,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 1.4.2",
				&select_item_response_data_142,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 1.5.1",
				&select_item_response_data_151,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 3.1.1",
				&select_item_response_data_311,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 4.1.1",
				&select_item_response_data_411,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 5.1.1B",
				&select_item_response_data_511b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 6.1.1",
				&select_item_response_data_611,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 6.2.1",
				&select_item_response_data_621,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 7.1.1",
				&select_item_response_data_711,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Select Item response 8.1.1",
				&select_item_response_data_811,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Send SMS 1.1.1",
				&send_sms_data_111, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 1.2.1",
				&send_sms_data_121, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 1.3.1",
				&send_sms_data_131, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 1.4.1",
				&send_sms_data_141, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 1.5.1",
				&send_sms_data_151, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 1.6.1",
				&send_sms_data_161, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 1.7.1",
				&send_sms_data_171, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 1.8.1",
				&send_sms_data_181, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 2.1.1",
				&send_sms_data_211, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 2.1.2",
				&send_sms_data_212, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 2.1.3",
				&send_sms_data_213, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 3.1.1",
				&send_sms_data_311, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 3.2.1",
				&send_sms_data_321, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.1.1",
				&send_sms_data_411, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.1.2",
				&send_sms_data_412, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.2.1",
				&send_sms_data_421, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.2.2",
				&send_sms_data_422, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.3.1",
				&send_sms_data_431, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.3.2",
				&send_sms_data_432, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.4.1",
				&send_sms_data_441, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.4.2",
				&send_sms_data_442, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.4.3",
				&send_sms_data_443, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.5.1",
				&send_sms_data_451, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.5.2",
				&send_sms_data_452, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.5.3",
				&send_sms_data_453, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.6.1",
				&send_sms_data_461, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.6.2",
				&send_sms_data_462, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.6.3",
				&send_sms_data_463, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.7.1",
				&send_sms_data_471, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.7.2",
				&send_sms_data_472, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.7.3",
				&send_sms_data_473, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.8.1",
				&send_sms_data_481, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.8.2",
				&send_sms_data_482, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.8.3",
				&send_sms_data_483, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.9.1",
				&send_sms_data_491, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.9.2",
				&send_sms_data_492, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.9.3",
				&send_sms_data_493, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.10.1",
				&send_sms_data_4101, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 4.10.2",
				&send_sms_data_4102, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 5.1.1",
				&send_sms_data_511, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 5.1.2",
				&send_sms_data_512, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 5.1.3",
				&send_sms_data_513, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 6.1.1",
				&send_sms_data_611, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 6.1.2",
				&send_sms_data_612, test_send_sms);
	g_test_add_data_func("/teststk/Send SMS 6.1.3",
				&send_sms_data_613, test_send_sms);

	g_test_add_data_func("/teststk/Send SS 1.1.1",
				&send_ss_data_111, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 1.4.1",
				&send_ss_data_141, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 1.5.1",
				&send_ss_data_151, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 1.6.1",
				&send_ss_data_161, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 2.1.1",
				&send_ss_data_211, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 2.2.1",
				&send_ss_data_221, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 2.3.1",
				&send_ss_data_231, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 2.4.1",
				&send_ss_data_241, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 3.1.1",
				&send_ss_data_311, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.1.1",
				&send_ss_data_411, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.1.2",
				&send_ss_data_412, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.2.1",
				&send_ss_data_421, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.2.2",
				&send_ss_data_422, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.3.1",
				&send_ss_data_431, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.3.2",
				&send_ss_data_432, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.4.1",
				&send_ss_data_441, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.4.2",
				&send_ss_data_442, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.4.3",
				&send_ss_data_443, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.5.1",
				&send_ss_data_451, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.5.2",
				&send_ss_data_452, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.5.3",
				&send_ss_data_453, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.6.1",
				&send_ss_data_461, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.6.2",
				&send_ss_data_462, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.6.3",
				&send_ss_data_463, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.7.1",
				&send_ss_data_471, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.7.2",
				&send_ss_data_472, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.7.3",
				&send_ss_data_473, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.8.1",
				&send_ss_data_481, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.8.2",
				&send_ss_data_482, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.8.3",
				&send_ss_data_483, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.9.1",
				&send_ss_data_491, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.9.2",
				&send_ss_data_492, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.9.3",
				&send_ss_data_493, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.10.1",
				&send_ss_data_4101, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 4.10.2",
				&send_ss_data_4102, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 5.1.1",
				&send_ss_data_511, test_send_ss);
	g_test_add_data_func("/teststk/Send SS 6.1.1",
				&send_ss_data_611, test_send_ss);

	g_test_add_data_func("/teststk/Send USSD 1.1.1",
				&send_ussd_data_111, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 1.2.1",
				&send_ussd_data_121, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 1.3.1",
				&send_ussd_data_131, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 1.6.1",
				&send_ussd_data_161, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 1.7.1",
				&send_ussd_data_171, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 1.8.1",
				&send_ussd_data_181, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 2.1.1",
				&send_ussd_data_211, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 2.2.1",
				&send_ussd_data_221, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 2.3.1",
				&send_ussd_data_231, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 2.4.1",
				&send_ussd_data_241, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 3.1.1",
				&send_ussd_data_311, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.1.1",
				&send_ussd_data_411, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.1.2",
				&send_ussd_data_412, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.2.1",
				&send_ussd_data_421, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.2.2",
				&send_ussd_data_422, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.3.1",
				&send_ussd_data_431, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.3.2",
				&send_ussd_data_432, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.4.1",
				&send_ussd_data_441, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.4.2",
				&send_ussd_data_442, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.4.3",
				&send_ussd_data_443, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.5.1",
				&send_ussd_data_451, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.5.2",
				&send_ussd_data_452, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.5.3",
				&send_ussd_data_453, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.6.1",
				&send_ussd_data_461, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.6.2",
				&send_ussd_data_462, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.6.3",
				&send_ussd_data_463, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.7.1",
				&send_ussd_data_471, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.7.2",
				&send_ussd_data_472, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.7.3",
				&send_ussd_data_473, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.8.1",
				&send_ussd_data_481, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.8.2",
				&send_ussd_data_482, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.8.3",
				&send_ussd_data_483, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.9.1",
				&send_ussd_data_491, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.9.2",
				&send_ussd_data_492, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.9.3",
				&send_ussd_data_493, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.10.1",
				&send_ussd_data_4101, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 4.10.2",
				&send_ussd_data_4102, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 5.1.1",
				&send_ussd_data_511, test_send_ussd);
	g_test_add_data_func("/teststk/Send USSD 6.1.1",
				&send_ussd_data_611, test_send_ussd);

	g_test_add_data_func("/teststk/Send SMS response 1.1.1",
				&send_sms_response_data_111,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Send SMS response 1.2.1",
				&send_sms_response_data_121,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Send SMS response 3.1.1B",
				&send_sms_response_data_311b,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Setup Call 1.1.1",
				&setup_call_data_111, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 1.4.1",
				&setup_call_data_141, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 1.5.1",
				&setup_call_data_151, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 1.8.1",
				&setup_call_data_181, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 1.9.1",
				&setup_call_data_191, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 1.10.1",
				&setup_call_data_1101, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 1.11.1",
				&setup_call_data_1111, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 1.12.1",
				&setup_call_data_1121, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 2.1.1",
				&setup_call_data_211, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 3.1.1",
				&setup_call_data_311, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 3.2.1",
				&setup_call_data_321, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 3.3.1",
				&setup_call_data_331, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 3.4.1",
				&setup_call_data_341, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.1.1",
				&setup_call_data_411, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.1.2",
				&setup_call_data_412, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.2.1",
				&setup_call_data_421, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.2.2",
				&setup_call_data_422, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.3.1",
				&setup_call_data_431, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.3.2",
				&setup_call_data_432, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.4.1",
				&setup_call_data_441, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.4.2",
				&setup_call_data_442, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.4.3",
				&setup_call_data_443, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.5.1",
				&setup_call_data_451, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.5.2",
				&setup_call_data_452, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.5.3",
				&setup_call_data_453, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.6.1",
				&setup_call_data_461, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.6.2",
				&setup_call_data_462, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.6.3",
				&setup_call_data_463, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.7.1",
				&setup_call_data_471, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.7.2",
				&setup_call_data_472, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.7.3",
				&setup_call_data_473, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.8.1",
				&setup_call_data_481, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.8.2",
				&setup_call_data_482, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.8.3",
				&setup_call_data_483, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.9.1",
				&setup_call_data_491, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.9.2",
				&setup_call_data_492, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.9.3",
				&setup_call_data_493, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.10.1",
				&setup_call_data_4101, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 4.10.2",
				&setup_call_data_4102, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 5.1.1",
				&setup_call_data_511, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 5.2.1",
				&setup_call_data_521, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 6.1.1",
				&setup_call_data_611, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 6.2.1",
				&setup_call_data_621, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 7.1.1",
				&setup_call_data_711, test_setup_call);
	g_test_add_data_func("/teststk/Setup Call 7.2.1",
				&setup_call_data_721, test_setup_call);

	g_test_add_data_func("/teststk/Set Up Call response 1.1.1",
				&set_up_call_response_data_111,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Call response 1.2.1",
				&set_up_call_response_data_121,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Call response 1.4.1",
				&set_up_call_response_data_141,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Call response 1.5.1",
				&set_up_call_response_data_151,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Call response 1.6.1",
				&set_up_call_response_data_161,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Call response 1.7.1A",
				&set_up_call_response_data_171a,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Call response 1.7.1B",
				&set_up_call_response_data_171b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Call response 1.10.1",
				&set_up_call_response_data_1101,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Call response 1.11.1B",
				&set_up_call_response_data_1111b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Call response 1.12.1",
				&set_up_call_response_data_1121,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Call response 3.1.1B",
				&set_up_call_response_data_311b,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Refresh 1.2.1",
				&refresh_data_121, test_refresh);
	g_test_add_data_func("/teststk/Refresh 1.5.1",
				&refresh_data_151, test_refresh);

	g_test_add_data_func("/teststk/Refresh response 1.1.1A",
				&refresh_response_data_111a,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 1.1.1B",
				&refresh_response_data_111b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 1.2.1A",
				&refresh_response_data_121a,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 1.2.1B",
				&refresh_response_data_121b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 1.3.1A",
				&refresh_response_data_131a,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 1.3.1B",
				&refresh_response_data_141b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 1.4.1A",
				&refresh_response_data_141a,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 1.4.1B",
				&refresh_response_data_141b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 1.7.1",
				&refresh_response_data_171,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 2.4.1A",
				&refresh_response_data_241a,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 2.4.1B",
				&refresh_response_data_241b,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 3.1.1",
				&refresh_response_data_311,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Refresh response 3.1.2",
				&refresh_response_data_312,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Polling off 1.1.2",
				&polling_off_data_112, test_polling_off);

	g_test_add_data_func("/teststk/Polling off response 1.1.2",
				&polling_off_response_data_112,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Provide Local Info 1.2.1",
			&provide_local_info_data_121, test_provide_local_info);
	g_test_add_data_func("/teststk/Provide Local Info 1.4.1",
			&provide_local_info_data_141, test_provide_local_info);
	g_test_add_data_func("/teststk/Provide Local Info 1.5.1",
			&provide_local_info_data_151, test_provide_local_info);
	g_test_add_data_func("/teststk/Provide Local Info 1.8.1",
			&provide_local_info_data_181, test_provide_local_info);
	g_test_add_data_func("/teststk/Provide Local Info 1.9.1",
			&provide_local_info_data_191, test_provide_local_info);
	g_test_add_data_func("/teststk/Provide Local Info 1.11.1",
			&provide_local_info_data_1111, test_provide_local_info);

	g_test_add_data_func("/teststk/Provide Local Info response 1.1.1A",
			&provide_local_info_response_data_111a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.1.1B",
			&provide_local_info_response_data_111b,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.2.1",
			&provide_local_info_response_data_121,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.3.1",
			&provide_local_info_response_data_131,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.4.1",
			&provide_local_info_response_data_141,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.5.1",
			&provide_local_info_response_data_151,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.6.1",
			&provide_local_info_response_data_161,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.7.1",
			&provide_local_info_response_data_171,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.8.1",
			&provide_local_info_response_data_181,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.9.1",
			&provide_local_info_response_data_191,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.11.1",
			&provide_local_info_response_data_1111,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.12.1",
			&provide_local_info_response_data_1121,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.13.1",
			&provide_local_info_response_data_1131,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.14.1",
			&provide_local_info_response_data_1141,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.15.1",
			&provide_local_info_response_data_1151,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.16.1",
			&provide_local_info_response_data_1161,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Provide Local Info response 1.17.1",
			&provide_local_info_response_data_1171,
			test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Setup Event List 1.1.1",
			&setup_event_list_data_111, test_setup_event_list);
	g_test_add_data_func("/teststk/Setup Event List 1.2.1",
			&setup_event_list_data_121, test_setup_event_list);
	g_test_add_data_func("/teststk/Setup Event List 1.2.2",
			&setup_event_list_data_122, test_setup_event_list);
	g_test_add_data_func("/teststk/Setup Event List 1.3.1",
			&setup_event_list_data_131, test_setup_event_list);
	g_test_add_data_func("/teststk/Setup Event List 1.3.2",
			&setup_event_list_data_132, test_setup_event_list);
	g_test_add_data_func("/teststk/Setup Event List 1.4.1",
			&setup_event_list_data_141, test_setup_event_list);

	g_test_add_data_func("/teststk/Set Up Event List response 1.1.1",
			&set_up_event_list_response_data_111,
			test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Perform Card APDU 1.1.1",
			&perform_card_apdu_data_111, test_perform_card_apdu);
	g_test_add_data_func("/teststk/Perform Card APDU 1.1.2",
			&perform_card_apdu_data_112, test_perform_card_apdu);
	g_test_add_data_func("/teststk/Perform Card APDU 1.2.1",
			&perform_card_apdu_data_121, test_perform_card_apdu);
	g_test_add_data_func("/teststk/Perform Card APDU 1.2.2",
			&perform_card_apdu_data_122, test_perform_card_apdu);
	g_test_add_data_func("/teststk/Perform Card APDU 1.2.3",
			&perform_card_apdu_data_123, test_perform_card_apdu);
	g_test_add_data_func("/teststk/Perform Card APDU 1.2.4",
			&perform_card_apdu_data_124, test_perform_card_apdu);
	g_test_add_data_func("/teststk/Perform Card APDU 1.2.5",
			&perform_card_apdu_data_125, test_perform_card_apdu);
	g_test_add_data_func("/teststk/Perform Card APDU 1.5.1",
			&perform_card_apdu_data_151, test_perform_card_apdu);
	g_test_add_data_func("/teststk/Perform Card APDU 2.1.1",
			&perform_card_apdu_data_211, test_perform_card_apdu);

	g_test_add_data_func("/teststk/Get Reader Status 1.1.1",
			&get_reader_status_data_111, test_get_reader_status);

	g_test_add_data_func("/teststk/Timer Management 1.1.1",
			&timer_mgmt_data_111, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.1.2",
			&timer_mgmt_data_112, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.1.3",
			&timer_mgmt_data_113, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.1.4",
			&timer_mgmt_data_114, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.2.1",
			&timer_mgmt_data_121, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.2.2",
			&timer_mgmt_data_122, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.2.3",
			&timer_mgmt_data_123, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.2.4",
			&timer_mgmt_data_124, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.3.1",
			&timer_mgmt_data_131, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.3.2",
			&timer_mgmt_data_132, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.3.3",
			&timer_mgmt_data_133, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.3.4",
			&timer_mgmt_data_134, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.4.1",
			&timer_mgmt_data_141, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.4.2",
			&timer_mgmt_data_142, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.4.3",
			&timer_mgmt_data_143, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.4.4",
			&timer_mgmt_data_144, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.4.5",
			&timer_mgmt_data_145, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.4.6",
			&timer_mgmt_data_146, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.4.7",
			&timer_mgmt_data_147, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.4.8",
			&timer_mgmt_data_148, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.5.1",
			&timer_mgmt_data_151, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.5.2",
			&timer_mgmt_data_152, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.5.3",
			&timer_mgmt_data_153, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.5.4",
			&timer_mgmt_data_154, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.5.5",
			&timer_mgmt_data_155, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.5.6",
			&timer_mgmt_data_156, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.5.7",
			&timer_mgmt_data_157, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.5.8",
			&timer_mgmt_data_158, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.6.1",
			&timer_mgmt_data_161, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.6.2",
			&timer_mgmt_data_162, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.6.3",
			&timer_mgmt_data_163, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.6.4",
			&timer_mgmt_data_164, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.6.5",
			&timer_mgmt_data_165, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.6.6",
			&timer_mgmt_data_166, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.6.7",
			&timer_mgmt_data_167, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 1.6.8",
			&timer_mgmt_data_168, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 2.1.1",
			&timer_mgmt_data_211, test_timer_mgmt);
	g_test_add_data_func("/teststk/Timer Management 2.2.1",
			&timer_mgmt_data_221, test_timer_mgmt);

	g_test_add_data_func("/teststk/Timer Management response 1.1.1",
			&timer_mgmt_response_data_111,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.1.2",
			&timer_mgmt_response_data_112,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.1.4",
			&timer_mgmt_response_data_114,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.2.1",
			&timer_mgmt_response_data_121,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.2.2",
			&timer_mgmt_response_data_122,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.2.4",
			&timer_mgmt_response_data_124,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.3.1",
			&timer_mgmt_response_data_131,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.3.2",
			&timer_mgmt_response_data_132,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.3.4",
			&timer_mgmt_response_data_134,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.4.1A",
			&timer_mgmt_response_data_141a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.4.1B",
			&timer_mgmt_response_data_141b,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.4.2A",
			&timer_mgmt_response_data_142a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.4.3A",
			&timer_mgmt_response_data_143a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.4.4A",
			&timer_mgmt_response_data_144a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.4.5A",
			&timer_mgmt_response_data_145a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.4.6A",
			&timer_mgmt_response_data_146a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.4.7A",
			&timer_mgmt_response_data_147a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.4.8A",
			&timer_mgmt_response_data_148a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.5.1A",
			&timer_mgmt_response_data_151a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.5.1B",
			&timer_mgmt_response_data_151b,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.5.2A",
			&timer_mgmt_response_data_152a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.5.3A",
			&timer_mgmt_response_data_153a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.5.4A",
			&timer_mgmt_response_data_154a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.5.5A",
			&timer_mgmt_response_data_155a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.5.6A",
			&timer_mgmt_response_data_156a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.5.7A",
			&timer_mgmt_response_data_157a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.5.8A",
			&timer_mgmt_response_data_158a,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.6.3",
			&timer_mgmt_response_data_163,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.6.4",
			&timer_mgmt_response_data_164,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.6.5",
			&timer_mgmt_response_data_165,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.6.6",
			&timer_mgmt_response_data_166,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Timer Management response 1.6.7",
			&timer_mgmt_response_data_167,
			test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Setup Idle Mode Text 1.1.1",
		&setup_idle_mode_text_data_111, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 1.2.1",
		&setup_idle_mode_text_data_121, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 1.3.1",
		&setup_idle_mode_text_data_131, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 1.7.1",
		&setup_idle_mode_text_data_171, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 2.1.1",
		&setup_idle_mode_text_data_211, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 2.2.1",
		&setup_idle_mode_text_data_221, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 2.3.1",
		&setup_idle_mode_text_data_231, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 2.4.1",
		&setup_idle_mode_text_data_241, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 3.1.1",
		&setup_idle_mode_text_data_311, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.1.1",
		&setup_idle_mode_text_data_411, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.1.2",
		&setup_idle_mode_text_data_412, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.2.1",
		&setup_idle_mode_text_data_421, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.2.2",
		&setup_idle_mode_text_data_422, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.3.1",
		&setup_idle_mode_text_data_431, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.3.2",
		&setup_idle_mode_text_data_432, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.4.1",
		&setup_idle_mode_text_data_441, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.4.2",
		&setup_idle_mode_text_data_442, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.4.3",
		&setup_idle_mode_text_data_443, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.5.1",
		&setup_idle_mode_text_data_451, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.5.2",
		&setup_idle_mode_text_data_452, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.5.3",
		&setup_idle_mode_text_data_453, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.6.1",
		&setup_idle_mode_text_data_461, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.6.2",
		&setup_idle_mode_text_data_462, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.6.3",
		&setup_idle_mode_text_data_463, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.7.1",
		&setup_idle_mode_text_data_471, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.7.2",
		&setup_idle_mode_text_data_472, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.7.3",
		&setup_idle_mode_text_data_473, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.8.1",
		&setup_idle_mode_text_data_481, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.8.2",
		&setup_idle_mode_text_data_482, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.8.3",
		&setup_idle_mode_text_data_483, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.9.1",
		&setup_idle_mode_text_data_491, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.9.2",
		&setup_idle_mode_text_data_492, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.9.3",
		&setup_idle_mode_text_data_493, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.10.1",
		&setup_idle_mode_text_data_4101, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 4.10.2",
		&setup_idle_mode_text_data_4102, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 5.1.1",
		&setup_idle_mode_text_data_511, test_setup_idle_mode_text);
	g_test_add_data_func("/teststk/Setup Idle Mode Text 6.1.1",
		&setup_idle_mode_text_data_611, test_setup_idle_mode_text);

	g_test_add_data_func("/teststk/Set Up Idle Mode Text response 1.1.1",
			&set_up_idle_mode_text_response_data_111,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Idle Mode Text response 2.1.1B",
			&set_up_idle_mode_text_response_data_211b,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Set Up Idle Mode Text response 2.4.1",
			&set_up_idle_mode_text_response_data_241,
			test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Run At Command 1.1.1",
			&run_at_command_data_111, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 1.2.1",
			&run_at_command_data_121, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 1.3.1",
			&run_at_command_data_131, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 2.1.1",
			&run_at_command_data_211, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 2.2.1",
			&run_at_command_data_221, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 2.3.1",
			&run_at_command_data_231, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 2.4.1",
			&run_at_command_data_241, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 2.5.1",
			&run_at_command_data_251, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.1.1",
			&run_at_command_data_311, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.1.2",
			&run_at_command_data_312, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.2.1",
			&run_at_command_data_321, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.2.2",
			&run_at_command_data_322, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.3.1",
			&run_at_command_data_331, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.3.2",
			&run_at_command_data_332, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.4.1",
			&run_at_command_data_341, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.4.2",
			&run_at_command_data_342, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.4.3",
			&run_at_command_data_343, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.5.1",
			&run_at_command_data_351, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.5.2",
			&run_at_command_data_352, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.5.3",
			&run_at_command_data_353, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.6.1",
			&run_at_command_data_361, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.6.2",
			&run_at_command_data_362, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.6.3",
			&run_at_command_data_363, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.7.1",
			&run_at_command_data_371, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.7.2",
			&run_at_command_data_372, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.7.3",
			&run_at_command_data_373, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.8.1",
			&run_at_command_data_381, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.8.2",
			&run_at_command_data_382, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.8.3",
			&run_at_command_data_383, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.9.1",
			&run_at_command_data_391, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.9.2",
			&run_at_command_data_392, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.9.3",
			&run_at_command_data_393, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.10.1",
			&run_at_command_data_3101, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 3.10.2",
			&run_at_command_data_3102, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 4.1.1",
			&run_at_command_data_411, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 5.1.1",
			&run_at_command_data_511, test_run_at_command);
	g_test_add_data_func("/teststk/Run At Command 6.1.1",
			&run_at_command_data_611, test_run_at_command);

	g_test_add_data_func("/teststk/Run AT Command response 1.1.1",
			&run_at_command_response_data_111,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Run AT Command response 2.1.1B",
			&run_at_command_response_data_211b,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Run AT Command response 2.5.1",
			&run_at_command_response_data_251,
			test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Send DTMF 1.1.1",
			&send_dtmf_data_111, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 1.2.1",
			&send_dtmf_data_121, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 1.3.1",
			&send_dtmf_data_131, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 2.1.1",
			&send_dtmf_data_211, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 2.2.1",
			&send_dtmf_data_221, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 2.3.1",
			&send_dtmf_data_231, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 3.1.1",
			&send_dtmf_data_311, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.1.1",
			&send_dtmf_data_411, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.1.2",
			&send_dtmf_data_412, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.2.1",
			&send_dtmf_data_421, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.2.2",
			&send_dtmf_data_422, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.3.1",
			&send_dtmf_data_431, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.3.2",
			&send_dtmf_data_432, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.4.1",
			&send_dtmf_data_441, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.4.2",
			&send_dtmf_data_442, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.4.3",
			&send_dtmf_data_443, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.5.1",
			&send_dtmf_data_451, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.5.2",
			&send_dtmf_data_452, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.5.3",
			&send_dtmf_data_453, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.6.1",
			&send_dtmf_data_461, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.6.2",
			&send_dtmf_data_462, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.6.3",
			&send_dtmf_data_463, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.7.1",
			&send_dtmf_data_471, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.7.2",
			&send_dtmf_data_472, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.7.3",
			&send_dtmf_data_473, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.8.1",
			&send_dtmf_data_481, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.8.2",
			&send_dtmf_data_482, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.8.3",
			&send_dtmf_data_483, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.9.1",
			&send_dtmf_data_491, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.9.2",
			&send_dtmf_data_492, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.9.3",
			&send_dtmf_data_493, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.10.1",
			&send_dtmf_data_4101, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 4.10.2",
			&send_dtmf_data_4102, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 5.1.1",
			&send_dtmf_data_511, test_send_dtmf);
	g_test_add_data_func("/teststk/Send DTMF 6.1.1",
			&send_dtmf_data_611, test_send_dtmf);

	g_test_add_data_func("/teststk/Send DTMF response 1.1.1",
			&send_dtmf_response_data_111,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Send DTMF response 1.4.1",
			&send_dtmf_response_data_141,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Send DTMF response 2.1.1B",
			&send_dtmf_response_data_211b,
			test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Language Notification 1.1.1",
		&language_notification_data_111, test_language_notification);
	g_test_add_data_func("/teststk/Language Notification 1.2.1",
		&language_notification_data_121, test_language_notification);

	g_test_add_data_func("/teststk/Language Notification response 1.1.1",
			&language_notification_response_data_111,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Language Notification response 1.2.1",
			&language_notification_response_data_121,
			test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Launch Browser 1.1.1",
				&launch_browser_data_111, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 1.2.1",
				&launch_browser_data_121, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 1.3.1",
				&launch_browser_data_131, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 1.4.1",
				&launch_browser_data_141, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 2.1.1",
				&launch_browser_data_211, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 2.2.1",
				&launch_browser_data_221, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 2.3.1",
				&launch_browser_data_231, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 3.1.1",
				&launch_browser_data_311, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 4.1.1",
				&launch_browser_data_411, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 4.2.1",
				&launch_browser_data_421, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.1.1",
				&launch_browser_data_511, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.1.2",
				&launch_browser_data_512, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.2.1",
				&launch_browser_data_521, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.2.2",
				&launch_browser_data_522, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.3.1",
				&launch_browser_data_531, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.3.2",
				&launch_browser_data_532, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.4.1",
				&launch_browser_data_541, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.4.2",
				&launch_browser_data_542, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.4.3",
				&launch_browser_data_543, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.5.1",
				&launch_browser_data_551, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.5.2",
				&launch_browser_data_552, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.5.3",
				&launch_browser_data_553, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.6.1",
				&launch_browser_data_561, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.6.2",
				&launch_browser_data_562, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.6.3",
				&launch_browser_data_563, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.7.1",
				&launch_browser_data_571, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.7.2",
				&launch_browser_data_572, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.7.3",
				&launch_browser_data_573, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.8.1",
				&launch_browser_data_581, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.8.2",
				&launch_browser_data_582, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.8.3",
				&launch_browser_data_583, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.9.1",
				&launch_browser_data_591, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.9.2",
				&launch_browser_data_592, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.9.3",
				&launch_browser_data_593, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.10.1",
				&launch_browser_data_5101, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 5.10.2",
				&launch_browser_data_5102, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 6.1.1",
				&launch_browser_data_611, test_launch_browser);
	g_test_add_data_func("/teststk/Launch Browser 7.1.1",
				&launch_browser_data_711, test_launch_browser);

	g_test_add_data_func("/teststk/Launch Browser response 1.1.1",
			&launch_browser_response_data_111,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Launch Browser response 2.1.1",
			&launch_browser_response_data_211,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Launch Browser response 2.2.1",
			&launch_browser_response_data_221,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Launch Browser response 2.3.1",
			&launch_browser_response_data_231,
			test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Launch Browser response 4.1.1B",
			&launch_browser_response_data_411b,
			test_terminal_response_encoding);


	g_test_add_data_func("/teststk/Open channel 2.1.1",
				&open_channel_data_211, test_open_channel);
	g_test_add_data_func("/teststk/Open channel 2.2.1",
				&open_channel_data_221, test_open_channel);
	g_test_add_data_func("/teststk/Open channel 2.3.1",
				&open_channel_data_231, test_open_channel);
	g_test_add_data_func("/teststk/Open channel 2.4.1",
				&open_channel_data_241, test_open_channel);
	g_test_add_data_func("/teststk/Open channel 5.1.1",
				&open_channel_data_511, test_open_channel);
	g_test_add_data_func("/teststk/Open channel response 2.1.1",
				&open_channel_response_data_211,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Open channel response 2.7.1",
				&open_channel_response_data_271,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Close channel 1.1.1",
				&close_channel_data_111, test_close_channel);
	g_test_add_data_func("/teststk/Close channel 2.1.1",
				&close_channel_data_211, test_close_channel);
	g_test_add_data_func("/teststk/Close channel response 1.2.1",
				&close_channel_response_data_121,
				test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Close channel response 1.3.1",
				&close_channel_response_data_131,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Receive data 1.1.1",
				&receive_data_data_111, test_receive_data);
	g_test_add_data_func("/teststk/Receive data 2.1.1",
				&receive_data_data_211, test_receive_data);
	g_test_add_data_func("/teststk/Receive data response 1.1.1",
				&receive_data_response_data_111,
				test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Send data 1.1.1",
					&send_data_data_111, test_send_data);
	g_test_add_data_func("/teststk/Send data 1.2.1",
					&send_data_data_121, test_send_data);
	g_test_add_data_func("/teststk/Send data 2.1.1",
					&send_data_data_211, test_send_data);
	g_test_add_data_func("/teststk/Send data response 1.1.1",
					&send_data_response_data_111,
					test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Send data response 1.2.1",
					&send_data_response_data_121,
					test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Send data response 1.5.1",
					&send_data_response_data_151,
					test_terminal_response_encoding);

	g_test_add_data_func("/teststk/Get Channel status 1.1.1",
			&get_channel_status_data_111, test_get_channel_status);
	g_test_add_data_func("/teststk/Get Channel status response 1.1.1",
					&get_channel_status_response_data_111,
					test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Channel status response 1.2.1",
					&get_channel_status_response_data_121,
					test_terminal_response_encoding);
	g_test_add_data_func("/teststk/Get Channel status response 1.3.1",
					&get_channel_status_response_data_131,
					test_terminal_response_encoding);

	g_test_add_data_func("/teststk/SMS-PP data download 1.6.1",
			&sms_pp_data_download_data_161,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/SMS-PP data download 1.6.2",
			&sms_pp_data_download_data_162,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/SMS-PP data download 1.8.2",
			&sms_pp_data_download_data_182,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/CBS-PP data download 1.1",
			&cbs_pp_data_download_data_11, test_envelope_encoding);
	g_test_add_data_func("/teststk/CBS-PP data download 1.7",
			&cbs_pp_data_download_data_17, test_envelope_encoding);

	g_test_add_data_func("/teststk/Menu Selection 1.1.1",
			&menu_selection_data_111, test_envelope_encoding);
	g_test_add_data_func("/teststk/Menu Selection 1.1.2",
			&menu_selection_data_112, test_envelope_encoding);
	g_test_add_data_func("/teststk/Menu Selection 1.2.1",
			&menu_selection_data_121, test_envelope_encoding);
	g_test_add_data_func("/teststk/Menu Selection 1.2.2",
			&menu_selection_data_122, test_envelope_encoding);
	g_test_add_data_func("/teststk/Menu Selection 1.2.3",
			&menu_selection_data_123, test_envelope_encoding);
	g_test_add_data_func("/teststk/Menu Selection 2.1.1",
			&menu_selection_data_211, test_envelope_encoding);
	g_test_add_data_func("/teststk/Menu Selection 6.1.2",
			&menu_selection_data_612, test_envelope_encoding);
	g_test_add_data_func("/teststk/Menu Selection 6.4.1",
			&menu_selection_data_641, test_envelope_encoding);

	g_test_add_data_func("/teststk/Call Control 1.1.1A",
			&call_control_data_111a, test_envelope_encoding);
	g_test_add_data_func("/teststk/Call Control 1.1.1B",
			&call_control_data_111b, test_envelope_encoding);
	g_test_add_data_func("/teststk/Call Control 1.3.1A",
			&call_control_data_131a, test_envelope_encoding);
	g_test_add_data_func("/teststk/Call Control 1.3.1B",
			&call_control_data_131b, test_envelope_encoding);

	g_test_add_data_func("/teststk/MO Short Message Control 1.1.1A",
			&mo_short_message_control_data_111a,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/MO Short Message Control 1.1.1B",
			&mo_short_message_control_data_111b,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: MT Call 1.1.1",
			&event_download_mt_call_data_111,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: MT Call 1.1.2",
			&event_download_mt_call_data_112,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: Call Connected 1.1.1",
			&event_download_call_connected_data_111,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Call Connected 1.1.2",
			&event_download_call_connected_data_112,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: Call Disconnected 1.1.1",
			&event_download_call_disconnected_data_111,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Call Disconnected 1.1.2A",
			&event_download_call_disconnected_data_112a,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Call Disconnected 1.1.2B",
			&event_download_call_disconnected_data_112b,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Call Disconnected 1.1.2C",
			&event_download_call_disconnected_data_112c,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Call Disconnected 1.1.3A",
			&event_download_call_disconnected_data_113a,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Call Disconnected 1.1.3B",
			&event_download_call_disconnected_data_113b,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Call Disconnected 1.1.4A",
			&event_download_call_disconnected_data_114a,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Call Disconnected 1.1.4B",
			&event_download_call_disconnected_data_114b,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: Location Status 1.1.1",
			&event_download_location_status_data_111,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Location Status 1.1.2A",
			&event_download_location_status_data_112a,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Location Status 1.1.2B",
			&event_download_location_status_data_112b,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Location Status 1.2.2",
			&event_download_location_status_data_122,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: User Activity 1.1.1",
			&event_download_user_activity_data_111,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: Idle Screen Available 1.1.1",
			&event_download_idle_screen_available_data_111,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: Card Reader Status 1.1.1A",
			&event_download_card_reader_status_data_111a,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Card Reader Status 1.1.1B",
			&event_download_card_reader_status_data_111b,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Card Reader Status 1.1.1C",
			&event_download_card_reader_status_data_111c,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Card Reader Status 1.1.1D",
			&event_download_card_reader_status_data_111d,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Card Reader Status 1.1.2A",
			&event_download_card_reader_status_data_112a,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Card Reader Status 1.1.2B",
			&event_download_card_reader_status_data_112b,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Card Reader Status 1.1.2C",
			&event_download_card_reader_status_data_112c,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Card Reader Status 1.1.2D",
			&event_download_card_reader_status_data_112d,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Card Reader Status 2.1.2A",
			&event_download_card_reader_status_data_212a,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Card Reader Status 2.1.2B",
			&event_download_card_reader_status_data_212b,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: Language Selection 1.1.1",
			&event_download_language_selection_data_111,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Language Selection 1.2.2",
			&event_download_language_selection_data_122,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: Browser Termination 1.1.1",
			&event_download_browser_termination_data_111,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: Data Available 1.1.1",
			&event_download_data_available_data_111,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Data Available 2.1.1",
			&event_download_data_available_data_211,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: Channel Status 1.3.1",
			&event_download_channel_status_data_131,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Channel Status 2.1.1",
			&event_download_channel_status_data_211,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Channel Status 2.2.1",
			&event_download_channel_status_data_221,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Event: Network Rejection 1.1.1",
			&event_download_network_rejection_data_111,
			test_envelope_encoding);
	g_test_add_data_func("/teststk/Event: Network Rejection 1.2.1",
			&event_download_network_rejection_data_121,
			test_envelope_encoding);

	g_test_add_data_func("/teststk/Timer Expiration 2.1.1",
			&timer_expiration_data_211, test_envelope_encoding);
	g_test_add_data_func("/teststk/Timer Expiration 2.2.1A",
			&timer_expiration_data_221a, test_envelope_encoding);

	g_test_add_data_func("/teststk/HTML Attribute Test 1",
				&html_attr_data_1, test_html_attr);
	g_test_add_data_func("/teststk/HTML Attribute Test 2",
				&html_attr_data_2, test_html_attr);
	g_test_add_data_func("/teststk/HTML Attribute Test 3",
				&html_attr_data_3, test_html_attr);
	g_test_add_data_func("/teststk/HTML Attribute Test 4",
				&html_attr_data_4, test_html_attr);

	g_test_add_data_func("/teststk/IMG to XPM Test 1",
				&xpm_test_1, test_img_to_xpm);
	g_test_add_data_func("/teststk/IMG to XPM Test 2",
				&xpm_test_2, test_img_to_xpm);
	g_test_add_data_func("/teststk/IMG to XPM Test 3",
				&xpm_test_3, test_img_to_xpm);
	g_test_add_data_func("/teststk/IMG to XPM Test 4",
				&xpm_test_4, test_img_to_xpm);
	g_test_add_data_func("/teststk/IMG to XPM Test 5",
				&xpm_test_5, test_img_to_xpm);
	g_test_add_data_func("/teststk/IMG to XPM Test 6",
				&xpm_test_6, test_img_to_xpm);

	return g_test_run();
}
