// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/common_types.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/submission_package.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/deconstructed_rom_directory.h"
#include "core/loader/nca.h"
#include "core/loader/nsp.h"

namespace Loader {

namespace {

std::string PackageStem(const FileSys::VirtualFile& package) {
    if (package == nullptr) {
        return {};
    }
    const std::string filename = package->GetName();
    const auto dot = filename.find_last_of('.');
    return dot == std::string::npos ? filename : filename.substr(0, dot);
}

FileSys::VirtualFile FindPackageSidecar(const FileSys::VirtualFile& package,
                                        std::span<const std::string_view> names,
                                        std::span<const std::string_view> extensions) {
    if (package == nullptr) {
        return nullptr;
    }

    const auto parent = package->GetContainingDirectory();
    if (parent == nullptr) {
        return nullptr;
    }

    const std::string stem = PackageStem(package);
    for (const auto ext : extensions) {
        if (auto match = parent->GetFile(stem + "." + std::string(ext))) {
            return match;
        }
    }
    for (const auto name : names) {
        if (auto match = parent->GetFile(name)) {
            return match;
        }
    }

    const auto files = parent->GetFiles();
    const auto iter = std::find_if(files.begin(), files.end(), [&](const FileSys::VirtualFile& candidate) {
        const auto ext = Common::ToLower(candidate->GetExtension());
        return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
    });
    return iter == files.end() ? nullptr : *iter;
}

} // namespace

AppLoader_NSP::AppLoader_NSP(FileSys::VirtualFile file_,
                             const Service::FileSystem::FileSystemController& fsc,
                             const FileSys::ContentProvider& content_provider, u64 program_id,
                             std::size_t program_index)
    : AppLoader(file_), nsp(std::make_unique<FileSys::NSP>(file_, program_id, program_index)) {

    if (nsp->GetStatus() != ResultStatus::Success) {
        return;
    }

    if (nsp->IsExtractedType()) {
        secondary_loader =
            std::make_unique<AppLoader_DeconstructedRomDirectory>(nsp->GetExeFS(), false, true);
    } else {
        const auto control_nca =
            nsp->GetNCA(FileSys::GetBaseTitleID(nsp->GetProgramTitleID()), FileSys::ContentRecordType::Control);
        if (control_nca == nullptr || control_nca->GetStatus() != ResultStatus::Success) {
            return;
        }

        std::tie(nacp_file, icon_file) = [this, &content_provider, &control_nca, &fsc] {
            const FileSys::PatchManager pm{FileSys::GetBaseTitleID(nsp->GetProgramTitleID()), fsc, content_provider};
            return pm.ParseControlNCA(*control_nca);
        }();

        secondary_loader = std::make_unique<AppLoader_NCA>(
            nsp->GetNCAFile(nsp->GetProgramTitleID(), FileSys::ContentRecordType::Program));
    }
}

AppLoader_NSP::~AppLoader_NSP() = default;

FileType AppLoader_NSP::IdentifyType(const FileSys::VirtualFile& nsp_file) {
    // .nsz is NSP-compatible; return NSZ so the game list shows the right type.
    if (nsp_file->GetName().ends_with(".nsz")) {
        return FileType::NSZ;
    }

    // PFS0-shaped .xcz containers are handled by this loader too; report XCZ so the game
    // list does not show them as NSP. XCI-shaped .xcz is identified by AppLoader_XCI.
    if (nsp_file->GetName().ends_with(".xcz")) {
        return FileType::XCZ;
    }

    const FileSys::NSP nsp(nsp_file);

    if (nsp.GetStatus() != ResultStatus::Success) {
        return FileType::Error;
    }

    // Extracted Type case
    if (nsp.IsExtractedType() && nsp.GetExeFS() != nullptr &&
        FileSys::IsDirectoryExeFS(nsp.GetExeFS())) {
        return FileType::NSP;
    }

    // Non-extracted NSPs can legitimately contain only update/DLC content.
    // Identify the container format itself; bootability is validated by Load().
    if (!nsp.GetNCAs().empty()) {
        return FileType::NSP;
    }

    // Fallback when NCAs couldn't be parsed (e.g. missing keys) but the PFS still contains NCAs.
    for (const auto& entry : nsp.GetFiles()) {
        if (entry == nullptr) {
            continue;
        }

        const auto& name = entry->GetName();
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".nca") {
            return FileType::NSP;
        }
    }

