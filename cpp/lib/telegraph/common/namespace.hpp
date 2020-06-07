#ifndef __TELEGRAPH_NAMESPACE_HPP__
#define __TELEGRAPH_NAMESPACE_HPP__

#include "collection.hpp"
#include "value.hpp"
#include "data.hpp"
#include "params.hpp"

#include "../utils/uuid.hpp"
#include "../utils/io_fwd.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace telegraph {
    class namespace_;
    class variable;
    class action;
    class node;

    class subscription;
    using subscription_ptr = std::shared_ptr<subscription>;

    class data_point;
    class data_query;
    using data_query_ptr = std::shared_ptr<data_query>;

    class component;
    using component_ptr = std::shared_ptr<component>;

    class context;
    using context_ptr = std::shared_ptr<context>;

    struct mount_info {
        constexpr mount_info() 
            : src(), tgt() {}
        mount_info(const std::weak_ptr<context>& src, 
                             const std::weak_ptr<context>& tgt) 
            : src(src), tgt(tgt) {}
        std::weak_ptr<context> src;
        std::weak_ptr<context> tgt;
    };

    // underscore is to not conflict with builtin
    // namespace token
    class namespace_ {
    public:
        // these are public intentionally to force subclasses
        // to go through these variables!
        collection_ptr<mount_info> mounts;
        collection_ptr<context_ptr> contexts;
        collection_ptr<component_ptr> components;

        namespace_() {
            mounts = std::make_shared<collection<mount_info>>();
            contexts = std::make_shared<collection<context_ptr>>();
            components = std::make_shared<collection<component_ptr>>();
        }

        virtual context_ptr create_context(io::yield_ctx&, 
                    const std::string_view& name, const std::string_view& type, 
                    const params& p) = 0;

        virtual void destroy_context(io::yield_ctx&, const uuid& u) = 0;

        virtual component_ptr create_component(io::yield_ctx&, 
                    const std::string_view& name, const std::string_view& type, 
                    const params& p) = 0;

        virtual void destroy_component(io::yield_ctx&, const uuid& u) = 0;
    };

    class component : public std::enable_shared_from_this<component> {
    public:
        inline component(io::io_context& ioc, 
                uuid id, const std::string_view& name, 
                const std::string_view& type, const params& p) : 
                    ioc_(ioc), uuid_(id), 
                    name_(name), type_(type), params_(p) {}

        constexpr io::io_context& get_executor() { return ioc_; }

        virtual std::shared_ptr<namespace_> get_namespace() = 0;
        virtual std::shared_ptr<const namespace_> get_namespace() const = 0;

        const std::string& get_name() const { return name_; }
        const std::string& get_type() const { return type_; }
        const params& get_params() const { return params_; }
        const uuid& get_uuid() const { return uuid_; }

        virtual params_stream_ptr request(io::yield_ctx&, const params& p) = 0;

        virtual void destroy(io::yield_ctx& y) = 0;

        signal<> destroyed;
    protected:
        io::io_context& ioc_;
        uuid uuid_; // might be set after object creation...
        const std::string name_;
        const std::string type_;
        const params params_;
    };

    class context : public std::enable_shared_from_this<context> {
    public:
        inline context(io::io_context& ioc, 
                const uuid& uuid, const std::string_view& name, 
                const std::string_view& type, const params& p) : 
                    ioc_(ioc), uuid_(uuid), name_(name),
                    type_(type), params_(p) {}

        // mount-querying functions
        virtual collection_ptr<mount_info> mounts(bool srcs=true, bool tgts=true) const = 0;

        constexpr io::io_context& get_executor() { return ioc_; }

        virtual std::shared_ptr<namespace_> get_namespace() = 0;
        virtual std::shared_ptr<const namespace_> get_namespace() const = 0;

        const std::string& get_name() const { return name_; }
        const std::string& get_type() const { return type_; }
        const params& get_params() const { return params_; }
        const uuid& get_uuid() const { return uuid_; }

        virtual params_stream_ptr request(io::yield_ctx&, const params& p) = 0;

        virtual std::shared_ptr<node> fetch(io::yield_ctx& ctx) = 0;

        // tree manipulation functions
        virtual subscription_ptr  subscribe(io::yield_ctx& ctx, 
                                const std::vector<std::string_view>& variable,
                                float min_interval, float max_interval,
                                float timeout) = 0;
        virtual subscription_ptr  subscribe(io::yield_ctx& ctx, 
                                const variable* v,
                                float min_interval, float max_interval,
                                float timeout) = 0;

        virtual value call(io::yield_ctx& ctx, action* a, value v, float timeout) = 0;
        virtual value call(io::yield_ctx& ctx, const std::vector<std::string_view>& a, 
                                    value v, float timeout) = 0;

        virtual bool write_data(io::yield_ctx& yield, variable* v, 
                                    const std::vector<data_point>& data) = 0;
        virtual bool write_data(io::yield_ctx& yield, const std::vector<std::string_view>& var,
                                    const std::vector<data_point>& data) = 0;

        virtual data_query_ptr query_data(io::yield_ctx& yield, const variable* v) = 0;
        virtual data_query_ptr query_data(io::yield_ctx& yield, const std::vector<std::string_view>& v) = 0;

        virtual void mount(io::yield_ctx& ctx, const std::shared_ptr<context>& src) = 0;
        virtual void unmount(io::yield_ctx& ctx, const std::shared_ptr<context>& src) = 0;

        virtual void destroy(io::yield_ctx& yield) = 0;
        signal<> destroyed;
    protected:
        io::io_context& ioc_;
        const uuid uuid_;
        const std::string name_;
        const std::string type_;
        const params params_;
    };

    // query_key specialization
    // for context/info so that we can do get() on a query
    // to get by the uuid
    template<>
        struct collection_key<std::shared_ptr<context>> {
            typedef uuid type;
            static uuid get(const std::shared_ptr<context>& c) { 
                return c->get_uuid(); 
            }
        };
    template<>
        struct collection_key<std::shared_ptr<component>> {
            typedef uuid type;
            static uuid get(const std::shared_ptr<component>& t) {
                return t->get_uuid(); 
            }
        };

    // add equality operator the mount_info type
    inline bool operator==(const mount_info& lh, const mount_info& rh) {
        return lh.src.lock() == rh.src.lock() && lh.tgt.lock() == rh.tgt.lock();
    }
    inline bool operator!=(const mount_info& lh, const mount_info& rh) {
        return lh.src.lock() != rh.src.lock() || lh.tgt.lock() != rh.tgt.lock();
    }
}

namespace std {
    // make the mount type hashable
    // so we can put it in a query
    template<>
        struct hash<telegraph::mount_info> {
            std::size_t operator()(const telegraph::mount_info&i) const {
                auto s = i.src.lock();
                auto t = i.tgt.lock();
                if (!s || !t) 
                    return hash<telegraph::uuid>()(telegraph::uuid{});
                return hash<telegraph::uuid>()(s->get_uuid()) ^ 
                       (hash<telegraph::uuid>()(t->get_uuid()) << 1);
            }
        };
}

#endif
