/*
 * Copyright 2026 Development Desktop contributors
 * SPDX-License-Identifier: Apache-2.0 — Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Core C++20 Android runtime implementation: pinned artifact downloads,
 * safe archive extraction, bounded APK icon rendering, managed VM ownership,
 * framework overlay patching, and virtual-display window management.
 */
#pragma once

#include "common.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

namespace olympium::android {

inline constexpr const char *PACKAGE_REGEX = R"([A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z0-9_]+)+)";

inline bool is_valid_package(const std::string &s) {
    static const std::regex re(PACKAGE_REGEX);
    return std::regex_match(s, re);
}

/* ---- URL decoding for file:// URIs ---- */

inline std::string decode_file_uri(std::string_view uri) {
    std::string raw(uri.substr(7)); // strip "file://"
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == '%' && i + 2 < raw.size()) {
            char hex[3] = {raw[i + 1], raw[i + 2], 0};
            char *end = nullptr;
            unsigned long v = std::strtoul(hex, &end, 16);
            if (end == hex + 2) {
                out.push_back(static_cast<char>(v));
                i += 2;
                continue;
            }
        }
        out.push_back(raw[i]);
    }
    return out;
}

/* ---- Pinned artifact download (verifies .part before replacing target) ---- */

inline fs::path download_artifact(const Json &record, const fs::path &cache) {
    fs::create_directories(cache);
    std::string checksum = record.at("checksum").sval;
    std::string algo = record.at("algorithm").sval;
    std::string url = record.at("url").sval;
    std::string label = record.at("path").sval;
    int64_t expected_size = record.at("size").ival;

    fs::path target = cache / checksum;
    fs::path partial = target;
    partial += ".part";

    if (!fs::exists(target)) {
        std::fprintf(stderr, "Downloading Android support: %s\n", label.c_str());
        std::fflush(stderr);
        if (url.rfind("file://", 0) == 0) {
            fs::path src = decode_file_uri(url);
            fs::copy_file(src, partial, fs::copy_options::overwrite_existing);
        } else if (url.rfind("https://", 0) == 0) {
            std::vector<std::string> argv = {
                "curl", "-fsSL", "--connect-timeout", "30", "--max-time", "900",
                "-C", "-", "-o", partial.string(), "--", url
            };
            auto res = run_cmd(argv, 930);
            if (res.code != 0) {
                // Retry fresh if server rejected Range resume (HTTP 416 / 200).
                std::error_code ec;
                fs::remove(partial, ec);
                argv = {"curl", "-fsSL", "--connect-timeout", "30", "--max-time", "900",
                        "-o", partial.string(), "--", url};
                res = run_cmd(argv, 930);
                if (res.code != 0) {
                    throw std::runtime_error("Failed to download Android support: " + label);
                }
            }
        } else {
            throw std::runtime_error("Unsupported Android download URL scheme: " + url);
        }
        std::string actual = file_digest(partial, algo);
        int64_t actual_size = static_cast<int64_t>(fs::file_size(partial));
        if (actual_size != expected_size || actual != checksum) {
            std::error_code ec;
            fs::remove(partial, ec);
            throw std::runtime_error("Android publisher checksum mismatch: " + label);
        }
        fs::rename(partial, target);
        return target;
    }

    std::string actual = file_digest(target, algo);
    int64_t actual_size = static_cast<int64_t>(fs::file_size(target));
    if (actual_size != expected_size || actual != checksum) {
        throw std::runtime_error("Android publisher checksum mismatch: " + label);
    }
    return target;
}

