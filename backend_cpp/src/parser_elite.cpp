#include "parser_elite.hpp"
#include <tree_sitter/api.h>
#include <spdlog/spdlog.h>
#include <stack>

// 🚀 EXTERNAL LINKING
extern "C" {
    const TSLanguage* tree_sitter_cpp();
    const TSLanguage* tree_sitter_python();
    // const TSLanguage* tree_sitter_typescript(); // <--- COMMENT THIS OUT
}

namespace code_assistance::elite {

ASTBooster::ASTBooster() {
    parser_ = ts_parser_new();
}

ASTBooster::~ASTBooster() {
    if (parser_) ts_parser_delete(parser_);
}

const TSLanguage* ASTBooster::get_lang(const std::string& ext) {
    if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc") return tree_sitter_cpp();
    if (ext == ".py") return tree_sitter_python();
    
    // 🚀 TEMPORARILY DISABLED until TS grammar is fully linked
    // if (ext == ".ts" || ext == ".js" || ext == ".tsx" || ext == ".jsx") return tree_sitter_typescript();
    
    return nullptr;
}

bool ASTBooster::validate_syntax(const std::string& content, const std::string& extension) {
    const TSLanguage* lang = get_lang(extension);
    
    // If we don't support the language (or it's disabled), we assume it's valid to avoid blocking edits
    if (!lang) return true;

    ts_parser_set_language(parser_, lang);
    
    TSTree* tree = ts_parser_parse_string(parser_, nullptr, content.c_str(), (uint32_t)content.length());
    if (!tree) return false;

    TSNode root = ts_tree_root_node(tree);
    bool has_error = ts_node_has_error(root);
    
    if (!has_error) {
        if (ts_node_is_missing(root)) has_error = true;
    }

    ts_tree_delete(tree);
    return !has_error;
}

// ... rest of file (extract_symbols) ...
std::vector<CodeNode> ASTBooster::extract_symbols(const std::string& path, const std::string& content) {
    std::vector<CodeNode> nodes;
    std::string ext = std::filesystem::path(path).extension().string();
    const TSLanguage* lang = get_lang(ext);
    if (!lang) return {}; // Returns empty vector if language not supported

    ts_parser_set_language(parser_, lang);
    TSTree* tree = ts_parser_parse_string(parser_, nullptr, content.c_str(), (uint32_t)content.length());
    TSNode root = ts_tree_root_node(tree);

    std::stack<TSNode> traversal_stack;
    traversal_stack.push(root);

    while (!traversal_stack.empty()) {
        TSNode node = traversal_stack.top();
        traversal_stack.pop();

        std::string type = ts_node_type(node);
        
        if (type == "function_definition" || type == "class_specifier" || 
            type == "method_definition" || type == "function_item" || type == "struct_specifier") {
            
            CodeNode info;
            info.name = "unnamed_symbol";
            info.type = type;
            info.file_path = path;

            uint32_t child_count = ts_node_child_count(node);
            for (uint32_t i = 0; i < child_count; i++) {
                TSNode child = ts_node_child(node, i);
                std::string c_type = ts_node_type(child);
                if (c_type == "identifier" || c_type == "field_identifier" || c_type == "type_identifier") {
                    uint32_t start = ts_node_start_byte(child);
                    uint32_t end = ts_node_end_byte(child);
                    info.name = content.substr(start, end - start);
                    break;
                }
            }
            nodes.push_back(info);
        }

        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            traversal_stack.push(ts_node_child(node, i));
        }
    }
    
    ts_tree_delete(tree);
    return nodes;
}

}