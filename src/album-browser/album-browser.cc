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

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QFileDialog>
#include <QPixmap>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>
#include <libaudcore/runtime.h>
#include <libaudcore/drct.h>
#include <libaudcore/playlist.h>
#include <libaudcore/audstrings.h>
#include <libaudcore/probe.h>
#include <libaudcore/hook.h>

#include <libaudqt/libaudqt.h>

#include "scanner.h"
#include "album.h"
#include <memory>
#include <fstream>
#include <algorithm>
#include <map>

class AlbumBrowserWidget;

class AlbumTile : public QWidget
{
    Q_OBJECT
    
public:
    AlbumTile(const Album& album, AlbumBrowserWidget* browser, QWidget* parent = nullptr);
    
    const Album& get_album() const { return album_; }
    
protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    
private:
    Album album_;
    AlbumBrowserWidget* browser_;
    bool hovered_ = false;
};

class AlbumBrowserWidget : public QWidget
{
    Q_OBJECT
    
public:
    AlbumBrowserWidget(QWidget* parent = nullptr);
    ~AlbumBrowserWidget();
    
    void refresh_albums();
    void update_albums(std::vector<Album> albums);
    void add_album_to_playlist(const Album& album, bool clear_first);
    
protected:
    void resizeEvent(QResizeEvent* event) override;
    
private slots:
    void on_search_changed(const QString& text);
    void on_dir_button_clicked();
    void relayout_grid();
    void filter_albums();
    
private:
    void setup_file_monitor();
    void stop_file_monitor();
    void save_cache();
    void load_cache();
    std::string get_cache_path();
    bool album_matches_filter(const Album& album, const std::string& filter);
    std::string to_lower(const std::string& str);
    QPixmap load_cover_art(const Album& album);
    
    std::unique_ptr<Scanner> scanner_;
    std::vector<Album> albums_;
    std::map<std::string, QPixmap> pixbuf_cache_;
    
    QWidget* grid_container_ = nullptr;
    QGridLayout* grid_layout_ = nullptr;
    QScrollArea* scroll_area_ = nullptr;
    QLineEdit* search_entry_ = nullptr;
    QPushButton* dir_button_ = nullptr;
    
    std::string music_directory_;
    std::string search_filter_;
    std::string last_search_filter_;
    int last_cols_per_row_ = 0;
    
    QTimer* relayout_timer_ = nullptr;
    QTimer* search_timer_ = nullptr;
};

class AlbumBrowserPlugin : public GeneralPlugin
{
public:
    static constexpr PluginInfo info = {
        N_("Album Browser"),
        PACKAGE,
        nullptr, // about
        nullptr, // prefs
        PluginQtOnly
    };

    constexpr AlbumBrowserPlugin () : GeneralPlugin (info, false) {}

    void * get_qt_widget () override;
};

EXPORT AlbumBrowserPlugin aud_plugin_instance;

// AlbumTile implementation
AlbumTile::AlbumTile(const Album& album, AlbumBrowserWidget* browser, QWidget* parent)
    : QWidget(parent), album_(album), browser_(browser)
{
    setFixedSize(200, 250);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    
    // Set style for hover effect
    setStyleSheet(
        "AlbumTile {"
        "  border-radius: 8px;"
        "  padding: 5px;"
        "}"
        "AlbumTile:hover {"
        "  background-color: rgba(128, 128, 255, 30);"
        "}"
    );
}

void AlbumTile::enterEvent(QEnterEvent*)
{
    hovered_ = true;
    update();
}

void AlbumTile::leaveEvent(QEvent*)
{
    hovered_ = false;
    update();
}

void AlbumTile::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        browser_->add_album_to_playlist(album_, true);
    }
    else if (event->button() == Qt::RightButton)
    {
        browser_->add_album_to_playlist(album_, false);
    }
    QWidget::mousePressEvent(event);
}