    return FileType::Error;
}

AppLoader_NSP::LoadResult AppLoader_NSP::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    const auto title_id = nsp->GetProgramTitleID();

    if (!nsp->IsExtractedType() && title_id == 0) {
        return {ResultStatus::ErrorNSPMissingProgramNCA, {}};
    }

    const auto nsp_status = nsp->GetStatus();
    if (nsp_status != ResultStatus::Success) {
        return {nsp_status, {}};
    }

    const auto nsp_program_status = nsp->GetProgramStatus();
    if (nsp_program_status != ResultStatus::Success) {
        return {nsp_program_status, {}};
    }

    if (!nsp->IsExtractedType() &&
        nsp->GetNCA(title_id, FileSys::ContentRecordType::Program) == nullptr) {
        if (!Core::Crypto::KeyManager::KeyFileExists(false)) {
            return {ResultStatus::ErrorMissingProductionKeyFile, {}};
        }

        return {ResultStatus::ErrorNSPMissingProgramNCA, {}};
    }

    const auto result = secondary_loader->Load(process, system);
    if (result.first != ResultStatus::Success) {
        return result;
    }

    if (nsp->IsExtractedType()) {
        system.GetFileSystemController().RegisterProcess(
            process.GetProcessId(), title_id,
            std::make_shared<FileSys::RomFSFactory>(*this, system.GetContentProvider(),
                                                    system.GetFileSystemController()));
    }

    FileSys::VirtualFile update_raw;
    if (ReadUpdateRaw(update_raw) == ResultStatus::Success && update_raw != nullptr) {
        system.GetFileSystemController().SetPackedUpdate(process.GetProcessId(),
                                                         std::move(update_raw));
    }

    is_loaded = true;
    return result;
}