inline void ensure_runtime(const fs::path &root, const fs::path &pins_path) {
    struct utsname u {};
    uname(&u);
    if (std::string(u.machine) != "x86_64") {
        throw std::runtime_error("The bundled Android 17 emulator currently requires Linux x86_64");
    }
    std::string pins_bytes = read_file_bytes(pins_path);
    fs::path marker = root / "runtime-version.json";
    if (fs::is_regular_file(marker) && read_file_bytes(marker) == pins_bytes) {
        return;
    }
    if (fs::exists(root) && fs::is_directory(root) && !fs::is_empty(root) && !fs::is_regular_file(marker)) {
        throw std::runtime_error("Refusing to replace an existing unrecognized Android runtime");
    }
    fs::create_directories(root.parent_path());
    fs::path cache = root.parent_path() / "downloads";
    fs::create_directories(cache);

    std::string tmpl = (root.parent_path() / "android-runtime-XXXXXX").string();
    std::vector<char> tbuf(tmpl.begin(), tmpl.end());
    tbuf.push_back('\0');
    char *sdir = mkdtemp(tbuf.data());
    if (!sdir) throw std::runtime_error("mkdtemp failed for Android runtime staging");
    fs::path stage(sdir);
    struct StageGuard {
        fs::path p;
        bool active = true;
        ~StageGuard() { if (active) { std::error_code ec; fs::remove_all(p, ec); } }
    } sg{stage, true};

    Json pins = Json::parse(pins_bytes);
    std::vector<fs::path> archives;
    for (const auto &record : pins.at("artifacts").aval) {
        fs::path archive = download_artifact(record, cache);
        if (record.get_string("format") == "apk") {
            fs::path dest = stage / record.at("destination").sval;
            fs::create_directories(dest.parent_path());
            fs::copy_file(archive, dest, fs::copy_options::overwrite_existing);
        } else {
            extract_archive(archive, record.at("destination").sval,
                            static_cast<int>(record.get_int("strip", 0)), stage);
        }
        archives.push_back(archive);
    }
    write_file_bytes(stage / "runtime-version.json", pins_bytes);

    fs::path backup;
    if (fs::exists(root)) {
        if (!fs::is_empty(root)) {
            if (!fs::is_regular_file(marker)) {
                throw std::runtime_error("Refusing to replace an existing unrecognized Android runtime");
            }
            backup = root.parent_path() / ("." + root.filename().string() + ".old-" + std::to_string(getpid()));
            std::error_code ec;
            fs::remove_all(backup, ec);
            fs::rename(root, backup);
        } else {
            fs::remove(root);
        }
    }
    try {
        fs::rename(stage, root);
        sg.active = false;
    } catch (...) {
        if (!backup.empty() && fs::exists(backup) && !fs::exists(root)) {
            std::error_code ec;
            fs::rename(backup, root, ec);
        }
        throw;
    }
    if (!backup.empty()) {
        std::error_code ec;
        fs::remove_all(backup, ec);
    }
    for (const auto &a : archives) {
        std::error_code ec;
        fs::remove(a, ec);
    }
}

/* ---- Bounded APK icon extraction & vector/adaptive SVG rendering ---- */

struct XmlNode {
    std::string tag;
    std::map<std::string, std::string> attrs;
    std::vector<std::unique_ptr<XmlNode>> children;

    std::string get(const std::string &k, const std::string &def = "") const {
        auto it = attrs.find(k);
        return it == attrs.end() ? def : it->second;
    }
    const XmlNode *find_child(const std::string &t) const {
        for (const auto &c : children) if (c->tag == t) return c.get();
        return nullptr;
    }
};

inline std::string xml_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

