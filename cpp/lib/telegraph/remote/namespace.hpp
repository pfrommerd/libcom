#ifndef __TELEGRAPH_REMOTE_NAMESPACE_HPP__
#define __TELEGRAPH_REMOTE_NAMESPACE_HPP__

#include "context.hpp"

#include "../common/namespace.hpp"
#include "../utils/uuid.hpp"

#include <memory>

namespace telegraph {
    class relay;
    class connection;

    class remote_namespace : public namespace_ {
        friend class remote_context;
    private:
        io::io_context& ioc_;
        connection& conn_;
        std::weak_ptr<remote_namespace> wptr_;
    public:
        // must also wait on init()!
        remote_namespace(io::io_context& ioc, connection& conn);
        // closes any open quieries on our side
        ~remote_namespace();

        void connect(io::yield_ctx& yield, const std::weak_ptr<remote_namespace>& wptr);
        bool is_connected() const;

        connection& get_conn() { return conn_; }
        const connection& get_conn() const { return conn_; }

        context_ptr create_context(io::yield_ctx&, 
                    const std::string_view& name, const std::string_view& type, 
                    const info& params, const sources_map& srcs) override;

        void destroy_context(io::yield_ctx&, const uuid& u) override;

        task_ptr create_task(io::yield_ctx&, 
                    const std::string_view& name, const std::string_view& type, 
                    const info& params, const sources_map& srcs) override;

        void destroy_task(io::yield_ctx&, const uuid& u) override;

        query_ptr<mount_info> mounts(io::yield_ctx& yield,
                                    const uuid& srcs_of=uuid(),
                                    const uuid& tgts_of=uuid()) const override;

        query_ptr<context_ptr> contexts(io::yield_ctx&, const uuid& by_uuid=uuid(), 
                                            const std::string_view& by_name=std::string(), 
                                            const std::string_view& by_type=std::string()) const override;

        query_ptr<task_ptr> tasks(io::yield_ctx&,  const uuid& by_uuid=uuid(), 
                                    const std::string_view& by_name=std::string(), 
                                    const std::string_view& by_type=std::string()) const override;

        std::shared_ptr<node> fetch(io::yield_ctx& yield, const uuid& uuid, 
                                    context_ptr owner=context_ptr()) const override;

        subscription_ptr subscribe(io::yield_ctx& yield,
               const uuid& ctx, const std::vector<std::string_view>& path,
               float min_interval, float max_interval, float timeout) override;

        value call(io::yield_ctx& yield, const uuid& ctx, 
                const std::vector<std::string_view>& path, value arg, float timeout) override;

        std::unique_ptr<data_query> query_data(io::yield_ctx& yield,
                const uuid& ctx, const std::vector<std::string_view>& path) const override;

        bool write_data(io::yield_ctx& yield, const uuid& ctx, 
                    const std::vector<std::string_view>& path,
                    const std::vector<data_point>& data) override;

        void mount(io::yield_ctx& yield, const uuid& src, const uuid& tgt) override;
        void unmount(io::yield_ctx& yield, const uuid& src, const uuid& tgt) override;

        void start_task(io::yield_ctx& yield, const uuid& task) override;
        void stop_task(io::yield_ctx& yield, const uuid& task) override;
        info_stream_ptr query_task(io::yield_ctx& yield, const uuid& task, const info& i) override;
    };

    class remote_context : public context {
    public:
        remote_context(io::io_context& ioc, 
                const std::shared_ptr<remote_namespace>& ns, const uuid& uuid,
                const std::string_view& name, const std::string_view& type, 
                const info& i) : context(ioc, uuid, name, type, i), ns_(ns) {}

        std::shared_ptr<namespace_> get_namespace() override { return ns_; }
        std::shared_ptr<const namespace_> get_namespace() const override { return ns_; }

        std::shared_ptr<node> fetch(io::yield_ctx& ctx) override;

        // tree manipulation functions
        subscription_ptr  subscribe(io::yield_ctx& ctx, variable* v, 
                                float min_interval, float max_interval, 
                                float timeout) override;
        subscription_ptr  subscribe(io::yield_ctx& ctx, 
                                const std::vector<std::string_view>& variable,
                                float min_interval, float max_interval,
                                float timeout) override;

        value call(io::yield_ctx& ctx, action* a, value v, float timeout) override;
        value call(io::yield_ctx& ctx, const std::vector<std::string_view>& a, 
                            value v, float timeout) override;

        bool write_data(io::yield_ctx& yield, variable* v, 
                                    const std::vector<data_point>& data) override;
        bool write_data(io::yield_ctx& yield, const std::vector<std::string_view>& var,
                                    const std::vector<data_point>& data) override;

        data_query_ptr query_data(io::yield_ctx& yield, const node* n) const override;
        data_query_ptr query_data(io::yield_ctx& yield, const std::vector<std::string_view>& n) const override;

        // mount-querying functions
        query_ptr<mount_info> mounts(io::yield_ctx& yield, bool srcs=true, bool tgts=true) const override;

        void mount(io::yield_ctx& ctx, const context_ptr& src) override;
        void unmount(io::yield_ctx& ctx, const context_ptr& src) override;

        void destroy(io::yield_ctx& yield) override;
    private:
        // shared ptr since
        // remote_context depends on remote_namespace
        // (other way around for local_context and local_namespace)
        std::shared_ptr<remote_namespace> ns_;
    };
}

#endif