// AlbumBrowserWidget implementation
AlbumBrowserWidget::AlbumBrowserWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("AlbumBrowserWidget");
    
    scanner_ = std::make_unique<Scanner>();
    
    // Get music directory
    const char* home = getenv("HOME");
    music_directory_ = std::string(home ? home : "") + "/Music";
    
    // Create main layout
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(5, 5, 5, 5);
    
    // Create toolbar
    auto* toolbar = new QHBoxLayout();
    
    // Directory button
    dir_button_ = new QPushButton("~/Music", this);
    connect(dir_button_, &QPushButton::clicked, this, &AlbumBrowserWidget::on_dir_button_clicked);
    toolbar->addWidget(dir_button_);
    
    // Search entry
    search_entry_ = new QLineEdit(this);
    search_entry_->setPlaceholderText("Search albums...");
    search_entry_->setMinimumWidth(300);
    connect(search_entry_, &QLineEdit::textChanged, this, &AlbumBrowserWidget::on_search_changed);
    toolbar->addWidget(search_entry_, 1);
    
    main_layout->addLayout(toolbar);
    
    // Create scroll area
    scroll_area_ = new QScrollArea(this);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // Create grid container
    grid_container_ = new QWidget();
    grid_layout_ = new QGridLayout(grid_container_);
    grid_layout_->setSpacing(10);
    grid_layout_->setContentsMargins(10, 10, 10, 10);
    
    scroll_area_->setWidget(grid_container_);
    main_layout->addWidget(scroll_area_);
    
    // Create timers
    relayout_timer_ = new QTimer(this);
    relayout_timer_->setSingleShot(true);
    relayout_timer_->setInterval(100);
    connect(relayout_timer_, &QTimer::timeout, this, &AlbumBrowserWidget::relayout_grid);
    
    search_timer_ = new QTimer(this);
    search_timer_->setSingleShot(true);
    search_timer_->setInterval(300);
    connect(search_timer_, &QTimer::timeout, this, &AlbumBrowserWidget::filter_albums);
    
    // Load cache and start scan
    load_cache();
    if (!albums_.empty())
        relayout_grid();
    
    refresh_albums();
}

AlbumBrowserWidget::~AlbumBrowserWidget()
{
    if (scanner_)
        scanner_->cancel();
    
    stop_file_monitor();
}

void AlbumBrowserWidget::refresh_albums()
{
    if (scanner_->is_scanning())
        return;
    
    scanner_->scan_async(music_directory_, [this](std::vector<Album> albums) {
        QMetaObject::invokeMethod(this, [this, albums]() {
            update_albums(albums);
        }, Qt::QueuedConnection);
    });
}

void AlbumBrowserWidget::update_albums(std::vector<Album> albums)
{
    albums_ = albums;
    pixbuf_cache_.clear();
    save_cache();
    relayout_grid();
}

void AlbumBrowserWidget::resizeEvent(QResizeEvent*)
{
    relayout_timer_->start();
}

void AlbumBrowserWidget::on_search_changed(const QString& text)
{
    search_filter_ = text.toStdString();
    search_timer_->start();
}

void AlbumBrowserWidget::on_dir_button_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Music Directory"),
        QString::fromStdString(music_directory_)
    );
    
    if (!dir.isEmpty())
    {
        music_directory_ = dir.toStdString();
        
        // Update button label
        QString home = QDir::homePath();
        QString label = dir;
        if (label.startsWith(home))
            label = "~" + label.mid(home.length());
        dir_button_->setText(label);
        
        stop_file_monitor();
        setup_file_monitor();
        refresh_albums();
    }
}

