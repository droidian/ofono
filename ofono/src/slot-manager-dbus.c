/*
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2015-2021 Jolla Ltd.
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

#include "slot-manager-dbus.h"

#include <ofono/log.h>
#include <ofono/dbus.h>

#include <gutil_macros.h>
#include <gutil_strv.h>
#include <gutil_log.h>

#include <gdbus.h>

#include "ofono.h"

typedef void (*slot_manager_dbus_append_fn)(DBusMessageIter *it,
				struct slot_manager_dbus *dbus);
typedef gboolean (*slot_manager_dbus_slot_select_fn)
				(const struct ofono_slot *slot);
typedef const char *(*slot_manager_dbus_slot_string_fn)
				(const struct ofono_slot *slot);

struct slot_manager_dbus_request {
	DBusMessage *msg;
	slot_manager_dbus_append_fn fn;
	enum slot_manager_dbus_block block;
};

struct slot_manager_dbus {
	struct ofono_slot_manager *manager;
	const struct slot_manager_dbus_cb *cb;
	DBusConnection *conn;
	enum slot_manager_dbus_block block_mask;
	GSList *blocked_req;
	guint mms_watch;
};

#define SM_DBUS_PATH               "/"
#define SM_DBUS_INTERFACE          "org.nemomobile.ofono.ModemManager"
#define SM_DBUS_INTERFACE_VERSION  (8)

#define SM_DBUS_SIGNAL_ENABLED_MODEMS_CHANGED      "EnabledModemsChanged"
#define SM_DBUS_SIGNAL_PRESENT_SIMS_CHANGED        "PresentSimsChanged"
#define SM_DBUS_SIGNAL_DEFAULT_VOICE_SIM_CHANGED   "DefaultVoiceSimChanged"
#define SM_DBUS_SIGNAL_DEFAULT_DATA_SIM_CHANGED    "DefaultDataSimChanged"
#define SM_DBUS_SIGNAL_DEFAULT_VOICE_MODEM_CHANGED "DefaultVoiceModemChanged"
#define SM_DBUS_SIGNAL_DEFAULT_DATA_MODEM_CHANGED  "DefaultDataModemChanged"
#define SM_DBUS_SIGNAL_MMS_SIM_CHANGED             "MmsSimChanged"
#define SM_DBUS_SIGNAL_MMS_MODEM_CHANGED           "MmsModemChanged"
#define SM_DBUS_SIGNAL_READY_CHANGED               "ReadyChanged"
#define SM_DBUS_SIGNAL_MODEM_ERROR                 "ModemError"
#define SM_DBUS_IMSI_AUTO                          "auto"

#define SM_DBUS_ERROR_SIGNATURE                    "si"

static gboolean slot_manager_dbus_enabled(const struct ofono_slot *s)
{
	return s->enabled;
}

static gboolean slot_manager_dbus_present(const struct ofono_slot *s)
{
	return s->sim_presence == OFONO_SLOT_SIM_PRESENT;
}

static const char *slot_manager_dbus_imei(const struct ofono_slot *s)
{
	return s->imei;
}

static const char *slot_manager_dbus_imeisv(const struct ofono_slot *s)
{
	return s->imeisv;
}

static void slot_manager_dbus_append_path_array(DBusMessageIter *it,
	struct slot_manager_dbus *dbus, slot_manager_dbus_slot_select_fn fn)
{
	DBusMessageIter array;
	const ofono_slot_ptr *ptr = dbus->manager->slots;

	dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY,
				DBUS_TYPE_OBJECT_PATH_AS_STRING, &array);

	if (ptr) {
		while (*ptr) {
			const struct ofono_slot *slot = *ptr++;

			if (!fn || fn(slot)) {
				const char *path = slot->path;
				dbus_message_iter_append_basic(&array,
					DBUS_TYPE_OBJECT_PATH, &path);
			}
		}
	}

	dbus_message_iter_close_container(it, &array);
}

static void slot_manager_dbus_append_string_array(DBusMessageIter *it,
	struct slot_manager_dbus *dbus, slot_manager_dbus_slot_string_fn fn)
{
	DBusMessageIter array;
	const ofono_slot_ptr *ptr = dbus->manager->slots;

	dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY,
		DBUS_TYPE_STRING_AS_STRING, &array);

	if (ptr) {
		while (*ptr) {
			const struct ofono_slot *slot = *ptr++;
			const char *str = fn(slot);

			if (!str) str = "";
			dbus_message_iter_append_basic(&array,
				DBUS_TYPE_STRING, &str);
		}
	}

	dbus_message_iter_close_container(it, &array);
}

static void slot_manager_dbus_append_boolean_array(DBusMessageIter *it,
	struct slot_manager_dbus *dbus, slot_manager_dbus_slot_select_fn value)
{
	DBusMessageIter array;
	const ofono_slot_ptr *ptr = dbus->manager->slots;

	dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY,
		DBUS_TYPE_BOOLEAN_AS_STRING, &array);

	if (ptr) {
		while (*ptr) {
			const struct ofono_slot *slot = *ptr++;
			dbus_bool_t b = value(slot);

			dbus_message_iter_append_basic(&array,
				DBUS_TYPE_BOOLEAN, &b);
		}
	}

	dbus_message_iter_close_container(it, &array);
}

static void slot_manager_dbus_append_boolean(DBusMessageIter *it, dbus_bool_t b)
{
	dbus_message_iter_append_basic(it, DBUS_TYPE_BOOLEAN, &b);
}

static void slot_manager_dbus_append_string(DBusMessageIter *it, const char *s)
{
	if (!s) s = "";
	dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &s);
}

static void slot_manager_dbus_append_imsi(DBusMessageIter *it, const char *imsi)
{
	if (!imsi) imsi = SM_DBUS_IMSI_AUTO;
	dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &imsi);
}

static void slot_manager_dbus_append_path(DBusMessageIter *it, const char *path)
{
	if (!path) path = "";
	/* It's DBUS_TYPE_STRING since DBUS_TYPE_OBJECT_PATH can't be empty */
	dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &path);
}

