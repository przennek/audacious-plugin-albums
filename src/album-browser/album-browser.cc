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
#include <libaudcore/probe.h>
#include <libaudgui/libaudgui.h>
#include <libaudgui/libaudgui-gtk.h>

#include "scanner.h"
#include "album.h"
#include <memory>
#include <fstream>
#include <algorithm>
#include <map>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

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
    void filter_albums();
    void setup_file_monitor();
    void stop_file_monitor();
    void save_cache();
    void load_cache();
    std::string get_cache_path();
    bool album_matches_filter(const Album& album, const std::string& filter);
    std::string to_lower(const std::string& str);
    
    std::unique_ptr<Scanner> scanner_;
    std::vector<Album> albums_;
    std::map<std::string, GdkPixbuf*> pixbuf_cache_;  // Cache cover art pixbufs by album directory
    
    GtkWidget* main_widget_ = nullptr;
    GtkWidget* grid_view_ = nullptr;
    GtkWidget* search_entry_ = nullptr;
    GtkWidget* scrolled_window_ = nullptr;
    
    std::string music_directory_;
    std::string search_filter_;
    std::string last_search_filter_;
    bool widget_created_by_gtkui_ = false;
    bool relayout_in_progress_ = false;
    int last_grid_width_ = 0;
    int last_cols_per_row_ = 0;
    guint relayout_timeout_id_ = 0;
    guint rescan_timeout_id_ = 0;
    guint search_timeout_id_ = 0;
    
    GFileMonitor* file_monitor_ = nullptr;
};

EXPORT AlbumBrowserPlugin aud_plugin_instance;

bool AlbumBrowserPlugin::init ()
{
    audgui_init ();
    
    scanner_ = std::make_unique<Scanner>();
    
    // Get music directory from environment or use default
    const char* home = getenv("HOME");
    music_directory_ = std::string(home ? home : "") + "/Music";
    
    // Load cached album list
    load_cache();
    
    // Setup file system monitoring
    setup_file_monitor();
    
    return true;
}

void AlbumBrowserPlugin::cleanup ()
{
    if (scanner_)
        scanner_->cancel();
    
    scanner_.reset();
    albums_.clear();
    
    // Clear pixbuf cache
    for (auto& pair : pixbuf_cache_)
    {
        if (pair.second)
            g_object_unref(pair.second);
    }
    pixbuf_cache_.clear();
    
    // Stop file monitoring
    stop_file_monitor();
    
    // Cancel any pending relayout timeout
    if (relayout_timeout_id_ > 0)
    {
        g_source_remove(relayout_timeout_id_);
        relayout_timeout_id_ = 0;
    }
    
    // Cancel any pending rescan timeout
    if (rescan_timeout_id_ > 0)
    {
        g_source_remove(rescan_timeout_id_);
        rescan_timeout_id_ = 0;
    }
    
    // Cancel any pending search timeout
    if (search_timeout_id_ > 0)
    {
        g_source_remove(search_timeout_id_);
        search_timeout_id_ = 0;
    }
    
    // Don't destroy the widget - it's owned by gtkui now
    // Just clear our reference
    main_widget_ = nullptr;
    grid_view_ = nullptr;
    search_entry_ = nullptr;
    scrolled_window_ = nullptr;
    
    audgui_cleanup ();
}

void AlbumBrowserPlugin::refresh_albums()
{
    if (scanner_->is_scanning())
        return;
    
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
    
    // Clear pixbuf cache when albums change
    for (auto& pair : pixbuf_cache_)
    {
        if (pair.second)
            g_object_unref(pair.second);
    }
    pixbuf_cache_.clear();
    
    // Save to cache
    save_cache();
    
    relayout_grid();
}