void AlbumBrowserWidget::relayout_grid()
{
    if (albums_.empty())
        return;
    
    int window_width = scroll_area_->viewport()->width();
    const int tile_width = 220;
    int cols_per_row = std::max(1, window_width / tile_width);
    
    bool filter_changed = (search_filter_ != last_search_filter_);
    bool columns_changed = (cols_per_row != last_cols_per_row_);
    
    if (!filter_changed && !columns_changed)
        return;
    
    last_cols_per_row_ = cols_per_row;
    last_search_filter_ = search_filter_;
    
    // Clear existing grid
    while (grid_layout_->count() > 0)
    {
        QLayoutItem* item = grid_layout_->takeAt(0);
        if (item->widget())
            delete item->widget();
        delete item;
    }
    
    // Add matching album tiles
    int row = 0, col = 0;
    for (const auto& album : albums_)
    {
        if (album_matches_filter(album, search_filter_))
        {
            auto* tile = new AlbumTile(album, this);
            auto* tile_layout = new QVBoxLayout(tile);
            tile_layout->setSpacing(5);
            tile_layout->setContentsMargins(5, 5, 5, 5);
            
            // Cover art
            QPixmap pixmap = load_cover_art(album);
            auto* image_label = new QLabel();
            if (!pixmap.isNull())
                image_label->setPixmap(pixmap.scaled(180, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            else
                image_label->setText("[No Cover]");
            image_label->setAlignment(Qt::AlignCenter);
            tile_layout->addWidget(image_label);
            
            // Title
            auto* title_label = new QLabel(QString::fromStdString(album.title));
            title_label->setWordWrap(true);
            title_label->setAlignment(Qt::AlignCenter);
            tile_layout->addWidget(title_label);
            
            // Artist
            if (!album.artist.empty())
            {
                auto* artist_label = new QLabel(QString::fromStdString(album.artist));
                artist_label->setWordWrap(true);
                artist_label->setAlignment(Qt::AlignCenter);
                artist_label->setStyleSheet("color: gray; font-size: 90%;");
                tile_layout->addWidget(artist_label);
            }
            
            tile_layout->addStretch();
            
            grid_layout_->addWidget(tile, row, col);
            
            col++;
            if (col >= cols_per_row)
            {
                col = 0;
                row++;
            }
        }
    }
    
    grid_layout_->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding), row + 1, 0);
}

void AlbumBrowserWidget::filter_albums()
{
    relayout_grid();
}

std::string AlbumBrowserWidget::to_lower(const std::string& str)
{
    return QString::fromStdString(str).toLower().toStdString();
}

bool AlbumBrowserWidget::album_matches_filter(const Album& album, const std::string& filter)
{
    if (filter.empty())
        return true;
    
    std::string filter_lower = to_lower(filter);
    
    if (to_lower(album.title).find(filter_lower) != std::string::npos)
        return true;
    
    if (to_lower(album.artist).find(filter_lower) != std::string::npos)
        return true;
    
    if (to_lower(album.directory_path).find(filter_lower) != std::string::npos)
        return true;
    
    if (album.year > 0)
    {
        std::string year_str = std::to_string(album.year);
        if (year_str.find(filter) != std::string::npos)
            return true;
    }
    
    return false;
}

QPixmap AlbumBrowserWidget::load_cover_art(const Album& album)
{
    // Check cache first
    auto cache_it = pixbuf_cache_.find(album.directory_path);
    if (cache_it != pixbuf_cache_.end())
        return cache_it->second;
    
    QPixmap pixmap;
    
    // Try loading from file
    if (album.has_cover_art())
    {
        pixmap.load(QString::fromStdString(album.cover_art_path));
    }
    
    // Try embedded art
    if (pixmap.isNull() && !album.audio_files.empty())
    {
        String uri = String(filename_to_uri(album.audio_files[0].c_str()));
        AudArtPtr art = aud_art_request(uri, AUD_ART_DATA);
        
        if (art)
        {
            auto data = art.data();
            if (data && data->len() > 0)
            {
                pixmap.loadFromData(reinterpret_cast<const uchar*>(data->begin()), data->len());
            }
        }
    }
    
    // Cache it
    pixbuf_cache_[album.directory_path] = pixmap;
    
    return pixmap;
}