static void slot_manager_dbus_message_append_path_array(DBusMessage *msg,
	struct slot_manager_dbus *dbus, slot_manager_dbus_slot_select_fn fn)
{
	DBusMessageIter iter;

	dbus_message_iter_init_append(msg, &iter);
	slot_manager_dbus_append_path_array(&iter, dbus, fn);
}

static void slot_manager_dbus_append_error_count(DBusMessageIter *it,
	const char *id, dbus_uint32_t count)
{
	DBusMessageIter sub;

	dbus_message_iter_open_container(it, DBUS_TYPE_STRUCT, NULL, &sub);
	dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &id);
	dbus_message_iter_append_basic(&sub, DBUS_TYPE_INT32, &count);
	dbus_message_iter_close_container(it, &sub);
}

static void slot_manager_dbus_append_error_counts(DBusMessageIter *it,
	GHashTable *errors)
{
	DBusMessageIter counts;

	dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY,
				"(" SM_DBUS_ERROR_SIGNATURE ")", &counts);

	if (errors && g_hash_table_size(errors)) {
		gpointer key, value;
		GHashTableIter iter;

		g_hash_table_iter_init(&iter, errors);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			slot_manager_dbus_append_error_count(&counts,
				key, GPOINTER_TO_INT(value));
		}
	}

	dbus_message_iter_close_container(it, &counts);
}

static void slot_manager_dbus_append_modem_errors(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	DBusMessageIter slots;
	const ofono_slot_ptr *ptr = dbus->manager->slots;

	dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY,
		"a(" SM_DBUS_ERROR_SIGNATURE ")", &slots);

	if (ptr) {
		while (*ptr) {
			const struct ofono_slot *slot = *ptr++;

			slot_manager_dbus_append_error_counts(&slots,
				dbus->cb->get_slot_errors(slot));
		}
	}

	dbus_message_iter_close_container(it, &slots);
}

static void slot_manager_dbus_append_errors(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_error_counts(it,
		dbus->cb->get_errors(dbus->manager));
}

static void slot_manager_dbus_signal_path_array(struct slot_manager_dbus *dbus,
	const char *name, slot_manager_dbus_slot_select_fn fn)
{
	DBusMessage *signal = dbus_message_new_signal(SM_DBUS_PATH,
		SM_DBUS_INTERFACE, name);

	slot_manager_dbus_message_append_path_array(signal, dbus, fn);
	g_dbus_send_message(dbus->conn, signal);
}

static inline void slot_manager_dbus_signal_imsi(struct slot_manager_dbus *dbus,
	const char *name, const char *imsi)
{
	if (!imsi) imsi = SM_DBUS_IMSI_AUTO;
	DBG("%s %s", name, imsi);
	g_dbus_emit_signal(dbus->conn, SM_DBUS_PATH, SM_DBUS_INTERFACE,
		name, DBUS_TYPE_STRING, &imsi, DBUS_TYPE_INVALID);
}

static inline void slot_manager_dbus_signal_string
	(struct slot_manager_dbus *dbus, const char *name, const char *str)
{
	if (!str) str = "";
	DBG("%s %s", name, str);
	g_dbus_emit_signal(dbus->conn, SM_DBUS_PATH, SM_DBUS_INTERFACE,
		name, DBUS_TYPE_STRING, &str, DBUS_TYPE_INVALID);
}

static inline void slot_manager_dbus_signal_boolean
	(struct slot_manager_dbus *dbus, const char *name, dbus_bool_t value)
{
	DBG("%s %d", name, value);
	g_dbus_emit_signal(dbus->conn, SM_DBUS_PATH, SM_DBUS_INTERFACE,
		name, DBUS_TYPE_BOOLEAN, &value, DBUS_TYPE_INVALID);
}

