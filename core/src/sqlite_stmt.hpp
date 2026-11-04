#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace liora::detail {

// Minimal prepared statement wrapper shared by Core modules that use SQLite.
class Stmt {
 public:
  Stmt(sqlite3* db, const std::string& sql) {
    if (!db || sqlite3_prepare_v2(db, sql.c_str(), -1, &s_, nullptr) != SQLITE_OK) s_ = nullptr;
  }
  ~Stmt() { sqlite3_finalize(s_); }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  explicit operator bool() const { return s_ != nullptr; }

  Stmt& bind(int i, int64_t v) {
    if (s_) sqlite3_bind_int64(s_, i, v);
    return *this;
  }
  Stmt& bind(int i, std::string_view v) {
    if (s_) sqlite3_bind_text(s_, i, v.data() ? v.data() : "", static_cast<int>(v.size()), SQLITE_TRANSIENT);
    return *this;
  }
  Stmt& bindNull(int i) {
    if (s_) sqlite3_bind_null(s_, i);
    return *this;
  }

  // Advances to the next row. Returns false at the end or on error.
  bool step() { return s_ && sqlite3_step(s_) == SQLITE_ROW; }
  // Runs a statement that does not need its rows.
  bool exec() {
    if (!s_) return false;
    int rc = sqlite3_step(s_);
    return rc == SQLITE_DONE || rc == SQLITE_ROW;
  }

  int64_t i64(int col) const { return sqlite3_column_int64(s_, col); }
  double f64(int col) const { return sqlite3_column_double(s_, col); }
  bool isNull(int col) const { return sqlite3_column_type(s_, col) == SQLITE_NULL; }
  std::string text(int col) const {
    auto p = sqlite3_column_text(s_, col);
    if (!p) return {};
    return {reinterpret_cast<const char*>(p), static_cast<size_t>(sqlite3_column_bytes(s_, col))};
  }

 private:
  sqlite3_stmt* s_ = nullptr;
};

}  // namespace liora::detail
