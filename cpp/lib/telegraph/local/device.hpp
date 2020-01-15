#ifndef __TELEGRAPH_LOCAL_DEVICE_HPP__
#define __TELEGRAPH_LOCAL_DEVICE_HPP__

#include "namespace.hpp"

#include "../common/adapter.hpp"
#include "../common/nodes.hpp"

#include "../utils/io_fwd.hpp"

#include <string>
#include <memory>
#include <unordered_map>
#include <deque>

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/streambuf.hpp>

#include "stream.pb.h"

namespace telegraph {
    class device_io_worker;
    class device : public local_context {
    private:
        struct sub_req {
            bool cancel; // true if this is a cancel request
            uint32_t min_interval; // set if cancel is false
            uint32_t max_interval; // set if cancel is false

            io::deadline_timer& event;
            bool& success;
        };
        struct call_req {
            io::deadline_timer& event;
            value& ret;
        };
        struct tree_req {
            io::deadline_timer& event;
            std::shared_ptr<node>& tree;
        };

        std::deque<stream::Packet> write_queue_;
        io::streambuf write_buf_;
        io::streambuf read_buf_;

        // map from var id -> queue of subscription-changing requests
        // used for both cancel() and change_sub() to guarantee
        // the callback order is sane
        std::unordered_map<uint32_t, std::deque<sub_req*>> sub_reqs_;

        uint32_t call_id_counter_;
        std::unordered_map<uint32_t, call_req> call_reqs_;
        std::deque<tree_req> tree_reqs_;
        
        // subscription adapters
        std::unordered_map<uint32_t, adapter> adapters_;

        io::serial_port port_;
    public:
        device(io::io_context& ioc, 
             const std::string& name,
             const std::string& port, int baud);
        ~device();

        // init should be called right after construction!
        // or the context will not have a tree
        void init(io::yield_ctx&);

        // the overridden functions
        subscription_ptr subscribe(io::yield_ctx& ctx, variable* v,
                                int32_t min_interval, int32_t max_interval);
        value call(io::yield_ctx& ctx, action* a, value v);

        // path-based overloads
        inline subscription_ptr subscribe(io::yield_ctx& ctx, const std::vector<std::string>& path,
                                int32_t min_interval, int32_t max_interval) {
            auto v = dynamic_cast<variable*>(tree_->from_path(path));
            if (!v) return nullptr;
            return subscribe(ctx, v, min_interval, max_interval);
        }

        inline value call(io::yield_ctx& ctx, 
                const std::vector<std::string>& path, value v) {
            auto a = dynamic_cast<action*>(tree_->from_path(path));
            if (!a) return value();
            return call(ctx, a, v);
        }

        // unimplemented context functions
        inline bool write_data(io::yield_ctx&, variable* v, 
                const std::vector<data_point>& d) override { return false; }

        inline bool write_data(io::yield_ctx&, 
                const std::vector<std::string>& path, 
                const std::vector<data_point>& d) override { return false; }

        inline std::unique_ptr<data_query> query_data(io::yield_ctx& yield, 
                                            const node* n) const override { return nullptr; }
        inline std::unique_ptr<data_query> query_data(io::yield_ctx& yield, 
                                const std::vector<std::string>& p) const override { return nullptr; }

        // disable mounting
        inline void mount(io::yield_ctx&, const context_ptr& src) override {
            throw bad_type_error("cannot mount on a device");
        }
        inline void unmount(io::yield_ctx&, const context_ptr& src) override {
            throw bad_type_error("cannot unmount on a device");
        }
    private:
        inline std::shared_ptr<device> shared_device_this() {
            return std::static_pointer_cast<device>(shared_from_this());
        }

        bool change_sub(io::yield_ctx& yield, uint32_t var_id,
                        uint32_t min_interval, uint32_t max_interval);
        bool cancel_sub(io::yield_ctx& yield, uint32_t var_id);

        // called from within the port executing strand
        void on_start();
        void do_reading(size_t requested = 0); // requested of 0 just read any amount
        void on_read(const boost::system::error_code& ec, size_t transferred);

        void do_write_next();
        void write_packet(stream::Packet&& p);

        void on_read(const stream::Packet& p);

        void start_call(uint32_t call_id, uint32_t action_id, value arg);
        void start_fetch();
        void start_next_sub(uint32_t var_id);
    };

    // the io_task doesn't actually do the work, but
    // will destroy the associated device on stop
    class device_io_task : public local_task {
    public:
        device_io_task(io::io_context& ioc, 
                const std::string& name, const std::string& port);

        void start(io::yield_ctx&, const std::string& name, int baud);
        void start(io::yield_ctx&, const info& info) override;
        void stop(io::yield_ctx&) override;

        void destroy(io::yield_ctx&) override;
    private:
        std::string port_name_;
        std::weak_ptr<device> dev_;
    };

    class device_scan_task : public local_task {
    public:
        device_scan_task(io::io_context& ioc, const std::string& name);

        void start(io::yield_ctx&, const info& info) override;
        void stop(io::yield_ctx&) override;

        void destroy(io::yield_ctx&) override;
    private:
        std::vector<std::shared_ptr<device_io_task>> devices_;
    };
}
#endif