void slot_manager_dbus_signal(struct slot_manager_dbus *dbus,
	enum slot_manager_dbus_signal mask)
{
	if (dbus) {
		const struct ofono_slot_manager *manager = dbus->manager;

		if (mask & SLOT_MANAGER_DBUS_SIGNAL_VOICE_IMSI) {
			slot_manager_dbus_signal_imsi(dbus,
				SM_DBUS_SIGNAL_DEFAULT_VOICE_SIM_CHANGED,
				manager->default_voice_imsi);
		}
		if (mask & SLOT_MANAGER_DBUS_SIGNAL_DATA_IMSI) {
			slot_manager_dbus_signal_imsi(dbus,
				SM_DBUS_SIGNAL_DEFAULT_DATA_SIM_CHANGED,
				manager->default_data_imsi);
		}
		if (mask & SLOT_MANAGER_DBUS_SIGNAL_MMS_IMSI) {
			slot_manager_dbus_signal_string(dbus,
				SM_DBUS_SIGNAL_MMS_SIM_CHANGED,
				manager->mms_imsi);
		}
		if (mask & SLOT_MANAGER_DBUS_SIGNAL_ENABLED_SLOTS) {
			slot_manager_dbus_signal_path_array(dbus,
				SM_DBUS_SIGNAL_ENABLED_MODEMS_CHANGED,
				slot_manager_dbus_enabled);
		}
		if (mask & SLOT_MANAGER_DBUS_SIGNAL_VOICE_PATH) {
			slot_manager_dbus_signal_string(dbus,
				SM_DBUS_SIGNAL_DEFAULT_VOICE_MODEM_CHANGED,
				manager->default_voice_path);
		}
		if (mask & SLOT_MANAGER_DBUS_SIGNAL_DATA_PATH) {
			slot_manager_dbus_signal_string(dbus,
				SM_DBUS_SIGNAL_DEFAULT_DATA_MODEM_CHANGED,
				manager->default_data_path);
		}
		if (mask & SLOT_MANAGER_DBUS_SIGNAL_MMS_PATH) {
			slot_manager_dbus_signal_string(dbus,
				SM_DBUS_SIGNAL_MMS_MODEM_CHANGED,
				manager->mms_path);
		}
		if (mask & SLOT_MANAGER_DBUS_SIGNAL_READY) {
			slot_manager_dbus_signal_boolean(dbus,
				SM_DBUS_SIGNAL_READY_CHANGED,
				manager->ready);
		}
	}
}

void slot_manager_dbus_signal_sim(struct slot_manager_dbus *dbus,
	int index, enum slot_manager_dbus_slot_signal mask)
{
	if (dbus) {
		const struct ofono_slot *slot = dbus->manager->slots[index];

		if (mask & SLOT_MANAGER_DBUS_SLOT_SIGNAL_PRESENT) {
			dbus_bool_t value = slot_manager_dbus_present(slot);

			g_dbus_emit_signal(dbus->conn,
				SM_DBUS_PATH, SM_DBUS_INTERFACE,
				SM_DBUS_SIGNAL_PRESENT_SIMS_CHANGED,
				DBUS_TYPE_INT32, &index,
				DBUS_TYPE_BOOLEAN, &value,
				DBUS_TYPE_INVALID);
		}
	}
}

void slot_manager_dbus_emit_modem_error(struct slot_manager_dbus *dbus,
	const char *path, const char *id, const char *message)
{
	if (!message) message = "";
	g_dbus_emit_signal(dbus->conn, SM_DBUS_PATH, SM_DBUS_INTERFACE,
			SM_DBUS_SIGNAL_MODEM_ERROR,
			DBUS_TYPE_OBJECT_PATH, &path,
			DBUS_TYPE_STRING, &id,
			DBUS_TYPE_STRING, &message,
			DBUS_TYPE_INVALID);
}

void slot_manager_dbus_signal_modem_error(struct slot_manager_dbus *dbus,
	int index, const char *id, const char *msg)
{
	slot_manager_dbus_emit_modem_error(dbus,
			dbus->manager->slots[index]->path, id, msg);
}

void slot_manager_dbus_signal_error(struct slot_manager_dbus *dbus,
	const char *id, const char *message)
{
	slot_manager_dbus_emit_modem_error(dbus, "/", id, message);
}

static DBusMessage *slot_manager_dbus_reply(DBusMessage *msg,
	struct slot_manager_dbus *dbus,
	slot_manager_dbus_append_fn append)
{
	DBusMessage *reply = dbus_message_new_method_return(msg);
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	append(&iter, dbus);
	return reply;
}

static const char* slot_manager_dbus_block_name
				(enum slot_manager_dbus_block block)
{
	return (block == SLOT_MANAGER_DBUS_BLOCK_IMEI) ? "IMEI" :
		(block == SLOT_MANAGER_DBUS_BLOCK_MODEM) ? "MODEM" :
		(block == SLOT_MANAGER_DBUS_BLOCK_ALL) ? "ALL" :
		"???";
}

static void slot_manager_dbus_cancel_request(gpointer data)
{
	struct slot_manager_dbus_request *req = data;

	DBG("cancelling %s request %p",
		slot_manager_dbus_block_name(req->block), req);
	__ofono_dbus_pending_reply(&req->msg,
		ofono_dbus_error_canceled(req->msg));
	g_slice_free(struct slot_manager_dbus_request, req);
}

void slot_manager_dbus_set_block(struct slot_manager_dbus *dbus,
	enum slot_manager_dbus_block mask)
{
	enum slot_manager_dbus_block block = mask & ~dbus->block_mask;
	enum slot_manager_dbus_block unblock = dbus->block_mask & ~mask;

	dbus->block_mask = mask;
	if (block) {
		DBG("blocking %s requests",
			slot_manager_dbus_block_name(block));
	}
	if (unblock) {
		GSList *link = dbus->blocked_req, *prev = NULL;

		DBG("unblocking %s requests",
			slot_manager_dbus_block_name(unblock));
		while (link) {
			struct slot_manager_dbus_request *req = link->data;
			GSList *next = link->next;

			if (req->block & dbus->block_mask) {
				prev = link;
			} else {
				if (prev) {
					prev->next = next;
				} else {
					dbus->blocked_req = next;
				}
				link->next = NULL;
				__ofono_dbus_pending_reply(&req->msg,
					slot_manager_dbus_reply(req->msg,
							dbus, req->fn));
				gutil_slice_free(req);
				g_slist_free1(link);
			}
			link = next;
		}
	}
}

