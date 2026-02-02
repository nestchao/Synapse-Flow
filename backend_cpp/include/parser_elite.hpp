#pragma once
#include <tree_sitter/api.h>
#include <string>
#include <vector>
#include <filesystem>
#include "code_graph.hpp"

// Forward declare grammars from third_party
extern "C" {
    TSLanguage* tree_sitter_cpp();
    TSLanguage* tree_sitter_python();
    TSLanguage* tree_sitter_json();
}

namespace code_assistance {
    namespace elite {    

class ASTBooster {
public:
    ASTBooster();
    ~ASTBooster();

    // 🛡️ The Eyes of the Journal: Returns true if code is syntactically perfect
    bool validate_syntax(const std::string& content, const std::string& extension);

    // 🛰️ The Map Maker: Breaks file into logical nodes
    std::vector<CodeNode> extract_symbols(const std::string& path, const std::string& content);

private:
    TSParser* parser_;
    const TSLanguage* get_lang(const std::string& ext);
};

    }
}