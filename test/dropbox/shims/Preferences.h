#pragma once
#include <map>
#include <string>
class String {
 public:
  String(std::string s = {}) : value(std::move(s)) {}
  bool isEmpty() const { return value.empty(); }
  size_t length() const { return value.size(); }
  const char* c_str() const { return value.c_str(); }

 private:
  std::string value;
};
inline std::map<std::string, std::string> dropboxTestNvs;
inline bool dropboxTestNvsFail = false;
class Preferences {
 public:
  bool begin(const char*, bool) { return true; }
  void end() {}
  String getString(const char* key, const char* fallback) {
    return String(dropboxTestNvs.count(key) ? dropboxTestNvs[key] : fallback);
  }
  size_t putString(const char* key, const char* value) {
    if (dropboxTestNvsFail) return 0;
    dropboxTestNvs[key] = value;
    return std::char_traits<char>::length(value);
  }
};
