// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/frontend/applets/general_frontend.h"

namespace Core::Frontend {

PhotoViewerApplet::~PhotoViewerApplet() = default;

DefaultPhotoViewerApplet::~DefaultPhotoViewerApplet() {}

void DefaultPhotoViewerApplet::ShowPhotosForApplication(u64 title_id,
                                                        std::function<void()> finished) const {
    LOG_INFO(Service_AM,
             "Application requested frontend to display stored photos for title_id={:016X}",
             title_id);
    finished();
}

void DefaultPhotoViewerApplet::ShowAllPhotos(std::function<void()> finished) const {
    LOG_INFO(Service_AM, "Application requested frontend to display all stored photos.");
    finished();
}

} // namespace Core::Frontend
