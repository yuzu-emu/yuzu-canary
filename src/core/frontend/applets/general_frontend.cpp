// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/frontend/applets/general_frontend.h"

namespace Core::Frontend {

ParentalControlsApplet::~ParentalControlsApplet() = default;

DefaultParentalControlsApplet::~DefaultParentalControlsApplet() = default;

void DefaultParentalControlsApplet::VerifyPIN(std::function<void(bool)> finished,
                                              bool suspend_future_verification_temporarily) {
    LOG_INFO(Service_AM,
             "Application requested frontend to verify PIN (normal), "
             "suspend_future_verification_temporarily={}, verifying as correct.",
             suspend_future_verification_temporarily);
    finished(true);
}

void DefaultParentalControlsApplet::VerifyPINForSettings(std::function<void(bool)> finished) {
    LOG_INFO(Service_AM,
             "Application requested frontend to verify PIN (settings), verifying as correct.");
    finished(true);
}

void DefaultParentalControlsApplet::RegisterPIN(std::function<void()> finished) {
    LOG_INFO(Service_AM, "Application requested frontend to register new PIN");
    finished();
}

void DefaultParentalControlsApplet::ChangePIN(std::function<void()> finished) {
    LOG_INFO(Service_AM, "Application requested frontend to change PIN to new value");
    finished();
}

PhotoViewerApplet::~PhotoViewerApplet() = default;

DefaultPhotoViewerApplet::~DefaultPhotoViewerApplet() = default;

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

ECommerceApplet::~ECommerceApplet() = default;

DefaultECommerceApplet::~DefaultECommerceApplet() = default;

void DefaultECommerceApplet::ShowApplicationInformation(
    std::function<void()> finished, u64 title_id, std::optional<u128> user_id,
    std::optional<bool> full_display, std::optional<std::string> extra_parameter) {
    const auto value = user_id.value_or(u128{});
    LOG_INFO(Service_AM,
             "Application requested frontend show application information for EShop, "
             "title_id={:016X}, user_id={:016X}{:016X}, full_display={}, extra_parameter={}",
             title_id, value[1], value[0],
             full_display.has_value() ? fmt::format("{}", *full_display) : "null",
             extra_parameter.value_or("null"));
    finished();
}

void DefaultECommerceApplet::ShowAddOnContentList(std::function<void()> finished, u64 title_id,
                                                  std::optional<u128> user_id,
                                                  std::optional<bool> full_display) {
    const auto value = user_id.value_or(u128{});
    LOG_INFO(Service_AM,
             "Application requested frontend show add on content list for EShop, "
             "title_id={:016X}, user_id={:016X}{:016X}, full_display={}",
             title_id, value[1], value[0],
             full_display.has_value() ? fmt::format("{}", *full_display) : "null");
    finished();
}

void DefaultECommerceApplet::ShowSubscriptionList(std::function<void()> finished, u64 title_id,
                                                  std::optional<u128> user_id) {
    const auto value = user_id.value_or(u128{});
    LOG_INFO(Service_AM,
             "Application requested frontend show subscription list for EShop, title_id={:016X}, "
             "user_id={:016X}{:016X}",
             title_id, value[1], value[0]);
    finished();
}

void DefaultECommerceApplet::ShowConsumableItemList(std::function<void()> finished, u64 title_id,
                                                    std::optional<u128> user_id) {
    const auto value = user_id.value_or(u128{});
    LOG_INFO(
        Service_AM,
        "Application requested frontend show consumable item list for EShop, title_id={:016X}, "
        "user_id={:016X}{:016X}",
        title_id, value[1], value[0]);
    finished();
}

void DefaultECommerceApplet::ShowShopHome(std::function<void()> finished, u128 user_id,
                                          bool full_display) {
    LOG_INFO(Service_AM,
             "Application requested frontend show home menu for EShop, user_id={:016X}{:016X}, "
             "full_display={}",
             user_id[1], user_id[0], full_display);
    finished();
}

void DefaultECommerceApplet::ShowSettings(std::function<void()> finished, u128 user_id,
                                          bool full_display) {
    LOG_INFO(Service_AM,
             "Application requested frontend show settings menu for EShop, user_id={:016X}{:016X}, "
             "full_display={}",
             user_id[1], user_id[0], full_display);
    finished();
}

} // namespace Core::Frontend
