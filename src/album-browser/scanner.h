/*
 * scanner.h
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

#ifndef SCANNER_H
#define SCANNER_H

#include "album.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>

class Scanner {
public:
    using ScanCallback = std::function<void(std::vector<Album>)>;
    
    Scanner();
    ~Scanner();
    
    void scan_async(const std::string& root_path, ScanCallback callback);
    void cancel();
    bool is_scanning() const;
    
private:
    std::vector<Album> scan_directory_tree(const std::string& root);
    bool is_leaf_directory(const std::string& path);
    bool contains_audio_files(const std::string& path);
    Album create_album_from_directory(const std::string& path);
    std::string find_cover_art(const std::string& path);
    std::string extract_embedded_art(const std::string& audio_file);
    
    std::atomic<bool> scanning_;
    std::atomic<bool> cancel_requested_;
    std::thread scan_thread_;
};

#endif // SCANNER_H
