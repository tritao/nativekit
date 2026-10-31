#include "nativekit.h"
#include "nativekit_dialog.h"
#include <gtk/gtk.h>
#include <assert.h>
#include <string.h>

static GtkWidget *find_checkbox(GtkWidget *widget) {
    if (GTK_IS_CHECK_BUTTON(widget))
        return widget;
    if (!GTK_IS_CONTAINER(widget))
        return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *found = NULL;
    for (GList *item = children; item && !found; item = item->next)
        found = find_checkbox(GTK_WIDGET(item->data));
    g_list_free(children);
    return found;
}

static void check_dialog(int response, int checked) {
    nk_message_dialog_options options = {0};
    options.struct_size = sizeof(options);
    options.kind = NK_MESSAGE_QUESTION;
    options.buttons = NK_MESSAGE_BUTTON_YES | NK_MESSAGE_BUTTON_CANCEL;
    options.title = "Deletion confirmation test";
    options.message = "Move this folder and its contents to the Trash?";
    options.yes_label = "Move to Trash";
    options.checkbox_label = "Do not ask me again";
    nk_request_id request = 0;
    assert(nk_dialog_message(0, &options, &request) == NK_OK);
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *dialog = NULL;
    for (GList *item = windows; item; item = item->next)
        if (GTK_IS_MESSAGE_DIALOG(item->data))
            dialog = item->data;
    g_list_free(windows);
    assert(dialog);
    GtkWidget *affirmative =
        gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
    assert(strcmp(gtk_button_get_label(GTK_BUTTON(affirmative)), "Move to Trash") == 0);
    GtkWidget *cancel = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    assert(gtk_window_get_default_widget(GTK_WINDOW(dialog)) == cancel);
    GtkWidget *checkbox = find_checkbox(dialog);
    assert(checkbox && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox)));
    assert(strcmp(gtk_button_get_label(GTK_BUTTON(checkbox)), "Do not ask me again") == 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox), checked);
    if (response == GTK_RESPONSE_CANCEL)
        assert(gtk_window_activate_default(GTK_WINDOW(dialog)));
    else
        gtk_dialog_response(GTK_DIALOG(dialog), response);
    int received = 0;
    for (int i = 0; i < 1000 && !received; ++i) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_DIALOG_MESSAGE_COMPLETE && event.request_id == request) {
            assert(event.data_size == sizeof(nk_dialog_message_result));
            const nk_dialog_message_result *result = event.data;
            assert(result->button == (response == GTK_RESPONSE_YES ? NK_MESSAGE_RESULT_YES
                                                                   : NK_MESSAGE_RESULT_CANCEL));
            assert(result->checkbox_checked == (uint32_t)checked);
            received = 1;
        }
        nk_event_release(&event);
        if (!received)
            g_usleep(1000);
    }
    assert(received);
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    check_dialog(GTK_RESPONSE_YES, 1);
    check_dialog(GTK_RESPONSE_YES, 0);
    check_dialog(GTK_RESPONSE_CANCEL, 1);
    check_dialog(GTK_RESPONSE_DELETE_EVENT, 0);
    nk_shutdown();
    return 0;
}