void AlbumBrowserPlugin::relayout_grid()
{
    if (!grid_view_ || albums_.empty() || relayout_in_progress_)
        return;
    
    // Calculate number of columns based on window width
    int window_width = gtk_widget_get_allocated_width(scrolled_window_);
    const int tile_width = 220;  // 200 + spacing
    int cols_per_row = std::max(1, window_width / tile_width);
    
    // Check if we need to relayout (column count changed OR search filter changed)
    bool filter_changed = (search_filter_ != last_search_filter_);
    bool columns_changed = (cols_per_row != last_cols_per_row_);
    
    if (!filter_changed && !columns_changed)
        return;
    
    last_grid_width_ = window_width;
    last_cols_per_row_ = cols_per_row;
    last_search_filter_ = search_filter_;
    relayout_in_progress_ = true;
    
    // Clear existing grid
    gtk_container_foreach(GTK_CONTAINER(grid_view_), 
        [](GtkWidget* widget, gpointer) { gtk_widget_destroy(widget); }, nullptr);
    
    // Add matching album tiles
    int row = 0, col = 0;
    for (const auto& album : albums_)
    {
        if (album_matches_filter(album, search_filter_))
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
    }
    
    gtk_widget_show_all(grid_view_);
    relayout_in_progress_ = false;
}

void AlbumBrowserPlugin::filter_albums()
{
    // Just trigger a relayout - simpler and more reliable
    relayout_grid();
}

std::string AlbumBrowserPlugin::to_lower(const std::string& str)
{
    // Use GLib's Unicode-aware case folding
    gchar* lower = g_utf8_strdown(str.c_str(), -1);
    std::string result(lower);
    g_free(lower);
    return result;
}

