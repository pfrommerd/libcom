#include "config.hpp"

#include "../utils/errors.hpp"
#include "../utils/json.hpp"

#include <sstream>
#include <iostream>

namespace telegraph {
    static void unpack_profile(profile* p,
                               const std::vector<node*>& nodes,
                               const json& j) {
        for (const auto& item : j.items()) {
            const json& filter = item.value();

            const std::string& name = item.key();
            std::vector<std::string> includes =
                filter.value<std::vector<std::string>>("includes",{});
            std::vector<std::string> excludes =
                filter.value<std::vector<std::string>>("excludes",{});

            std::unordered_set<node*> set;
            for (node* n : nodes) {
                std::string path = n->topic();
                bool inc = false;
                for (const std::string& s : includes) {
                    if (path.size() > s.size() &&
                            path.substr(0, s.size()) == s) {
                        inc = true;
                        break;
                    }
                }
                if (inc) {
                    for (const std::string& s : excludes) {
                        if (path.size() > s.size() &&
                                path.substr(0, s.size()) == s) {
                            inc = false;
                            break;
                        }
                    }
                }
                if (inc) set.insert(n);
            }
            p->add(name, std::move(set));
        }
    }

    static std::unordered_map<std::string, value_type::type_class> s_type_map = {
        {"invalid", value_type::Invalid},
        {"none", value_type::None},
        {"enum", value_type::Enum},
        {"bool", value_type::Bool},
        {"uint8", value_type::Uint8},
        {"uint16", value_type::Uint16},
        {"uint32", value_type::Uint32},
        {"uint64", value_type::Uint64},
        {"int8", value_type::Int8},
        {"int16", value_type::Int16},
        {"int32", value_type::Int32},
        {"int64", value_type::Int64},
        {"float", value_type::Float},
        {"double", value_type::Double}
    };
    static
    value_type::type_class unpack_type_class(const std::string& tc) {
        try {
            return s_type_map.at(tc);
        } catch (const std::out_of_range& r) {
            return value_type::Invalid;
        }
    }
    static std::unique_ptr<node> unpack_node(int32_t* id_counter, const std::string& name, const json& json);

    static value_type unpack_type(const json& json) {
        value_type t(value_type::Invalid);
        if (json.is_object()) {
            if (json.find("type") == json.end())
                throw parse_error("for type objects, expecting type key");

            t = unpack_type(json["type"]);
            if (json.find("type_name") != json.end()) {
                t.set_name(json["type_name"]);
            }
            if (t.get_class() == value_type::Enum) {
                std::vector<std::string> strs = json.value<std::vector<std::string>>("labels", {});
                t.set_labels(std::move(strs));
                if (json.find("type_name") == json.end())
                    throw parse_error("enum type expects type_name: " + json.dump());
            }
        } else if (json.is_string()) {
            t = value_type(unpack_type_class(json.get<std::string>()));
        } else throw parse_error("unable to parse type: " + json.dump());
        return t;
    }

    static std::unique_ptr<variable> unpack_variable(int32_t* id_counter, const std::string& name,
                                     const json& json) {
        value_type t = unpack_type(json);
        std::string pretty = json.is_object() ? json.value("pretty", "") : "";
        std::string desc = json.is_object() ? json.value("desc", "") : "";
        return std::make_unique<variable>((*id_counter)++, name, pretty, desc, t);
    }

    static std::unique_ptr<action> unpack_action(int32_t* id_counter, const std::string& name,
                                 const json& json) {
        value_type arg(value_type::None);
        value_type ret(value_type::None);
        if (json.find("arg") != json.end()) arg = unpack_type(json["arg"]);
        if (json.find("ret") != json.end()) ret = unpack_type(json["ret"]);
        std::string pretty = json.value("pretty", "");
        std::string desc = json.value("desc", "");
        return std::make_unique<action>((*id_counter)++, name, pretty, desc, arg, ret);
    }

    static std::unique_ptr<group> unpack_group(int32_t* id_counter, const std::string& name,
                                const json& json) {
        int32_t id = (*id_counter)++;
        std::string schema = json.value("schema", "none");
        int version = json.value("version", 0);
        std::string pretty = json.value("pretty", "");
        std::string desc = json.value("desc", "");
        std::vector<std::unique_ptr<node>> children;
        for (const auto& item : json.items()) {
            const auto& key = item.key();
            if (key != "schema" && key != "version" &&
                 key != "pretty" && key != "desc" && key != "type") {
                children.push_back(std::move(unpack_node(id_counter, key, item.value())));
            }
        }
        return std::make_unique<group>(id, name, pretty, desc, schema, version, std::move(children));
    }

    static std::unique_ptr<node> unpack_array(int32_t* id_counter, const std::string& name, const json& j) {
        int32_t id = (*id_counter)++;
        size_t length = j.value("length", 0);
        if (length == 0) {
            throw parse_error("array must have array_length > 0");
        }
        int version = j.value("version", 0);
        std::string pretty = j.value("pretty", "");
        std::string desc = j.value("desc", "");

        if (j.find("of") == j.end()) {
            throw parse_error("array must have 'of' key supplying array type");
        }
        const json& array_type = j["of"];

        std::vector<std::unique_ptr<node>> children;
        for (size_t i = 0; i < length; i++) {
            children.push_back(unpack_node(id_counter, std::to_string(i), array_type));
        }
        return std::make_unique<group>(id, name, pretty, desc, "array", version, std::move(children));
    }

    static std::unique_ptr<node> unpack_node(int32_t* id_counter, const std::string& name,
                             const json& json) {
        if (json.is_string()) return unpack_variable(id_counter, name, json);
        if (!json.is_object()) throw parse_error("unable to parse node: " + json.dump());

        std::string type = json.value("type", "group");
        if (type == "group") {
            return unpack_group(id_counter, name, json);
        } else if (type == "action") {
            return unpack_action(id_counter, name, json);
        } else if (type == "array") {
            // pseudo-type -- translates into group with
            // "array" schema and a bunch of children
            return unpack_array(id_counter, name, json);
        } else {
            return unpack_variable(id_counter, name, json);
        }
    }

    config::config(const json& j) : tree_(nullptr), profiles_() {
        int32_t id_counter = 0;
        tree_ = unpack_node(&id_counter, "root", j["root"]);

        std::vector<node*> nodes = tree_->nodes();
        if (j.find("configs") != j.end()) {
            auto& c = j["configs"];
            for (const auto& item : c.items()) {
                std::string name = item.key();
                profile p = profile(name);
                unpack_profile(&p, nodes, item.value());
                profiles_.emplace(std::make_pair(name, std::move(p)));
            }
        }
    }

    config::~config() {}
}
