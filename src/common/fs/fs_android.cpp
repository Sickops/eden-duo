// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/android/android_common.h"
#include "common/android/id_cache.h"
#include "common/assert.h"
#include "common/fs/fs_android.h"
#include "common/string_util.h"

namespace Common::FS::Android {

void RegisterCallbacks(JNIEnv* env, jclass clazz) {
    env->GetJavaVM(&g_jvm);
    native_library = clazz;

    s_get_parent_directory = env->GetStaticMethodID(native_library, "getParentDirectory",
                                                    "(Ljava/lang/String;)Ljava/lang/String;");
    s_get_filename = env->GetStaticMethodID(native_library, "getFilename",
                                            "(Ljava/lang/String;)Ljava/lang/String;");
    s_get_size = env->GetStaticMethodID(native_library, "getSize", "(Ljava/lang/String;)J");
    s_is_directory = env->GetStaticMethodID(native_library, "isDirectory", "(Ljava/lang/String;)Z");
    s_file_exists = env->GetStaticMethodID(native_library, "exists", "(Ljava/lang/String;)Z");
    s_open_content_uri = env->GetStaticMethodID(native_library, "openContentUri",
                                                "(Ljava/lang/String;Ljava/lang/String;)I");
    s_operate_document = env->GetStaticMethodID(native_library, "operateDocument",
        "(Ljava/lang/String;Ljava/lang/String;I)Z");
    s_list_document_entries = env->GetStaticMethodID(native_library, "listDocumentEntries",
        "(Ljava/lang/String;)[Ljava/lang/String;");
}

void UnRegisterCallbacks() {
    s_operate_document = nullptr;
    s_list_document_entries = nullptr;
    s_get_parent_directory = nullptr;
    s_get_filename = nullptr;

    s_get_size = nullptr;
    s_is_directory = nullptr;
    s_file_exists = nullptr;

    s_open_content_uri = nullptr;
}

bool IsContentUri(const std::string& path) {
    constexpr std::string_view prefix = "content://";
    if (path.size() < prefix.size()) [[unlikely]] {
        return false;
    }

    return path.find(prefix) == 0;
}

s32 OpenContentUri(const std::string& filepath, OpenMode openmode) {
    if (s_open_content_uri == nullptr)
        return -1;

    const char* mode = "";
    switch (openmode) {
    case OpenMode::Read:
        mode = "r";
        break;
    case OpenMode::Write:
    case OpenMode::WriteTruncate: mode = "wt"; break;
    case OpenMode::ReadWrite: mode = "rw"; break;
    case OpenMode::WriteAppend: mode = "wa"; break;
    case OpenMode::ReadWriteAppend: mode = "rw"; break;
    case OpenMode::ReadWriteTruncate: mode = "rwt"; break;
    default: return -1;
    }
    auto env = Common::Android::GetEnvForThread();
    jstring j_filepath = Common::Android::ToJString(env, filepath);
    jstring j_mode = Common::Android::ToJString(env, mode);
    const auto result = env->CallStaticIntMethod(native_library, s_open_content_uri, j_filepath, j_mode);
    env->DeleteLocalRef(j_filepath);
    env->DeleteLocalRef(j_mode);
    return result;
}

u64 GetSize(const std::string& filepath) {
    if (!s_get_size) return 0;
    auto env = Common::Android::GetEnvForThread();
    const auto path = Common::Android::ToJString(env, filepath);
    const auto result = env->CallStaticLongMethod(native_library, s_get_size, path);
    env->DeleteLocalRef(path);
    return result;
}

bool IsDirectory(const std::string& filepath) {
    if (!s_is_directory) return false;
    auto env = Common::Android::GetEnvForThread();
    const auto path = Common::Android::ToJString(env, filepath);
    const auto result = env->CallStaticBooleanMethod(native_library, s_is_directory, path);
    env->DeleteLocalRef(path);
    return result;
}

bool Exists(const std::string& filepath) {
    if (!s_file_exists) return false;
    auto env = Common::Android::GetEnvForThread();
    const auto path = Common::Android::ToJString(env, filepath);
    const auto result = env->CallStaticBooleanMethod(native_library, s_file_exists, path);
    env->DeleteLocalRef(path);
    return result;
}

std::string GetParentDirectory(const std::string& filepath) {
    if (!s_get_parent_directory) return {};
    auto env = Common::Android::GetEnvForThread();
    const auto path = Common::Android::ToJString(env, filepath);
    const auto value = static_cast<jstring>(env->CallStaticObjectMethod(native_library, s_get_parent_directory, path));
    env->DeleteLocalRef(path);
    if (!value) return {};
    const auto result = Common::Android::GetJString(env, value);
    env->DeleteLocalRef(value);
    return result;
}

std::string GetFilename(const std::string& filepath) {
    if (!s_get_filename) return {};
    auto env = Common::Android::GetEnvForThread();
    const auto path = Common::Android::ToJString(env, filepath);
    const auto value = static_cast<jstring>(env->CallStaticObjectMethod(native_library, s_get_filename, path));
    env->DeleteLocalRef(path);
    if (!value) return {};
    const auto result = Common::Android::GetJString(env, value);
    env->DeleteLocalRef(value);
    return result;
}

bool OperateDocument(const std::string& path, const std::string& other, int operation) {
    if (!s_operate_document) return false;
    auto env = Common::Android::GetEnvForThread();
    const auto source = Common::Android::ToJString(env, path);
    const auto target = Common::Android::ToJString(env, other);
    const bool result = env->CallStaticBooleanMethod(native_library, s_operate_document, source, target, operation);
    env->DeleteLocalRef(source);
    env->DeleteLocalRef(target);
    return result;
}

std::vector<std::pair<std::string, bool>> ListDocumentEntries(const std::string& path) {
    std::vector<std::pair<std::string, bool>> entries;
    if (!s_list_document_entries) return entries;
    auto env = Common::Android::GetEnvForThread();
    const auto source = Common::Android::ToJString(env, path);
    const auto array = static_cast<jobjectArray>(env->CallStaticObjectMethod(native_library, s_list_document_entries, source));
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
