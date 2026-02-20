/*
 * metadata.cc
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

#include "metadata.h"
#include <regex>
#include <filesystem>

namespace fs = std::filesystem;

void extract_metadata(const std::string& directory_path, Album& album)
{
    fs::path path(directory_path);
    
    // Get the album directory name (leaf)
    std::string album_name = path.filename().string();
    
    // Get the parent directory name (potential artist)
    std::string parent_name;
    if (path.has_parent_path() && path.parent_path().has_filename())
    {
        parent_name = path.parent_path().filename().string();
    }
    
    // Pattern 1: Extract year from album name: "(YYYY) Album Name"
    std::regex year_pattern(R"(^\((\d{4})\)\s+(.+)$)");
    std::smatch year_match;
    
    if (std::regex_match(album_name, year_match, year_pattern))
    {
        // Extract year and album title
        album.year = std::stoi(year_match[1].str());
        album.title = year_match[2].str();
    }
    else
    {
        // No year pattern, use full album name as title
        album.title = album_name;
        album.year = 0;
    }
    
    // Pattern 2: Extract artist from parent directory
    // Only use parent as artist if it's not a common root directory name
    if (!parent_name.empty() && 
        parent_name != "Music" && 
        parent_name != "music" &&
        parent_name != "Albums" &&
        parent_name != "albums")
    {
        album.artist = parent_name;
    }
    else
    {
        album.artist = "";
    }
}
