#ifndef __TELEGRAPH_CONFIGURATION_HPP__
#define __TELEGRAPH_CONFIGURATION_HPP__

#include "../utils/json_fwd.hpp"
#include "../common/nodes.hpp"

#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace telegraph {
    class tree;

    class profile {
    public:
        inline profile(const std::string& name) : name_(name), sets_() {}
        profile(std::string&& name) : name_(name), sets_() {}

        constexpr const std::string& get_name() const { return name_; }

        inline std::unordered_set<std::string> sets() const {
            std::unordered_set<std::string> s;
            for (auto a : sets_) s.insert(a.first);
            return s;
        }
        inline std::unordered_set<node*> &get(const std::string& name) { return sets_.at(name); }
        inline const std::unordered_set<node*> &get(const std::string& name) const { return sets_.at(name); }

        inline void add(const std::string& name, const std::unordered_set<node*>& nodes) {
            std::unordered_set<node*> s;
            for (node* n : nodes) s.insert(n);
            sets_[name] = std::move(s);
        }
        inline void add(const std::string& name, std::unordered_set<node*>&& nodes) {
            sets_[name] = std::move(nodes);
        }
    private:
        std::string name_;
        std::unordered_map<std::string, std::unordered_set<node*>> sets_;
    };

    // configs are named sets of nodes
    class config {
    public:
        config(const json& j);
        ~config();

        const node* get_tree() const { return tree_.get(); }

        profile& get_profile(const std::string& name) { return profiles_.at(name); }
        const profile& get_profile(const std::string& name) const { return profiles_.at(name); }
    private:
        std::unique_ptr<node> tree_;
        std::unordered_map<std::string, profile> profiles_;
    };
}
#endif
