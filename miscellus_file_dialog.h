#ifndef MISCELLUS_FILE_DIALOG_H
#define MISCELLUS_FILE_DIALOG_H

int miscellus_file_dialog(char *out_path, int unsigned out_path_max_length, int save);

#ifdef MFD_IMPLEMENTATION

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(__linux__)

#include <string.h>
#include <gtk/gtk.h>

int miscellus_file_dialog(char *out_path, int unsigned out_path_max_length, int save) {

	if ( !gtk_init_check( NULL, NULL ) ) {
		return 0;
	}

	GtkWidget *dialog = gtk_file_chooser_dialog_new(
		save ? "Save" : "Open",
		GTK_WINDOW(NULL),
		save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel",
		GTK_RESPONSE_CANCEL,
		save ? "_Save" : "_Open",
		GTK_RESPONSE_ACCEPT,
		NULL);

	gtk_window_set_keep_above(GTK_WINDOW(dialog), 1);
	gtk_window_present(GTK_WINDOW(dialog));
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ALWAYS);

	int did_succeed = 0;

	int res = gtk_dialog_run(GTK_DIALOG(dialog));
	if (res == GTK_RESPONSE_ACCEPT) {

		GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
		char *filename = gtk_file_chooser_get_filename(chooser);
		int unsigned copy_amount = strlen(filename)+1;

		if (copy_amount <= out_path_max_length) {
			memcpy(out_path, filename, copy_amount);
			did_succeed = 1;
		}
		g_free(filename);
	}

	// while (gtk_events_pending()) gtk_main_iteration();
#if 1
	gtk_widget_destroy(dialog);
#else
	g_object_unref(dialog);
#endif
	while (gtk_events_pending()) gtk_main_iteration();

	return did_succeed;
}

#elif defined(__APPLE__) && defined(__MACH__)
#error "MacOS not yet supported"
#elif defined(_WIN32) || defined(WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

int miscellus_file_dialog(char *out_path, int unsigned out_path_max_length, int save) {

	// TODO(jakob): implement save dialog for windows
	if (save) return false;

	OPENFILENAME open_dialog;

	ZeroMemory(&open_dialog, sizeof(open_dialog));
	open_dialog.lStructSize = sizeof(open_dialog);
	open_dialog.hwndOwner = NULL;
	open_dialog.lpstrFile = out_path;
	// Set lpstrFile[0] to '\0' so that GetOpenFileName does not
	// use the contents of szFile to initialize itself.
	open_dialog.lpstrFile[0] = '\0';
	open_dialog.nMaxFile = out_path_max_length;
	open_dialog.lpstrFilter = "All\0*.*\0Binary\0*.bin\0";
	open_dialog.nFilterIndex = 1;
	open_dialog.lpstrFileTitle = NULL;
	open_dialog.nMaxFileTitle = 0;
	open_dialog.lpstrInitialDir = NULL;
	open_dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	// Display the Open dialog box.
	return (TRUE == GetOpenFileName(&open_dialog));
}

#endif


#ifdef __cplusplus
} // extern "C"
#endif

#endif //MISCELLUS_FILE_DIALOG_IMPLEMENTATION
#endif //MISCELLUS_FILE_DIALOG_H