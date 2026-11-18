#include <gtk/gtk.h>

static void copy_text(GtkButton *button, gpointer unused)
{
    (void)unused;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "Linux Wayland copy", -1);
    gtk_button_set_label(button, "Linux text copied");
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Linux Wayland Clipboard");
    gtk_window_set_default_size(GTK_WINDOW(window), 280, 110);
    GtkWidget *button = gtk_button_new_with_label("Copy from Linux");
    gtk_container_add(GTK_CONTAINER(window), button);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(button, "clicked", G_CALLBACK(copy_text), NULL);
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
