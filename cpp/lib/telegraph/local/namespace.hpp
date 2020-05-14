#ifndef __TELEGRAPH_LOCAL_NAMESPACE_HPP__
#define __TELEGRAPH_LOCAL_NAMESPACE_HPP__

#include "context.hpp"

#include "../utils/uuid.hpp"
#include "../utils/errors.hpp"
#include "../utils/io_fwd.hpp"

#include "../common/namespace.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <map>
#include <functional>

namespace telegraph {
    class local_context;
    class local_component;
    using local_context_ptr = std::shared_ptr<local_context>;
    using local_component_ptr = std::shared_ptr<local_component>;

    class local_namespace : 
            public std::enable_shared_from_this<local_namespace>,
            public namespace_ {
        friend class local_context;
        friend class local_component
   ;
    private:
        using component_factory = std::function<local_component_ptr(io::yield_ctx&,io::io_context&, 
                                    const std::string_view&, const std::string_view&,
                                    const params&, sources_map&&)>;

        using context_factory = std::function<local_context_ptr(io::yield_ctx&,io::io_context&, 
                                    const std::string_view&, const std::string_view&, 
                                    const params&, sources_map&&)>;

        io::io_context& ioc_;
        std::map<std::string, component_factory, std::less<>> component_factories_;
        std::map<std::string, context_factory, std::less<>> context_factories_;
    public:
        local_namespace(io::io_context& ioc);

        // add factories
        void register_component_factory(const std::string& type, const component_factory& f) {
            component_factories_.emplace(std::make_pair(type, f));
        }

        void register_context_factory(const std::string& type, const context_factory& f) {
            context_factories_.emplace(std::make_pair(type, f));
        }

        //

        context_ptr create_context(io::yield_ctx& yield, 
                    const std::string_view& name, const std::string_view& type, 
                    const params& p, sources_uuid_map&& srcs) override;

        component_ptr create_component(io::yield_ctx& yield, 
                    const std::string_view& name, const std::string_view& type, 
                    const params& p, sources_uuid_map&& srcs) override;

        void destroy_context(io::yield_ctx& y, const uuid& u) override;
        void destroy_component(io::yield_ctx& y, const uuid& u) override;
    };

    class local_context : public context {
    public:
        local_context(io::io_context& ioc, 
                const std::string_view& name, const std::string_view& type, 
                const params& i, const std::shared_ptr<node>& tree);

        std::shared_ptr<namespace_> get_namespace() override { return ns_.lock(); }
        std::shared_ptr<const namespace_> get_namespace() const override { return ns_.lock(); }

        void reg(io::yield_ctx& yield, const std::shared_ptr<local_namespace>& ns);
        void destroy(io::yield_ctx& yield) override;

        collection_ptr<mount_info> mounts(bool srcs=true, bool tgts=true) const override;

        inline std::shared_ptr<node> fetch(io::yield_ctx&) override {  return tree_; }

        void mount(io::yield_ctx&, const context_ptr& src) override;
        void unmount(io::yield_ctx&, const context_ptr& src) override;
    protected:
        std::shared_ptr<node> tree_;
        std::weak_ptr<local_namespace> ns_;
    };

    class local_component : public component {
    public:
        local_component(io::io_context& ioc, const std::string_view& name,
                const std::string_view& type, const params& i);

        std::shared_ptr<namespace_> get_namespace() override { return ns_.lock(); }
        std::shared_ptr<const namespace_> get_namespace() const override { return ns_.lock(); }

        void reg(io::yield_ctx& ctx, const std::shared_ptr<local_namespace>& ns);
        void destroy(io::yield_ctx& ctx) override;
    protected:
        std::weak_ptr<local_namespace> ns_;
    };
}

#endif
