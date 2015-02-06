/*
	liblinphone_tester - liblinphone test suite
	Copyright (C) 2013  Belledonne Communications SARL

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "private.h"

#if defined(VIDEO_ENABLED) && defined(HAVE_GTK)

#include <stdio.h>
#include "CUnit/Basic.h"
#include "linphonecore.h"
#include "liblinphone_tester.h"
#include "lpconfig.h"


#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#elif defined(WIN32)
#include <gdk/gdkwin32.h>
#elif defined(__APPLE__)
extern void *gdk_quartz_window_get_nswindow(GdkWindow      *window);
extern void *gdk_quartz_window_get_nsview(GdkWindow      *window);
#endif

#include <gdk/gdkkeysyms.h>


static unsigned long get_native_handle(GdkWindow *gdkw) {
#ifdef GDK_WINDOWING_X11
	return (unsigned long)GDK_WINDOW_XID(gdkw);
#elif defined(WIN32)
	return (unsigned long)GDK_WINDOW_HWND(gdkw);
#elif defined(__APPLE__)
	return (unsigned long)gdk_quartz_window_get_nsview(gdkw);
#endif
	g_warning("No way to get the native handle from gdk window");
	return 0;
}

static GtkWidget *create_video_window(LinphoneCall *call, LinphoneCallState cstate) {
	GtkWidget *video_window;
	GdkDisplay *display;
	GdkColor color;
	MSVideoSize vsize = MS_VIDEO_SIZE_CIF;
	const char *cstate_str;
	char *title;
	stats* counters = get_stats(call->core);

	cstate_str = linphone_call_state_to_string(cstate);
	title = g_strdup_printf("%s", cstate_str);
	video_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(video_window), title);
	g_free(title);
	gtk_window_resize(GTK_WINDOW(video_window), vsize.width, vsize.height);
	gdk_color_parse("black", &color);
	gtk_widget_modify_bg(video_window, GTK_STATE_NORMAL, &color);
	gtk_widget_show(video_window);
	g_object_set_data(G_OBJECT(video_window), "call", call);
#if GTK_CHECK_VERSION(2,24,0)
	display = gdk_window_get_display(gtk_widget_get_window(video_window));
#else // backward compatibility with Debian 6 and Centos 6
	display = gdk_drawable_get_display(gtk_widget_get_window(video_window));
#endif
	gdk_display_flush(display);
	counters->number_of_video_windows_created++;
	return video_window;
}

static void show_video_window(LinphoneCall *call, LinphoneCallState cstate) {
	GtkWidget *video_window = (GtkWidget *)linphone_call_get_user_data(call);
	if (video_window == NULL) {
		video_window = create_video_window(call, cstate);
		linphone_call_set_user_data(call, video_window);
		linphone_call_set_native_video_window_id(call, get_native_handle(gtk_widget_get_window(video_window)));
	}
}

static void hide_video_video(LinphoneCall *call) {
	GtkWidget *video_window = (GtkWidget *)linphone_call_get_user_data(call);
	if (video_window != NULL) {
		gtk_widget_destroy(video_window);
		linphone_call_set_user_data(call, NULL);
		linphone_call_set_native_video_window_id(call, 0);
	}
}

static void video_call_state_changed(LinphoneCore *lc, LinphoneCall *call, LinphoneCallState cstate, const char *msg) {
	switch (cstate) {
		case LinphoneCallIncomingEarlyMedia:
		case LinphoneCallOutgoingEarlyMedia:
		case LinphoneCallConnected:
			show_video_window(call, cstate);
			break;
		case LinphoneCallEnd:
			hide_video_video(call);
			break;
		default:
			break;
	}
}

static void early_media_video_call_state_changed(LinphoneCore *lc, LinphoneCall *call, LinphoneCallState cstate, const char *msg) {
	LinphoneCallParams *params;

	video_call_state_changed(lc, call, cstate, msg);
	switch (cstate) {
		case LinphoneCallIncomingReceived:
			params = linphone_core_create_default_call_parameters(lc);
			linphone_call_params_enable_video(params, TRUE);
			linphone_core_accept_early_media_with_params(lc, call, params);
			linphone_call_params_unref(params);
			break;
		default:
			break;
	}
}

bool_t wait_for_three_cores(LinphoneCore *lc1, LinphoneCore *lc2, LinphoneCore *lc3, int timeout) {
	MSList *lcs = NULL;
	bool_t result;
	int dummy = 0;
	if (lc1) lcs = ms_list_append(lcs, lc1);
	if (lc2) lcs = ms_list_append(lcs, lc2);
	if (lc3) lcs = ms_list_append(lcs, lc3);
	result = wait_for_list(lcs, &dummy, 1, timeout);
	ms_list_free(lcs);
	return result;
}

static bool_t video_call_with_params(LinphoneCoreManager* caller_mgr, LinphoneCoreManager* callee_mgr, const LinphoneCallParams *caller_params, const LinphoneCallParams *callee_params, bool_t automatically_accept) {
	int retry = 0;
	stats initial_caller = caller_mgr->stat;
	stats initial_callee = callee_mgr->stat;
	bool_t result = TRUE;
	bool_t did_received_call;

	CU_ASSERT_PTR_NOT_NULL(linphone_core_invite_address_with_params(caller_mgr->lc, callee_mgr->identity, caller_params));
	did_received_call = wait_for(callee_mgr->lc, caller_mgr->lc, &callee_mgr->stat.number_of_LinphoneCallIncomingReceived, initial_callee.number_of_LinphoneCallIncomingReceived + 1);
	if (!did_received_call) return 0;

	CU_ASSERT_EQUAL(caller_mgr->stat.number_of_LinphoneCallOutgoingProgress, initial_caller.number_of_LinphoneCallOutgoingProgress + 1);

	while (caller_mgr->stat.number_of_LinphoneCallOutgoingRinging != (initial_caller.number_of_LinphoneCallOutgoingRinging + 1)
		&& caller_mgr->stat.number_of_LinphoneCallOutgoingEarlyMedia != (initial_caller.number_of_LinphoneCallOutgoingEarlyMedia + 1)
		&& retry++ < 20) {
		linphone_core_iterate(caller_mgr->lc);
		linphone_core_iterate(callee_mgr->lc);
		ms_usleep(100000);
	}

	CU_ASSERT_TRUE((caller_mgr->stat.number_of_LinphoneCallOutgoingRinging == initial_caller.number_of_LinphoneCallOutgoingRinging + 1)
		|| (caller_mgr->stat.number_of_LinphoneCallOutgoingEarlyMedia == initial_caller.number_of_LinphoneCallOutgoingEarlyMedia + 1));

	CU_ASSERT_PTR_NOT_NULL(linphone_core_get_current_call_remote_address(callee_mgr->lc));
	if(!linphone_core_get_current_call(caller_mgr->lc) || !linphone_core_get_current_call(callee_mgr->lc) || !linphone_core_get_current_call_remote_address(callee_mgr->lc)) {
		return 0;
	}

	if (automatically_accept == TRUE) {
		linphone_core_accept_call_with_params(callee_mgr->lc, linphone_core_get_current_call(callee_mgr->lc), callee_params);

		CU_ASSERT_TRUE(wait_for(callee_mgr->lc, caller_mgr->lc, &callee_mgr->stat.number_of_LinphoneCallConnected, initial_callee.number_of_LinphoneCallConnected + 1));
		CU_ASSERT_TRUE(wait_for(callee_mgr->lc, caller_mgr->lc, &caller_mgr->stat.number_of_LinphoneCallConnected, initial_callee.number_of_LinphoneCallConnected + 1));
		result = wait_for(callee_mgr->lc, caller_mgr->lc, &caller_mgr->stat.number_of_LinphoneCallStreamsRunning, initial_caller.number_of_LinphoneCallStreamsRunning + 1)
			&& wait_for(callee_mgr->lc, caller_mgr->lc, &callee_mgr->stat.number_of_LinphoneCallStreamsRunning, initial_callee.number_of_LinphoneCallStreamsRunning + 1);
	}
	return result;
}

static LinphoneCallParams * _configure_for_video(LinphoneCoreManager *manager, LinphoneCoreCallStateChangedCb cb) {
	LinphoneCallParams *params;
	LinphoneCoreVTable *vtable = linphone_core_v_table_new();
	vtable->call_state_changed = cb;
	linphone_core_add_listener(manager->lc, vtable);
	linphone_core_set_video_device(manager->lc, "StaticImage: Static picture");
	linphone_core_enable_video_capture(manager->lc, TRUE);
	linphone_core_enable_video_display(manager->lc, TRUE);
	params = linphone_core_create_default_call_parameters(manager->lc);
	linphone_call_params_enable_video(params, TRUE);
	disable_all_video_codecs_except_one(manager->lc, "VP8");
	return params;
}

static LinphoneCallParams * configure_for_video(LinphoneCoreManager *manager) {
	return _configure_for_video(manager, video_call_state_changed);
}

static LinphoneCallParams * configure_for_early_media_video_sending(LinphoneCoreManager *manager) {
	LinphoneCallParams *params = _configure_for_video(manager, video_call_state_changed);
	linphone_call_params_enable_early_media_sending(params, TRUE);
	return params;
}

static LinphoneCallParams * configure_for_early_media_video_receiving(LinphoneCoreManager *manager) {
	return _configure_for_video(manager, early_media_video_call_state_changed);
}


static void early_media_video_during_video_call_test(void) {
	LinphoneCoreManager *marie;
	LinphoneCoreManager *pauline;
	LinphoneCoreManager *laure;
	LinphoneCallParams *marie_params;
	LinphoneCallParams *pauline_params;
	LinphoneCallParams *laure_params;

	marie = linphone_core_manager_new("marie_rc");
	pauline = linphone_core_manager_new("pauline_rc");
	laure = linphone_core_manager_new("laure_rc");
	marie_params = configure_for_early_media_video_receiving(marie);
	pauline_params = configure_for_video(pauline);
	laure_params = configure_for_early_media_video_sending(laure);

	/* Normal automatically accepted video call from marie to pauline. */
	CU_ASSERT_TRUE(video_call_with_params(marie, pauline, marie_params, pauline_params, TRUE));

	/* Wait for 2s. */
	wait_for_three_cores(marie->lc, pauline->lc, NULL, 2000);

	/* Early media video call from laure to marie. */
	CU_ASSERT_TRUE(video_call_with_params(laure, marie, laure_params, NULL, FALSE));

	/* Wait for 2s. */
	wait_for_three_cores(marie->lc, pauline->lc, laure->lc, 2000);

	linphone_core_terminate_all_calls(marie->lc);
	CU_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallEnd, 1));
	CU_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallEnd, 1));
	CU_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &marie->stat.number_of_LinphoneCallReleased, 1));
	CU_ASSERT_TRUE(wait_for(marie->lc, pauline->lc, &pauline->stat.number_of_LinphoneCallReleased, 1));
	CU_ASSERT_TRUE(wait_for(marie->lc, laure->lc, &marie->stat.number_of_LinphoneCallEnd, 1));
	CU_ASSERT_TRUE(wait_for(marie->lc, laure->lc, &laure->stat.number_of_LinphoneCallEnd, 1));

	CU_ASSERT_EQUAL(marie->stat.number_of_video_windows_created, 2);
	CU_ASSERT_EQUAL(pauline->stat.number_of_video_windows_created, 1);
	CU_ASSERT_EQUAL(laure->stat.number_of_video_windows_created, 1);

	linphone_call_params_unref(marie_params);
	linphone_call_params_unref(pauline_params);
	linphone_call_params_unref(laure_params);
	linphone_core_manager_destroy(marie);
	linphone_core_manager_destroy(pauline);
	linphone_core_manager_destroy(laure);
}

test_t video_tests[] = {
	{ "Early-media video during video call", early_media_video_during_video_call_test }
};

test_suite_t video_test_suite = {
	"Video",
	NULL,
	NULL,
	sizeof(video_tests) / sizeof(video_tests[0]),
	video_tests
};

#endif /* VIDEO_ENABLED */