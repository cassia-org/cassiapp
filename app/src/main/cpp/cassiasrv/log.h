#pragma once

#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "cassiasrv", __VA_ARGS__);
