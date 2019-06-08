// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <optional>
#include "common/common_types.h"

namespace Core::Frontend {

class ParentalControlsApplet {
public:
    virtual ~ParentalControlsApplet();

    // Prompts the user to enter a PIN and calls the callback with whether or not it matches the
    // correct PIN. If the bool is passed, and the PIN was recently entered correctly, the frontend
    // should not prompt and simply return true.
    virtual void VerifyPIN(std::function<void(bool)> finished,
                           bool suspend_future_verification_temporarily) = 0;

    // Prompts the user to enter a PIN and calls the callback for correctness. Frontends can
    // optionally alert the user that this is to change parental controls settings.
    virtual void VerifyPINForSettings(std::function<void(bool)> finished) = 0;

    // Prompts the user to create a new PIN for pctl and stores it with the service.
    virtual void RegisterPIN(std::function<void()> finished) = 0;

    // Prompts the user to verify the current PIN and then store a new one into pctl.
    virtual void ChangePIN(std::function<void()> finished) = 0;
};

class DefaultParentalControlsApplet final : public ParentalControlsApplet {
public:
    ~DefaultParentalControlsApplet() override;

    void VerifyPIN(std::function<void(bool)> finished,
                   bool suspend_future_verification_temporarily) override;
    void VerifyPINForSettings(std::function<void(bool)> finished) override;
    void RegisterPIN(std::function<void()> finished) override;
    void ChangePIN(std::function<void()> finished) override;
};

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

class ECommerceApplet {
public:
    virtual ~ECommerceApplet();

    // Shows a page with application icons, description, name, and price.
    virtual void ShowApplicationInformation(std::function<void()> finished, u64 title_id,
                                            std::optional<u128> user_id = {},
                                            std::optional<bool> full_display = {},
                                            std::optional<std::string> extra_parameter = {}) = 0;

    // Shows a page with all of the add on content available for a game, with name, description, and
    // price.
    virtual void ShowAddOnContentList(std::function<void()> finished, u64 title_id,
                                      std::optional<u128> user_id = {},
                                      std::optional<bool> full_display = {}) = 0;

    // Shows a page with all of the subscriptions (recurring payments) for a game, with name,
    // description, price, and renewal period.
    virtual void ShowSubscriptionList(std::function<void()> finished, u64 title_id,
                                      std::optional<u128> user_id = {}) = 0;

    // Shows a page with a list of any additional game related purchasable items (DLC,
    // subscriptions, etc) for a particular game, with name, description, type, and price.
    virtual void ShowConsumableItemList(std::function<void()> finished, u64 title_id,
                                        std::optional<u128> user_id = {}) = 0;

    // Shows the home page of the shop.
    virtual void ShowShopHome(std::function<void()> finished, u128 user_id, bool full_display) = 0;

    // Shows the user settings page of the shop.
    virtual void ShowSettings(std::function<void()> finished, u128 user_id, bool full_display) = 0;
};

class DefaultECommerceApplet : public ECommerceApplet {
public:
    ~DefaultECommerceApplet() override;

    void ShowApplicationInformation(std::function<void()> finished, u64 title_id,
                                    std::optional<u128> user_id, std::optional<bool> full_display,
                                    std::optional<std::string> extra_parameter) override;
    void ShowAddOnContentList(std::function<void()> finished, u64 title_id,
                              std::optional<u128> user_id,
                              std::optional<bool> full_display) override;
    void ShowSubscriptionList(std::function<void()> finished, u64 title_id,
                              std::optional<u128> user_id) override;
    void ShowConsumableItemList(std::function<void()> finished, u64 title_id,
                                std::optional<u128> user_id) override;
    void ShowShopHome(std::function<void()> finished, u128 user_id, bool full_display) override;
    void ShowSettings(std::function<void()> finished, u128 user_id, bool full_display) override;
};

} // namespace Core::Frontend
