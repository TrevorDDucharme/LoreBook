#pragma once
#include <functional>
#include <string>
#include "Vault.hpp"

namespace LoreBook {

struct DBConnectionInfo;

struct VaultSync {
    // progressCb: (percent [-1 if not applicable], message)
    static void startUpload(Vault* localVault, const DBConnectionInfo &remoteCI, bool dryRun, int64_t uploaderUserID, std::function<void(int,const std::string&)> progressCb);
};

} // namespace LoreBook