static DBusMessage *slot_manager_dbus_reply_or_block(DBusMessage *msg,
	struct slot_manager_dbus *dbus, slot_manager_dbus_append_fn fn,
	enum slot_manager_dbus_block block)
{
	if (dbus->block_mask & block) {
		struct slot_manager_dbus_request *req =
			g_slice_new(struct slot_manager_dbus_request);

		DBG("blocking %s request %s %p",
				slot_manager_dbus_block_name(block),
				dbus_message_get_member(msg), req);
		req->msg = dbus_message_ref(msg);
		req->fn = fn;
		req->block = block;
		dbus->blocked_req = g_slist_append(dbus->blocked_req, req);
		return NULL;
	} else {
		return slot_manager_dbus_reply(msg, dbus, fn);
	}
}

static DBusMessage *slot_manager_dbus_modem_reply(DBusMessage *msg,
	struct slot_manager_dbus *dbus, slot_manager_dbus_append_fn fn)
{
	return slot_manager_dbus_reply_or_block(msg, dbus, fn,
		SLOT_MANAGER_DBUS_BLOCK_MODEM);
}

static DBusMessage *slot_manager_dbus_imei_reply(DBusMessage *msg,
	struct slot_manager_dbus *dbus, slot_manager_dbus_append_fn fn)
{
	return slot_manager_dbus_reply_or_block(msg, dbus, fn,
		SLOT_MANAGER_DBUS_BLOCK_IMEI);
}

static void slot_manager_dbus_append_version(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	dbus_int32_t version = SM_DBUS_INTERFACE_VERSION;

	dbus_message_iter_append_basic(it, DBUS_TYPE_INT32, &version);
}

static void slot_manager_dbus_append_available_modems(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_path_array(it, dbus, NULL);
}

static void slot_manager_dbus_append_enabled_modems(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_path_array(it, dbus,
		slot_manager_dbus_enabled);
}

static void slot_manager_dbus_append_present_sims(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_boolean_array(it, dbus,
		slot_manager_dbus_present);
}

static void slot_manager_dbus_append_imei_array(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_string_array(it, dbus,
		slot_manager_dbus_imei);
}

static void slot_manager_dbus_append_imeisv_array(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_string_array(it, dbus,
		slot_manager_dbus_imeisv);
}

static void slot_manager_dbus_append_all(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	struct ofono_slot_manager *manager = dbus->manager;

	slot_manager_dbus_append_version(it, dbus);
	slot_manager_dbus_append_available_modems(it, dbus);
	slot_manager_dbus_append_enabled_modems(it, dbus);
	slot_manager_dbus_append_imsi(it, manager->default_data_imsi);
	slot_manager_dbus_append_imsi(it, manager->default_voice_imsi);
	slot_manager_dbus_append_path(it, manager->default_data_path);
	slot_manager_dbus_append_path(it, manager->default_voice_path);
}

static void slot_manager_dbus_append_all2(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_all(it, dbus);
	slot_manager_dbus_append_present_sims(it, dbus);
}

static void slot_manager_dbus_append_all3(DBusMessageIter *it,
					struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_all2(it, dbus);
	slot_manager_dbus_append_imei_array(it, dbus);
}

static void slot_manager_dbus_append_all4(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	struct ofono_slot_manager *manager = dbus->manager;

	slot_manager_dbus_append_all3(it, dbus);
	slot_manager_dbus_append_string(it, manager->mms_imsi);
	slot_manager_dbus_append_path(it, manager->mms_path);
}

static void slot_manager_dbus_append_all5(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_all4(it, dbus);
	slot_manager_dbus_append_boolean(it, dbus->manager->ready);
}

static void slot_manager_dbus_append_all6(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_all5(it, dbus);
	slot_manager_dbus_append_modem_errors(it, dbus);
}

static void slot_manager_dbus_append_all7(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_all6(it, dbus);
	slot_manager_dbus_append_imeisv_array(it, dbus);
}

static void slot_manager_dbus_append_all8(DBusMessageIter *it,
	struct slot_manager_dbus *dbus)
{
	slot_manager_dbus_append_all7(it, dbus);
	slot_manager_dbus_append_errors(it, dbus);
}

static DBusMessage *slot_manager_dbus_get_all(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_modem_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_all);
}

static DBusMessage *slot_manager_dbus_get_all2(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_modem_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_all2);
}

static DBusMessage *slot_manager_dbus_get_all3(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_imei_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_all3);
}

static DBusMessage *slot_manager_dbus_get_all4(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_imei_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_all4);
}

static DBusMessage *slot_manager_dbus_get_all5(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_imei_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_all5);
}

static DBusMessage *slot_manager_dbus_get_all6(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_imei_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_all6);
}

static DBusMessage *slot_manager_dbus_get_all7(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_imei_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_all7);
}

static DBusMessage *slot_manager_dbus_get_all8(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_imei_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_all8);
}

static DBusMessage *slot_manager_dbus_get_interface_version
	(DBusConnection *conn, DBusMessage *msg, void *data)
{
	return slot_manager_dbus_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_version);
}