bool AlbumBrowserPlugin::album_matches_filter(const Album& album, const std::string& filter)
{
    if (filter.empty())
        return true;
    
    std::string filter_lower = to_lower(filter);
    
    // Search in title
    if (to_lower(album.title).find(filter_lower) != std::string::npos)
        return true;
    
    // Search in artist
    if (to_lower(album.artist).find(filter_lower) != std::string::npos)
        return true;
    
    // Search in directory path (for albums identified by folder name)
    if (to_lower(album.directory_path).find(filter_lower) != std::string::npos)
        return true;
    
    // Search in year (if present)
    if (album.year > 0)
    {
        std::string year_str = std::to_string(album.year);
        if (year_str.find(filter) != std::string::npos)
            return true;
    }
    
    return false;
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
    
    // Cover art - check cache first
    GtkWidget* image = nullptr;
    GdkPixbuf* pixbuf = nullptr;
    
    // Use directory path as cache key
    auto cache_it = pixbuf_cache_.find(album.directory_path);
    if (cache_it != pixbuf_cache_.end())
    {
        // Found in cache - reuse it
        pixbuf = cache_it->second;
        if (pixbuf)
            g_object_ref(pixbuf);  // Add reference since we'll unref after creating image
    }
    else
    {
        // Not in cache - load it
        if (album.has_cover_art())
        {
            // Try loading from file
            GError* error = nullptr;
            pixbuf = gdk_pixbuf_new_from_file_at_scale(
                album.cover_art_path.c_str(), 180, 180, TRUE, &error);
            
            if (!pixbuf && error)
            {
                AUDWARN("Failed to load cover art %s: %s\n", 
                    album.cover_art_path.c_str(), error->message);
                g_error_free(error);
            }
        }
        
        // If no file-based cover, try embedded art from first audio file
        if (!pixbuf && !album.audio_files.empty())
        {
            String uri = String(filename_to_uri(album.audio_files[0].c_str()));
            AudArtPtr art = aud_art_request(uri, AUD_ART_DATA);
            
            if (art)
            {
                auto data = art.data();
                if (data && data->len() > 0)
                {
                    GInputStream* stream = g_memory_input_stream_new_from_data(
                        data->begin(), data->len(), nullptr);
                    
                    GError* error = nullptr;
                    pixbuf = gdk_pixbuf_new_from_stream_at_scale(
                        stream, 180, 180, TRUE, nullptr, &error);
                    
                    g_object_unref(stream);
                    
                    if (error)
                        g_error_free(error);
                }
            }
        }
        
        // Cache the pixbuf (even if null)
        if (pixbuf)
        {
            pixbuf_cache_[album.directory_path] = pixbuf;
            g_object_ref(pixbuf);  // Keep one reference in cache
        }
        else
        {
            pixbuf_cache_[album.directory_path] = nullptr;
        }
    }
    
    if (pixbuf)
    {
        image = gtk_image_new_from_pixbuf(pixbuf);
        g_object_unref(pixbuf);  // Release our reference (image widget has its own)
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
    // Build items list
    Index<PlaylistAddItem> items;
    for (const auto& file : album.audio_files)
    {
        String uri = String(filename_to_uri(file.c_str()));
        items.append(uri);
    }
    
    if (clear_first)
    {
        // Left-click: Update the "Album" playlist
        // Find or create the "Album" playlist
        Playlist album_playlist;
        int n_playlists = Playlist::n_playlists();
        bool found = false;
        
        for (int i = 0; i < n_playlists; i++)
        {
            Playlist pl = Playlist::by_index(i);
            if (strcmp(pl.get_title(), "Album") == 0)
            {
                album_playlist = pl;
                found = true;
                break;
            }
        }
        
        // If not found, create it
        if (!found)
        {
            album_playlist = Playlist::new_playlist();
            album_playlist.set_title("Album");
        }
        
        // Check if we're currently playing from the "Album" playlist
        bool should_autoplay = false;
        Playlist playing_playlist = Playlist::playing_playlist();
        if (playing_playlist.exists() && strcmp(playing_playlist.get_title(), "Album") == 0)
        {
            // We're playing from Album playlist - autoplay the new album
            should_autoplay = true;
        }
        
        // Clear and add items to the Album playlist
        album_playlist.remove_all_entries();
        album_playlist.insert_items(0, std::move(items), should_autoplay);
    }
    else
    {
        // Right-click: add to current playlist
        auto playlist = Playlist::active_playlist();
        int insert_pos = playlist.n_entries();
        playlist.insert_items(insert_pos, std::move(items), false);
    }
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
    
    // Add directory chooser button
    GtkWidget* dir_button = gtk_button_new_with_label("~/Music");
    g_signal_connect(dir_button, "clicked", 
        G_CALLBACK(+[](GtkButton* button, gpointer data) {
            auto* plugin = static_cast<AlbumBrowserPlugin*>(data);
            
            GtkWidget* dialog = gtk_file_chooser_dialog_new(
                "Select Music Directory",
                nullptr,
                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                "_Cancel", GTK_RESPONSE_CANCEL,
                "_Select", GTK_RESPONSE_ACCEPT,
                nullptr);
            
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), 
                plugin->music_directory_.c_str());
            
            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
            {
                char* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
                if (folder)
                {
                    plugin->music_directory_ = folder;
                    
                    // Update button label with relative path
                    const char* home = getenv("HOME");
                    std::string label = plugin->music_directory_;
                    if (home && label.find(home) == 0)
                    {
                        label = "~" + label.substr(strlen(home));
                    }
                    gtk_button_set_label(button, label.c_str());
                    
                    g_free(folder);
                    
                    // Restart file monitoring with new directory
                    plugin->stop_file_monitor();
                    plugin->setup_file_monitor();
                    
                    // Rescan new directory
                    plugin->refresh_albums();
                }
            }
            
            gtk_widget_destroy(dialog);
        }), this);
    gtk_box_pack_start(GTK_BOX(toolbar), dir_button, FALSE, FALSE, 0);
    
    // Add search entry
    search_entry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry_), "Search albums...");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(search_entry_), GTK_ENTRY_ICON_PRIMARY, "edit-find");
    gtk_widget_set_size_request(search_entry_, 300, -1);
    
    // Connect search entry to filter function with debouncing
    g_signal_connect(search_entry_, "changed",
        G_CALLBACK(+[](GtkEntry* entry, gpointer data) {
            auto* plugin = static_cast<AlbumBrowserPlugin*>(data);
            const char* text = gtk_entry_get_text(entry);
            plugin->search_filter_ = text ? text : "";
            
            // Cancel previous timeout
            if (plugin->search_timeout_id_ > 0)
                g_source_remove(plugin->search_timeout_id_);
            
            // Debounce: wait 300ms after last keystroke before filtering
            plugin->search_timeout_id_ = g_timeout_add(300,
                +[](gpointer data) -> gboolean {
                    auto* plugin = static_cast<AlbumBrowserPlugin*>(data);
                    plugin->search_timeout_id_ = 0;
                    plugin->filter_albums();
                    return G_SOURCE_REMOVE;
                }, data);
        }), this);
    
    gtk_box_pack_start(GTK_BOX(toolbar), search_entry_, TRUE, TRUE, 10);
    
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
    
    // Add resize handler for dynamic column adjustment with debouncing to prevent hang
    g_signal_connect(scrolled_window_, "size-allocate",
        G_CALLBACK(+[](GtkWidget*, GdkRectangle*, gpointer data) {
            auto* plugin = static_cast<AlbumBrowserPlugin*>(data);
            
            // Cancel previous timeout
            if (plugin->relayout_timeout_id_ > 0)
                g_source_remove(plugin->relayout_timeout_id_);
            
            // Debounce: wait 100ms after last resize before relayouting
            plugin->relayout_timeout_id_ = g_timeout_add(100,
                +[](gpointer data) -> gboolean {
                    auto* plugin = static_cast<AlbumBrowserPlugin*>(data);
                    plugin->relayout_timeout_id_ = 0;
                    plugin->relayout_grid();
                    return G_SOURCE_REMOVE;
                }, data);
        }), this);
    
    gtk_container_add(GTK_CONTAINER(scrolled_window_), grid_view_);
    gtk_box_pack_start(GTK_BOX(main_widget_), scrolled_window_, TRUE, TRUE, 0);
    
    gtk_widget_show_all(main_widget_);
    
    // If we have cached albums, display them immediately
    if (!albums_.empty())
    {
        relayout_grid();
    }
    
    // Start scan in background (will update if anything changed)
    refresh_albums();
    
    return main_widget_;
}


