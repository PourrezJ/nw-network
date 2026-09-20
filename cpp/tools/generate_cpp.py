#!/usr/bin/env python3
from __future__ import annotations
import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

def norm(value: str | None) -> str:
    return re.sub(r"[^a-z0-9]", "", (value or "").lower())

def ident(value: str | None, fallback: str) -> str:
    out = re.sub(r"[^A-Za-z0-9_]", "", value or "")
    if not out or not (out[0].isalpha() or out[0] == "_"):
        out = fallback
    return out

def cpp_string(value: object | None) -> str:
    return json.dumps("" if value is None else str(value), ensure_ascii=True)

def uuid_cpp(value: str | None) -> str:
    if not value:
        return "Uuid{}"
    raw = value.replace("-", "")
    if len(raw) != 32:
        return "Uuid{}"
    vals = ",".join("0x" + raw[i:i+2] for i in range(0, 32, 2))
    return "Uuid::from_bytes({" + vals + "})"

def balanced_body(text: str, open_pos: int) -> tuple[str, int]:
    depth = 0
    quote = None
    escape = False
    for i in range(open_pos, len(text)):
        ch = text[i]
        if quote is not None:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == quote:
                quote = None
            continue
        if ch == '"':
            quote = ch
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[open_pos + 1:i], i + 1
    raise RuntimeError("unterminated Rust struct")

def parse_fields(body: str) -> list[dict]:
    fields: list[dict] = []
    pos = 0
    pending_attrs: list[str] = []
    while pos < len(body):
        mws = re.match(r"\s+", body[pos:])
        if mws:
            pos += mws.end()
            continue
        if body.startswith("//", pos):
            nl = body.find("\n", pos)
            pos = len(body) if nl < 0 else nl + 1
            continue
        if body.startswith("#[", pos):
            end = body.find("]", pos + 2)
            if end < 0:
                break
            pending_attrs.append(body[pos:end + 1])
            pos = end + 1
            continue
        m = re.match(r"pub(?:\([^)]*\))?\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*", body[pos:])
        if not m:
            nl = body.find("\n", pos)
            pending_attrs.clear()
            pos = len(body) if nl < 0 else nl + 1
            continue
        name = m.group(1)
        type_start = pos + m.end()
        i = type_start
        angle = paren = bracket = brace = 0
        quote = None
        escape = False
        while i < len(body):
            ch = body[i]
            if quote is not None:
                if escape:
                    escape = False
                elif ch == "\\":
                    escape = True
                elif ch == quote:
                    quote = None
                i += 1
                continue
            if ch == '"':
                quote = ch
            elif ch == "<":
                angle += 1
            elif ch == ">":
                angle = max(0, angle - 1)
            elif ch == "(":
                paren += 1
            elif ch == ")":
                paren = max(0, paren - 1)
            elif ch == "[":
                bracket += 1
            elif ch == "]":
                bracket = max(0, bracket - 1)
            elif ch == "{":
                brace += 1
            elif ch == "}":
                brace = max(0, brace - 1)
            elif ch == "," and angle == paren == bracket == brace == 0:
                break
            i += 1
        ty = re.sub(r"\s+", " ", body[type_start:i].strip())
        attrs = " ".join(pending_attrs)
        pending_attrs.clear()
        pos = i + 1
        if "replicated_state(base)" in attrs or re.search(r"\bReplicatedState\b", ty) and "ReplicatedField" not in ty:
            continue
        if re.search(r"replicated_state\s*\([^)]*\bskip\b", attrs):
            continue
        group_m = re.search(r"\bgroup\s*=\s*([0-9_]+)", attrs)
        name_m = re.search(r'\bname\s*=\s*"([^"]+)"', attrs)
        fields.append({
            "name": name_m.group(1) if name_m else name,
            "source_name": name,
            "rust_type": ty,
            "group": int(group_m.group(1).replace("_", "")) if group_m else 0,
        })
    return fields

def parse_manual_states(root: Path) -> dict[str, dict]:
    found: dict[str, dict] = {}
    attr_struct = re.compile(r"((?:\s*#\[[^\]]+\]\s*)+)\s*pub\s+struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{", re.S)
    for path in sorted((root / "src" / "states").rglob("*.rs")):
        text = path.read_text(encoding="utf-8")
        for m in attr_struct.finditer(text):
            attrs, name = m.group(1), m.group(2)
            if "replicated_state" not in attrs:
                continue
            reg = re.search(r"type_registry\s*\(\s*([0-9_]+)", attrs)
            if not reg:
                continue
            body, _ = balanced_body(text, m.end() - 1)
            found[name] = {
                "type_index": int(reg.group(1).replace("_", "")),
                "fields": parse_fields(body),
                "source": str(path.relative_to(root)).replace("\\", "/"),
            }
    return found

def exported_states(root: Path) -> list[str]:
    text = (root / "src" / "states" / "mod.rs").read_text(encoding="utf-8")
    out: list[str] = []
    seen = set()
    for name in re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*ReplicatedState)\b", text):
        if name not in seen:
            seen.add(name)
            out.append(name)
    return out