static DBusMessage *slot_manager_dbus_get_available_modems
	(DBusConnection *conn, DBusMessage *msg, void *data)
{
	return slot_manager_dbus_modem_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_available_modems);
}

static DBusMessage *slot_manager_dbus_get_enabled_modems
	(DBusConnection *conn, DBusMessage *msg, void *data)
{
	return slot_manager_dbus_modem_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_enabled_modems);
}

static DBusMessage *slot_manager_dbus_get_present_sims(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_imei_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_present_sims);
}

static DBusMessage *slot_manager_dbus_get_imei(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_imei_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_imei_array);
}

static DBusMessage *slot_manager_dbus_get_imeisv(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_imei_reply(msg, (struct slot_manager_dbus *)
		data, slot_manager_dbus_append_imeisv_array);
}

static DBusMessage *slot_manager_dbus_reply_with_string(DBusMessage *msg,
	const char *str)
{
	DBusMessage *reply = dbus_message_new_method_return(msg);
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	slot_manager_dbus_append_string(&iter, str);
	return reply;
}

static DBusMessage *slot_manager_dbus_reply_with_imsi(DBusMessage *msg,
							const char *imsi)
{
	DBusMessage *reply = dbus_message_new_method_return(msg);
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	slot_manager_dbus_append_imsi(&iter, imsi);
	return reply;
}

static DBusMessage *slot_manager_dbus_get_default_data_sim
	(DBusConnection *conn, DBusMessage *msg, void *data)
{
	struct slot_manager_dbus *dbus = data;

	return slot_manager_dbus_reply_with_imsi(msg,
		dbus->manager->default_data_imsi);
}

static DBusMessage *slot_manager_dbus_get_default_voice_sim
	(DBusConnection *conn, DBusMessage *msg, void *data)
{
	struct slot_manager_dbus *dbus = data;

	return slot_manager_dbus_reply_with_imsi(msg,
		dbus->manager->default_voice_imsi);
}

static DBusMessage *slot_manager_dbus_get_mms_sim(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	struct slot_manager_dbus *dbus = data;

	return slot_manager_dbus_reply_with_string(msg,
		dbus->manager->mms_imsi);
}

static DBusMessage *slot_manager_dbus_reply_with_path(DBusMessage *msg,
	const char *path)
{
	DBusMessage *reply = dbus_message_new_method_return(msg);
	DBusMessageIter iter;

	dbus_message_iter_init_append(reply, &iter);
	slot_manager_dbus_append_path(&iter, path);
	return reply;
}

static DBusMessage *slot_manager_dbus_get_default_data_modem
	(DBusConnection *conn, DBusMessage *msg, void *data)
{
	struct slot_manager_dbus *dbus = data;

	return slot_manager_dbus_reply_with_path(msg,
		dbus->manager->default_data_path);
}

static DBusMessage *slot_manager_dbus_get_default_voice_modem
	(DBusConnection *conn, DBusMessage *msg, void *data)
{
	struct slot_manager_dbus *dbus = data;

	return slot_manager_dbus_reply_with_path(msg,
		dbus->manager->default_voice_path);
}

static DBusMessage *slot_manager_dbus_get_mms_modem(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	struct slot_manager_dbus *dbus = data;

	return slot_manager_dbus_reply_with_path(msg, dbus->manager->mms_path);
}

static DBusMessage *slot_manager_dbus_get_ready(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	struct slot_manager_dbus *dbus = data;
	DBusMessage *reply = dbus_message_new_method_return(msg);
	DBusMessageIter it;

	dbus_message_iter_init_append(reply, &it);
	slot_manager_dbus_append_boolean(&it, dbus->manager->ready);
	return reply;
}

static DBusMessage *slot_manager_dbus_get_modem_errors(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_reply(msg, (struct slot_manager_dbus *) data,
		slot_manager_dbus_append_modem_errors);
}

static DBusMessage *slot_manager_dbus_get_errors(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	return slot_manager_dbus_reply(msg, (struct slot_manager_dbus *)data,
		slot_manager_dbus_append_errors);
}

static DBusMessage *slot_manager_dbus_set_enabled_modems(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	struct slot_manager_dbus *dbus = data;
	DBusMessageIter iter;

	dbus_message_iter_init(msg, &iter);
	if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
		char **paths = NULL;
		DBusMessageIter array;

		dbus_message_iter_recurse(&iter, &array);
		while (dbus_message_iter_get_arg_type(&array) ==
						DBUS_TYPE_OBJECT_PATH) {
			DBusBasicValue value;

			dbus_message_iter_get_basic(&array, &value);
			paths = gutil_strv_add(paths, value.str);
			dbus_message_iter_next(&array);
		}

		dbus->cb->set_enabled_slots(dbus->manager, paths);
		g_strfreev(paths);
		return dbus_message_new_method_return(msg);
	} else {
		return ofono_dbus_error_invalid_args(msg);
	}
}

static DBusMessage *slot_manager_dbus_set_imsi(struct slot_manager_dbus *dbus,
	DBusMessage *msg, void (*apply)(struct ofono_slot_manager *manager,
	const char *imsi))
{
	DBusMessageIter iter;

	dbus_message_iter_init(msg, &iter);
	if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
		DBusBasicValue value;
		const char *imsi;

		dbus_message_iter_get_basic(&iter, &value);
		imsi = value.str;
		if (!g_strcmp0(imsi, SM_DBUS_IMSI_AUTO)) imsi = NULL; 
		apply(dbus->manager, imsi);
		return dbus_message_new_method_return(msg);
	} else {
		return ofono_dbus_error_invalid_args(msg);
	}
}

