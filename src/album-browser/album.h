/*
 * album.h
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

#ifndef ALBUM_H
#define ALBUM_H

#include <string>
#include <vector>

struct Album {
    std::string directory_path;
    std::string title;
    std::string artist;
    int year;  // 0 if not available
    std::string cover_art_path;
    std::vector<std::string> audio_files;
    
    Album() : year(0) {}
    
    bool has_cover_art() const {
        return !cover_art_path.empty();
    }
    
    std::string get_display_title() const {
        return title.empty() ? directory_path : title;
    }
    
    std::string get_display_artist() const {
        return artist;
    }
};

#endif // ALBUM_H
