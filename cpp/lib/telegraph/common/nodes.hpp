#ifndef __TELEGRAPH_NODES_HPP__
#define __TELEGRAPH_NODES_HPP__

#include "type.hpp"
#include "value.hpp"
#include "data.hpp"
#include "../utils/errors.hpp"

#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <map>

#include "common.pb.h"

namespace telegraph {

    class group;

    class context;
    using context_ptr = std::shared_ptr<context>;


    class node {
        friend std::ostream& operator<<(std::ostream&, const node&);
        friend group;
    public:
        using id = uint16_t;
        node(id i, const std::string_view& name,
             const std::string_view& pretty, const std::string_view& desc) :
                id_(i), name_(name), pretty_(pretty),
                desc_(desc), parent_(nullptr), owner_() {}
        node(const node& n) : id_(n.get_id()), name_(n.get_name()),
                              pretty_(n.get_pretty()), desc_(n.get_desc()),
                              parent_(nullptr), owner_() {}
        virtual ~node() {}

        node& operator=(const node& n) = delete;

        constexpr const id get_id() const { return id_; }
        constexpr const std::string& get_name() const { return name_; }
        constexpr const std::string& get_pretty() const { return pretty_; }
        constexpr const std::string& get_desc() const { return desc_; }

        constexpr group* get_parent() { return parent_; }
        constexpr const group* get_parent() const { return parent_; }

        // will return the /-prefixed path to this node
        std::string topic() const;
        std::vector<std::string> path() const;

        // To be overloaded by group
        virtual node* from_path(const std::vector<std::string_view>& p,
                                       size_t idx=0) {
            return idx != p.size() ? nullptr : this;
        }
        virtual const node* from_path(const std::vector<std::string_view>& p,
                                       size_t idx=0) const {
            return idx != p.size() ? nullptr : this;
        }
        virtual std::vector<node*> nodes() {
            std::vector<node*> n;
            n.push_back(this);
            return n;
        }
        virtual std::vector<const node*> nodes() const {
            std::vector<const node*> n;
            n.push_back(this);
            return n;
        }
        virtual node* operator[](size_t idx) { return nullptr; }
        virtual const node* operator[](size_t idx) const { return nullptr; }
        virtual node* operator[](const std::string& child) { return nullptr; }
        virtual const node* operator[](const std::string& child) const { return nullptr; }

        virtual void set_owner(const std::weak_ptr<context>& c) {
            if (owner_.lock() && c.lock())
                throw tree_error("node already has owner!");
            owner_ = c;
        }

        virtual void set_unowned() { owner_.reset(); }

        virtual bool compatible_with(node* other) const = 0;

        // pack and unpack functions
        virtual void pack(Node* proto) const = 0;
        static std::unique_ptr<node> unpack(const Node& proto);

        virtual std::unique_ptr<node> clone() const = 0;
    protected:
        constexpr void set_parent(group* g) { parent_ = g; }
        virtual void print(std::ostream& o, int ident=0) const;

        id id_;
        std::string name_;
        std::string pretty_; // For display
        std::string desc_; // For documentation

        group* parent_;
        std::weak_ptr<context> owner_;
    };

    // print operator
    inline std::ostream& operator<<(std::ostream& o, const node& n) {
        n.print(o, 0);
        return o;
    }

    class group : public node {
    public:
        group(id i, const std::string_view& name, const std::string_view& pretty,
                    const std::string_view& desc, const std::string_view& schema, int version,
                    std::vector<std::unique_ptr<node>>&& children) :
                node(i, name, pretty, desc),
                schema_(schema), version_(version), placeholders_(),
                children_(std::move(children)), children_map_() {
            // populate the children map
            for (auto& n : children_) {
                n->set_parent(this);
                children_map_[n->get_name()] = n.get();
            }
        }
        group(id i, const std::string_view& name, const std::string_view& pretty,
                    const std::string_view& desc, const std::string_view& schema, int version,
                    std::vector<node::id>&& placeholders) :
                node(i, name, pretty, desc),
                schema_(schema), version_(version), placeholders_(std::move(placeholders)),
                children_(), children_map_() {
            // populate the children map
            for (auto& n : children_) {
                n->set_parent(this);
                children_map_[n->get_name()] = n.get();
            }
        }
        group(const group& g) :
            node(g), schema_(g.get_schema()),
            version_(g.get_version()), children_(),
            children_map_() {
            for (auto& n : g) {
                std::unique_ptr<node> nn = n->clone();
                nn->set_parent(this);
                children_map_[nn->get_name()] = nn.get();
                children_.push_back(std::move(nn));
            }
        }
        ~group() {}

        const std::string& get_schema() const { return schema_; }
        int get_version() const { return version_; }

        void set_owner(const std::weak_ptr<context>& c) override {
            node::set_owner(c);
            for (auto& child : children_) child->set_owner(c);
        }

        void set_unowned() override {
            node::set_unowned();
            for (auto& c : children_) c->set_unowned();
        }