static DBusMessage *slot_manager_dbus_set_default_voice_sim
	(DBusConnection *conn, DBusMessage *msg, void *data)
{
	struct slot_manager_dbus *dbus = data;

	GASSERT(conn == dbus->conn);
	return slot_manager_dbus_set_imsi(dbus, msg,
		dbus->cb->set_default_voice_imsi);
}

static DBusMessage *slot_manager_dbus_set_default_data_sim
	(DBusConnection *conn, DBusMessage *msg, void *data)
{
	struct slot_manager_dbus *dbus = data;

	GASSERT(conn == dbus->conn);
	return slot_manager_dbus_set_imsi(dbus, msg,
		dbus->cb->set_default_data_imsi);
}

static void slot_manager_dbus_mms_disconnect(DBusConnection *conn, void *data)
{
	struct slot_manager_dbus *dbus = data;

	dbus->mms_watch = 0;
	if (dbus->manager->mms_imsi) {
		DBG("MMS client is gone");
		dbus->cb->set_mms_imsi(dbus->manager, NULL);
	}
}

static DBusMessage *slot_manager_dbus_set_mms_sim(DBusConnection *conn,
	DBusMessage *msg, void *data)
{
	DBusMessageIter iter;
	struct slot_manager_dbus *dbus = data;

	GASSERT(conn == dbus->conn);
	dbus_message_iter_init(msg, &iter);
	if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
		struct ofono_slot_manager *manager = dbus->manager;
		DBusBasicValue value;
		const char *imsi;

		dbus_message_iter_get_basic(&iter, &value);
		imsi = value.str;

		/*
		 * MMS IMSI is not persistent and has to be eventually
		 * reset by the client or cleaned up if the client
		 * unexpectedly disappears.
		 */
		if (dbus->cb->set_mms_imsi(manager, imsi)) {

			/*
			 * Clear the previous MMS owner
			 */
			if (dbus->mms_watch) {
				g_dbus_remove_watch(dbus->conn,
					dbus->mms_watch);
				dbus->mms_watch = 0;
			}

			if (manager->mms_imsi && manager->mms_imsi[0]) {
				/*
				 * This client becomes the owner
				 */
				const char* sender =
					dbus_message_get_sender(msg);

				DBG("Owner: %s", sender);
				dbus->mms_watch = g_dbus_add_disconnect_watch
					(dbus->conn, sender,
					slot_manager_dbus_mms_disconnect,
					dbus, NULL);
			}

			return slot_manager_dbus_reply_with_string(msg,
				manager->mms_path);
		} else {
			return ofono_dbus_error_not_available(msg);
		}
	} else {
		return ofono_dbus_error_invalid_args(msg);
	}
}

/*
 * The client can call GetInterfaceVersion followed by the appropriate
 * GetAllx call to get all settings in two steps. Alternatively, it can
 * call GetAll followed by GetAllx based on the interface version returned
 * by GetAll. In either case, two D-Bus calls are required, unless the
 * client is willing to make the assumption about the ofono version it's
 * talking to.
 */

#define SM_DBUS_VERSION_ARG             {"version", "i"}
#define SM_DBUS_AVAILABLE_MODEMS_ARG    {"availableModems", "ao"}
#define SM_DBUS_ENABLED_MODEMS_ARG      {"enabledModems", "ao" }
#define SM_DBUS_DEFAULT_DATA_SIM_ARG    {"defaultDataSim", "s" }
#define SM_DBUS_DEFAULT_VOICE_SIM_ARG   {"defaultVoiceSim", "s" }
#define SM_DBUS_DEFAULT_DATA_MODEM_ARG  {"defaultDataModem", "s" }
#define SM_DBUS_DEFAULT_VOICE_MODEM_ARG {"defaultVoiceModem" , "s"}
#define SM_DBUS_PRESENT_SIMS_ARG        {"presentSims" , "ab"}
#define SM_DBUS_IMEI_ARG                {"imei" , "as"}
#define SM_DBUS_MMS_SIM_ARG             {"mmsSim", "s"}
#define SM_DBUS_MMS_MODEM_ARG           {"mmsModem" , "s"}
#define SM_DBUS_READY_ARG               {"ready" , "b"}
#define SM_DBUS_MODEM_ERRORS_ARG        {"modemErrors" , \
                                          "aa(" SM_DBUS_ERROR_SIGNATURE ")"}
#define SM_DBUS_IMEISV_ARG              {"imeisv" , "as"}
#define SM_DBUS_ERRORS_ARG              {"errors" , \
                                          "a(" SM_DBUS_ERROR_SIGNATURE ")"}
#define SM_DBUS_GET_ALL_ARGS \
	SM_DBUS_VERSION_ARG, \
	SM_DBUS_AVAILABLE_MODEMS_ARG, \
	SM_DBUS_ENABLED_MODEMS_ARG, \
	SM_DBUS_DEFAULT_DATA_SIM_ARG, \
	SM_DBUS_DEFAULT_VOICE_SIM_ARG, \
	SM_DBUS_DEFAULT_DATA_MODEM_ARG, \
	SM_DBUS_DEFAULT_VOICE_MODEM_ARG
#define SM_DBUS_GET_ALL2_ARGS \
	SM_DBUS_GET_ALL_ARGS, \
	SM_DBUS_PRESENT_SIMS_ARG
