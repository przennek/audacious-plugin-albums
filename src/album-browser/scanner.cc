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
#include <fstream>
#include <libaudcore/runtime.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/id3v2frame.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/flacpicture.h>

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
                std::string filename = entry.path().filename().string();
                
                // Skip macOS metadata files (._filename)
                if (filename.length() > 2 && filename[0] == '.' && filename[1] == '_')
                    continue;
                
                // Skip hidden files
                if (filename[0] == '.')
                    continue;
                
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                // Only check for actual audio files (not .cue or other metadata)
                if (ext == ".flac" || ext == ".mp3" || ext == ".ogg" || 
                    ext == ".opus" || ext == ".m4a" || ext == ".aac" ||
                    ext == ".wav" || ext == ".wv" || ext == ".ape")
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
    // First try common cover art names with priority
    const char* cover_names[] = {
        // JPG variants
        "Cover.jpg", "Folder.jpg", "cover.jpg", "folder.jpg", "front.jpg", "Front.jpg",
        "album.jpg", "Album.jpg", "artwork.jpg", "Artwork.jpg",
        // PNG variants
        "Cover.png", "Folder.png", "cover.png", "folder.png", "front.png", "Front.png",
        "album.png", "Album.png", "artwork.png", "Artwork.png",
        // JPEG variants
        "Cover.jpeg", "cover.jpeg", "Folder.jpeg", "folder.jpeg",
        // Other common names
        "cover.JPG", "COVER.JPG", "folder.JPG", "FOLDER.JPG"
    };
    
    for (const char* name : cover_names)
    {
        fs::path cover_path = fs::path(path) / name;
        if (fs::exists(cover_path) && fs::is_regular_file(cover_path))
            return cover_path.string();
    }
    
    // If no common name found, look for ANY image file
    try {
        for (const auto& entry : fs::directory_iterator(path))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                
                // Skip macOS metadata files
                if (filename.length() > 2 && filename[0] == '.' && filename[1] == '_')
                    continue;
                
                // Skip hidden files
                if (filename[0] == '.')
                    continue;
                
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                // Check for image extensions
                if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || 
                    ext == ".gif" || ext == ".bmp" || ext == ".webp")
                {
                    return entry.path().string();
                }
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        AUDWARN("Cannot read directory for cover art %s: %s\n", path.c_str(), e.what());
    }
    
    return "";
}

std::string Scanner::extract_embedded_art(const std::string& audio_file)
{
    try {
        std::string ext = fs::path(audio_file).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".flac")
        {
            TagLib::FLAC::File file(audio_file.c_str());
            if (file.isValid() && !file.pictureList().isEmpty())
            {
                const TagLib::FLAC::Picture* picture = file.pictureList().front();
                TagLib::ByteVector data = picture->data();
                
                if (data.size() > 0)
                {
                    // Create temp file in /tmp
                    std::string temp_path = "/tmp/audacious_cover_" + 
                        std::to_string(std::hash<std::string>{}(audio_file)) + ".jpg";
                    
                    std::ofstream out(temp_path, std::ios::binary);
                    out.write(data.data(), data.size());
                    out.close();
                    
                    return temp_path;
                }
            }
        }
        else if (ext == ".mp3")
        {
            TagLib::MPEG::File file(audio_file.c_str());
            if (file.isValid() && file.ID3v2Tag())
            {
                TagLib::ID3v2::FrameList frames = file.ID3v2Tag()->frameListMap()["APIC"];
                if (!frames.isEmpty())
                {
                    TagLib::ID3v2::AttachedPictureFrame* frame = 
                        static_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
                    TagLib::ByteVector data = frame->picture();
                    
                    if (data.size() > 0)
                    {
                        // Create temp file in /tmp
                        std::string temp_path = "/tmp/audacious_cover_" + 
                            std::to_string(std::hash<std::string>{}(audio_file)) + ".jpg";
                        
                        std::ofstream out(temp_path, std::ios::binary);
                        out.write(data.data(), data.size());
                        out.close();
                        
                        return temp_path;
                    }
                }
            }
        }
    }
    catch (const std::exception& e) {
        AUDWARN("Failed to extract embedded art from %s: %s\n", audio_file.c_str(), e.what());
    }
    
    return "";
}

Album Scanner::create_album_from_directory(const std::string& path)
{
    Album album;
    album.directory_path = path;
    
    // Extract metadata from directory name
    extract_metadata(path, album);
    
    // Find cover art (file-based first)
    album.cover_art_path = find_cover_art(path);
    
    // Collect audio files
    try {
        for (const auto& entry : fs::directory_iterator(path))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                
                // Skip macOS metadata files (._filename)
                if (filename.length() > 2 && filename[0] == '.' && filename[1] == '_')
                    continue;
                
                // Skip hidden files
                if (filename[0] == '.')
                    continue;
                
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                // Only include actual audio files (exclude .cue and other metadata)
                if (ext == ".flac" || ext == ".mp3" || ext == ".ogg" || 
                    ext == ".opus" || ext == ".m4a" || ext == ".aac" ||
                    ext == ".wav" || ext == ".wv" || ext == ".ape")
                {
                    // Verify file actually exists and is accessible
                    std::string file_path = entry.path().string();
                    if (fs::exists(file_path) && fs::is_regular_file(file_path))
                    {
                        album.audio_files.push_back(file_path);
                    }
                }
            }
        }
        
        // Sort audio files alphanumerically
        std::sort(album.audio_files.begin(), album.audio_files.end());
        
        // If no file-based cover art found, try extracting from first audio file
        if (album.cover_art_path.empty() && !album.audio_files.empty())
        {
            album.cover_art_path = extract_embedded_art(album.audio_files[0]);
        }
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
    
    // First pass: collect all potential album directories
    std::vector<std::string> album_dirs;
    
    try {
        // Recursive directory traversal - just collect paths
        for (const auto& entry : fs::recursive_directory_iterator(root, 
            fs::directory_options::skip_permission_denied))
        {
            if (cancel_requested_)
                break;
            
            if (entry.is_directory())
            {
                std::string dir_path = entry.path().string();
                
                // Quick check: is this a leaf directory with audio files?
                if (is_leaf_directory(dir_path) && contains_audio_files(dir_path))
                {
                    album_dirs.push_back(dir_path);
                }
            }
        }
    }
    catch (const fs::filesystem_error& e) {
        AUDWARN("Error scanning directory tree: %s\n", e.what());
    }
    
    // Second pass: process albums (this is the slow part with metadata extraction)
    albums.reserve(album_dirs.size());
    for (const auto& dir : album_dirs)
    {
        if (cancel_requested_)
            break;
            
        Album album = create_album_from_directory(dir);
        if (!album.audio_files.empty())
            albums.push_back(album);
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
