/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *  Copyright (C) ST-Ericsson SA 2010.
 *  Copyright (C) 2008-2011 Intel Corporation. All rights reserved.
 *  Copyright (C) 2013-2021 Jolla Ltd
 *  Copyright (C) 2014  Canonical Ltd
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

#include <ofono/log.h>
#include <ofono/misc.h>
#include <ofono/modem.h>
#include <ofono/phonebook.h>
#include <ofono/plugin.h>
#include <ofono/sim.h>

#include <glib.h>
#include <errno.h>
#include <string.h>

#define CALLBACK_WITH_FAILURE(cb, args...)		\
	do {						\
		struct ofono_error cb_e;		\
		cb_e.type = OFONO_ERROR_TYPE_FAILURE;	\
		cb_e.error = 0;				\
							\
		cb(&cb_e, ##args);			\
	} while (0)					\

#define CALLBACK_WITH_SUCCESS(f, args...)		\
	do {						\
		struct ofono_error e;			\
		e.type = OFONO_ERROR_TYPE_NO_ERROR;	\
		e.error = 0;				\
		f(&e, ##args);				\
	} while (0)

#define SIM_EFPBR_FILEID 0x4F30
#define SIM_EFADN_FILEID 0x6F3A
#define SIM_EFEXT1_FILEID 0x6F4A

#define UNUSED	0xFF

#define EXT1_CP_SUBADDRESS	1
#define EXT1_ADDITIONAL_DATA	2

/* TON (Type Of Number) See TS 24.008 */
#define TON_MASK		0x70
#define TON_INTERNATIONAL	0x10

enum constructed_tag {
	TYPE_1_TAG = 0xA8,
	TYPE_2_TAG = 0xA9,
	TYPE_3_TAG = 0xAA
};

enum file_type_tag {
	TYPE_ADN = 0xC0,
	TYPE_IAP = 0xC1,
	TYPE_EXT1 = 0xC2,
	TYPE_SNE = 0xC3,
	TYPE_ANR = 0xC4,
	TYPE_PBC = 0xC5,
	TYPE_GPR = 0xC6,
	TYPE_AAS = 0xC7,
	TYPE_GAS = 0xC8,
	TYPE_UID = 0xC9,
	TYPE_EMAIL = 0xCA,
	TYPE_CCP1 = 0xCB
};

struct pb_file_info {
	enum constructed_tag pbr_type;
	int file_id;
	enum file_type_tag file_type;
	int file_length;
	int record_length;
};

struct record_to_read {
	int file_id;
	enum file_type_tag type_tag;
	int record_length;
	int record;
	int adn_idx;
	gboolean anr_ext;	/* Is it an EXT1 record for ANR? */
	gboolean set_by_iap;	/* Type 2 file? */
};

struct phonebook_entry {
	int entry;
	char *name;
	char *number;
	char *email;
	char *anr;
	char *sne;
};

static unsigned char sim_path[] = { 0x3F, 0x00, 0x7F, 0x10 };
static unsigned char usim_path[] = { 0x3F, 0x00, 0x7F, 0x10, 0x5F, 0x3A };

/*
 * Table for BCD to utf8 conversion. See table 4.4 in TS 31.102.
 * BCD 0x0C indicates pause before sending following digits as DTMF tones.
 * BCD 0x0D is a wildcard that means "any digit". These values are mapped to
 * ',' and '?', following the Android/iPhone convention for the first and Nokia
 * convention for the second (only OEM that I have seen that supports this
 * feature). BCD 0x0E is reserved, we convert it to 'r'.
 */
static const char digit_to_utf8[] = "0123456789*#,?r\0";

/* One of these for each record in EF_PBR */
struct pb_ref_rec {
	GSList *pb_files;	/* File ids to read (pb_file_info nodes) */
	GSList *pb_next;	/* Next file info to read */
	GSList *pending_records;	/* List of record_to_read */
	GSList *next_record;	/* Next record_to_read to process */
	GTree *phonebook;	/* Container of phonebook_entry structures */
};

struct pb_data {
	GSList *pb_refs;
	GSList *pb_ref_next;
	struct ofono_sim *sim;
	struct ofono_sim_context *sim_context;
	const unsigned char *df_path;
	guint register_id;
	size_t df_size;
	ofono_phonebook_cb_t cb;
	void *cb_data;
};

static void read_info_cb(int ok, unsigned char file_status,
				int total_length, int record_length,
				void *userdata);

static gint comp_int(gconstpointer a, gconstpointer b)
{
	int a_val = GPOINTER_TO_INT(a);
	int b_val = GPOINTER_TO_INT(b);

	return a_val - b_val;
}

static const struct pb_file_info *ext1_info(const GSList *pb_files)
{
	const GSList *l;
	for (l = pb_files; l; l = l->next) {
		const struct pb_file_info *f_info = l->data;
		if (f_info->file_type == TYPE_EXT1)
			return f_info;
	}

	return NULL;
}

static struct phonebook_entry *handle_adn(size_t len, const unsigned char *msg,
					struct pb_ref_rec *ref, int adn_idx)
{
	unsigned name_length = len - 14;
	unsigned number_start = name_length;
	unsigned number_length;
	unsigned extension_record = UNUSED;
	unsigned i, prefix;
	char *number = NULL;
	char *name = ofono_sim_string_to_utf8(msg, name_length);
	struct phonebook_entry *new_entry;

	/* Length contains also TON & NPI */
	number_length = msg[number_start];

	if (number_length != UNUSED && number_length != 0) {
		number_length--;
		/* '+' + number + terminator */
		number = g_malloc0(2 * number_length + 2);

		prefix = 0;

		if ((msg[number_start + 1] & TON_MASK) == TON_INTERNATIONAL) {
			number[0] = '+';
			prefix = 1;
		}

		for (i = 0; i < number_length; i++) {
			number[2 * i + prefix] =
				digit_to_utf8[msg[number_start + 2 + i] & 0x0f];
			number[2 * i + 1 + prefix] =
				digit_to_utf8[msg[number_start + 2 + i] >> 4];
		}

		extension_record = msg[len - 1];
	}

	DBG("ADN name %s, number %s ", name, number);
	DBG("number length %d extension_record %d",
		2 * number_length, extension_record);

	if ((name == NULL || *name == '\0') && number == NULL)
		goto end;

	new_entry = g_new0(struct phonebook_entry, 1);
	new_entry->name = name;
	new_entry->number = number;

	DBG("Creating PB entry %d with", adn_idx);
	DBG("name %s and number %s", new_entry->name, new_entry->number);

	g_tree_insert(ref->phonebook, GINT_TO_POINTER(adn_idx), new_entry);

	if (extension_record != UNUSED) {
		const struct pb_file_info *f_info = ext1_info(ref->pb_files);

		if (f_info) {
			struct record_to_read *ext_rec =
				g_new0(struct record_to_read, 1);

			ext_rec->file_id = f_info->file_id;
			ext_rec->type_tag = TYPE_EXT1;
			ext_rec->record_length = f_info->record_length;
			ext_rec->record = extension_record;
			ext_rec->adn_idx = adn_idx;

			ref->pending_records =
				g_slist_prepend(ref->pending_records, ext_rec);
		}
	}

	return new_entry;

end:
	ofono_sim_string_free(name);
	g_free(number);

	return NULL;
}

static void handle_iap(size_t len, const unsigned char *msg,
			struct pb_ref_rec *ref,
			const struct record_to_read *rec_data)
{
	GSList *l;
	size_t i = 0;

	for (l = ref->pb_files; l; l = l->next) {
		struct pb_file_info *f_info = l->data;
		if (f_info->pbr_type == TYPE_2_TAG) {
			if (i >= len) {
				ofono_error("%s: EF_IAP record too small",
						__func__);
				return;
			}
			if (msg[i] != UNUSED) {
				struct record_to_read *new_rec =
					g_new0(struct record_to_read, 1);

				DBG("type 0x%X record %d",
					f_info->file_type, msg[i]);

				new_rec->file_id = f_info->file_id;
				new_rec->type_tag = f_info->file_type;
				new_rec->record_length = f_info->record_length;
				new_rec->record = msg[i];
				new_rec->adn_idx = rec_data->adn_idx;
				new_rec->anr_ext = FALSE;
				new_rec->set_by_iap = TRUE;

				ref->pending_records =
					g_slist_prepend(ref->pending_records,
							new_rec);
			}
			++i;
		}
	}
}

static void handle_sne(size_t len, const unsigned char *msg,
			struct pb_ref_rec *ref,
			const struct record_to_read *rec_data)
{
	char *sne;

	/* There are additional fields for type 2 files */
	if (rec_data->set_by_iap)
		len -= 2;

	sne = ofono_sim_string_to_utf8(msg, len);

	if (sne && *sne != '\0') {
		struct phonebook_entry *entry;

		entry = g_tree_lookup(ref->phonebook,
				GINT_TO_POINTER(rec_data->adn_idx));
		if (entry) {
			/* If one already exists, delete it */
			ofono_sim_string_free(entry->sne);

			DBG("Adding SNE %s to %d", sne, rec_data->adn_idx);
			DBG("name %s", entry->name);

			entry->sne = sne;
			sne = NULL;
		}
	}

	ofono_sim_string_free(sne);
}

static void handle_anr(size_t len,
			const unsigned char *msg,
			struct pb_ref_rec *ref,
			const struct record_to_read *rec_data)
{
	unsigned number_length;
	unsigned extension_record;
	unsigned aas_record;
	unsigned i, prefix;
	char *anr;
	struct phonebook_entry *entry;

	if (len < 15) {
		ofono_error("%s: bad EF_ANR record size", __func__);
		return;
	}

	aas_record = msg[0];
	if (aas_record == UNUSED)
		return;

	DBG("ANR %d", aas_record);

	/* Length contains also TON & NPI */
	number_length = msg[1];
	if (number_length < 2)
		return;

	number_length--;
	/* '+' + number + terminator */
	anr = g_malloc0(2 * number_length + 2);

	prefix = 0;
	if ((msg[2] & TON_MASK) == TON_INTERNATIONAL) {
		anr[0] = '+';
		prefix = 1;
	}

	for (i = 0; i < number_length; i++) {
		anr[2 * i + prefix] = digit_to_utf8[msg[3 + i] & 0x0f];
		anr[2 * i + 1 + prefix] = digit_to_utf8[msg[3 + i] >> 4];
	}

	entry = g_tree_lookup(ref->phonebook,
				GINT_TO_POINTER(rec_data->adn_idx));
	if (entry == NULL) {
		g_free(anr);
		return;
	}

	/* If one already exists, delete it */
	if (entry->anr)
		g_free(entry->anr);

	DBG("Adding ANR %s to %d", anr, rec_data->adn_idx);
	DBG("name %s", entry->name);

	entry->anr = anr;

	extension_record = msg[14];

	DBG("ANR to entry %d number %s number length %d",
		rec_data->adn_idx, anr, number_length);
	DBG("extension_record %d aas %d", extension_record, aas_record);

	if (extension_record != UNUSED) {
		const struct pb_file_info *f_info = ext1_info(ref->pb_files);

		if (f_info) {
			struct record_to_read *ext_rec =
				g_new0(struct record_to_read, 1);

			ext_rec->file_id = f_info->file_id;
			ext_rec->type_tag = TYPE_EXT1;
			ext_rec->record_length = f_info->record_length;
			ext_rec->record = extension_record;
			ext_rec->adn_idx = rec_data->adn_idx;
			ext_rec->anr_ext = TRUE;

			ref->pending_records =
				g_slist_prepend(ref->pending_records, ext_rec);
		}
	}
}

static void handle_email(size_t len, const unsigned char *msg,
			struct pb_ref_rec *ref,
			const struct record_to_read *rec_data)
{
	char *email;

	/* There are additional fields for type 2 files */
	if (rec_data->set_by_iap)
		len -= 2;

	email = ofono_sim_string_to_utf8(msg, len);

	if (email && *email) {
		struct phonebook_entry *entry;

		entry = g_tree_lookup(ref->phonebook,
				GINT_TO_POINTER(rec_data->adn_idx));
		if (entry) {
			/* if one already exists, delete it */
			ofono_sim_string_free(entry->email);

			DBG("Adding email to entry %d", rec_data->adn_idx);
			DBG("name %s", entry->name);

			entry->email = email;
			email = NULL;
		}
	}

	ofono_sim_string_free(email);
}

static void handle_ext1(size_t len, const unsigned char *msg,
			struct pb_ref_rec *ref,
			const struct record_to_read *rec_data)
{
	unsigned number_length, i, next_extension_record;
	struct phonebook_entry *entry;
	char *ext_number;

	if (len < 13) {
		ofono_error("%s: bad EF_EXT1 record size", __func__);
		return;
	}

	/* Check if there is more extension data */
	next_extension_record = msg[12];
	if (next_extension_record != UNUSED) {
		const struct pb_file_info *f_info = ext1_info(ref->pb_files);

		if (f_info) {
			struct record_to_read *ext_rec =
				g_new0(struct record_to_read, 1);

			DBG("next_extension_record %d", next_extension_record);

			ext_rec->file_id = f_info->file_id;
			ext_rec->record_length = f_info->record_length;
			ext_rec->type_tag = TYPE_EXT1;
			ext_rec->record = next_extension_record;
			ext_rec->adn_idx = rec_data->adn_idx;
			ext_rec->anr_ext = rec_data->anr_ext;

			ref->pending_records =
				g_slist_prepend(ref->pending_records, ext_rec);
		}
	}

	if (msg[0] != EXT1_ADDITIONAL_DATA) {
		DBG("EXT1 record with subaddress ignored");
		return;
	}

	number_length = msg[1];
	ext_number = g_malloc0(2 * number_length + 1);

	for (i = 0; i < number_length; i++) {
		ext_number[2 * i] = digit_to_utf8[msg[2 + i] & 0x0f];
		ext_number[2 * i + 1] = digit_to_utf8[msg[2 + i] >> 4];
	}

	DBG("Number extension %s", ext_number);
	DBG("number length %d", number_length);

	DBG("Looking for ADN entry %d", rec_data->adn_idx);
	entry = g_tree_lookup(ref->phonebook,
				GINT_TO_POINTER(rec_data->adn_idx));
	if (entry == NULL) {
		g_free(ext_number);
		return;
	}

	if (rec_data->anr_ext) {
		char *anr = entry->anr;
		entry->anr = g_strconcat(anr, ext_number, NULL);
		g_free(anr);
	} else {
		char *number = entry->number;
		entry->number = g_strconcat(number, ext_number, NULL);
		g_free(number);
	}

	g_free(ext_number);
}

static const char *file_tag_to_string(enum file_type_tag tag)
{
	switch (tag) {
	case TYPE_ADN: return "ADN";
	case TYPE_IAP: return "IAP";
	case TYPE_EXT1: return "EXT1";
	case TYPE_SNE: return "SNE";
	case TYPE_ANR: return "ANR";
	case TYPE_PBC: return "PBC";
	case TYPE_GPR: return "GPR";
	case TYPE_AAS: return "AAS";
	case TYPE_GAS: return "GAS";
	case TYPE_UID: return "UID";
	case TYPE_EMAIL: return "EMAIL";
	case TYPE_CCP1: return "CCP1";
	default: return "<UNKNOWN>";
	}
}

static void decode_read_response(const struct record_to_read *rec_data,
					const unsigned char *msg, size_t len,
					struct pb_ref_rec *ref)
{
	DBG("Decoding %s type record", file_tag_to_string(rec_data->type_tag));
	switch (rec_data->type_tag) {
	case TYPE_IAP:
		handle_iap(len, msg, ref, rec_data);
		break;
	case TYPE_SNE:
		handle_sne(len, msg, ref, rec_data);
		break;
	case TYPE_ANR:
		handle_anr(len, msg, ref, rec_data);
		break;
	case TYPE_EMAIL:
		handle_email(len, msg, ref, rec_data);
		break;
	case TYPE_EXT1:
		handle_ext1(len, msg, ref, rec_data);
		break;
	default:
		DBG("Skipping type");
		break;
	}
}

static gboolean free_entry(gpointer key, gpointer value, gpointer data)
{
	struct phonebook_entry *entry = value;

	ofono_sim_string_free(entry->name);
	ofono_sim_string_free(entry->email);
	ofono_sim_string_free(entry->sne);
	g_free(entry->number);
	g_free(entry->anr);
	g_free(entry);

	return FALSE;
}

static gboolean export_entry(gpointer key, gpointer value, gpointer data)
{
	struct ofono_phonebook *pb = data;
	struct phonebook_entry *entry = value;

	ofono_phonebook_entry(pb, -1,
				entry->number, -1,
				entry->name, -1,
				NULL,
				entry->anr, -1,
				entry->sne,
				entry->email,
				NULL, NULL);

	return free_entry(key, value, NULL);
}

static void free_pb_refs(struct pb_data *pbd, GTraverseFunc entry_func,
						struct ofono_phonebook *pb)
{
	GSList *l;

	for (l = pbd->pb_refs; l != NULL; l = l->next) {
		struct pb_ref_rec *ref = l->data;

		g_tree_foreach(ref->phonebook, entry_func, pb);
		g_tree_destroy(ref->phonebook);
		g_slist_free_full(ref->pending_records, g_free);
		g_slist_free_full(ref->pb_files, g_free);
	}

	g_slist_free_full(pbd->pb_refs, g_free);
	pbd->pb_refs = NULL;
}

static void export_and_return(struct ofono_phonebook *pb, gboolean ok)
{
	struct pb_data *pbd = ofono_phonebook_get_data(pb);

	DBG("phonebook fully read");
	free_pb_refs(pbd, export_entry, pb);

	if (pbd->cb) {
		if (ok) {
			CALLBACK_WITH_SUCCESS(pbd->cb, pbd->cb_data);
		} else {
			CALLBACK_WITH_FAILURE(pbd->cb, pbd->cb_data);
		}
		pbd->cb = NULL;
		pbd->cb_data = NULL;
	}
}

static void read_record_cb(int ok, int total_length, int record,
			const unsigned char *data,
			int record_length, void *userdata)
{
	struct ofono_phonebook *pb = userdata;
	struct pb_data *pbd = ofono_phonebook_get_data(pb);
	struct pb_ref_rec *ref = pbd->pb_ref_next->data;
	struct record_to_read *rec;

	if (!ok) {
		ofono_error("%s: error %d", __func__, ok);
		export_and_return(pb, FALSE);
		return;
	}

	DBG("ok %d; total_length %d; record %d; record_length %d",
		ok, total_length, record, record_length);

	rec = ref->next_record->data;

	/* This call might add elements to pending_records */
	decode_read_response(rec, data, record_length, ref);

	ref->pending_records = g_slist_remove(ref->pending_records, rec);
	g_free(rec);

	if (ref->pending_records) {
		struct record_to_read *rec;

		ref->next_record = ref->pending_records;
		rec = ref->next_record->data;

		ofono_sim_read_record(pbd->sim_context, rec->file_id,
					OFONO_SIM_FILE_STRUCTURE_FIXED,
					rec->record,
					rec->record_length,
					pbd->df_path, pbd->df_size,
					read_record_cb, pb);
	} else {
		/* Read files from next EF_PBR record, if any */

		pbd->pb_ref_next = pbd->pb_ref_next->next;
		if (pbd->pb_ref_next == NULL) {
			export_and_return(pb, TRUE);
		} else {
			struct pb_ref_rec *ref;

			DBG("Next EFpbr record");

			ref = pbd->pb_ref_next->data;

			if (!ref->pb_files) {
				export_and_return(pb, TRUE);
			} else {
				struct pb_file_info *file_info;

				ref->pb_next = ref->pb_files;
				file_info = ref->pb_files->data;

				ofono_sim_read_info(pbd->sim_context,
						file_info->file_id,
						OFONO_SIM_FILE_STRUCTURE_FIXED,
						pbd->df_path, pbd->df_size,
						read_info_cb, pb);
			}
		}
	}
}

static void pb_adn_cb(int ok, int total_length, int record,
			const unsigned char *data,
			int record_length, void *userdata)
{
	struct ofono_phonebook *pb = userdata;
	struct pb_data *pbd = ofono_phonebook_get_data(pb);
	struct pb_ref_rec *ref = pbd->pb_ref_next->data;
	GSList *l;

	if (!ok) {
		ofono_error("%s: error %d", __func__, ok);
		export_and_return(pb, FALSE);
		return;
	}

	DBG("ok %d; total_length %d; record %d; record_length %d",
		ok, total_length, record, record_length);

	if (handle_adn(record_length, data, ref, record) != NULL) {
		/* Add type 1 records */
		for (l = ref->pb_files; l; l = l->next) {
			const struct pb_file_info *f_info = l->data;

			if (f_info->pbr_type == TYPE_1_TAG &&
					f_info->file_type != TYPE_ADN) {
				struct record_to_read *ext_rec =
					g_new0(struct record_to_read, 1);

				ext_rec->file_id = f_info->file_id;
				ext_rec->type_tag = f_info->file_type;
				ext_rec->record_length = f_info->record_length;
				ext_rec->record = record;
				ext_rec->adn_idx = record;

				ref->pending_records =
					g_slist_prepend(ref->pending_records,
							ext_rec);
			}
		}
	}

	if (record*record_length >= total_length) {
		DBG("All ADN records read: reading additional files");

		if (ref->pending_records) {
			struct record_to_read *rec;

			ref->next_record = ref->pending_records;
			rec = ref->next_record->data;

			ofono_sim_read_record(pbd->sim_context, rec->file_id,
						OFONO_SIM_FILE_STRUCTURE_FIXED,
						rec->record,
						rec->record_length,
						pbd->df_path, pbd->df_size,
						read_record_cb, pb);
		} else {
			export_and_return(pb, TRUE);
		}
	}
}

static void read_info_cb(int ok, unsigned char file_status,
				int total_length, int record_length,
				void *userdata)
{
	struct ofono_phonebook *pb = userdata;
	struct pb_data *pbd = ofono_phonebook_get_data(pb);
	struct pb_file_info *file_info;
	struct pb_ref_rec *ref = pbd->pb_ref_next->data;

	file_info = ref->pb_next->data;
	ref->pb_next = ref->pb_next->next;

	if (ok) {
		file_info->record_length = record_length;
		file_info->file_length = total_length;

		DBG("file id %x record length %d total_length %d",
			file_info->file_id, record_length, total_length);
	} else {
		ofono_warn("%s: %x not found", __func__, file_info->file_id);
		ref->pb_files = g_slist_remove(ref->pb_files, file_info);
		g_free(file_info);
	}

	if (ref->pb_next == NULL) {
		if (ref->pb_files == NULL) {
			ofono_warn("%s: no phonebook on SIM", __func__);
			export_and_return(pb, FALSE);
			return;
		}

		/* Read full contents of the master file */
		file_info = ref->pb_files->data;

		ofono_sim_read_path(pbd->sim_context, file_info->file_id,
					OFONO_SIM_FILE_STRUCTURE_FIXED,
					pbd->df_path, pbd->df_size,
					pb_adn_cb, pb);
	} else {
		file_info = ref->pb_next->data;

		ofono_sim_read_info(pbd->sim_context, file_info->file_id,
					OFONO_SIM_FILE_STRUCTURE_FIXED,
					pbd->df_path, pbd->df_size,
					read_info_cb, pb);
	}
}

static void start_sim_app_read(struct ofono_phonebook *pb)
{
	struct pb_data *pbd = ofono_phonebook_get_data(pb);
	struct pb_ref_rec *ref_rec;
	struct pb_file_info *f_info;
	struct pb_file_info *f_ext1;

	pbd->df_path = sim_path;
	pbd->df_size = sizeof(sim_path);

	ref_rec = g_new0(struct pb_ref_rec, 1);
	ref_rec->phonebook = g_tree_new(comp_int);

	/* Only EF_ADN and EF_EXT1 read for SIM */

	f_info = g_new0(struct pb_file_info, 1);
	f_info->file_id = SIM_EFADN_FILEID;
	f_info->pbr_type = TYPE_1_TAG;
	f_info->file_type = TYPE_ADN;
	ref_rec->pb_files = g_slist_append(ref_rec->pb_files, f_info);

	f_ext1 = g_new0(struct pb_file_info, 1);
	f_ext1->file_id = SIM_EFEXT1_FILEID;
	f_ext1->pbr_type = TYPE_3_TAG;
	f_ext1->file_type = TYPE_EXT1;
	ref_rec->pb_files = g_slist_append(ref_rec->pb_files, f_ext1);

	pbd->pb_refs = g_slist_append(pbd->pb_refs, ref_rec);
	pbd->pb_ref_next = pbd->pb_refs;

	ref_rec->pb_next = ref_rec->pb_files;

	/* Start reading process for MF */
	ofono_sim_read_info(pbd->sim_context, f_info->file_id,
				OFONO_SIM_FILE_STRUCTURE_FIXED,
				pbd->df_path, pbd->df_size,
				read_info_cb, pb);
}

static void pb_reference_data_cb(int ok, int total_length, int record,
					const unsigned char *sdata,
					int record_length, void *userdata)
{
	struct ofono_phonebook *pb = userdata;
	struct pb_data *pbd = ofono_phonebook_get_data(pb);
	const unsigned char *ptr = sdata;
	gboolean finished = FALSE;
	struct pb_ref_rec *ref_rec;

	DBG("total_length %d record %d record_length %d",
		total_length, record, record_length);

	if (!ok) {
		/* We migh have a SIM instead of USIM application: try that */
		DBG("%s: error %d, trying SIM files", __func__, ok);
		start_sim_app_read(pb);
		return;
	}

	ref_rec = g_new0(struct pb_ref_rec, 1);
	ref_rec->phonebook = g_tree_new(comp_int);

	while (ptr < sdata + record_length && finished == FALSE) {
		int typelen, file_id, i;
		enum constructed_tag pbr_type = *ptr;

		switch (pbr_type) {
		case TYPE_1_TAG:
		case TYPE_2_TAG:
		case TYPE_3_TAG:
			typelen = *(ptr + 1);
			DBG("File type=%02X, len=%d", *ptr, typelen);
			ptr += 2;
			i = 0;

			while (i < typelen) {
				struct pb_file_info *file_info =
					g_new0(struct pb_file_info, 1);

				file_id = (ptr[i + 2] << 8) + ptr[i + 3];

				DBG("creating file info for File type=%02X",
					ptr[i]);
				DBG("File ID=%04X", file_id);

				file_info->pbr_type = pbr_type;
				file_info->file_type = ptr[i];
				file_info->file_id = file_id;
				/* Keep order, important for type 2 files */
				ref_rec->pb_files =
					g_slist_append(ref_rec->pb_files,
							file_info);
				i += ptr[i + 1] + 2;
			}

			ptr += typelen;
			break;
		default:
			DBG("All handled %02x", *ptr);
			finished = TRUE;
			break;
		}
	}

	pbd->pb_refs = g_slist_append(pbd->pb_refs, ref_rec);

	if (record*record_length >= total_length) {
		struct pb_ref_rec *ref;
		struct pb_file_info *file_info;

		DBG("All EFpbr records read");

		pbd->pb_ref_next = pbd->pb_refs;
		ref = pbd->pb_ref_next->data;

		if (ref->pb_files == NULL) {
			ofono_error("%s: no files to read", __func__);
			export_and_return(pb, FALSE);
			return;
		}

		ref->pb_next = ref->pb_files;
		file_info = ref->pb_files->data;

		/* Start reading process for first EF_PBR entry */

		ofono_sim_read_info(pbd->sim_context, file_info->file_id,
					OFONO_SIM_FILE_STRUCTURE_FIXED,
					pbd->df_path, pbd->df_size,
					read_info_cb, pb);
	}
}

static void export_entries(struct ofono_phonebook *pb,
				const char *storage,
				ofono_phonebook_cb_t cb, void *data)
{
	struct pb_data *pbd = ofono_phonebook_get_data(pb);

	DBG("Storage %s", storage);

	/* Only for SIM memory */
	if (strcmp(storage, "SM") != 0) {
		CALLBACK_WITH_FAILURE(cb, data);
		return;
	}

	pbd->cb = cb;
	pbd->cb_data = data;

	/* Assume USIM, change in case EF_PBR is not present */
	pbd->df_path = usim_path;
	pbd->df_size = sizeof(usim_path);

	ofono_sim_read_path(pbd->sim_context, SIM_EFPBR_FILEID,
			OFONO_SIM_FILE_STRUCTURE_FIXED,
			pbd->df_path, pbd->df_size,
			pb_reference_data_cb, pb);
}

static gboolean delayed_register(gpointer user_data)
{
	struct ofono_phonebook *pb = user_data;
	struct pb_data *pbd = ofono_phonebook_get_data(pb);

	pbd->register_id = 0;
	ofono_phonebook_register(pb);
	return FALSE;
}

static int phonebook_probe(struct ofono_phonebook *pb,
				unsigned int vendor, void *user)
{
	struct ofono_modem *modem = user;
	struct pb_data *pd = g_new0(struct pb_data, 1);

	pd->sim = ofono_modem_get_sim(modem);
	if (pd->sim == NULL)
		return -ENOENT;

	pd->sim_context = ofono_sim_context_create(pd->sim);
	if (pd->sim_context == NULL)
		return -ENOENT;

	ofono_phonebook_set_data(pb, pd);

	pd->register_id = g_idle_add(delayed_register, pb);

	return 0;
}

static void phonebook_remove(struct ofono_phonebook *pb)
{
	struct pb_data *pbd = ofono_phonebook_get_data(pb);

	if (pbd->register_id) {
		g_source_remove(pbd->register_id);
	}

	ofono_phonebook_set_data(pb, NULL);
	ofono_sim_context_free(pbd->sim_context);

	free_pb_refs(pbd, free_entry, NULL);
	g_free(pbd);
}

static const struct ofono_phonebook_driver phonebook_driver = {
	.name		= "generic",
	.probe		= phonebook_probe,
	.remove		= phonebook_remove,
	.export_entries	= export_entries
};

static int phonebook_init(void)
{
	return ofono_phonebook_driver_register(&phonebook_driver);
}

static void phonebook_exit(void)
{
	ofono_phonebook_driver_unregister(&phonebook_driver);
}

OFONO_PLUGIN_DEFINE(generic_phonebook, "Generic Phonebook Plugin",
	OFONO_VERSION, OFONO_PLUGIN_PRIORITY_DEFAULT,
	phonebook_init, phonebook_exit)

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