#define SM_DBUS_GET_ALL3_ARGS \
	SM_DBUS_GET_ALL2_ARGS, \
	SM_DBUS_IMEI_ARG
#define SM_DBUS_GET_ALL4_ARGS \
	SM_DBUS_GET_ALL3_ARGS, \
	SM_DBUS_MMS_SIM_ARG, \
	SM_DBUS_MMS_MODEM_ARG
#define SM_DBUS_GET_ALL5_ARGS \
	SM_DBUS_GET_ALL4_ARGS, \
	SM_DBUS_READY_ARG
#define SM_DBUS_GET_ALL6_ARGS \
	SM_DBUS_GET_ALL5_ARGS, \
	SM_DBUS_MODEM_ERRORS_ARG
#define SM_DBUS_GET_ALL7_ARGS \
	SM_DBUS_GET_ALL6_ARGS, \
	SM_DBUS_IMEISV_ARG
#define SM_DBUS_GET_ALL8_ARGS \
	SM_DBUS_GET_ALL7_ARGS, \
	SM_DBUS_ERRORS_ARG
static const GDBusMethodTable slot_manager_dbus_methods[] = {
	{ GDBUS_ASYNC_METHOD("GetAll",
			NULL, GDBUS_ARGS(SM_DBUS_GET_ALL_ARGS),
			slot_manager_dbus_get_all) },
	{ GDBUS_ASYNC_METHOD("GetAll2",
			NULL, GDBUS_ARGS(SM_DBUS_GET_ALL2_ARGS),
			slot_manager_dbus_get_all2) },
	{ GDBUS_ASYNC_METHOD("GetAll3",
			NULL, GDBUS_ARGS(SM_DBUS_GET_ALL3_ARGS),
			slot_manager_dbus_get_all3) },
	{ GDBUS_ASYNC_METHOD("GetAll4",
			NULL, GDBUS_ARGS(SM_DBUS_GET_ALL4_ARGS),
			slot_manager_dbus_get_all4) },
	{ GDBUS_ASYNC_METHOD("GetAll5",
			NULL, GDBUS_ARGS(SM_DBUS_GET_ALL5_ARGS),
			slot_manager_dbus_get_all5) },
	{ GDBUS_ASYNC_METHOD("GetAll6",
			NULL, GDBUS_ARGS(SM_DBUS_GET_ALL6_ARGS),
			slot_manager_dbus_get_all6) },
	{ GDBUS_ASYNC_METHOD("GetAll7",
			NULL, GDBUS_ARGS(SM_DBUS_GET_ALL7_ARGS),
			slot_manager_dbus_get_all7) },
	{ GDBUS_ASYNC_METHOD("GetAll8",
			NULL, GDBUS_ARGS(SM_DBUS_GET_ALL8_ARGS),
			slot_manager_dbus_get_all8) },
	{ GDBUS_ASYNC_METHOD("GetInterfaceVersion",
			NULL, GDBUS_ARGS(SM_DBUS_VERSION_ARG),
			slot_manager_dbus_get_interface_version) },
	{ GDBUS_ASYNC_METHOD("GetAvailableModems",
			NULL, GDBUS_ARGS(SM_DBUS_AVAILABLE_MODEMS_ARG),
			slot_manager_dbus_get_available_modems) },
	{ GDBUS_ASYNC_METHOD("GetEnabledModems",
			NULL, GDBUS_ARGS(SM_DBUS_ENABLED_MODEMS_ARG),
			slot_manager_dbus_get_enabled_modems) },
	{ GDBUS_ASYNC_METHOD("GetPresentSims",
			NULL, GDBUS_ARGS(SM_DBUS_PRESENT_SIMS_ARG),
			slot_manager_dbus_get_present_sims) },
	{ GDBUS_ASYNC_METHOD("GetIMEI",
			NULL, GDBUS_ARGS(SM_DBUS_IMEI_ARG),
			slot_manager_dbus_get_imei) },
	{ GDBUS_ASYNC_METHOD("GetIMEISV",
			NULL, GDBUS_ARGS(SM_DBUS_IMEISV_ARG),
			slot_manager_dbus_get_imeisv) },
	{ GDBUS_ASYNC_METHOD("GetDefaultDataSim",
			NULL, GDBUS_ARGS(SM_DBUS_DEFAULT_DATA_SIM_ARG),
			slot_manager_dbus_get_default_data_sim) },
	{ GDBUS_ASYNC_METHOD("GetDefaultVoiceSim",
			NULL, GDBUS_ARGS(SM_DBUS_DEFAULT_VOICE_SIM_ARG),
			slot_manager_dbus_get_default_voice_sim) },
	{ GDBUS_ASYNC_METHOD("GetMmsSim",
			NULL, GDBUS_ARGS(SM_DBUS_MMS_SIM_ARG),
			slot_manager_dbus_get_mms_sim) },
	{ GDBUS_ASYNC_METHOD("GetDefaultDataModem",
			NULL, GDBUS_ARGS(SM_DBUS_DEFAULT_DATA_MODEM_ARG),
			slot_manager_dbus_get_default_data_modem) },
	{ GDBUS_ASYNC_METHOD("GetDefaultVoiceModem",
			NULL, GDBUS_ARGS(SM_DBUS_DEFAULT_VOICE_MODEM_ARG),
			slot_manager_dbus_get_default_voice_modem) },
	{ GDBUS_ASYNC_METHOD("GetMmsModem",
			NULL, GDBUS_ARGS(SM_DBUS_MMS_MODEM_ARG),
			slot_manager_dbus_get_mms_modem) },
	{ GDBUS_ASYNC_METHOD("GetReady",
			NULL, GDBUS_ARGS(SM_DBUS_READY_ARG),
			slot_manager_dbus_get_ready) },
	{ GDBUS_ASYNC_METHOD("GetModemErrors",
			NULL, GDBUS_ARGS(SM_DBUS_MODEM_ERRORS_ARG),
			slot_manager_dbus_get_modem_errors) },
	{ GDBUS_ASYNC_METHOD("GetErrors",
			NULL, GDBUS_ARGS(SM_DBUS_ERRORS_ARG),
			slot_manager_dbus_get_errors) },
	{ GDBUS_ASYNC_METHOD("SetEnabledModems",
			GDBUS_ARGS({ "modems", "ao" }), NULL,
			slot_manager_dbus_set_enabled_modems) },
	{ GDBUS_ASYNC_METHOD("SetDefaultDataSim",
			GDBUS_ARGS({ "imsi", "s" }), NULL,
			slot_manager_dbus_set_default_data_sim) },
	{ GDBUS_ASYNC_METHOD("SetDefaultVoiceSim",
			GDBUS_ARGS({ "imsi", "s" }), NULL,
			slot_manager_dbus_set_default_voice_sim) },
	{ GDBUS_ASYNC_METHOD("SetMmsSim",
			GDBUS_ARGS({ "imsi", "s" }), NULL,
			slot_manager_dbus_set_mms_sim) },
	{ }
};

