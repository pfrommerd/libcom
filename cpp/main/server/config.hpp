#include <telegraph/utils/hocon.hpp>
#include <telegraph/tree.hpp>

#include <filesystem>

telegraph::tree* read_config_file(const std::string& path);