void AlbumBrowserWidget::add_album_to_playlist(const Album& album, bool clear_first)
{
    Index<PlaylistAddItem> items;
    for (const auto& file : album.audio_files)
    {
        String uri = String(filename_to_uri(file.c_str()));
        items.append(uri);
    }
    
    if (clear_first)
    {
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
        
        if (!found)
        {
            album_playlist = Playlist::new_playlist();
            album_playlist.set_title("Album");
        }
        
        bool should_autoplay = false;
        Playlist playing_playlist = Playlist::playing_playlist();
        if (playing_playlist.exists() && strcmp(playing_playlist.get_title(), "Album") == 0)
            should_autoplay = true;
        
        album_playlist.remove_all_entries();
        album_playlist.insert_items(0, std::move(items), should_autoplay);
    }
    else
    {
        auto playlist = Playlist::active_playlist();
        int insert_pos = playlist.n_entries();
        playlist.insert_items(insert_pos, std::move(items), false);
    }
}

void AlbumBrowserWidget::setup_file_monitor()
{
    // Qt file monitoring would go here
    // For now, skip it
}

void AlbumBrowserWidget::stop_file_monitor()
{
    // Qt file monitoring cleanup
}

std::string AlbumBrowserWidget::get_cache_path()
{
    QString cache_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cache_dir);
    return (cache_dir + "/album-browser-cache.dat").toStdString();
}

void AlbumBrowserWidget::save_cache()
{
    std::string cache_file = get_cache_path();
    std::ofstream out(cache_file, std::ios::binary);
    
    if (!out.is_open())
        return;
    
    uint32_t version = 1;
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    uint32_t dir_len = music_directory_.length();
    out.write(reinterpret_cast<const char*>(&dir_len), sizeof(dir_len));
    out.write(music_directory_.c_str(), dir_len);
    
    uint32_t album_count = albums_.size();
    out.write(reinterpret_cast<const char*>(&album_count), sizeof(album_count));
    
    for (const auto& album : albums_)
    {
        uint32_t len = album.directory_path.length();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(album.directory_path.c_str(), len);
        
        len = album.title.length();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(album.title.c_str(), len);
        
        len = album.artist.length();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(album.artist.c_str(), len);
        
        out.write(reinterpret_cast<const char*>(&album.year), sizeof(album.year));
        
        len = album.cover_art_path.length();
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(album.cover_art_path.c_str(), len);
        
        uint32_t file_count = album.audio_files.size();
        out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));
        
        for (const auto& file : album.audio_files)
        {
            len = file.length();
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            out.write(file.c_str(), len);
        }
    }
    
    out.close();
}

void AlbumBrowserWidget::load_cache()
{
    std::string cache_file = get_cache_path();
    std::ifstream in(cache_file, std::ios::binary);
    
    if (!in.is_open())
        return;
    
    try {
        uint32_t version;
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != 1)
            return;
        
        uint32_t dir_len;
        in.read(reinterpret_cast<char*>(&dir_len), sizeof(dir_len));
        std::string cached_dir(dir_len, '\0');
        in.read(&cached_dir[0], dir_len);
        
        if (cached_dir != music_directory_)
            return;
        
        uint32_t album_count;
        in.read(reinterpret_cast<char*>(&album_count), sizeof(album_count));
        
        albums_.clear();
        albums_.reserve(album_count);
        
        for (uint32_t i = 0; i < album_count; i++)
        {
            Album album;
            uint32_t len;
            
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            album.directory_path.resize(len);
            in.read(&album.directory_path[0], len);
            
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            album.title.resize(len);
            in.read(&album.title[0], len);
            
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            album.artist.resize(len);
            in.read(&album.artist[0], len);
            
            in.read(reinterpret_cast<char*>(&album.year), sizeof(album.year));
            
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            album.cover_art_path.resize(len);
            in.read(&album.cover_art_path[0], len);
            
            uint32_t file_count;
            in.read(reinterpret_cast<char*>(&file_count), sizeof(file_count));
            
            for (uint32_t j = 0; j < file_count; j++)
            {
                in.read(reinterpret_cast<char*>(&len), sizeof(len));
                std::string file(len, '\0');
                in.read(&file[0], len);
                album.audio_files.push_back(file);
            }
            
            albums_.push_back(album);
        }
    }
    catch (const std::exception&) {
        albums_.clear();
    }
    
    in.close();
}

void * AlbumBrowserPlugin::get_qt_widget()
{
    return new AlbumBrowserWidget();
}

#include "album-browser.moc"
