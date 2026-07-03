#include "input_source.hpp"

#include <algorithm>
#include <cctype>

namespace torrview {
namespace {

std::string lower_ascii(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

bool starts_with_ascii(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

} // namespace

std::string trim(std::string_view value) {
  auto begin = value.begin();
  auto end = value.end();

  while (begin != end && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
    ++begin;
  }

  while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
    --end;
  }

  return std::string(begin, end);
}

bool has_torrent_extension(std::string_view path) {
  const std::string lower = lower_ascii(path);
  return lower.size() >= 8 && lower.ends_with(".torrent");
}

bool has_media_extension(std::string_view path) {
  const std::string lower = lower_ascii(path);
  return lower.ends_with(".mkv") || lower.ends_with(".mp4") || lower.ends_with(".webm") ||
         lower.ends_with(".avi") || lower.ends_with(".mov") || lower.ends_with(".m4v") ||
         lower.ends_with(".mpg") || lower.ends_with(".mpeg") || lower.ends_with(".ts") ||
         lower.ends_with(".mts") || lower.ends_with(".m2ts") || lower.ends_with(".flv") ||
         lower.ends_with(".wmv") || lower.ends_with(".ogv");
}

bool is_magnet_link(std::string_view text) {
  return starts_with_ascii(lower_ascii(trim(text)), "magnet:?");
}

ParsedInput parse_file_input(std::string_view path) {
  const std::string cleaned = trim(path);
  if (has_torrent_extension(cleaned)) {
    return {.kind = InputKind::torrent_file, .value = cleaned};
  }

  if (has_media_extension(cleaned)) {
    return {.kind = InputKind::local_media_file, .value = cleaned};
  }

  return {.kind = InputKind::unsupported_file, .value = cleaned};
}

ParsedInput parse_text_input(std::string_view text) {
  const std::string cleaned = trim(text);
  if (is_magnet_link(cleaned)) {
    return {.kind = InputKind::magnet_link, .value = cleaned};
  }

  if (has_torrent_extension(cleaned)) {
    return {.kind = InputKind::torrent_file, .value = cleaned};
  }

  if (has_media_extension(cleaned)) {
    return {.kind = InputKind::local_media_file, .value = cleaned};
  }

  return {.kind = InputKind::dropped_text, .value = cleaned};
}

const char* input_kind_label(InputKind kind) {
  switch (kind) {
  case InputKind::none:
    return "No input";
  case InputKind::torrent_file:
    return "Torrent file";
  case InputKind::local_media_file:
    return "Local media";
  case InputKind::magnet_link:
    return "Magnet link";
  case InputKind::dropped_text:
    return "Dropped text";
  case InputKind::unsupported_file:
    return "Unsupported file";
  }

  return "Unknown";
}

std::string compact_value(std::string_view value, std::size_t max_length) {
  if (value.size() <= max_length) {
    return std::string(value);
  }

  if (max_length <= 3) {
    return std::string(value.substr(0, max_length));
  }

  std::string result(value.substr(0, max_length - 3));
  result += "...";
  return result;
}

} // namespace torrview
