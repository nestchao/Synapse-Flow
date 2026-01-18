#include "tools/FileSystemTools.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <spdlog/spdlog.h>
#include <omp.h>
#include <vector>
#include <string>
#include <algorithm>

namespace code_assistance {

namespace fs = std::filesystem;

// 🚀 HELPER: Base64 Decode
std::string base64_decode(const std::string &in) {
    std::string out;
    std::vector<int> T(256, -1);
    static const char* code = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) T[code[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// 🚀 RESOLVE ID -> REAL PATH
std::string FileSystemTools::resolve_project_root(const std::string& project_id) {
    fs::path config_path = fs::path("data") / project_id / "config.json";
    
    if (fs::exists(config_path)) {
        try {
            std::ifstream f(config_path);
            auto j = nlohmann::json::parse(f);
            std::string local_path = j.value("local_path", "");
            if (!local_path.empty() && fs::exists(local_path)) {
                return local_path;
            }
        } catch (...) {
            spdlog::error("❌ Failed to parse config for {}", project_id);
        }
    }
    
    // Fallback: Try Base64 Decode
    try {
        std::string decoded = base64_decode(project_id);
        // Simple sanity check: does it look like a path and exist?
        if (decoded.length() > 2 && (decoded[1] == ':' || decoded[0] == '/')) {
            if (fs::exists(decoded)) {
                spdlog::info("🔓 Auto-Resolved Base64 Project ID: {} -> {}", project_id, decoded);
                return decoded;
            }
        }
    } catch(...) {}

    // Last Resort: Treat ID as raw path
    if (fs::exists(project_id)) return project_id;

    return ""; // Failed to resolve
}

// 🛡️ SECURITY SANDBOX
bool FileSystemTools::is_safe_path(const fs::path& root, const fs::path& target) {
    if (root.empty()) return false;
    
    try {
        auto root_abs = fs::absolute(root).lexically_normal();
        auto target_abs = fs::absolute(target).lexically_normal();

        std::string root_str = root_abs.string();
        std::string target_str = target_abs.string();

        // Convert to lower case for Windows case-insensitive check
        std::string root_lower = root_str;
        std::string target_lower = target_str;
        std::transform(root_lower.begin(), root_lower.end(), root_lower.begin(), ::tolower);
        std::transform(target_lower.begin(), target_lower.end(), target_lower.begin(), ::tolower);

        // Check if target starts with root
        if (target_lower.find(root_lower) != 0) {
            spdlog::warn("🚨 SECURITY ALERT: Path escape blocked! Root: {} | Target: {}", root_str, target_str);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Helper for filtering
bool is_inside_path(const fs::path& child, const fs::path& parent) {
    if (parent.empty()) return false;
    auto c = child.lexically_normal();
    auto p = parent.lexically_normal();
    auto it_c = c.begin();
    for (auto it_p = p.begin(); it_p != p.end(); ++it_p) {
        if (it_c == c.end() || *it_c != *it_p) return false;
        ++it_c;
    }
    return true;
}

bool FileSystemTools::is_path_allowed(const std::string& project_id, const fs::path& target_path) {
    std::string root_str = resolve_project_root(project_id);
    if (root_str.empty()) return false;
    
    fs::path root = fs::path(root_str);
    
    // 1. Sandbox Check
    if (!is_safe_path(root, target_path)) return false;

    // 2. Load Config
    ProjectFilter filter = load_config(project_id);
    if (filter.ignored_paths.empty()) return true; // No restrictions

    // 3. Calculate Relative Path for matching
    std::error_code ec;
    fs::path rel_path = fs::relative(target_path, root, ec);
    if (ec) return false;

    // 4. Check Blocklist
    bool is_ignored = false;
    for (const auto& p : filter.ignored_paths) {
        if (is_inside_path(rel_path, fs::path(p))) {
            is_ignored = true;
            break;
        }
    }

    // 5. Check Whitelist (Exception)
    // If it's ignored, we only allow it if it is inside an included path
    if (is_ignored) {
        bool is_exception = false;
        for (const auto& p : filter.included_paths) {
            if (is_inside_path(rel_path, fs::path(p))) {
                is_exception = true;
                break;
            }
        }
        return is_exception; // Allow only if exception found
    }

    return true; // Not ignored
}

ProjectFilter FileSystemTools::load_config(const std::string& project_id) {
    ProjectFilter filter;
    fs::path config_path = fs::path("data") / project_id / "config.json";

    if (fs::exists(config_path)) {
        try {
            std::ifstream f(config_path);
            auto j = nlohmann::json::parse(f);
            filter.allowed_extensions = j.value("allowed_extensions", std::vector<std::string>{});
            filter.ignored_paths = j.value("ignored_paths", std::vector<std::string>{});
            filter.included_paths = j.value("included_paths", std::vector<std::string>{});
        } catch (...) {}
    }
    return filter;
}

std::string FileSystemTools::list_dir_deep(const std::string& project_id, const std::string& sub, const ProjectFilter& filter, int max_depth) {
    
    std::string root_str = resolve_project_root(project_id);
    if (root_str.empty()) return "ERROR: Project path invalid or not registered.";

    fs::path base_root = fs::path(root_str);
    fs::path target_path;
    if (sub == "." || sub.empty() || sub == "/" || sub == "\\") {
        target_path = base_root;
    } else {
        target_path = (base_root / sub);
    }

    if (!is_safe_path(base_root, target_path)) return "ERROR: Access Denied (Outside Workspace).";
    if (!fs::exists(target_path)) return "ERROR: Path not found.";

    std::vector<fs::directory_entry> all_entries;
    try {
        auto iter_options = fs::directory_options::skip_permission_denied;
        for (const auto& entry : fs::recursive_directory_iterator(target_path, iter_options)) {
            all_entries.push_back(entry);
            if (all_entries.size() > 5000) break; 
        }
    } catch (...) {}

    std::vector<std::string> results(all_entries.size(), "");
    int found_count = 0;

    #pragma omp parallel
    {
        #pragma omp for reduction(+:found_count)
        for (int i = 0; i < (int)all_entries.size(); ++i) {
            const auto& entry = all_entries[i];
            fs::path current = entry.path();
            std::error_code ec;
            
            auto depth_rel = fs::relative(current, target_path, ec);
            if (ec) continue;

            int depth = 0;
            for (auto it = depth_rel.begin(); it != depth_rel.end(); ++it) depth++;
            if (depth > max_depth) continue;

            auto rel_path = fs::relative(current, base_root, ec);
            if (ec) continue;

            bool is_ignored = false;
            for (const auto& p : filter.ignored_paths) {
                if (is_inside_path(rel_path, fs::path(p))) { is_ignored = true; break; }
            }

            bool is_exception = false;
            for (const auto& p : filter.included_paths) {
                if (is_inside_path(rel_path, fs::path(p))) { is_exception = true; break; }
            }

            // 🚀 BRIDGE CHECK: Is this folder a parent of an exception?
            bool is_bridge = false;
            if (is_ignored) {
                for (const auto& p : filter.included_paths) {
                    if (is_inside_path(fs::path(p), rel_path)) { 
                        is_bridge = true; 
                        break; 
                    }
                }
            }

            if (entry.is_directory()) {
                // Allow if NOT ignored, OR is exception, OR is bridge to exception
                if (is_ignored && !is_exception && !is_bridge) continue; 
            } else {
                if (is_ignored && !is_exception) continue;
                
                std::string ext = current.extension().string();
                if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                bool ext_match = filter.allowed_extensions.empty();
                for (const auto& a : filter.allowed_extensions) if (ext == a) { ext_match = true; break; }
                if (!ext_match && !is_exception) continue;
            }

            std::string line = "";
            for (int d = 0; d < depth - 1; ++d) line += "  ";
            line += (entry.is_directory() ? "📁 " : "📄 ") + rel_path.generic_string() + "\n";
            results[i] = line;
            found_count++;
        }
    }

    std::stringstream ss;
    ss << "📂 WORKSPACE: " << base_root.generic_string() << "\n";
    for (const auto& s : results) if (!s.empty()) ss << s;
    if (found_count == 0) ss << "(No visible files matching filters)\n";

    return ss.str();
}

// 🚀 THIS WAS MISSING IN THE PREVIOUS TURN
std::string FileSystemTools::read_file_safe(const std::string& project_id, const std::string& rel) {
    std::string root_str = resolve_project_root(project_id);
    if (root_str.empty()) return "ERROR: Project path invalid.";

    fs::path target = (fs::path(root_str) / rel);
    
    if (!is_safe_path(fs::path(root_str), target)) return "ERROR: Security Block (Path Traversal).";

    // 🚀 NEW: CHECK FILTER
    if (!is_path_allowed(project_id, target)) {
        spdlog::warn("🛑 ACCESS DENIED (Ignored Path): {}", target.string());
        return "ERROR: Access Denied. This path is in the project's ignored list.";
    }

    if (!fs::exists(target)) {
        return "ERROR: File not found at " + rel;
    }
    
    if (fs::file_size(target) > 1024 * 512) return "ERROR: File too large (>512KB).";

    std::ifstream f(target, std::ios::in | std::ios::binary);
    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

// 🔧 Wrapper Updates
std::string ReadFileTool::execute(const std::string& args_json) {
    try {
        auto j = nlohmann::json::parse(args_json);
        std::string pid = j.value("project_id", "");
        std::string path = j.value("path", "."); // Default to "."
        
        // Normalize root request
        if (path == "/" || path == "\\" || path.empty()) path = ".";
        
        auto filter = FileSystemTools::load_config(pid);
        return FileSystemTools::list_dir_deep(pid, path, filter, j.value("depth", 2));
    } catch (...) { return "ERROR: Invalid JSON."; }
}

std::string ListDirTool::execute(const std::string& args_json) {
    try {
        auto j = nlohmann::json::parse(args_json);
        std::string pid = j.value("project_id", "");
        auto filter = FileSystemTools::load_config(pid);
        return FileSystemTools::list_dir_deep(pid, j.value("path", "."), filter, j.value("depth", 2));
    } catch (...) { return "ERROR: Invalid JSON."; }
}

}