ResultStatus AppLoader_NSP::VerifyIntegrity(std::function<bool(size_t, size_t)> progress_callback) {
    // Extracted-type NSPs can't be verified.
    if (nsp->IsExtractedType()) {
        return ResultStatus::ErrorIntegrityVerificationNotImplemented;
    }

    // Get list of all NCAs.
    const auto ncas = nsp->GetNCAsCollapsed();

    size_t total_size = 0;
    size_t processed_size = 0;

    // Loop over NCAs, collecting the total size to verify.
    for (const auto& nca : ncas) {
        total_size += nca->GetBaseFile()->GetSize();
    }

    // Loop over NCAs again, verifying each.
    for (const auto& nca : ncas) {
        AppLoader_NCA loader_nca(nca->GetBaseFile());

        const auto NcaProgressCallback = [&](size_t nca_processed_size, size_t nca_total_size) {
            return progress_callback(processed_size + nca_processed_size, total_size);
        };

        const auto verification_result = loader_nca.VerifyIntegrity(NcaProgressCallback);
        if (verification_result != ResultStatus::Success) {
            return verification_result;
        }

        processed_size += nca->GetBaseFile()->GetSize();
    }

    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadRomFS(FileSys::VirtualFile& out_file) {
    const auto res = secondary_loader->ReadRomFS(out_file);
    if (res == ResultStatus::Success) {
        return ResultStatus::Success;
    }
    if (nsp->IsExtractedType() && nsp->GetRomFS() != nullptr) {
        out_file = nsp->GetRomFS();
        return ResultStatus::Success;
    }
    return res;
}

ResultStatus AppLoader_NSP::ReadUpdateRaw(FileSys::VirtualFile& out_file) {
    if (nsp->IsExtractedType()) {
        return ResultStatus::ErrorNoPackedUpdate;
    }

    const auto read = nsp->GetNCAFile(FileSys::GetUpdateTitleID(nsp->GetProgramTitleID()),
                                      FileSys::ContentRecordType::Program);

    if (read == nullptr) {
        return ResultStatus::ErrorNoPackedUpdate;
    }

    const auto nca_test = std::make_shared<FileSys::NCA>(read);
    if (nca_test->GetStatus() != ResultStatus::ErrorMissingBKTRBaseRomFS) {
        return nca_test->GetStatus();
    }

    out_file = read;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadProgramId(u64& out_program_id) {
    out_program_id = nsp->GetProgramTitleID();
    if (out_program_id == 0) {
        return ResultStatus::ErrorNotInitialized;
    }
    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadProgramIds(std::vector<u64>& out_program_ids) {
    out_program_ids = nsp->GetProgramTitleIDs();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadIcon(std::vector<u8>& buffer) {
    if (icon_file != nullptr) {
        buffer = icon_file->ReadAllBytes();
        return ResultStatus::Success;
    }
    if (secondary_loader != nullptr &&
        secondary_loader->ReadIcon(buffer) == ResultStatus::Success) {
        return ResultStatus::Success;
    }
    static constexpr std::array<std::string_view, 5> icon_names{
        "icon_AmericanEnglish.dat", "icon.jpg", "icon.png", "icon.bmp", "icon.jpeg"};
    static constexpr std::array<std::string_view, 4> icon_exts{"jpg", "png", "bmp", "jpeg"};
    if (const auto sidecar = FindPackageSidecar(file, icon_names, icon_exts)) {
        buffer = sidecar->ReadAllBytes();
        return ResultStatus::Success;
    }
    return ResultStatus::ErrorNoControl;
}

ResultStatus AppLoader_NSP::ReadTitle(std::string& title) {
    if (nacp_file != nullptr) {
        title = nacp_file->GetApplicationName();
        return ResultStatus::Success;
    }
    if (secondary_loader != nullptr &&
        secondary_loader->ReadTitle(title) == ResultStatus::Success && !title.empty()) {
        return ResultStatus::Success;
    }
    FileSys::NACP nacp{};
    if (ReadControlData(nacp) == ResultStatus::Success) {
        title = nacp.GetApplicationName();
        if (!title.empty()) {
            return ResultStatus::Success;
        }
    }
    if (nsp->IsExtractedType()) {
        title = PackageStem(file);
        if (!title.empty()) {
            return ResultStatus::Success;
        }
    }
    return ResultStatus::ErrorNoControl;
}

ResultStatus AppLoader_NSP::ReadControlData(FileSys::NACP& nacp) {
    if (nacp_file != nullptr) {
        nacp = *nacp_file;
        return ResultStatus::Success;
    }
    if (secondary_loader != nullptr &&
        secondary_loader->ReadControlData(nacp) == ResultStatus::Success) {
        return ResultStatus::Success;
    }
    static constexpr std::array<std::string_view, 1> nacp_names{"control.nacp"};
    static constexpr std::array<std::string_view, 1> nacp_exts{"nacp"};
    if (const auto sidecar = FindPackageSidecar(file, nacp_names, nacp_exts)) {
        nacp = FileSys::NACP{sidecar};
        return ResultStatus::Success;
    }
    return ResultStatus::ErrorNoControl;
}

ResultStatus AppLoader_NSP::ReadDisplayVersion(std::string& version) {
    if (nacp_file != nullptr && !nacp_file->GetVersionString().empty()) {
        version = nacp_file->GetVersionString();
        return ResultStatus::Success;
    }
    if (secondary_loader != nullptr &&
        secondary_loader->ReadDisplayVersion(version) == ResultStatus::Success) {
        return ResultStatus::Success;
    }
    return AppLoader::ReadDisplayVersion(version);
}

ResultStatus AppLoader_NSP::ReadManualRomFS(FileSys::VirtualFile& out_file) {
    if (nsp->IsExtractedType()) {
        return ResultStatus::ErrorNoRomFS;
    }
    const auto nca =
        nsp->GetNCA(nsp->GetProgramTitleID(), FileSys::ContentRecordType::HtmlDocument);
    if (nsp->GetStatus() != ResultStatus::Success || nca == nullptr) {
        return ResultStatus::ErrorNoRomFS;
    }

    out_file = nca->GetRomFS();
    return out_file == nullptr ? ResultStatus::ErrorNoRomFS : ResultStatus::Success;
}

ResultStatus AppLoader_NSP::ReadBanner(std::vector<u8>& buffer) {
    return secondary_loader->ReadBanner(buffer);
}

ResultStatus AppLoader_NSP::ReadLogo(std::vector<u8>& buffer) {
    return secondary_loader->ReadLogo(buffer);
}

ResultStatus AppLoader_NSP::ReadNSOModules(Modules& modules) {
    return secondary_loader->ReadNSOModules(modules);
}

} // namespace Loader
