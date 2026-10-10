#pragma once

#include <switch.h>
#include <utility>

// Module id used for morph's own result codes (see Result_* below).
enum { Module_Morph = 505 };

// The only raw FS error code the project compares against.
enum FsError {
    FsError_PathAlreadyExists = 0x402,
};

// Ordinal of each morph-specific result; combined with Module_Morph into Result_* below.
enum class MorphResult : Result {
    FsNotActive,
    FsReadOnly,
    FsUnknownStdioError,
    FsStdioFailedToOpenFile,
    FsStdioFailedToOpenDirectory,
    FsFailedStdioOpendir,
    FsFailedStdioStat,
    FsStdioFailedToCreate,
    FsStdioFailedToCreateDirectory,
    FsStdioFailedToDeleteFile,
    FsStdioFailedToDeleteDirectory,
    FsStdioFailedToRead,
    FsStdioFailedToWrite,
    FsStdioFailedToSeek,
    FsStdioFailedToStat,
    FsStdioFailedToTruncate,
    FsStdioFailedToRename,
    NroBadMagic,
    NroBadSize,
    KeyFileNotFound,
    KeyFailedDecryptETicketDeviceKey,
    ForwarderBadArgs,
    ForwarderNpdmPatchFailed,
    ForwarderWriteFailed,
    ForwarderReadFailed,
    ForwarderInstallFailed,
};

#define MAKE_MORPH_RESULT_ENUM(x) Result_##x = MAKERESULT(Module_Morph, (Result)MorphResult::x)

enum : Result {
    MAKE_MORPH_RESULT_ENUM(FsNotActive),
    MAKE_MORPH_RESULT_ENUM(FsReadOnly),
    MAKE_MORPH_RESULT_ENUM(FsUnknownStdioError),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToOpenFile),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToOpenDirectory),
    MAKE_MORPH_RESULT_ENUM(FsFailedStdioOpendir),
    MAKE_MORPH_RESULT_ENUM(FsFailedStdioStat),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToCreate),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToCreateDirectory),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToDeleteFile),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToDeleteDirectory),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToRead),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToWrite),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToSeek),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToStat),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToTruncate),
    MAKE_MORPH_RESULT_ENUM(FsStdioFailedToRename),
    MAKE_MORPH_RESULT_ENUM(NroBadMagic),
    MAKE_MORPH_RESULT_ENUM(NroBadSize),
    MAKE_MORPH_RESULT_ENUM(KeyFileNotFound),
    MAKE_MORPH_RESULT_ENUM(KeyFailedDecryptETicketDeviceKey),
    MAKE_MORPH_RESULT_ENUM(ForwarderBadArgs),
    MAKE_MORPH_RESULT_ENUM(ForwarderNpdmPatchFailed),
    MAKE_MORPH_RESULT_ENUM(ForwarderWriteFailed),
    MAKE_MORPH_RESULT_ENUM(ForwarderReadFailed),
    MAKE_MORPH_RESULT_ENUM(ForwarderInstallFailed),
};

#undef MAKE_MORPH_RESULT_ENUM

#define R_SUCCEED() return (Result)0

#define R_THROW(_rc) return _rc

#define R_TRY_RESULT(r, _result) { \
    if (const auto _rc = (r); R_FAILED(_rc)) { \
        R_THROW(_result); \
    } \
}

#define R_TRY(r) { \
    if (const auto _rc = (r); R_FAILED(_rc)) { \
        R_THROW(_rc); \
    } \
}

#define R_UNLESS(expr, res) { \
    if (!(expr)) { \
        R_THROW(res); \
    } \
}

#define CONCATENATE_IMPL(s1, s2) s1##s2
#define CONCATENATE(s1, s2) CONCATENATE_IMPL(s1, s2)
#define ANONYMOUS_VARIABLE(pref) CONCATENATE(pref, __COUNTER__)

template<typename Function>
struct ScopeGuard {
    ScopeGuard(Function&& function) : m_function(std::forward<Function>(function)) {

    }
    ~ScopeGuard() {
        m_function();
    }

    ScopeGuard(const ScopeGuard&) = delete;
    void operator=(const ScopeGuard&) = delete;

private:
    const Function m_function;
};

struct ScopedMutex {
    ScopedMutex(Mutex* mutex) : m_mutex{mutex} {
        mutexLock(m_mutex);
    }
    ~ScopedMutex() {
        mutexUnlock(m_mutex);
    }

    ScopedMutex(const ScopedMutex&) = delete;
    void operator=(const ScopedMutex&) = delete;

private:
    Mutex* const m_mutex;
};

#define ON_SCOPE_EXIT(_f) ScopeGuard ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE_){[&] { _f; }};
#define SCOPED_MUTEX(_m) ScopedMutex ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE_){_m}
