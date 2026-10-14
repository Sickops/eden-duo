// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

// Eden Duo: SAF document operations for shared-folder import, on top of fs_android.h. Header-only
// (inline functions and variables), so the upstream build lists stay untouched; fs_android.cpp
// registers the callbacks with RegisterCallbacks.

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <jni.h>

#include "common/android/android_common.h"

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

/// The entries of the document folder [path]: name and whether it is a folder.
inline std::vector<std::pair<std::string, bool>> ListDocumentEntries(const std::string& path) {
    std::vector<std::pair<std::string, bool>> entries;
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
        if (text.size() > 2) entries.emplace_back(text.substr(2), text.starts_with("d:"));
        env->DeleteLocalRef(item);
    }
    env->DeleteLocalRef(array);
    return entries;
}

} // namespace Common::FS::Android