void AlbumBrowserPlugin::setup_file_monitor()
{
    GFile* music_dir = g_file_new_for_path(music_directory_.c_str());
    GError* error = nullptr;
    
    file_monitor_ = g_file_monitor_directory(music_dir, G_FILE_MONITOR_WATCH_MOVES, nullptr, &error);
    
    if (error)
    {
        AUDWARN("Failed to setup file monitor for %s: %s\n", music_directory_.c_str(), error->message);
        g_error_free(error);
        g_object_unref(music_dir);
        return;
    }
    
    if (file_monitor_)
    {
        g_signal_connect(file_monitor_, "changed",
            G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent event, gpointer data) {
                auto* plugin = static_cast<AlbumBrowserPlugin*>(data);
                
                // Only trigger rescan on relevant events
                if (event == G_FILE_MONITOR_EVENT_CREATED ||
                    event == G_FILE_MONITOR_EVENT_DELETED ||
                    event == G_FILE_MONITOR_EVENT_MOVED_IN ||
                    event == G_FILE_MONITOR_EVENT_MOVED_OUT)
                {
                    // Debounce: cancel previous timeout and schedule new one
                    if (plugin->rescan_timeout_id_ > 0)
                        g_source_remove(plugin->rescan_timeout_id_);
                    
                    // Wait 2 seconds after last change before rescanning
                    plugin->rescan_timeout_id_ = g_timeout_add(2000,
                        +[](gpointer data) -> gboolean {
                            auto* plugin = static_cast<AlbumBrowserPlugin*>(data);
                            plugin->rescan_timeout_id_ = 0;
                            plugin->refresh_albums();
                            return G_SOURCE_REMOVE;
                        }, data);
                }
            }), this);
    }
    
    g_object_unref(music_dir);
}

void AlbumBrowserPlugin::stop_file_monitor()
{
    if (file_monitor_)
    {
        g_file_monitor_cancel(file_monitor_);
        g_object_unref(file_monitor_);
        file_monitor_ = nullptr;
    }
}

std::string AlbumBrowserPlugin::get_cache_path()
{
    const char* cache_dir = getenv("XDG_CACHE_HOME");
    std::string cache_path;
    
    if (cache_dir)
        cache_path = std::string(cache_dir) + "/audacious";
    else
    {
        const char* home = getenv("HOME");
        cache_path = std::string(home ? home : "") + "/.cache/audacious";
    }
    
    // Create cache directory if it doesn't exist
    g_mkdir_with_parents(cache_path.c_str(), 0755);
    
    return cache_path + "/album-browser-cache.dat";
}

