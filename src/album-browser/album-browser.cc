/*
 * album-browser.cc
 * Copyright 2024 Album Browser Plugin Authors
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>
#include <libaudcore/runtime.h>
#include <libaudcore/drct.h>
#include <libaudcore/playlist.h>
#include <libaudcore/audstrings.h>
#include <libaudgui/libaudgui.h>
#include <libaudgui/libaudgui-gtk.h>

#include "scanner.h"
#include "album.h"
#include <memory>
#include <gdk-pixbuf/gdk-pixbuf.h>

class AlbumBrowserPlugin : public GeneralPlugin
{
public:
    static constexpr PluginInfo info = {
        N_("Album Browser"),
        PACKAGE,
        nullptr, // about
        nullptr, // prefs
        PluginGLibOnly
    };

    constexpr AlbumBrowserPlugin () : GeneralPlugin (info, false) {}

    bool init () override;
    void cleanup () override;
    void * get_gtk_widget () override;

private:
    void refresh_albums();
    void update_albums(std::vector<Album> albums);
    void on_album_clicked(const Album& album, GdkEventButton* event);
    void add_album_to_playlist(const Album& album, bool clear_first);
    GtkWidget* create_album_tile(const Album& album);
    void relayout_grid();
    
    std::unique_ptr<Scanner> scanner_;
    std::vector<Album> albums_;
    
    GtkWidget* main_widget_ = nullptr;
    GtkWidget* grid_view_ = nullptr;
    GtkWidget* refresh_button_ = nullptr;
    GtkWidget* status_label_ = nullptr;
    GtkWidget* scrolled_window_ = nullptr;
    
    std::string music_directory_;
    bool widget_created_by_gtkui_ = false;
    bool relayout_in_progress_ = false;
    int last_grid_width_ = 0;
};

EXPORT AlbumBrowserPlugin aud_plugin_instance;

static gboolean refresh_button_clicked(GtkButton* button, AlbumBrowserPlugin* plugin)
{
    g_object_set_data(G_OBJECT(button), "plugin", plugin);
    return FALSE;
}

bool AlbumBrowserPlugin::init ()
{
    audgui_init ();
    
    scanner_ = std::make_unique<Scanner>();
    
    // Get music directory from environment or use default
    const char* home = getenv("HOME");
    music_directory_ = std::string(home ? home : "") + "/Music";
    
    return true;
}

void AlbumBrowserPlugin::cleanup ()
{
    if (scanner_)
        scanner_->cancel();
    
    scanner_.reset();
    albums_.clear();
    
    // Don't destroy the widget - it's owned by gtkui now
    // Just clear our reference
    main_widget_ = nullptr;
    grid_view_ = nullptr;
    refresh_button_ = nullptr;
    status_label_ = nullptr;
    scrolled_window_ = nullptr;
    
    audgui_cleanup ();
}

void AlbumBrowserPlugin::refresh_albums()
{
    if (scanner_->is_scanning())
        return;
    
    gtk_widget_set_sensitive(refresh_button_, FALSE);
    gtk_label_set_text(GTK_LABEL(status_label_), "Scanning...");
    
    scanner_->scan_async(music_directory_, [this](std::vector<Album> albums) {
        // This callback runs in the scan thread, so we need to use g_idle_add
        // to update the UI on the main thread
        auto* albums_copy = new std::vector<Album>(albums);
        g_idle_add([](gpointer data) -> gboolean {
            auto* albums_ptr = static_cast<std::vector<Album>*>(data);
            auto* plugin = &aud_plugin_instance;
            plugin->update_albums(*albums_ptr);
            delete albums_ptr;
            return FALSE;
        }, albums_copy);
    });
}

void AlbumBrowserPlugin::update_albums(std::vector<Album> albums)
{
    albums_ = albums;
    relayout_grid();
    
    // Update status
    char status[256];
    snprintf(status, sizeof(status), "Found %zu albums", albums_.size());
    gtk_label_set_text(GTK_LABEL(status_label_), status);
    gtk_widget_set_sensitive(refresh_button_, TRUE);
}

void AlbumBrowserPlugin::relayout_grid()
{
    if (!grid_view_ || albums_.empty() || relayout_in_progress_)
        return;
    
    // Calculate number of columns based on window width
    int window_width = gtk_widget_get_allocated_width(scrolled_window_);
    const int tile_width = 220;  // 200 + spacing
    int cols_per_row = std::max(1, window_width / tile_width);
    
    // Only relayout if width changed significantly
    if (abs(window_width - last_grid_width_) < tile_width / 2)
        return;
    
    last_grid_width_ = window_width;
    relayout_in_progress_ = true;
    
    // Clear existing grid
    gtk_container_foreach(GTK_CONTAINER(grid_view_), 
        [](GtkWidget* widget, gpointer) { gtk_widget_destroy(widget); }, nullptr);
    
    // Add album tiles
    int row = 0, col = 0;
    for (const auto& album : albums_)
    {
        GtkWidget* tile = create_album_tile(album);
        gtk_grid_attach(GTK_GRID(grid_view_), tile, col, row, 1, 1);
        
        col++;
        if (col >= cols_per_row)
        {
            col = 0;
            row++;
        }
    }
    
    gtk_widget_show_all(grid_view_);
    relayout_in_progress_ = false;
}

GtkWidget* AlbumBrowserPlugin::create_album_tile(const Album& album)
{
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(vbox, 200, 250);
    
    // Add CSS for hover effect
    static bool css_loaded = false;
    if (!css_loaded)
    {
        GtkCssProvider* provider = gtk_css_provider_new();
        const char* css = 
            "eventbox {"
            "  border-radius: 8px;"
            "  transition: all 200ms ease-in-out;"
            "  padding: 5px;"
            "}"
            "eventbox:hover {"
            "  background-color: alpha(@theme_selected_bg_color, 0.15);"
            "  box-shadow: 0 4px 8px rgba(0,0,0,0.2);"
            "}"
            "eventbox:active {"
            "  background-color: alpha(@theme_selected_bg_color, 0.25);"
            "  box-shadow: 0 2px 4px rgba(0,0,0,0.2);"
            "}";
        gtk_css_provider_load_from_data(provider, css, -1, nullptr);
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        css_loaded = true;
    }
    
    // Cover art
    GtkWidget* image;
    if (album.has_cover_art())
    {
        GError* error = nullptr;
        GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(
            album.cover_art_path.c_str(), 180, 180, TRUE, &error);
        
        if (pixbuf)
        {
            image = gtk_image_new_from_pixbuf(pixbuf);
            g_object_unref(pixbuf);
        }
        else
        {
            image = gtk_image_new_from_icon_name("audio-x-generic", GTK_ICON_SIZE_DIALOG);
            if (error)
            {
                AUDWARN("Failed to load cover art %s: %s\n", 
                    album.cover_art_path.c_str(), error->message);
                g_error_free(error);
            }
        }
    }
    else
    {
        image = gtk_image_new_from_icon_name("audio-x-generic", GTK_ICON_SIZE_DIALOG);
    }
    
    gtk_box_pack_start(GTK_BOX(vbox), image, FALSE, FALSE, 0);
    
    // Title label
    GtkWidget* title_label = gtk_label_new(album.title.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(title_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(title_label), 20);
    gtk_box_pack_start(GTK_BOX(vbox), title_label, FALSE, FALSE, 0);
    
    // Artist label
    if (!album.artist.empty())
    {
        GtkWidget* artist_label = gtk_label_new(album.artist.c_str());
        gtk_label_set_line_wrap(GTK_LABEL(artist_label), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(artist_label), 20);
        
        // Make artist label smaller and gray
        PangoAttrList* attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_scale_new(0.9));
        pango_attr_list_insert(attrs, pango_attr_foreground_new(0x8000, 0x8000, 0x8000));
        gtk_label_set_attributes(GTK_LABEL(artist_label), attrs);
        pango_attr_list_unref(attrs);
        
        gtk_box_pack_start(GTK_BOX(vbox), artist_label, FALSE, FALSE, 0);
    }
    
    // Make the tile clickable
    GtkWidget* event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(event_box), vbox);
    
    // Store album pointer in widget data
    Album* album_copy = new Album(album);
    g_object_set_data_full(G_OBJECT(event_box), "album", album_copy, 
        [](gpointer data) { delete static_cast<Album*>(data); });
    
    g_signal_connect(event_box, "button-press-event", 
        G_CALLBACK(+[](GtkWidget* widget, GdkEventButton* event, gpointer) -> gboolean {
            Album* album = static_cast<Album*>(g_object_get_data(G_OBJECT(widget), "album"));
            
            if (album && event->button == 1)  // Left click
            {
                aud_plugin_instance.add_album_to_playlist(*album, true);
                return TRUE;
            }
            else if (album && event->button == 3)  // Right click
            {
                aud_plugin_instance.add_album_to_playlist(*album, false);
                return TRUE;
            }
            return FALSE;
        }), nullptr);
    
    // Add hover effect
    g_signal_connect(event_box, "enter-notify-event",
        G_CALLBACK(+[](GtkWidget* widget, GdkEventCrossing*, gpointer) -> gboolean {
            GtkWidget* vbox = gtk_bin_get_child(GTK_BIN(widget));
            gtk_widget_set_state_flags(vbox, GTK_STATE_FLAG_PRELIGHT, FALSE);
            return FALSE;
        }), nullptr);
    
    g_signal_connect(event_box, "leave-notify-event",
        G_CALLBACK(+[](GtkWidget* widget, GdkEventCrossing*, gpointer) -> gboolean {
            GtkWidget* vbox = gtk_bin_get_child(GTK_BIN(widget));
            gtk_widget_unset_state_flags(vbox, GTK_STATE_FLAG_PRELIGHT);
            return FALSE;
        }), nullptr);
    
    return event_box;
}

void AlbumBrowserPlugin::add_album_to_playlist(const Album& album, bool clear_first)
{
    auto playlist = Playlist::active_playlist();
    
    if (clear_first)
        playlist.remove_all_entries();
    
    // Build list of items to add - convert file paths to URIs
    Index<PlaylistAddItem> items;
    for (const auto& file : album.audio_files)
    {
        // Convert file path to file:// URI
        String uri = String(filename_to_uri(file.c_str()));
        items.append(uri);
    }
    
    // Add all items at once
    playlist.insert_items(-1, std::move(items), clear_first);
}

void * AlbumBrowserPlugin::get_gtk_widget ()
{
    // Only create widget once when called from gtkui
    // Return nullptr if already created to prevent dock system from adding it
    if (main_widget_)
        return nullptr;
    
    widget_created_by_gtkui_ = true;
    
    // Create main container
    main_widget_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(main_widget_), 5);
    
    // Create toolbar
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    
    refresh_button_ = gtk_button_new_with_label("Refresh");
    g_signal_connect(refresh_button_, "clicked", 
        G_CALLBACK(+[](GtkButton*, gpointer data) {
            static_cast<AlbumBrowserPlugin*>(data)->refresh_albums();
        }), this);
    gtk_box_pack_start(GTK_BOX(toolbar), refresh_button_, FALSE, FALSE, 0);
    
    status_label_ = gtk_label_new("Click Refresh to scan for albums");
    gtk_box_pack_start(GTK_BOX(toolbar), status_label_, FALSE, FALSE, 10);
    
    gtk_box_pack_start(GTK_BOX(main_widget_), toolbar, FALSE, FALSE, 0);
    
    // Create scrolled window for grid
    scrolled_window_ = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window_),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    
    // Create grid for album tiles
    grid_view_ = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid_view_), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid_view_), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid_view_), 10);
    
    // Add resize handler for dynamic column adjustment
    g_signal_connect(scrolled_window_, "size-allocate",
        G_CALLBACK(+[](GtkWidget*, GdkRectangle*, gpointer data) {
            static_cast<AlbumBrowserPlugin*>(data)->relayout_grid();
        }), this);
    
    gtk_container_add(GTK_CONTAINER(scrolled_window_), grid_view_);
    gtk_box_pack_start(GTK_BOX(main_widget_), scrolled_window_, TRUE, TRUE, 0);
    
    gtk_widget_show_all(main_widget_);
    
    return main_widget_;
}

