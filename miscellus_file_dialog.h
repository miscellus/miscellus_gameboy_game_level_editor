#ifndef MISCELLUS_FILE_DIALOG_H
#define MISCELLUS_FILE_DIALOG_H

int miscellus_file_dialog(char *out_path, int unsigned out_path_max_length);

#ifdef MFD_IMPLEMENTATION

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(__linux__)

#include <string.h>
#include <gtk/gtk.h>

int miscellus_file_dialog(char *out_path, int unsigned out_path_max_length) {

	if ( !gtk_init_check( NULL, NULL ) ) {
		return 0;
	}

	GtkWidget *dialog = gtk_file_chooser_dialog_new(
		"Open File",
		GTK_WINDOW(NULL),
		GTK_FILE_CHOOSER_ACTION_OPEN,
		"_Cancel",
		GTK_RESPONSE_CANCEL,
		"_Open",
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
#error "Windows not yet supported"
#endif 


#ifdef __cplusplus
} // extern "C"
#endif

#endif //MISCELLUS_FILE_DIALOG_IMPLEMENTATION
#endif //MISCELLUS_FILE_DIALOG_H