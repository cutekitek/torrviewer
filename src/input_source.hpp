#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace torrview {

enum class InputKind {
  none,
  torrent_file,
  local_media_file,
  magnet_link,
  dropped_text,
  unsupported_file,
};

struct ParsedInput {
  InputKind kind = InputKind::none;
  std::string value;
};

std::string trim(std::string_view value);
bool is_magnet_link(std::string_view text);
bool has_torrent_extension(std::string_view path);
bool has_media_extension(std::string_view path);
ParsedInput parse_file_input(std::string_view path);
ParsedInput parse_text_input(std::string_view text);
const char* input_kind_label(InputKind kind);
std::string compact_value(std::string_view value, std::size_t max_length = 96);

} // namespace torrview
