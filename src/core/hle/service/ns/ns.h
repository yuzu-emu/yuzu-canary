// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IAccountProxyInterface final : public ServiceFramework<IAccountProxyInterface> {
public:
    explicit IAccountProxyInterface();
    ~IAccountProxyInterface();
};

class IApplicationManagerInterface final : public ServiceFramework<IApplicationManagerInterface> {
public:
    explicit IApplicationManagerInterface();
    ~IApplicationManagerInterface();

    ResultVal<u8> GetApplicationDesiredLanguage(u32 supported_languages);
    ResultVal<u64> ConvertApplicationLanguageToLanguageCode(u8 application_language);

private:
    void GetApplicationControlData(Kernel::HLERequestContext& ctx);
    void GetApplicationDesiredLanguage(Kernel::HLERequestContext& ctx);
    void ConvertApplicationLanguageToLanguageCode(Kernel::HLERequestContext& ctx);
};

class IApplicationVersionInterface final : public ServiceFramework<IApplicationVersionInterface> {
public:
    explicit IApplicationVersionInterface();
    ~IApplicationVersionInterface();
};

class IContentManagerInterface final : public ServiceFramework<IContentManagerInterface> {
public:
    explicit IContentManagerInterface();
    ~IContentManagerInterface();
};

class IDocumentInterface final : public ServiceFramework<IDocumentInterface> {
public:
    explicit IDocumentInterface();
    ~IDocumentInterface();
};

class IDownloadTaskInterface final : public ServiceFramework<IDownloadTaskInterface> {
public:
    explicit IDownloadTaskInterface();
    ~IDownloadTaskInterface();
};

class IECommerceInterface final : public ServiceFramework<IECommerceInterface> {
public:
    explicit IECommerceInterface();
    ~IECommerceInterface();
};

class IFactoryResetInterface final : public ServiceFramework<IFactoryResetInterface> {
public:
    explicit IFactoryResetInterface();
    ~IFactoryResetInterface();
};

class NS final : public ServiceFramework<NS> {
public:
    explicit NS(const char* name);
    ~NS();

    std::shared_ptr<IApplicationManagerInterface> GetApplicationManagerInterface() const;

private:
    template <typename T>
    void PushInterface(Kernel::HLERequestContext& ctx) {
        LOG_DEBUG(Service_NS, "called");

        IPC::ResponseBuilder rb{ctx, 2, 0, 1};
        rb.Push(RESULT_SUCCESS);
        rb.PushIpcInterface<T>();
    }

    template <typename T>
    std::shared_ptr<T> GetInterface() const {
        static_assert(std::is_base_of_v<Kernel::SessionRequestHandler, T>,
                      "Not a base of ServiceFrameworkBase");

        return std::make_shared<T>();
    }
};

/// Registers all NS services with the specified service manager.
void InstallInterfaces(SM::ServiceManager& service_manager);

} // namespace Service::NS
