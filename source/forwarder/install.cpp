// Installs a forwarder that was built by BuildFiles(): writes its NCAs into the NCM content
// storage, registers the CNMT in the meta database and pushes the application record to NS.
// Ported from sphaira's install_forwader_internal (source/owo.cpp, GPLv3).
#include "forwarder/forwarder.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "yati/nx/ncm.hpp"

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace morph::forwarder {
namespace {

// Same layout as the NcmContentMetaData written by BuildFiles().
struct NcmContentMetaData {
    NcmContentMetaHeader header;
    NcmApplicationMetaExtendedHeader extended;
    NcmContentInfo infos[3];
};

struct NcaFile {
    std::vector<u8> data;
    u8 hash[SHA256_HASH_SIZE];
};

bool ReadFile(const std::string& path, std::vector<u8>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size > 0 ? (size_t)size : 0);
    const bool ok = size <= 0 || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

template <typename T>
bool ReadStruct(const std::string& path, T& out) {
    std::vector<u8> buf;
    if (!ReadFile(path, buf) || buf.size() != sizeof(T)) return false;
    std::memcpy(&out, buf.data(), sizeof(T));
    return true;
}

// Record state stored with the application record. Only Installed (0x3) makes HOME treat the
// title as fully installed; any other value looks like an unfinished download and makes the
// system try to fetch the missing parts from the eShop (NIM error 2137-7403).
enum ApplicationRecordType : u8 {
    ApplicationRecordType_Installed = 0x3,
};

// NS application-manager calls that libnx does not expose; the command ids match sphaira's.
struct NcmGuard {
    ~NcmGuard() { ncmExit(); }
};

Service g_ns_app{};
bool g_ns_ready = false;

Result NsAppInit() {
    if (g_ns_ready) R_SUCCEED();
    R_TRY(nsInitialize());
    const Result rc = nsGetApplicationManagerInterface(&g_ns_app);
    if (R_FAILED(rc)) {
        nsExit();
        R_THROW(rc);
    }
    g_ns_ready = true;
    R_SUCCEED();
}

void NsAppExit() {
    if (!g_ns_ready) return;
    serviceClose(&g_ns_app);
    nsExit();
    g_ns_ready = false;
}

Result NsPushApplicationRecord(u64 tid, const ncm::ContentStorageRecord* records, u32 count) {
    const struct {
        u8 last_modified_event;
        u8 padding[0x7];
        u64 tid;
    } in = {ApplicationRecordType_Installed, {0}, tid};

    return serviceDispatchIn(&g_ns_app, 16, in,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { records, sizeof(*records) * count } });
}

Result NsInvalidateApplicationControlCache(u64 tid) {
    return serviceDispatchIn(&g_ns_app, 404, tid);
}

// Diagnostic logging for install state.
#ifdef MORPH_FORWARDER_DIAG
void LogInstallState(u64 tid, const NcmContentMetaKey& key, NcmStorageId storage_id,
                     const std::vector<NcaFile>& ncas) {
    // 1. content storage: is every NCA there, and where?
    NcmContentStorage cs;
    if (R_SUCCEEDED(ncmOpenContentStorage(&cs, storage_id))) {
        for (size_t i = 0; i < ncas.size(); i++) {
            NcmContentId id;
            std::memcpy(&id, ncas[i].hash, sizeof(id));
            bool has = false;
            const Result rc_has = ncmContentStorageHas(&cs, &has, &id);
            char path[0x301]{};
            const Result rc_path = ncmContentStorageGetPath(&cs, path, sizeof(path), &id);
            log_write("diag: nca[%zu] has=%d (rc 0x%X) path=%s (rc 0x%X)\n", i, has, rc_has, path, rc_path);
        }
        ncmContentStorageClose(&cs);
    }

    // 2. meta database: does NCM accept the entry, and how does it parse the content list?
    NcmContentMetaDatabase db;
    if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, storage_id))) {
        bool has = false;
        const Result rc_has = ncmContentMetaDatabaseHas(&db, &has, &key);
        log_write("diag: meta has=%d (rc 0x%X)\n", has, rc_has);

        NcmContentInfo infos[8]{};
        s32 written = 0;
        const Result rc_list = ncmContentMetaDatabaseListContentInfo(&db, &written, infos, 8, &key, 0);
        log_write("diag: ListContentInfo rc=0x%X written=%d\n", rc_list, written);
        for (s32 i = 0; i < written && i < 8; i++) {
            u8 id_hex[0x10];
            std::memcpy(id_hex, &infos[i].content_id, sizeof(id_hex));
            u64 size = 0;
            ncmContentInfoSizeToU64(&infos[i], &size);
            log_write("diag:  info[%d] type=%u size=%llu id=%02X%02X%02X%02X...\n", i, infos[i].content_type,
                      (unsigned long long)size, id_hex[0], id_hex[1], id_hex[2], id_hex[3]);
        }

        u8 raw[0x200]{};
        u64 raw_size = 0;
        const Result rc_get = ncmContentMetaDatabaseGet(&db, &key, &raw_size, raw, sizeof(raw));
        log_write("diag: Get rc=0x%X stored_size=%llu\n", rc_get, (unsigned long long)raw_size);
        ncmContentMetaDatabaseClose(&db);
    }

    // 3. NS: can it read the control data (what HOME needs for the icon and the name)?
    {
        static NsApplicationControlData control;
        u64 actual = 0;
        const Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, &control, sizeof(control), &actual);
        log_write("diag: ControlData rc=0x%X actual=%llu\n", rc, (unsigned long long)actual);
    }

    // 4. NS: what does HOME think the application state is (download flags, error context)?
    {
        NsApplicationView view{};
        const Result rc = nsGetApplicationView(&view, &tid, 1);
        log_write("diag: ApplicationView rc=0x%X flags=0x%08X\n", rc, view.flags);

        ErrorContext ctx{};
        const Result rc_ctx = nsGetApplicationViewDownloadErrorContext(tid, &ctx);
        log_write("diag: DownloadErrorContext rc=0x%X type=%u\n", rc_ctx, (unsigned)ctx.type);
    }

    // 5. NS: is the entity installed?
    {
        bool installed = false;
        const Result rc = nsIsAnyApplicationEntityInstalled(tid, &installed);
        log_write("diag: EntityInstalled=%d (rc 0x%X)\n", installed, rc);
    }
}

