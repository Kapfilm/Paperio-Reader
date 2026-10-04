#pragma once
#include <string>
enum class StrId { STR_NOTO_SANS };
struct I18nStub { std::string get(StrId) const { return "Noto Sans"; } };
inline I18nStub I18N;