inline std::string fmt_num(double v) {
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

inline std::optional<fs::path> apk_icon(
    const fs::path &apk, const std::string &badging,
    const std::function<std::string(const std::vector<std::string> &)> &aapt,
    const fs::path &output_stem) {
    try {
        static const std::regex icon_re(R"(^application-icon-(\d+):'([^']+)'$)");
        int best_density = -1;
        std::string member;
        for (const auto &line : split_lines(badging)) {
            std::smatch m;
            if (std::regex_match(line, m, icon_re)) {
                int d = std::stoi(m[1].str());
                if (d > best_density) {
                    best_density = d;
                    member = m[2].str();
                }
            }
        }
        if (member.empty()) return std::nullopt;

        {
            ZipReader zr(apk);
            const ZipEntry *ent = zr.find(member);
            if (!ent) return std::nullopt;
            if (ent->uncomp_size > 4ULL * 1024 * 1024) return std::nullopt;
            if (member.size() >= 4 && member.compare(member.size() - 4, 4, ".png") == 0) {
                std::string data = zr.read_member(*ent, 4ULL * 1024 * 1024);
                static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
                if (data.size() < 24 || std::memcmp(data.data(), sig, 8) != 0) return std::nullopt;
                auto be32 = [&](size_t off) -> uint32_t {
                    return (static_cast<uint32_t>(static_cast<unsigned char>(data[off])) << 24) |
                           (static_cast<uint32_t>(static_cast<unsigned char>(data[off + 1])) << 16) |
                           (static_cast<uint32_t>(static_cast<unsigned char>(data[off + 2])) << 8) |
                           static_cast<uint32_t>(static_cast<unsigned char>(data[off + 3]));
                };
                uint32_t w = be32(16), h = be32(20);
                if (w == 0 || w > 2048 || h == 0 || h > 2048) return std::nullopt;
                fs::path out = output_stem;
                out.replace_extension(".png");
                write_file_bytes(out, data);
                return out;
            }
            if (member.size() < 4 || member.compare(member.size() - 4, 4, ".xml") != 0) {
                return std::nullopt;
            }
        }

        std::optional<std::string> resources_cache;
        std::function<std::string(const std::string &, int)> resolve = [&](const std::string &val, int depth) -> std::string {
            if (depth > 8) throw std::runtime_error("Recursive drawable");
            if (val.rfind("@0x", 0) != 0) return val;
            if (!resources_cache) {
                resources_cache = aapt({"dump", "--values", "resources", apk.string()});
            }
            std::string needle = "        resource " + val.substr(1) + " ";
            size_t pos = resources_cache->find(needle);
            if (pos == std::string::npos) throw std::runtime_error("Unresolved drawable");
            size_t nl = resources_cache->find('\n', pos);
            if (nl == std::string::npos) throw std::runtime_error("Unresolved drawable");
            size_t nl2 = resources_cache->find('\n', nl + 1);
            std::string next = nl2 == std::string::npos ? resources_cache->substr(nl + 1)
                                                        : resources_cache->substr(nl + 1, nl2 - nl - 1);
            if (next.rfind("          ", 0) != 0) throw std::runtime_error("Unresolved drawable");
            std::string data = next.substr(10);
            if (data.rfind("(reference) ", 0) == 0) {
                auto ws = split_ws(data);
                if (ws.empty()) throw std::runtime_error("Unresolved drawable");
                return resolve("@" + ws.back(), depth + 1);
            }
            if (data.rfind("(color) ", 0) == 0) {
                auto ws = split_ws(data);
                if (ws.empty()) throw std::runtime_error("Unresolved color");
                return ws.back();
            }
            if (data.rfind("(string8) ", 0) == 0 || data.rfind("(string16) ", 0) == 0) {
                size_t q = data.find('"');
                if (q == std::string::npos) throw std::runtime_error("Bad string resource");
                return Json::parse(data.substr(q)).sval;
            }
            throw std::runtime_error("Unsupported resource");
        };

        auto parse_tree = [&](const std::string &res_member) -> std::unique_ptr<XmlNode> {
            if (res_member.rfind("res/", 0) != 0) throw std::runtime_error("Invalid resource path");
            for (const auto &part : fs::path(res_member)) {
                if (part == "..") throw std::runtime_error("Invalid resource path");
            }
            std::string text = aapt({"dump", "xmltree", apk.string(), res_member});
            static const std::regex elem_re(R"(( *)E: ([A-Za-z-]+) .*)");
            static const std::regex attr_re(R"( *A: android:([A-Za-z]+)\([^)]*\)=(.*))");
            std::vector<std::pair<size_t, XmlNode *>> stack;
            std::unique_ptr<XmlNode> root;
            for (const auto &line : split_lines(text)) {
                std::smatch m;
                if (std::regex_match(line, m, elem_re)) {
                    size_t indent = m[1].str().size();
                    auto node = std::make_unique<XmlNode>();
                    node->tag = m[2].str();
                    XmlNode *raw = node.get();
                    while (!stack.empty() && stack.back().first >= indent) stack.pop_back();
                    if (!stack.empty()) stack.back().second->children.push_back(std::move(node));
                    else root = std::move(node);
                    stack.emplace_back(indent, raw);
                } else if (std::regex_match(line, m, attr_re) && !stack.empty()) {
                    std::string v = m[2].str();
                    if (!v.empty() && v[0] == '"') {
                        // Parse JSON string literal prefix (aapt may append " (Raw: ...)").
                        size_t i = 1;
                        while (i < v.size()) {
                            if (v[i] == '\\' && i + 1 < v.size()) { i += 2; continue; }
                            if (v[i] == '"') { i++; break; }
                            i++;
                        }
                        v = Json::parse(v.substr(0, i)).sval;
                    }
                    stack.back().second->attrs[m[1].str()] = v;
                }
            }
            if (!root) throw std::runtime_error("No drawable root");
            return root;
        };

        auto parse_number = [](const std::string &v, double def) -> double {
            if (v.empty()) return def;
            static const std::regex hex_float(R"(\(type 0x4\)0x([0-9a-fA-F]+))");
            std::smatch m;
            double result = 0.0;
            if (std::regex_match(v, m, hex_float)) {
                uint32_t bits = static_cast<uint32_t>(std::stoul(m[1].str(), nullptr, 16));
                float f = 0.0f;
                std::memcpy(&f, &bits, sizeof f);
                result = static_cast<double>(f);
            } else {
                result = std::stod(v);
            }
            if (!(result > -10000.0 && result < 10000.0)) throw std::runtime_error("Coordinate out of bounds");
            return result;
        };

        auto parse_color = [&](std::string val) -> std::pair<std::string, double> {
            val = resolve(val, 0);
            static const std::regex type_col(R"(\(type 0x1[c-f]\)0x([0-9a-fA-F]{8}))");
            std::smatch m;
            if (std::regex_match(val, m, type_col)) val = "#" + m[1].str();
            static const std::regex argb_re(R"(#[0-9a-fA-F]{8})");
            if (!std::regex_match(val, argb_re)) throw std::runtime_error("Unsupported color");
            unsigned int alpha = static_cast<unsigned int>(std::stoul(val.substr(1, 2), nullptr, 16));
            return {"#" + val.substr(3), static_cast<double>(alpha) / 255.0};
        };

        static const std::regex path_re(R"([MmZzLlHhVvCcSsQqTtAa0-9eE+.,\s-]+)");
        std::function<void(const XmlNode &, std::ostringstream &)> render =
            [&](const XmlNode &node, std::ostringstream &svg) {
            if (node.tag == "path") {
                std::string d = node.get("pathData");
                if (d.size() > 100000 || !std::regex_match(d, path_re)) {
                    throw std::runtime_error("Unsupported vector path");
                }
                auto [fill, op] = parse_color(node.get("fillColor", "#ff000000"));
                svg << "<path d=\"" << xml_escape(d) << "\" fill=\"" << fill
                    << "\" fill-opacity=\"" << fmt_num(op) << "\"";
                if (node.attrs.count("strokeColor")) {
                    auto [stroke, sop] = parse_color(node.get("strokeColor"));
                    double sw = parse_number(node.get("strokeWidth"), 0.0);
                    svg << " stroke=\"" << stroke << "\" stroke-opacity=\"" << fmt_num(sop)
                        << "\" stroke-width=\"" << fmt_num(sw) << "\"";
                }
                svg << "/>";
            } else if (node.tag == "group") {
                double tx = parse_number(node.get("translateX"), 0.0);
                double ty = parse_number(node.get("translateY"), 0.0);
                double px = parse_number(node.get("pivotX"), 0.0);
                double py = parse_number(node.get("pivotY"), 0.0);
                double sx = parse_number(node.get("scaleX"), 1.0);
                double sy = parse_number(node.get("scaleY"), 1.0);
                double rot = parse_number(node.get("rotation"), 0.0);
                svg << "<g transform=\"translate(" << fmt_num(tx) << " " << fmt_num(ty)
                    << ") translate(" << fmt_num(px) << " " << fmt_num(py)
                    << ") rotate(" << fmt_num(rot) << ") scale(" << fmt_num(sx) << " " << fmt_num(sy)
                    << ") translate(" << fmt_num(-px) << " " << fmt_num(-py) << ")\">";
                for (const auto &ch : node.children) render(*ch, svg);
                svg << "</g>";
            } else {
                throw std::runtime_error("Unsupported vector element");
            }
        };

        auto node = parse_tree(member);
        std::ostringstream svg;
        svg << "<?xml version='1.0' encoding='utf-8'?>\n";
        bool adaptive = (node->tag == "adaptive-icon");
        if (adaptive) {
            const XmlNode *bg = node->find_child("background");
            const XmlNode *fg = node->find_child("foreground");
            if (!bg || !fg) throw std::runtime_error("Incomplete adaptive icon");
            auto [fill, op] = parse_color(bg->get("drawable"));
            svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"128\" height=\"128\" viewBox=\"0 0 108 108\">";
            svg << "<rect width=\"108\" height=\"108\" rx=\"24\" fill=\"" << fill
                << "\" fill-opacity=\"" << fmt_num(op) << "\"/>";
            node = parse_tree(resolve(fg->get("drawable"), 0));
            if (node->tag != "vector") throw std::runtime_error("Unsupported icon type");
            double w = parse_number(node->get("viewportWidth"), 24.0);
            double h = parse_number(node->get("viewportHeight"), 24.0);
            if (w <= 0.0 || h <= 0.0) throw std::runtime_error("Empty vector viewport");
            svg << "<svg width=\"108\" height=\"108\" viewBox=\"0 0 " << fmt_num(w) << " " << fmt_num(h) << "\">";
            for (const auto &ch : node->children) render(*ch, svg);
            svg << "</svg></svg>";
        } else {
            if (node->tag != "vector") throw std::runtime_error("Unsupported icon type");
            double w = parse_number(node->get("viewportWidth"), 24.0);
            double h = parse_number(node->get("viewportHeight"), 24.0);
            if (w <= 0.0 || h <= 0.0) throw std::runtime_error("Empty vector viewport");
            svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"128\" height=\"128\" viewBox=\"0 0 "
                << fmt_num(w) << " " << fmt_num(h) << "\">";
            for (const auto &ch : node->children) render(*ch, svg);
            svg << "</svg>";
        }
        fs::path out = output_stem;
        out.replace_extension(".svg");
        write_file_bytes(out, svg.str());
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

/* ---- Android 10 & Android 17 window stack/root-task boundary parsing ---- */

inline std::string regex_escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (std::string(".^$|()[]{}+*?\\").find(c) != std::string::npos) out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

inline std::optional<std::pair<int, int>> app_root_task(const std::string &text, const std::string &package) {
    std::vector<std::string> blocks;
    auto lines = split_lines(text);
    for (const auto &line : lines) {
        if (line.rfind("RootTask id=", 0) == 0) blocks.push_back(line + "\n");
        else if (!blocks.empty()) blocks.back() += line + "\n";
    }
    static const std::regex hdr_re(R"(RootTask id=(\d+).*?\bdisplayId=(\d+)\b)");
    static const std::regex task_re(R"(^  taskId=\d+: (\S+)(.*)$)");
    std::regex top_re(R"(topActivity=ComponentInfo\{)" + regex_escape(package) + "/");
    std::vector<std::pair<int, int>> candidates;
    for (const auto &block : blocks) {
        auto blines = split_lines(block);
        if (blines.empty()) continue;
        std::smatch hm;
        if (!std::regex_search(blines[0], hm, hdr_re)) continue;
        int root_id = std::stoi(hm[1].str());
        int display_id = std::stoi(hm[2].str());
        std::vector<std::pair<std::string, std::string>> tasks, own;
        for (size_t i = 1; i < blines.size(); i++) {
            std::smatch tm;
            if (std::regex_match(blines[i], tm, task_re)) {
                std::string name = tm[1].str(), info = tm[2].str();
                tasks.emplace_back(name, info);
                if (name == package || name.rfind(package + "/", 0) == 0) own.emplace_back(name, info);
            }
        }
        if (own.empty()) continue;
        if (tasks.size() != 1) throw std::runtime_error("Refusing to move a root containing other tasks");
        if (own[0].second.find("visible=true") == std::string::npos ||
            !std::regex_search(own[0].second, top_re)) {
            continue;
        }
        candidates.emplace_back(display_id, root_id);
    }
    if (candidates.size() > 1) throw std::runtime_error("Ambiguous application root task");
    if (candidates.empty()) return std::nullopt;
    return candidates[0];
}

inline std::optional<std::pair<int, int>> app_stack(const std::string &text, const std::string &package) {
    struct Entry { int display; int stack; std::vector<std::string> lines; };
    std::vector<Entry> found;
    int display = -1;
    static const std::regex disp_re(R"(Display #(\d+).*)");
    static const std::regex std_re(R"(  Stack #(\d+): type=standard\b.*)");
    for (const auto &line : split_lines(text)) {
        std::smatch m;
        if (std::regex_match(line, m, disp_re)) {
            display = std::stoi(m[1].str());
        } else if (std::regex_match(line, m, std_re)) {
            found.push_back({display, std::stoi(m[1].str()), {}});
        } else if (line.rfind("  Stack #", 0) == 0) {
            found.push_back({display, -1, {}});
        } else if (!found.empty()) {
            found.back().lines.push_back(line);
        }
    }
    std::regex resumed_re(R"(mResumedActivity:.*\bu\d+ )" + regex_escape(package) + "/");
    std::regex app_re(R"(\bA=)" + regex_escape(package) + R"((?:\s|\}))");
    std::vector<std::pair<int, int>> candidates;
    for (const auto &e : found) {
        if (e.stack < 0) continue;
        std::string block;
        std::vector<std::string> tasks;
        for (const auto &l : e.lines) {
            block += l + "\n";
            if (l.rfind("    * TaskRecord{", 0) == 0) tasks.push_back(l);
        }
        if (!std::regex_search(block, resumed_re)) continue;
        if (tasks.size() != 1 || !std::regex_search(tasks[0], app_re)) {
            throw std::runtime_error("Refusing to move a stack containing other tasks");
        }
        candidates.emplace_back(e.display, e.stack);
    }
    if (candidates.size() > 1) throw std::runtime_error("Ambiguous application stack");
    if (candidates.empty()) return std::nullopt;
    return candidates[0];
}

/* ---- Managed runtime ownership & AVD lock cleanup ---- */

inline Json owned_runtime(const fs::path &root, const fs::path &runtime, const std::string &serial) {
    if (serial != "emulator-5560") {
        throw std::runtime_error("Automatic framework support requires the managed Android runtime");
    }
    Json record = Json::parse(read_file_bytes(root / "android/avd/process.json"));
    int64_t pid = record.at("pid").ival;
    auto current = process_identity(pid);
    if (current.to_json() != record) {
        throw std::runtime_error("Managed Android emulator process identity changed");
    }
    fs::path proc = fs::path("/proc") / std::to_string(pid);
    fs::path exe = fs::canonical(proc / "exe");
    fs::path emu_dir = fs::weakly_canonical(runtime) / "emulator";
    if (!path_is_relative_to(exe, emu_dir)) {
        throw std::runtime_error("Android emulator does not belong to the managed SDK");
    }
    std::string cmd_raw = read_file_bytes(proc / "cmdline");
    std::vector<std::string> words;
    size_t pos = 0;
    while (pos <= cmd_raw.size()) {
        size_t nul = cmd_raw.find('\0', pos);
        if (nul == std::string::npos) { words.push_back(cmd_raw.substr(pos)); break; }
        words.push_back(cmd_raw.substr(pos, nul - pos));
        pos = nul + 1;
    }
    auto has_pair = [&](const std::string &flag, const std::string &val) {
        for (size_t i = 0; i + 1 < words.size(); i++) {
            if (words[i] == flag && words[i + 1] == val) return true;
        }
        return false;
    };
    if (!has_pair("-avd", "olympium-android17") || !has_pair("-port", "5560")) {
        throw std::runtime_error("Android emulator command does not match the managed runtime");
    }
    std::string env_raw = read_file_bytes(proc / "environ");
    std::string expected_home = "ANDROID_AVD_HOME=" + (root / "android/avd").string();
    bool found_env = false;
    pos = 0;
    while (pos <= env_raw.size()) {
        size_t nul = env_raw.find('\0', pos);
        std::string entry = nul == std::string::npos ? env_raw.substr(pos) : env_raw.substr(pos, nul - pos);
        if (entry == expected_home) { found_env = true; break; }
        if (nul == std::string::npos) break;
        pos = nul + 1;
    }
    if (!found_env) {
        throw std::runtime_error("Android emulator belongs to another desktop data directory");
    }
    return record;
}

inline void clean_stale_avd_lock(const fs::path &avd) {
    fs::path lock = avd / "hardware-qemu.ini.lock";
    if (!fs::exists(lock)) return;
    struct stat st {};
    if (stat(lock.c_str(), &st) != 0) return;
    if (st.st_uid != getuid()) {
        throw std::runtime_error("Android AVD lock belongs to another user");
    }
    std::string text = read_file_bytes(lock);
    while (!text.empty() && text.back() == '\0') text.pop_back();
    text = trim(text);
    bool digits = !text.empty() && std::all_of(text.begin(), text.end(),
        [](unsigned char c) { return std::isdigit(c); });
    int64_t pid = 0;
    if (digits) {
        try { pid = std::stoll(text); } catch (...) { digits = false; }
    }
    if (!digits || pid < 1 || pid > 2147483647LL) {
        throw std::runtime_error("Invalid Android AVD lock; refusing to overwrite it");
    }
    fs::path proc = fs::path("/proc") / text;
    struct stat pst {};
    if (stat(proc.c_str(), &pst) == 0) {
        if (pst.st_uid != getuid()) return;
        std::string stat_txt = read_file_bytes(proc / "stat");
        size_t rp = stat_txt.rfind(')');
        if (rp != std::string::npos && rp + 2 < stat_txt.size()) {
            auto fields = split_ws(std::string_view(stat_txt).substr(rp + 2));
            if (!fields.empty() && fields[0] != "Z") return;
        }
    }
    std::error_code ec;
    fs::remove(lock, ec);
}

/* ---- Framework services.jar patching (aligned DEX) ---- */

inline fs::path patch_services(const fs::path &original, const fs::path &helper_dex,
                               const fs::path &output, const Json &profile) {
    if (file_digest(original, "sha256") != profile.at("original_services_jar_sha256").sval) {
        throw std::runtime_error("Android framework differs from the supported, pinned system image");
    }
    fs::create_directories(output);
    fs::path tree = output / "smali";
    if (fs::exists(tree)) {
        throw std::runtime_error("Framework compilation needs a fresh staging directory");
    }
    std::string target_dex = profile.at("target_dex").sval;
    fs::path dex = output / "original.dex";
    {
        ZipReader zr(original);
        const ZipEntry *ent = zr.find(target_dex);
        if (!ent) throw std::runtime_error("Target DEX missing from services.jar");
        write_file_bytes(dex, zr.read_member(*ent, 64ULL * 1024 * 1024));
    }
    if (run_cmd({"baksmali", "disassemble", dex.string(), "-o", tree.string()}, 180).code != 0) {
        throw std::runtime_error("baksmali disassemble failed");
    }
    fs::path engine = tree / "com/android/server/pm/ComputerEngine.smali";
    std::string text = read_file_bytes(engine);
    const std::string signature = "(Lcom/android/server/pm/pkg/PackageStateInternal;JI)Landroid/content/pm/PackageInfo;";
    const std::string method = ".method public final generatePackageInfo" + signature;
    const std::string renamed = "generatePackageInfoWithoutOlympiumCompat";
    size_t first = text.find(method);
    if (first == std::string::npos || text.find(method, first + 1) != std::string::npos ||
        text.find(renamed) != std::string::npos) {
        throw std::runtime_error("Unexpected Android package-manager API; framework was not changed");
    }
    text.replace(first, method.size(), ".method public final " + renamed + signature);
    text += "\n" + method + "\n"
            "    .registers 7\n"
            "    invoke-virtual/range {p0 .. p4}, Lcom/android/server/pm/ComputerEngine;->" + renamed + signature + "\n"
            "    move-result-object v0\n"
            "    invoke-static {v0, p1}, Lorg/olympium/android/compat/MicrogSignatureCompat;->rewrite(Landroid/content/pm/PackageInfo;Ljava/lang/Object;)Landroid/content/pm/PackageInfo;\n"
            "    move-result-object v0\n"
            "    return-object v0\n"
            ".end method\n";
    write_file_bytes(engine, text);
    fs::path patched = output / "patched.dex";
    if (run_cmd({"smali", "assemble", tree.string(), "-o", patched.string()}, 180).code != 0) {
        throw std::runtime_error("smali assemble failed");
    }
    fs::path jar = output / "services.jar";
    {
        ZipReader zr(original);
        ZipWriter zw(jar);
        std::set<std::string> names;
        for (const auto &e : zr.entries()) names.insert(e.name);
        int idx = 2;
        while (names.count("classes" + std::to_string(idx) + ".dex")) idx++;
        std::string patched_bytes = read_file_bytes(patched);
        for (const auto &e : zr.entries()) {
            std::string data = (e.name == target_dex) ? patched_bytes : zr.read_member(e, 64ULL * 1024 * 1024);
            zw.write_entry(e.name, data, true, e.method, e.ext_attr, e.extra);
        }
        zw.write_entry("classes" + std::to_string(idx) + ".dex", read_file_bytes(helper_dex), true, 0);
        zw.finish();
    }
    return jar;
}

/* ---- Managed Android window runner (scrcpy + virtual display) ---- */

static volatile sig_atomic_t g_window_term = 0;
inline void window_sigterm_handler(int) { g_window_term = 1; }

inline void run_android_window(const std::string &adb_bin, const std::string &scrcpy_bin,
                               const std::string &serial, const std::string &package,
                               const std::string &title, const fs::path &output) {
    if (!is_valid_package(package)) throw std::runtime_error("Invalid package identifier");
    fs::create_directories(output);
    auto shell = [&](const std::vector<std::string> &args, int timeout = 60) -> std::string {
        std::vector<std::string> argv = {adb_bin, "-s", serial, "shell"};
        argv.insert(argv.end(), args.begin(), args.end());
        auto res = run_cmd(argv, timeout);
        if (res.code != 0) throw std::runtime_error("adb shell failed: " + trim(res.output));
        return res.output;
    };

    std::string api = trim(shell({"getprop", "ro.build.version.sdk"}));
    if (api != "29" && api != "37") {
        throw std::runtime_error("This window backend targets Android 17 (API 37); API 29 remains a legacy test backend");
    }
    std::string resolved = shell({"cmd", "package", "resolve-activity", "--brief",
                                  "-a", "android.intent.action.MAIN",
                                  "-c", "android.intent.category.LAUNCHER", "-p", package});
    std::regex comp_re(regex_escape(package) + R"(/[A-Za-z0-9_.$]+)");
    std::vector<std::string> components;
    for (const auto &line : split_lines(resolved)) {
        if (std::regex_match(line, comp_re)) components.push_back(line);
    }
    if (components.size() != 1) throw std::runtime_error("No unique launcher activity");

    Json record = Json::object();
    record["package"] = package;
    record["serial"] = serial;
    record["android_api"] = api;
    record["component"] = components[0];
    record["status"] = "starting";
    record["desktop_input_verified"] = false;
    fs::path report = output / "window.json";
    auto save = [&]() { write_file_bytes(report, record.dump(2) + "\n"); };
    save();

    g_window_term = 0;
    auto prev_term = signal(SIGTERM, window_sigterm_handler);
    pid_t bridge = -1;
    try {
        fs::path scrcpy_log = output / "scrcpy.log";
        int logfd = open(scrcpy_log.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (logfd < 0) throw std::runtime_error("Cannot create scrcpy.log");
        bridge = fork();
        if (bridge == 0) {
            dup2(logfd, 1);
            dup2(logfd, 2);
            if (logfd > 2) close(logfd);
            setenv("ADB", adb_bin.c_str(), 1);
            setenv("SDL_APP_ID", package.c_str(), 1);
            std::string title_arg = "--window-title=" + title;
            execl(scrcpy_bin.c_str(), scrcpy_bin.c_str(), "-s", serial.c_str(),
                  "--new-display=360x640/160", "--no-vd-system-decorations",
                  title_arg.c_str(), "--no-audio", "--max-fps=15",
                  "--render-driver=software", static_cast<char *>(nullptr));
            _exit(127);
        }
        close(logfd);
        if (bridge < 0) throw std::runtime_error("Failed to fork scrcpy");

        int display = -1;
        static const std::regex disp_re(R"(New display:.*\(id=(\d+)\))");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(240);
        while (std::chrono::steady_clock::now() < deadline && !g_window_term) {
            int st = 0;
            if (waitpid(bridge, &st, WNOHANG) == bridge) {
                bridge = -1;
                throw std::runtime_error("scrcpy exited before creating its display");
            }
            std::string log_txt = read_file_bytes(scrcpy_log);
            std::smatch m;
            if (std::regex_search(log_txt, m, disp_re)) {
                display = std::stoi(m[1].str());
                break;
            }
            sleep(1);
        }
        if (display < 0) throw std::runtime_error("Virtual display readiness timed out");
        record["display_id"] = display;

        std::string launched = shell({"am", "start", "-W", "--display", std::to_string(display),
                                      "-a", "android.intent.action.MAIN",
                                      "-c", "android.intent.category.LAUNCHER",
                                      "-n", sh_quote(components[0])}, 180);
        write_file_bytes(output / "launch.txt", launched);
        if (launched.find("Error:") != std::string::npos ||
            launched.find("SecurityException") != std::string::npos) {
            throw std::runtime_error("Activity launch rejected");
        }

        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
        bool on_display = false;
        while (std::chrono::steady_clock::now() < deadline && !g_window_term) {
            int st = 0;
            if (waitpid(bridge, &st, WNOHANG) == bridge) {
                bridge = -1;
                throw std::runtime_error("scrcpy exited during app launch");
            }
            std::string activities;
            std::optional<std::pair<int, int>> stack;
            if (api == "37") {
                activities = shell({"am", "stack", "list"});
                stack = app_root_task(activities, package);
            } else {
                activities = shell({"dumpsys", "activity", "activities"});
                stack = app_stack(activities, package);
            }
            if (stack && stack->first != display) {
                shell({"am", "display", "move-stack", std::to_string(stack->second), std::to_string(display)});
            } else if (stack) {
                write_file_bytes(output / "activities.txt", activities);
                record["status"] = "task-on-app-display";
                record["stack_id"] = stack->second;
                save();
                std::printf("App task is on its virtual display; verify rendering and host input.\n");
                std::fflush(stdout);
                on_display = true;
                break;
            }
            sleep(2);
        }
        if (!on_display) throw std::runtime_error("App task did not remain on its own display");

        int st = 0;
        while (!g_window_term) {
            pid_t r = waitpid(bridge, &st, WNOHANG);
            if (r == bridge) { bridge = -1; break; }
            usleep(100000);
        }
        if (g_window_term) throw std::runtime_error("Terminated by SIGTERM");
        int code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
        if (code != 0) throw std::runtime_error("scrcpy exited with status " + std::to_string(code));
        record["status"] = "closed";
    } catch (const std::exception &e) {
        record["status"] = "failed";
        record["error"] = std::string(e.what());
        if (bridge > 0) {
            kill(bridge, SIGTERM);
            auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            int st = 0;
            while (waitpid(bridge, &st, WNOHANG) == 0 && std::chrono::steady_clock::now() < end) {
                usleep(100000);
            }
            if (waitpid(bridge, &st, WNOHANG) == 0) {
                kill(bridge, SIGKILL);
                waitpid(bridge, &st, 0);
            }
        }
        save();
        signal(SIGTERM, prev_term);
        throw;
    }
    save();
    signal(SIGTERM, prev_term);
}

} // namespace olympium::android