static const GDBusSignalTable slot_manager_dbus_signals[] = {
	{ GDBUS_SIGNAL(SM_DBUS_SIGNAL_ENABLED_MODEMS_CHANGED,
			GDBUS_ARGS(SM_DBUS_ENABLED_MODEMS_ARG)) },
	{ GDBUS_SIGNAL(SM_DBUS_SIGNAL_PRESENT_SIMS_CHANGED,
			GDBUS_ARGS({"index", "i" },
			{"present" , "b"})) },
	{ GDBUS_SIGNAL(SM_DBUS_SIGNAL_DEFAULT_DATA_SIM_CHANGED,
			GDBUS_ARGS(SM_DBUS_DEFAULT_DATA_SIM_ARG)) },
	{ GDBUS_SIGNAL(SM_DBUS_SIGNAL_DEFAULT_VOICE_SIM_CHANGED,
			GDBUS_ARGS(SM_DBUS_DEFAULT_VOICE_SIM_ARG)) },
	{ GDBUS_SIGNAL(SM_DBUS_SIGNAL_DEFAULT_DATA_MODEM_CHANGED,
			GDBUS_ARGS(SM_DBUS_DEFAULT_DATA_MODEM_ARG)) },
	{ GDBUS_SIGNAL(SM_DBUS_SIGNAL_DEFAULT_VOICE_MODEM_CHANGED,
			GDBUS_ARGS(SM_DBUS_DEFAULT_VOICE_MODEM_ARG)) },
	{ GDBUS_SIGNAL(SM_DBUS_SIGNAL_MMS_SIM_CHANGED,
			GDBUS_ARGS(SM_DBUS_MMS_SIM_ARG)) },
	{ GDBUS_SIGNAL(SM_DBUS_SIGNAL_MMS_MODEM_CHANGED,
			GDBUS_ARGS(SM_DBUS_MMS_MODEM_ARG)) },
	{ GDBUS_SIGNAL(SM_DBUS_SIGNAL_READY_CHANGED,
			GDBUS_ARGS(SM_DBUS_READY_ARG)) },
	{ GDBUS_SIGNAL(SM_DBUS_SIGNAL_MODEM_ERROR,
			GDBUS_ARGS({"path","o"},
			{"error_id", "s"},
			{"message", "s"})) },
	{ }
};

struct slot_manager_dbus *slot_manager_dbus_new(struct ofono_slot_manager *m,
	const struct slot_manager_dbus_cb *cb)
{
	struct slot_manager_dbus *dbus = g_slice_new0(struct slot_manager_dbus);

	dbus->conn = dbus_connection_ref(ofono_dbus_get_connection());
	dbus->manager = m;
	dbus->cb = cb;
	if (g_dbus_register_interface(dbus->conn, SM_DBUS_PATH,
		SM_DBUS_INTERFACE, slot_manager_dbus_methods,
		slot_manager_dbus_signals, NULL, dbus, NULL)) {
		return dbus;
	} else {
		ofono_error(SM_DBUS_INTERFACE " D-Bus register failed");
		slot_manager_dbus_free(dbus);
		return NULL;
	}
}

void slot_manager_dbus_free(struct slot_manager_dbus *dbus)
{
	if (dbus) {
		if (dbus->mms_watch) {
			g_dbus_remove_watch(dbus->conn, dbus->mms_watch);
		}

		g_slist_free_full(dbus->blocked_req,
			slot_manager_dbus_cancel_request);
		g_dbus_unregister_interface(dbus->conn, SM_DBUS_PATH,
			SM_DBUS_INTERFACE);
		dbus_connection_unref(dbus->conn);
		gutil_slice_free(dbus);
	}
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
