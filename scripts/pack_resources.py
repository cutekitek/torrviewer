#!/usr/bin/env python3

import argparse
import pathlib
import re
import sys


MIME_TYPES = {
    ".otf": "otf",
    ".svg": "svg",
    ".ttf": "ttf",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Pack resource files into C++ source files.")
    parser.add_argument("--prefix", required=True, help="Resource root used for generated names")
    parser.add_argument("--source", required=True, help="Generated .cpp output path")
    parser.add_argument("--header", required=True, help="Generated .hpp output path")
    parser.add_argument("--namespace", default="torrview::resources", help="Generated C++ namespace")
    parser.add_argument("inputs", nargs="+", help="Resource files to pack")
    return parser.parse_args()


def resource_name(path: pathlib.Path, prefix: pathlib.Path) -> str:
    try:
        relative = path.resolve().relative_to(prefix.resolve())
    except ValueError as error:
        raise ValueError(f"{path} is not under resource prefix {prefix}") from error

    return relative.as_posix()


def symbol_name(name: str) -> str:
    symbol = re.sub(r"[^0-9A-Za-z_]", "_", name)
    if symbol[0].isdigit():
        symbol = f"_{symbol}"
    return f"resource_{symbol}"


def mime_type(path: pathlib.Path) -> str:
    return MIME_TYPES.get(path.suffix.lower(), "application/octet-stream")


def cpp_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def format_bytes(data: bytes) -> str:
    lines = []
    for offset in range(0, len(data), 12):
        chunk = data[offset : offset + 12]
        lines.append("  " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return "\n".join(lines)


def namespace_open(namespace: str) -> str:
    parts = [part for part in namespace.split("::") if part]
    if not parts:
        raise ValueError("namespace must not be empty")
    return f"namespace {'::'.join(parts)} {{"


def namespace_close(namespace: str) -> str:
    parts = [part for part in namespace.split("::") if part]
    return f"}} // namespace {'::'.join(parts)}"


def collect_resources(inputs: list[str], prefix: pathlib.Path) -> list[dict[str, object]]:
    resources = []
    seen_names = set()
    seen_symbols = set()

    for input_path in inputs:
        path = pathlib.Path(input_path)
        if not path.is_file():
            raise ValueError(f"{path} is not a file")

        name = resource_name(path, prefix)
        symbol = symbol_name(name)
        if name in seen_names:
            raise ValueError(f"duplicate resource name: {name}")
        if symbol in seen_symbols:
            raise ValueError(f"resource symbol collision for {name}: {symbol}")

        data = path.read_bytes()
        if not data:
            raise ValueError(f"{path} is empty")

        seen_names.add(name)
        seen_symbols.add(symbol)
        resources.append(
            {
                "name": name,
                "symbol": symbol,
                "mime_type": mime_type(path),
                "data": data,
            }
        )

    return sorted(resources, key=lambda resource: str(resource["name"]))


def write_header(path: pathlib.Path, namespace: str) -> None:
    content = f"""#pragma once

#include <cstddef>
#include <string_view>

{namespace_open(namespace)}

struct Resource {{
  std::string_view name;
  const unsigned char* data;
  std::size_t size;
  std::string_view mime_type;
}};

const Resource* find(std::string_view name) noexcept;

{namespace_close(namespace)}
"""
    path.write_text(content, encoding="utf-8")


def write_source(path: pathlib.Path, header_name: str, namespace: str, resources: list[dict[str, object]]) -> None:
    lines = [
        f'#include "{header_name}"',
        "",
        namespace_open(namespace),
        "namespace {",
        "",
    ]

    for resource in resources:
        lines.append(f'const unsigned char {resource["symbol"]}[] = {{')
        lines.append(format_bytes(resource["data"]))
        lines.append("};")
        lines.append("")

    lines.append("const Resource resource_table[] = {")
    for resource in resources:
        lines.append(
            "  {"
            + f'{cpp_string(resource["name"])}, '
            + f'{resource["symbol"]}, '
            + f'sizeof({resource["symbol"]}), '
            + f'{cpp_string(resource["mime_type"])}'
            + "},"
        )
    lines.extend(
        [
            "};",
            "",
            "} // namespace",
            "",
            "const Resource* find(std::string_view name) noexcept {",
            "  for (const auto& resource : resource_table) {",
            "    if (resource.name == name) {",
            "      return &resource;",
            "    }",
            "  }",
            "",
            "  return nullptr;",
            "}",
            "",
            namespace_close(namespace),
            "",
        ]
    )

    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    args = parse_args()
    prefix = pathlib.Path(args.prefix)
    source = pathlib.Path(args.source)
    header = pathlib.Path(args.header)

    try:
        resources = collect_resources(args.inputs, prefix)
        source.parent.mkdir(parents=True, exist_ok=True)
        header.parent.mkdir(parents=True, exist_ok=True)
        write_header(header, args.namespace)
        write_source(source, header.name, args.namespace, resources)
    except ValueError as error:
        print(f"pack_resources.py: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
