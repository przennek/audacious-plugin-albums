/*
 * scanner.cc
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

#include "scanner.h"
#include "metadata.h"
#include <filesystem>
#include <algorithm>
#include <libaudcore/runtime.h>

namespace fs = std::filesystem;

Scanner::Scanner() : scanning_(false), cancel_requested_(false)
{
}

Scanner::~Scanner()
{
    cancel();
    if (scan_thread_.joinable())
        scan_thread_.join();
}

void Scanner::scan_async(const std::string& root_path, ScanCallback callback)
{
    if (scanning_)
        return;
    
    // Wait for previous thread to finish
    if (scan_thread_.joinable())
        scan_thread_.join();
    
    scanning_ = true;
    cancel_requested_ = false;
    
    scan_thread_ = std::thread([this, root_path, callback]() {
        std::vector<Album> albums = scan_directory_tree(root_path);
        scanning_ = false;
        
        if (!cancel_requested_ && callback)
            callback(albums);
    });
}

void Scanner::cancel()
{
    cancel_requested_ = true;
}

bool Scanner::is_scanning() const
{
    return scanning_;
}

bool Scanner::is_leaf_directory(const std::string& path)
{
    try {
        for (const auto& entry : fs::directory_iterator(path))
        {
            if (entry.is_directory())
                return false;
        }
        return true;
    }
    catch (const fs::filesystem_error& e) {
        AUDWARN("Cannot read directory %s: %s\n", path.c_str(), e.what());
        return false;
    }
}

bool Scanner::contains_audio_files(const std::string& path)
{
    try {
        for (const auto& entry : fs::directory_iterator(path))
        {
            if (entry.is_regular_file())
            {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                if (ext == ".flac" || ext == ".mp3")
                    return true;
            }
        }
        return false;
    }
    catch (const fs::filesystem_error& e) {
        AUDWARN("Cannot read directory %s: %s\n", path.c_str(), e.what());
        return false;
    }
}

std::string Scanner::find_cover_art(const std::string& path)
{
    // Search order: Cover.jpg, Folder.jpg, cover.jpg, folder.jpg, front.jpg
    const char* cover_names[] = {
        "Cover.jpg", "Folder.jpg", "cover.jpg", "folder.jpg", "front.jpg"
    };
    
    for (const char* name : cover_names)
    {
        fs::path cover_path = fs::path(path) / name;
        if (fs::exists(cover_path) && fs::is_regular_file(cover_path))
            return cover_path.string();
    }
    
    return "";
}

Album Scanner::create_album_from_directory(const std::string& path)
{
    Album album;
    album.directory_path = path;
    
    // Extract metadata from directory name
    extract_metadata(path, album);
    
    // Find cover art
    album.cover_art_path = find_cover_art(path);
    
    // Collect audio files
    try {
        for (const auto& entry : fs::directory_iterator(path))
        {
            if (entry.is_regular_file())
            {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                if (ext == ".flac" || ext == ".mp3")
                    album.audio_files.push_back(entry.path().string());
            }
        }
        
        // Sort audio files alphanumerically
        std::sort(album.audio_files.begin(), album.audio_files.end());
    }
    catch (const fs::filesystem_error& e) {
        AUDWARN("Cannot read directory %s: %s\n", path.c_str(), e.what());
    }
    
    return album;
}

std::vector<Album> Scanner::scan_directory_tree(const std::string& root)
{
    std::vector<Album> albums;
    
    if (!fs::exists(root) || !fs::is_directory(root))
    {
        AUDERR("Music directory does not exist: %s\n", root.c_str());
        return albums;
    }
    
    try {
        // Recursive directory traversal
        for (const auto& entry : fs::recursive_directory_iterator(root))
        {
            if (cancel_requested_)
                break;
            
            if (entry.is_directory())
            {
                std::string dir_path = entry.path().string();
                
                // Check if this is a leaf directory with audio files
                if (is_leaf_directory(dir_path) && contains_audio_files(dir_path))
                {
                    Album album = create_album_from_directory(dir_path);
                    if (!album.audio_files.empty())
                        albums.push_back(album);
                }
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        AUDWARN("Error scanning directory tree: %s\n", e.what());
    }
    
    // Sort albums by title
    std::sort(albums.begin(), albums.end(), [](const Album& a, const Album& b) {
        std::string title_a = a.title;
        std::string title_b = b.title;
        std::transform(title_a.begin(), title_a.end(), title_a.begin(), ::tolower);
        std::transform(title_b.begin(), title_b.end(), title_b.begin(), ::tolower);
        return title_a < title_b;
    });
    
    return albums;
}