def capabilities(item: dict) -> set[str]:
    return set(item.get("capabilities") or [])

def schema_candidates(item: dict) -> set[str]:
    vals = {item.get("rustName"), item.get("name")}
    name = item.get("name") or ""
    vals.add(name.split("::")[-1])
    vals.add(name.split("::")[-1] + "ReplicatedState")
    return {norm(v) for v in vals if v}

def align_manual_fields(schema_fields: list[dict], manual_fields: list[dict]) -> list[dict]:
    by_name = {norm(f.get("name")): f for f in schema_fields}
    out = []
    for idx, mf in enumerate(manual_fields):
        sf = by_name.get(norm(mf["name"])) or (schema_fields[idx] if idx < len(schema_fields) else {})
        out.append({
            "index": idx,
            "name": mf["name"],
            "group": mf["group"],
            "nativeType": sf.get("nativeType") or "",
            "rustType": mf["rust_type"] or sf.get("rustType") or "",
            "wireShape": sf.get("wireShape") or "",
        })
    return out

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--strict", action="store_true")
    args = ap.parse_args()
    root = Path(args.root).resolve()
    out = Path(args.out).resolve()
    (out / "nw_network").mkdir(parents=True, exist_ok=True)

    doc = json.loads((root / "crates/nw-network-types/codegen/network-schema.json").read_text(encoding="utf-8"))
    types = [x for x in doc.get("types", []) if x.get("typeIndex") is not None]
    types.sort(key=lambda x: int(x["typeIndex"]))
    by_index = {int(x["typeIndex"]): x for x in types}

    manual = parse_manual_states(root)
    exports = exported_states(root)

    replicated = [x for x in types if "replicated-state" in capabilities(x)]
    candidate_map: dict[str, list[dict]] = defaultdict(list)
    for item in replicated:
        for key in schema_candidates(item):
            candidate_map[key].append(item)

    resolved: dict[str, dict] = {}
    missing: list[str] = []
    ambiguous: dict[str, list[int]] = {}
    for name in exports:
        if name in manual:
            idx = manual[name]["type_index"]
            item = by_index.get(idx)
            if item is None:
                missing.append(name)
                continue
            resolved[name] = item
            continue
        candidates = candidate_map.get(norm(name), [])
        if len(candidates) == 1:
            resolved[name] = candidates[0]
        elif len(candidates) > 1:
            exact = [x for x in candidates if norm(x.get("rustName")) == norm(name)]
            if len(exact) == 1:
                resolved[name] = exact[0]
            else:
                ambiguous[name] = [int(x["typeIndex"]) for x in candidates]
                missing.append(name)
        else:
            missing.append(name)

    override_by_index: dict[int, list[dict]] = {}
    for name, meta in manual.items():
        item = by_index.get(meta["type_index"])
        if item:
            override_by_index[meta["type_index"]] = align_manual_fields(item.get("fields") or [], meta["fields"])

    flat_fields: list[dict] = []
    type_rows: list[dict] = []
    missing_shapes = 0
    unique_shapes = set()
    for item in types:
        idx = int(item["typeIndex"])
        fields = override_by_index.get(idx, sorted(item.get("fields") or [], key=lambda f: int(f.get("index", 0))))
        start = len(flat_fields)
        for field in fields:
            shape = field.get("wireShape") or ""
            if shape:
                unique_shapes.add(shape)
            elif "replicated-state" in capabilities(item):
                missing_shapes += 1
            flat_fields.append({
                "index": int(field.get("index", len(flat_fields) - start)),
                "name": field.get("name") or "",
                "group": -1 if field.get("group") is None else int(field.get("group")),
                "rust_type": field.get("rustType") or field.get("sourceTypeName") or "",
                "native_type": field.get("nativeType") or "",
                "wire_shape": shape,
            })
        raw_name = item.get("rustName") or (item.get("name") or "").split("::")[-1]
        type_rows.append({
            "type_id": item.get("typeId"),
            "type_index": idx,
            "name": item.get("name") or "",
            "cpp_name": ident(raw_name, "TypeIndex" + str(idx)),
            "replicated": "replicated-state" in capabilities(item),
            "direct": "direct-message" in capabilities(item) or "Messages::" in (item.get("name") or ""),
            "field_offset": start,
            "field_count": len(fields),
        })

    state_hpp = [
        "#pragma once",
        '#include "nw_network/nw_network.hpp"',
        "#include <cstddef>",
        "namespace nw::network::generated {",
    ]
    for name in exports:
        item = resolved.get(name)
        if not item:
            continue
        idx = int(item["typeIndex"])
        state_hpp += [
            "class " + name + " final : public DynamicReplicatedState {",
            "public:",
            "  static constexpr std::uint32_t TYPE_INDEX = " + str(idx) + "u;",
            "  " + name + "() : DynamicReplicatedState(TYPE_INDEX) {}",
            "};",
        ]
    state_hpp += [
        "inline constexpr std::size_t state_factory_count() noexcept { return " + str(len(resolved)) + "u; }",
        "} // namespace nw::network::generated",
        "",
    ]
    (out / "nw_network" / "generated_states.hpp").write_text("\n".join(state_hpp), encoding="utf-8")

    message_items = [r for r in type_rows if r["direct"]]
    used = set()
    msg_hpp = [
        "#pragma once",
        '#include "nw_network/nw_network.hpp"',
        "#include <cstddef>",
        "namespace nw::network::generated {",
    ]
    for row in message_items:
        cname = row["cpp_name"]
        if cname in used:
            cname += "_TypeIndex" + str(row["type_index"])
        used.add(cname)
        msg_hpp += [
            "struct " + cname + " {",
            "  static constexpr std::uint32_t TYPE_INDEX = " + str(row["type_index"]) + "u;",
            "};",
        ]
    msg_hpp += [
        "inline constexpr std::size_t message_factory_count() noexcept { return " + str(len(message_items)) + "u; }",
        "} // namespace nw::network::generated",
        "",
    ]
    (out / "nw_network" / "generated_messages.hpp").write_text("\n".join(msg_hpp), encoding="utf-8")

    cpp = [
        '// Generated by cpp/tools/generate_cpp.py',
        '#include "nw_network/nw_network.hpp"',
        "#include <algorithm>",
        "#include <array>",
        "namespace nw::network {",
        "namespace {",
        "constexpr std::array<NetworkFieldDescriptor, " + str(len(flat_fields)) + "> kFields{{",
    ]
    for f in flat_fields:
        cpp.append("  NetworkFieldDescriptor{" + str(f["index"]) + "u," + cpp_string(f["name"]) + "," + str(f["group"]) + "," + cpp_string(f["rust_type"]) + "," + cpp_string(f["native_type"]) + "," + cpp_string(f["wire_shape"]) + "},")
    cpp += ["}};", "constexpr std::array<NetworkTypeDescriptor, " + str(len(type_rows)) + "> kTypes{{"]
    for r in type_rows:
        cpp.append("  NetworkTypeDescriptor{" + uuid_cpp(r["type_id"]) + "," + str(r["type_index"]) + "u," + cpp_string(r["name"]) + "," + cpp_string(r["cpp_name"]) + "," + ("true" if r["replicated"] else "false") + "," + ("true" if r["direct"] else "false") + "," + str(r["field_offset"]) + "u," + str(r["field_count"]) + "u},")
    cpp += ["}};"]
    cpp += ["constexpr std::array<std::string_view, " + str(len(resolved)) + "> kExportedNames{{"]
    for name in exports:
        if name in resolved:
            cpp.append("  " + cpp_string(name) + ",")
    cpp += ["}};", "constexpr std::array<std::uint32_t, " + str(len(resolved)) + "> kExportedIds{{"]
    for name in exports:
        if name in resolved:
            cpp.append("  " + str(int(resolved[name]["typeIndex"])) + "u,")
    cpp += [
        "}};",
        "} // namespace",
        "std::span<const NetworkFieldDescriptor> network_fields() noexcept { return kFields; }",
        "std::span<const NetworkTypeDescriptor> network_types() noexcept { return kTypes; }",
        "const NetworkTypeDescriptor* type_by_index(std::uint32_t v) noexcept {",
        "  auto it=std::lower_bound(kTypes.begin(),kTypes.end(),v,[](const auto&x,auto key){return x.type_index<key;});",
        "  return it!=kTypes.end()&&it->type_index==v?&*it:nullptr;",
        "}",
        "std::span<const NetworkFieldDescriptor> fields_for(const NetworkTypeDescriptor& t) noexcept {",
        "  if(t.field_offset>kFields.size()||t.field_count>kFields.size()-t.field_offset)return {};",
        "  return std::span<const NetworkFieldDescriptor>(kFields).subspan(t.field_offset,t.field_count);",
        "}",
        "std::span<const std::string_view> exported_state_names() noexcept { return kExportedNames; }",
        "std::span<const std::uint32_t> exported_state_type_indices() noexcept { return kExportedIds; }",
        "} // namespace nw::network",
        "",
    ]
    (out / "network_schema_generated.cpp").write_text("\n".join(cpp), encoding="utf-8")

    manifest = {
        "summary": {
            "schema_types": len(types),
            "schema_fields": len(flat_fields),
            "replicated_schema_types": len(replicated),
            "exported_states": len(exports),
            "manual_replicated_states": len(manual),
            "resolved_exported_states": len(resolved),
            "missing_exported_states": len(missing),
            "ambiguous_exported_states": len(ambiguous),
            "generated_messages": len(message_items),
            "unique_wire_shapes": len(unique_shapes),
            "replicated_fields_missing_wire_shape": missing_shapes,
        },
        "missing_exported_states": missing,
        "ambiguous_exported_states": ambiguous,
        "manual_states": manual,
    }
    (out / "port_manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(manifest["summary"], indent=2))
    if missing:
        print("Missing exported states:", file=sys.stderr)
        for name in missing:
            print("  " + name, file=sys.stderr)
    if ambiguous:
        print("Ambiguous exported states:", json.dumps(ambiguous, indent=2), file=sys.stderr)
    if args.strict and missing:
        return 2
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