void AlbumBrowserPlugin::save_cache()
{
    std::string cache_file = get_cache_path();
    std::ofstream out(cache_file, std::ios::binary);
    
    if (!out.is_open())
    {
        AUDWARN("Failed to open cache file for writing: %s\n", cache_file.c_str());
        return;
    }
    
    // Write version
    uint32_t version = 1;
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write music directory
    uint32_t dir_len = music_directory_.length();
    out.write(reinterpret_cast<const char*>(&dir_len), sizeof(dir_len));
    out.write(music_directory_.c_str(), dir_len);
    
    // Write number of albums
    uint32_t album_count = albums_.size();
    out.write(reinterpret_cast<const char*>(&album_count), sizeof(album_count));
    
    // Write each album
    for (const auto& album : albums_)
    {
        // Write directory_path
        uint32_t len = album.directory_path.length();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(album.directory_path.c_str(), len);
        
        // Write title
        len = album.title.length();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(album.title.c_str(), len);
        
        // Write artist
        len = album.artist.length();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(album.artist.c_str(), len);
        
        // Write year
        out.write(reinterpret_cast<const char*>(&album.year), sizeof(album.year));
        
        // Write cover_art_path
        len = album.cover_art_path.length();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(album.cover_art_path.c_str(), len);
        
        // Write audio_files count
        uint32_t file_count = album.audio_files.size();
        out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));
        
        // Write each audio file
        for (const auto& file : album.audio_files)
        {
            len = file.length();
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            out.write(file.c_str(), len);
        }
    }
    
    out.close();
}

void AlbumBrowserPlugin::load_cache()
{
    std::string cache_file = get_cache_path();
    std::ifstream in(cache_file, std::ios::binary);
    
    if (!in.is_open())
        return;  // No cache file, that's okay
    
    try {
        // Read version
        uint32_t version;
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != 1)
        {
            AUDWARN("Cache version mismatch, ignoring cache\n");
            return;
        }
        
        // Read music directory
        uint32_t dir_len;
        in.read(reinterpret_cast<char*>(&dir_len), sizeof(dir_len));
        std::string cached_dir(dir_len, '\0');
        in.read(&cached_dir[0], dir_len);
        
        // Only use cache if directory matches
        if (cached_dir != music_directory_)
        {
            AUDINFO("Music directory changed, ignoring cache\n");
            return;
        }
        
        // Read number of albums
        uint32_t album_count;
        in.read(reinterpret_cast<char*>(&album_count), sizeof(album_count));
        
        albums_.clear();
        albums_.reserve(album_count);
        
        // Read each album
        for (uint32_t i = 0; i < album_count; i++)
        {
            Album album;
            uint32_t len;
            
            // Read directory_path
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            album.directory_path.resize(len);
            in.read(&album.directory_path[0], len);
            
            // Read title
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            album.title.resize(len);
            in.read(&album.title[0], len);
            
            // Read artist
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            album.artist.resize(len);
            in.read(&album.artist[0], len);
            
            // Read year
            in.read(reinterpret_cast<char*>(&album.year), sizeof(album.year));
            
            // Read cover_art_path
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            album.cover_art_path.resize(len);
            in.read(&album.cover_art_path[0], len);
            
            // Read audio_files count
            uint32_t file_count;
            in.read(reinterpret_cast<char*>(&file_count), sizeof(file_count));
            
            // Read each audio file
            for (uint32_t j = 0; j < file_count; j++)
            {
                in.read(reinterpret_cast<char*>(&len), sizeof(len));
                std::string file(len, '\0');
                in.read(&file[0], len);
                album.audio_files.push_back(file);
            }
            
            albums_.push_back(album);
        }
        
        AUDINFO("Loaded %zu albums from cache\n", albums_.size());
    }
    catch (const std::exception& e) {
        AUDWARN("Failed to load cache: %s\n", e.what());
        albums_.clear();
    }
    
    in.close();
}