#endif

} // namespace

Result InstallBuilt(const std::string& dir, u64 tid, NcmStorageId storage_id) {
    // Record files written by BuildFiles().
    NcmContentMetaKey content_meta_key{};
    ncm::ContentStorageRecord content_storage_record{};
    NcmContentMetaData content_meta_data{};
    R_UNLESS(ReadStruct(dir + "/content_meta_key.bin", content_meta_key), Result_ForwarderReadFailed);
    R_UNLESS(ReadStruct(dir + "/content_storage_record.bin", content_storage_record), Result_ForwarderReadFailed);
    R_UNLESS(ReadStruct(dir + "/content_meta_data.bin", content_meta_data), Result_ForwarderReadFailed);

    std::vector<NcaFile> ncas(3);
    const char* names[3] = {"program.nca", "control.nca", "meta.nca"};
    for (size_t i = 0; i < ncas.size(); i++) {
        R_UNLESS(ReadFile(dir + "/" + names[i], ncas[i].data), Result_ForwarderReadFailed);
        sha256CalculateHash(ncas[i].hash, ncas[i].data.data(), ncas[i].data.size());
    }

    R_TRY(ncmInitialize());
    NcmGuard ncm_guard{};   // ncmExit() on every return path
    R_TRY(NsAppInit());

    const Result install_rc = [&]() -> Result {
        // An earlier install of the same title must be removed first: otherwise the new meta
        // record and application record are ignored and the shortcut stays broken.
        NcmContentMetaDatabase old_db;
        if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&old_db, storage_id))) {
            ncmContentMetaDatabaseRemove(&old_db, &content_meta_key);  // not found is fine
            ncmContentMetaDatabaseCommit(&old_db);
            ncmContentMetaDatabaseClose(&old_db);
        }
        nsDeleteApplicationCompletely(tid);  // not found is fine: first install of this title

        // write the NCAs into the content storage
        NcmContentStorage cs;
        R_TRY(ncmOpenContentStorage(&cs, storage_id));
        Result rc = [&]() -> Result {
            for (const auto& nca : ncas) {
                NcmContentId content_id;
                NcmPlaceHolderId placeholder_id;
                std::memcpy(&content_id, nca.hash, sizeof(content_id));
                R_TRY(ncmContentStorageGeneratePlaceHolderId(&cs, &placeholder_id));
                ncmContentStorageDeletePlaceHolder(&cs, &placeholder_id);
                R_TRY(ncmContentStorageCreatePlaceHolder(&cs, &content_id, &placeholder_id, nca.data.size()));
                R_TRY(ncmContentStorageWritePlaceHolder(&cs, &placeholder_id, 0, nca.data.data(), nca.data.size()));
                ncmContentStorageDelete(&cs, &content_id);
                R_TRY(ncmContentStorageRegister(&cs, &content_id, &placeholder_id));
            }
            R_SUCCEED();
        }();
        ncmContentStorageClose(&cs);
        R_TRY(rc);

        // register the meta record
        NcmContentMetaDatabase db;
        R_TRY(ncmOpenContentMetaDatabase(&db, storage_id));
        rc = [&]() -> Result {
            R_TRY(ncmContentMetaDatabaseSet(&db, &content_meta_key, &content_meta_data, sizeof(content_meta_data)));
            R_TRY(ncmContentMetaDatabaseCommit(&db));
            R_SUCCEED();
        }();
        ncmContentMetaDatabaseClose(&db);
        R_TRY(rc);

        // push the application record
        R_TRY(NsPushApplicationRecord(tid, &content_storage_record, 1));
        NsInvalidateApplicationControlCache(tid);

#ifdef MORPH_FORWARDER_DIAG
        LogInstallState(tid, content_meta_key, storage_id, ncas);
#endif
        R_SUCCEED();
    }();

    NsAppExit();
    if (R_FAILED(install_rc)) {
        log_write("forwarder: install failed 0x%X\n", install_rc);
        R_THROW(Result_ForwarderInstallFailed);
    }
    log_write("forwarder: installed tid=%016lX\n", tid);
    R_SUCCEED();
}

} // namespace morph::forwarder
