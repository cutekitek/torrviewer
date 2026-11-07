#include "library.hpp"
#include "logger.hpp"
#include <sqlite3.h>
#include <stdexcept>

namespace torrview {
namespace {
void execute(sqlite3* db, const char* sql) {
  if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db));
}
class Statement {
public:
  Statement(sqlite3* db, const char* sql) : db_(db) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db));
  }
  ~Statement() { sqlite3_finalize(stmt_); }
  void text(int i, const std::string& s) {
    check(sqlite3_bind_text64(stmt_, i, s.data(), s.size(), SQLITE_TRANSIENT, SQLITE_UTF8));
  }
  void number(int i, double n) { check(sqlite3_bind_double(stmt_, i, n)); }
  void integer(int i, std::int64_t n) { check(sqlite3_bind_int64(stmt_, i, n)); }
  void blob(int i, const std::vector<char>& b) {
    check(sqlite3_bind_blob64(stmt_, i, b.data(), b.size(), SQLITE_TRANSIENT));
  }
  bool step() {
    int rc = sqlite3_step(stmt_);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE)
      check(rc);
    return rc == SQLITE_ROW;
  }
  std::string text(int i) {
    auto p = sqlite3_column_text(stmt_, i);
    return p ? reinterpret_cast<const char*>(p) : "";
  }
  std::int64_t integer(int i) { return sqlite3_column_int64(stmt_, i); }
  double number(int i) { return sqlite3_column_double(stmt_, i); }
  std::vector<char> blob(int i) {
    auto p = static_cast<const char*>(sqlite3_column_blob(stmt_, i));
    return p ? std::vector<char>(p, p + sqlite3_column_bytes(stmt_, i)) : std::vector<char>{};
  }

private:
  void check(int rc) {
    if (rc != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db_));
  }
  sqlite3* db_;
  sqlite3_stmt* stmt_ = nullptr;
};
} // namespace

Library::Library(const std::string& path) {
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    std::string message = db ? sqlite3_errmsg(db) : "Cannot open library database";
    sqlite3_close(db);
    throw std::runtime_error(message);
  }
  database_ = db;
  try {
    sqlite3_busy_timeout(db, 3000);
    {
      Statement version(db, "PRAGMA user_version");
      if (version.step() && version.integer(0) > 1)
        throw std::runtime_error("Library database was created by a newer Torrview version");
    }
    execute(db,
            "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;"
            "CREATE TABLE IF NOT EXISTS torrents (id TEXT PRIMARY KEY, source TEXT NOT NULL,"
            "name TEXT NOT NULL, metadata BLOB, last_file INTEGER NOT NULL);"
            "CREATE TABLE IF NOT EXISTS files (torrent TEXT NOT NULL REFERENCES torrents(id) "
            "ON DELETE CASCADE, idx INTEGER NOT NULL, path TEXT NOT NULL, extension TEXT,"
            "size INTEGER, offset INTEGER, video INTEGER, position REAL NOT NULL DEFAULT 0,"
            "duration REAL NOT NULL DEFAULT 0, PRIMARY KEY(torrent,idx)); PRAGMA user_version=1;");
    Statement rows(db,
                   "SELECT id,source,name,metadata,last_file FROM torrents ORDER BY rowid DESC");
    while (rows.step()) {
      LibraryEntry e;
      e.key = rows.text(0);
      e.source = rows.text(1);
      e.name = rows.text(2);
      e.metadata = rows.blob(3);
      e.last_file = static_cast<int>(rows.integer(4));
      Statement files(db, "SELECT idx,path,extension,size,offset,video,position,duration FROM "
                          "files WHERE torrent=? ORDER BY idx");
      files.text(1, e.key);
      while (files.step()) {
        TorrentFileInfo f;
        f.index = static_cast<int>(files.integer(0));
        f.path = files.text(1);
        f.extension = files.text(2);
        f.size = files.integer(3);
        f.offset = files.integer(4);
        f.likely_video = files.integer(5) != 0;
        e.progress[f.index] = {files.number(6), files.number(7)};
        e.files.push_back(std::move(f));
      }
      entries.push_back(std::move(e));
    }
    worker_ = std::thread([this] { run(); });
  } catch (...) {
    sqlite3_close(db);
    throw;
  }
}
Library::~Library() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  condition_.notify_one();
  if (worker_.joinable())
    worker_.join();
  sqlite3_close(database_);
}
void Library::save() {
  {
    std::lock_guard lock(mutex_);
    pending_ = entries;
    pending_progress_.clear();
  }
  condition_.notify_one();
}
void Library::save_progress(const std::string& key, int index, VideoProgress progress) {
  {
    std::lock_guard lock(mutex_);
    if (needs_full_save_) {
      pending_ = entries;
      pending_progress_.clear();
    }
    std::erase_if(pending_progress_,
                  [&](const auto& update) { return update.key == key && update.index == index; });
    pending_progress_.push_back({key, index, progress});
  }
  condition_.notify_one();
}
std::string Library::error() const {
  std::lock_guard lock(mutex_);
  return error_;
}
void Library::run() {
  auto db = database_;
  for (;;) {
    std::optional<std::vector<LibraryEntry>> batch;
    std::vector<ProgressUpdate> progress;
    {
      std::unique_lock lock(mutex_);
      condition_.wait(
          lock, [this] { return stopping_ || pending_.has_value() || !pending_progress_.empty(); });
      if (!pending_ && pending_progress_.empty())
        return;
      batch = std::move(pending_);
      pending_.reset();
      progress.swap(pending_progress_);
    }
    try {
      execute(db, "BEGIN IMMEDIATE");
      if (batch) {
        execute(db, "DELETE FROM torrents");
        for (auto it = batch->rbegin(); it != batch->rend(); ++it) {
          const auto& e = *it;
          Statement row(db, "INSERT INTO torrents VALUES(?,?,?,?,?)");
          row.text(1, e.key);
          row.text(2, e.source);
          row.text(3, e.name);
          row.blob(4, e.metadata);
          row.integer(5, e.last_file);
          row.step();
          for (const auto& f : e.files) {
            Statement file(db, "INSERT INTO files VALUES(?,?,?,?,?,?,?,?,?)");
            file.text(1, e.key);
            file.integer(2, f.index);
            file.text(3, f.path);
            file.text(4, f.extension);
            file.integer(5, f.size);
            file.integer(6, f.offset);
            file.integer(7, f.likely_video ? 1 : 0);
            auto p = e.progress.find(f.index);
            file.number(8, p == e.progress.end() ? 0 : p->second.position);
            file.number(9, p == e.progress.end() ? 0 : p->second.duration);
            file.step();
          }
        }
      }
      for (const auto& update : progress) {
        Statement file(db, "UPDATE files SET position=?,duration=? WHERE torrent=? AND idx=?");
        file.number(1, update.progress.position);
        file.number(2, update.progress.duration);
        file.text(3, update.key);
        file.integer(4, update.index);
        file.step();
        Statement torrent(db, "UPDATE torrents SET last_file=? WHERE id=?");
        torrent.integer(1, update.index);
        torrent.text(2, update.key);
        torrent.step();
      }
      execute(db, "COMMIT");
      std::lock_guard lock(mutex_);
      if (batch)
        needs_full_save_ = false;
      if (!needs_full_save_)
        error_.clear();
    } catch (const std::exception& e) {
      sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
      TORRVIEW_LOG_ERROR("Library save failed: " << e.what());
      std::lock_guard lock(mutex_);
      error_ = e.what();
      needs_full_save_ = true;
    }
  }
}
} // namespace torrview
