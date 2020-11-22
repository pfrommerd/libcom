#include "dummy_device.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/spawn.hpp>

namespace telegraph {
    dummy_device::dummy_device(io::io_context& ioc, const std::string_view& name,
                                std::unique_ptr<node>&& tree)
                           : local_context(ioc, name, "dummy_device",
                                    params{}, std::move(tree))  {}

    dummy_device::~dummy_device() {}
    void
    dummy_device::add_publisher(const variable* v, const publisher_ptr& p) {
        publishers_.emplace(v, p);
    }

    void
    dummy_device::add_handler(const action* a, const dummy_device::handler& h) {
        handlers_.emplace(a, h);
    }

    params_stream_ptr
    dummy_device::request(io::yield_ctx&, const params& p) {
        return nullptr;
    }

    subscription_ptr
    dummy_device::subscribe(io::yield_ctx&, const variable* v,
                            float min_interval, float max_interval,
                            float timeout) {
        auto it = publishers_.find(v);
        if (it == publishers_.end()) return nullptr;
        auto p = it->second;
        if (!p) return nullptr;
        return p->subscribe(min_interval, max_interval);
    }

    value
    dummy_device::call(io::yield_ctx&, action* a,
                            value arg, float timeout) {
        return value::invalid();
    }

    local_context_ptr
    dummy_device::create(io::yield_ctx&, io::io_context& ioc,
            const std::string_view& name, const std::string_view& type,
            const params& p) {
        auto childA = new variable(2, "a", "A", "", value_type::Float);
        auto childB = new variable(3, "b", "B", "", value_type::Uint8);

        std::vector<std::string> labels{"On", "Off"};
        auto status_type = value_type("Status", std::move(labels));
        auto childC = new variable(4, "c", "C", "", status_type);

        std::vector<std::unique_ptr<node>> children{};

        children.emplace_back(std::unique_ptr<node>(childA));
        children.emplace_back(std::unique_ptr<node>(childB));
        children.emplace_back(std::unique_ptr<node>(childC));

        auto root = std::make_unique<group>(1, "foo", "Foo", "", "", 1, std::move(children));
        auto dev = std::make_shared<dummy_device>(ioc, name, std::move(root));

        auto a_publisher = std::make_shared<publisher>(ioc, value_type::Float);
        auto b_publisher = std::make_shared<publisher>(ioc, value_type::Int32);
        auto c_publisher= std::make_shared<publisher>(ioc, status_type);

        dev->add_publisher(childA, a_publisher);
        dev->add_publisher(childB, b_publisher);
        dev->add_publisher(childC, c_publisher);

        // spawn data-pushing loop....
        std::weak_ptr<dummy_device> w{dev};

        // spawn loop for a
        io::spawn(ioc, [&ioc, w, a_publisher](io::yield_context yield) {
            // create a timer
            io::deadline_timer timer{ioc};
            while (true) {
                timer.expires_from_now(boost::posix_time::seconds(2));
                timer.async_wait(yield);
                { auto dev = w.lock(); if (!dev) break; }
                (*a_publisher) << value{1.0f};

                timer.expires_from_now(boost::posix_time::seconds(2));
                timer.async_wait(yield);
                { auto dev = w.lock(); if (!dev) break; }
                (*a_publisher) << value{2.5f};
                timer.expires_from_now(boost::posix_time::seconds(2));
                timer.async_wait(yield);
                { auto dev = w.lock(); if (!dev) break; }
                (*a_publisher) << value{3.8f};
            }
        });
        io::spawn(ioc, [&ioc, w, b_publisher](io::yield_context yield) {
            // create a timer
            io::deadline_timer timer{ioc};
            uint8_t val= 0;
            while (true) {
                timer.expires_from_now(boost::posix_time::milliseconds(5));
                timer.async_wait(yield);
                { auto dev = w.lock(); if (!dev) break; }
                (*b_publisher) << value{val};
                val++;
            }
        });
        return dev;
    }
}