        node* from_path(const std::vector<std::string_view>& p,
                                size_t idx=0) override {
            if (idx > p.size()) return nullptr;
            else if (idx == p.size()) return this;
            else {
                auto it = children_map_.find(p[idx]);
                if (it == children_map_.end()) return nullptr;
                return it->second->from_path(p, ++idx);
            }
        }
        const node* from_path(const std::vector<std::string_view>& p,
                                        size_t idx=0) const override {
            if (idx > p.size()) return nullptr;
            else if (idx == p.size()) return this;
            else {
                auto it = children_map_.find(p[idx]);
                if (it == children_map_.end()) return nullptr;
                return it->second->from_path(p, ++idx);
            }
        }

        std::vector<node*> nodes() override {
            std::vector<node*> n;
            n.push_back(this);
            for (auto& c : children_) {
                for (node* d : c->nodes()) {
                    n.push_back(d);
                }
            }
            return n;
        }
        std::vector<const node*> nodes() const override {
            std::vector<const node*> n;
            n.push_back(this);
            for (auto& c : children_) {
                for (const node* d : c->nodes()) {
                    n.push_back(d);
                }
            }
            return n;
        }

        const std::vector<node::id>& placeholders() const {
            return placeholders_;
        }

        node* operator[](size_t idx) override {
            if (idx >= children_.size()) return nullptr;
            return children_[idx].get();
        }
        const node* operator[](size_t idx) const override {
            if (idx >= children_.size()) return nullptr;
            return children_[idx].get();
        }
        node* operator[](const std::string& child) override {
            try {
                return children_map_.at(child);
            } catch (const std::out_of_range& e) {
                return nullptr;
            }
        }
        const node* operator[](const std::string& child) const override {
            try {
                return children_map_.at(child);
            } catch (const std::out_of_range& e) {
                return nullptr;
            }
        }
        inline std::vector<std::unique_ptr<node>>::iterator begin() { return children_.begin(); }
        inline std::vector<std::unique_ptr<node>>::const_iterator begin() const { return children_.begin(); }
        inline std::vector<std::unique_ptr<node>>::iterator end() { return children_.end(); }
        inline std::vector<std::unique_ptr<node>>::const_iterator end() const { return children_.end(); }

        inline size_t num_children() const { return children_.size(); }

        bool compatible_with(node* other) const override;

        void pack(Group* group) const;
        virtual void pack(Node* proto) const override;
        static std::unique_ptr<group> unpack(const Group& g);

        template<typename M>
            bool resolve_placeholders(M* nodes) {
                for (node::id p : placeholders_) {
                    if (nodes->find(p) == nodes->end()) return false;
                }
                for (node::id p : placeholders_) {
                    auto nodes_it = nodes->find(p);
                    if (nodes_it == nodes->end()) return false;
                    node* child = nodes_it->second;

                    children_.push_back(std::unique_ptr<node>(child));
                    child->set_parent(this);
                    children_map_[child->get_name()] = child;

                    nodes->erase(nodes_it);
                }
                return true;
            }

        std::unique_ptr<node> clone() const override {
            return std::make_unique<group>(*this);
        }
    private:
        void print(std::ostream& o, int ident=0) const override;

        std::string schema_;
        int version_;

        std::vector<node::id> placeholders_;
        std::vector<std::unique_ptr<node>> children_;
        std::map<std::string, node*, std::less<>> children_map_;
    };


    class variable : public node {
    public:
        variable(id i, const std::string_view& name,
                const std::string_view& pretty, const std::string_view& desc,
                const value_type& t) : node(i, name, pretty, desc), data_type_(t) {}
        variable(const variable& v) :
            node(v), data_type_(v.get_type()) {}
        const value_type& get_type() const { return data_type_; }

        bool compatible_with(node* other) const override;

        void pack(Variable* var) const;
        virtual void pack(Node* proto) const override;
        static std::unique_ptr<variable> unpack(const Variable& proto);

        std::unique_ptr<node> clone() const override {
            return std::make_unique<variable>(*this);
        }
    private:
        void print(std::ostream& o, int ident=0) const override;
        value_type data_type_;
    };

    class action : public node {
    public:
        action(id i, const std::string_view& name,
                const std::string_view& pretty, const std::string_view& desc,
                const value_type& arg_type, const value_type& ret_type) :
                node(i, name, pretty, desc),
                arg_type_(arg_type), ret_type_(ret_type) {}
        action(const action& a) : node(a),
            arg_type_(a.get_arg_type()),
            ret_type_(a.get_ret_type()) {}
        const value_type& get_arg_type() const { return arg_type_; }
        const value_type& get_ret_type() const { return ret_type_; }

        bool compatible_with(node* other) const override;

        void pack(Action* proto) const;
        virtual void pack(Node* proto) const override;
        static std::unique_ptr<action> unpack(const Action& proto);

        std::unique_ptr<node> clone() const override {
            return std::make_unique<action>(*this);
        }
    private:
        void print(std::ostream& o, int ident=0) const override;
        value_type arg_type_;
        value_type ret_type_;
    };
}

#endif
