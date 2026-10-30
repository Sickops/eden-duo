// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

// Eden Duo: SAF document operations for shared-folder import, on top of fs_android.h. Header-only
// (inline functions and variables), so the upstream build lists stay untouched; fs_android.cpp
// registers the callbacks with RegisterCallbacks.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <jni.h>

#include "common/android/android_common.h"
#include "common/common_types.h"

namespace Common::Android {
JNIEnv* GetEnvForThread();
} // namespace Common::Android

namespace Common::FS::Android {

namespace DocumentCallbacks {
inline jclass native_library = nullptr;
inline jmethodID operate_document = nullptr;
inline jmethodID list_document_entries = nullptr;
} // namespace DocumentCallbacks

/// Looks up NativeLibrary.operateDocument and NativeLibrary.listDocumentEntries.
inline void RegisterDocumentCallbacks(JNIEnv* env, jclass clazz) {
    DocumentCallbacks::native_library = clazz;
    DocumentCallbacks::operate_document = env->GetStaticMethodID(
        clazz, "operateDocument", "(Ljava/lang/String;Ljava/lang/String;I)Z");
    DocumentCallbacks::list_document_entries = env->GetStaticMethodID(
        clazz, "listDocumentEntries", "(Ljava/lang/String;)[Ljava/lang/String;");
}

inline void UnRegisterDocumentCallbacks() {
    DocumentCallbacks::operate_document = nullptr;
    DocumentCallbacks::list_document_entries = nullptr;
}

/// Runs DocumentFileAccess operation [operation] on [path] (and [other], when it takes two).
inline bool OperateDocument(const std::string& path, const std::string& other, int operation) {
    if (!DocumentCallbacks::operate_document) return false;
    auto env = Common::Android::GetEnvForThread();
    const auto source = Common::Android::ToJString(env, path);
    const auto target = Common::Android::ToJString(env, other);
    const bool result = env->CallStaticBooleanMethod(DocumentCallbacks::native_library,
                                                     DocumentCallbacks::operate_document, source,
                                                     target, operation);
    env->DeleteLocalRef(source);
    env->DeleteLocalRef(target);
    return result;
}

struct DocumentEntry {
    std::string name;
    bool directory{};
    std::optional<u64> size; ///< a file's size from the folder listing, when the provider has it
};

/// The entries of the document folder [path], with file sizes from the same listing query.
inline std::vector<DocumentEntry> ListDocumentEntries(const std::string& path) {
    std::vector<DocumentEntry> entries;
    if (!DocumentCallbacks::list_document_entries) return entries;
    auto env = Common::Android::GetEnvForThread();
    const auto source = Common::Android::ToJString(env, path);
    const auto array = static_cast<jobjectArray>(env->CallStaticObjectMethod(
        DocumentCallbacks::native_library, DocumentCallbacks::list_document_entries, source));
    env->DeleteLocalRef(source);
    if (!array) return entries;
    for (jsize i = 0; i < env->GetArrayLength(array); ++i) {
        const auto item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        const auto text = Common::Android::GetJString(env, item);
        if (text.starts_with("d:") && text.size() > 2) {
            entries.push_back({text.substr(2), true, std::nullopt});
        } else if (text.starts_with("f:")) {
            const auto slash = text.find('/', 2);
            if (slash == std::string::npos) {
                if (text.size() > 2) entries.push_back({text.substr(2), false, std::nullopt});
            } else if (slash + 1 < text.size()) {
                std::optional<u64> size;
                if (slash > 2) {
                    size = std::stoull(text.substr(2, slash - 2));
                }
                entries.push_back({text.substr(slash + 1), false, size});
            }
        }
        env->DeleteLocalRef(item);
    }
    env->DeleteLocalRef(array);
    return entries;
}

} // namespace Common::FS::Android
