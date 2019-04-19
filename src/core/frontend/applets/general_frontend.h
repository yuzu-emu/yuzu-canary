// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include "common/common_types.h"

namespace Core::Frontend {

class PhotoViewerApplet {
public:
    virtual ~PhotoViewerApplet();

    virtual void ShowPhotosForApplication(u64 title_id, std::function<void()> finished) const = 0;
    virtual void ShowAllPhotos(std::function<void()> finished) const = 0;
};

class DefaultPhotoViewerApplet final : public PhotoViewerApplet {
public:
    ~DefaultPhotoViewerApplet() override;

    void ShowPhotosForApplication(u64 title_id, std::function<void()> finished) const override;
    void ShowAllPhotos(std::function<void()> finished) const override;
};

} // namespace Core::Frontend
