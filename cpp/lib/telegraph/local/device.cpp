#include "device.hpp"

#include "../utils/io.hpp"

#include "crc.hpp"
#include "stream.pb.h"

#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"

#include <boost/asio.hpp>
#include <variant>
#include <queue>
#include <iostream>
#include <iomanip>
#include <memory>
#include <filesystem>

namespace fs = std::filesystem;

namespace telegraph {

    // Will encode a datastream and
    // compute a crc at the same time
    class encodebuf : public std::streambuf {
    private:
        io::streambuf* output_;
        uint32_t crc_;
    public:
        encodebuf(io::streambuf* output) : output_(output) {
            crc::crc32_start(crc_);
        }
        uint32_t get_crc() const { return crc_ ^ ~0U; }
    private:
        virtual int overflow(int c) override {
            if (c == EOF) return !EOF;
            if (c == 'S' || c == 'E' || c == '@') {
                int r1 = output_->sputc('@');
                if (r1 == EOF) return EOF;
            }
            crc::crc32_next(crc_, (uint8_t) c);
            return output_->sputc(c);
        }
        virtual int sync() {
            return output_->pubsync();
        }
    };

    // Will decode an escaped datastream
    // stopping (and consuming) after the first unescaped E
    // and stopping (but not consuming) the first unescaped S
    class decodebuf : public std::streambuf {
    private:
        io::streambuf* input_;
        bool finished_;
        char ch_; // single-byte buffer
    public:
        decodebuf(io::streambuf* input) : input_(input),
                                        finished_(false) {}
        bool finished() const { return finished_; }
    private:
        virtual int underflow() {
            int r = input_->sbumpc();
            if (r == EOF) return EOF;
            if (r == '@') {
                int c = input_->sbumpc();
                if (c == EOF) {
                    input_->sputbackc(r);
                    return EOF;
                }
                ch_ = c;
                setg(&ch_, &ch_, &ch_ + 1);
                return c;
            } else if (r == 'E') {
                finished_ = true;
                return EOF;
            } else if (r == 'S') {
                input_->sputbackc(r);
                finished_ = true;
                return EOF;
            }
            ch_ = r;
            setg(&ch_, &ch_, &ch_ + 1);
            return r;
        }

        virtual int sync() {
            return input_->pubsync();
        }
    };

    static params make_device_params(const std::string& port, int baud) {
        std::map<std::string, params, std::less<>> i;
        i["port"] = port;
        i["baud"] = baud;
        return params(std::move(i));
    }

    device::device(io::io_context& ioc, const std::string& name, const std::string& port, int baud)
            : local_context(ioc, name, "device", make_device_params(port, baud), nullptr),
              write_queue_(), write_buf_(), read_buf_(),
              one_start_(false), decoding_(false),
              decode_buf_(),
              req_id_(0), reqs_(), adapters_(),
              port_(ioc) {
        boost::system::error_code ec;
        port_.open(port, ec);
        if (ec) throw io_error("unable to open port: " + port);
        port_.set_option(io::serial_port::baud_rate(baud));
    }

    device::~device() {
        port_.close();
    }

    void
    device::init(io::yield_ctx& yield, int timeout_millisec) {
        // start reading (we can't do this in the constructor
        // since there shared_from_this() doesn't work)
        auto sthis = shared_device_this();
        io::dispatch(port_.get_executor(), [sthis] () { sthis->do_reading(0); });

        // do a ping
        if (!ping(yield, true, 50)) {
            throw io_error("no response from device");
        }

        // fetch the tree
        std::unordered_map<node::id, node*> nodes;
        std::queue<node::id> queue;
        queue.push(0);

        while (!queue.empty()) {
            node::id id = queue.front();
            queue.pop();
            node* n = nullptr;
            for (int i = 0; i < 5 && !n; i++) {
                n = fetch_node(yield, id);
            }
            // if we can't get a node, just fail
            if (!n) {
                // clean up the fetched nodes
                for (auto& p : nodes) {
                    delete p.second;
                }
                throw io_error("missing node response for " + std::to_string(id));
            } else {
                if (group* g = dynamic_cast<group*>(n)) {
                    for (node::id c : g->placeholders()) {
                        queue.push(c);
                    }
                }
                nodes.emplace(id, n);
            }
        }

        // resolve children of all the groups
        std::queue<group*> resolve_queue;
        for (auto& p : nodes) {
            group* g = dynamic_cast<group*>(p.second);
            if (g) resolve_queue.push(g);
        }
        while (!resolve_queue.empty()) {
            group* g = resolve_queue.front();
            resolve_queue.pop();
            g->resolve_placeholders(&nodes);
        }
        auto root_it = nodes.find(0);
        node* root = root_it->second;
        nodes.erase(root_it);
        if (!nodes.empty() || !root) {
            for (auto& p : nodes) {
                delete p.second;
            }
            delete root;
            throw io_error("too many node responses!");
        }
        tree_ = std::shared_ptr<node>(root);
        if (!tree_) return;
        tree_->set_owner(shared_device_this());

        // start a ping task
        auto wp = weak_device_this();
        io::io_context& ioc = ioc_;
        io::spawn(ioc_, [&ioc, wp, timeout_millisec](io::yield_context yield) {
            io::deadline_timer timer{ioc};
            io::yield_ctx ctx{yield};
            while (true) {
                timer.expires_from_now(boost::posix_time::milliseconds(timeout_millisec));
                timer.async_wait(yield);
                {
                    auto sp = wp.lock();
                    if (!sp) break;
                    sp->ping(ctx, false, 0);
                }
            }
        });
    }

    void
    device::destroy(io::yield_ctx& ctx) {
        local_context::destroy(ctx);
        port_.close();
        adapters_.clear();
    }

    bool
    device::ping(io::yield_ctx& yield, bool wait, int timeout_ms) {
        auto sthis = shared_device_this();
        if (wait) {
            io::deadline_timer timer(ioc_,
                boost::posix_time::milliseconds(timeout_ms));
            uint32_t req_id = sthis->req_id_++;
            stream::Packet res;

            sthis->reqs_.emplace(req_id, req(&timer, &res));
            io::dispatch(port_.get_executor(),
                    [sthis, req_id] () {
                        stream::Packet p;
                        p.set_req_id(req_id);
                        p.set_ping(0);
                        sthis->write_packet(std::move(p));
                    });

            boost::system::error_code ec;
            timer.async_wait(yield.ctx[ec]);
            reqs_.erase(req_id);
            if (ec != io::error::operation_aborted) {
                return false;
            }
            if (res.event_case() != stream::Packet::kPong) {
                return false;
            }
            return true;
        } else {
            uint32_t req_id = sthis->req_id_++;
            io::dispatch(port_.get_executor(),
                    [sthis, req_id] () {
                        stream::Packet p;
                        p.set_req_id(req_id);
                        p.set_ping(0);
                        sthis->write_packet(std::move(p));
                    });
            return true;
        }
    }

    node*
    device::fetch_node(io::yield_ctx& yield, node::id id) {
        auto sthis = shared_device_this();
        io::deadline_timer timer(ioc_,
            boost::posix_time::milliseconds(1000));
        uint32_t req_id = sthis->req_id_++;
        stream::Packet res;

        sthis->reqs_.emplace(req_id, req(&timer, &res));

        // put in request
        io::dispatch(port_.get_executor(),
                [sthis, req_id, id] () {
                    stream::Packet p;
                    p.set_req_id(req_id);
                    p.set_fetch_node(id);
                    sthis->write_packet(std::move(p));
                });

        // need to handle error code
        boost::system::error_code ec;
        timer.async_wait(yield.ctx[ec]);
        reqs_.erase(req_id);
        // if we timed out
        if (ec != io::error::operation_aborted) {
            return nullptr;
        }
        if (!res.has_node()) {
            return nullptr;
        }
        return node::unpack(res.node()).release();
    }

    subscription_ptr
    device::subscribe(io::yield_ctx& yield, const variable* v,
                        float min_interval, float max_interval, float timeout) {
        // get the adapter for the variable
        node::id id = v->get_id();
        auto it = adapters_.find(id);
        if (it == adapters_.end()) {
            auto wp = std::weak_ptr<device>(shared_device_this());
            auto change = [wp, id](io::yield_ctx& yield, float debounce,
                            float refresh, float timeout) -> bool {
                // get a shared pointer to the device
                // will be invalid if the device
                // has been destroyed
                auto sthis = wp.lock();
                if (!sthis) return false;

                io::deadline_timer timer(sthis->ioc_,
                    boost::posix_time::milliseconds(1000));
                uint32_t req_id = sthis->req_id_++;
                stream::Packet res;

                sthis->reqs_.emplace(req_id, req(&timer, &res));

                // put in the request
                io::dispatch(sthis->port_.get_executor(),
                        [sthis, req_id, id, debounce, refresh, timeout] () {
                            stream::Packet p;
                            p.set_req_id(req_id);
                            stream::Subscribe* s = p.mutable_change_sub();
                            s->set_var_id(id);
                            s->set_sub_timeout((uint32_t) (1000*timeout));
                            s->set_debounce((uint32_t) (1000*debounce));
                            s->set_refresh((uint32_t) (1000*refresh));
                            sthis->write_packet(std::move(p));
                        });
                // wait for response
                boost::system::error_code ec;
                timer.async_wait(yield.ctx[ec]);
                sthis->reqs_.erase(req_id);
                if (ec != io::error::operation_aborted) {
                    // timed out!
                    return false;
                }
                if (!res.success()) {
                    return false;
                }
                return true;
            };
            auto poll = [wp]() {
                auto sthis = wp.lock();
                if (!sthis) return;
                if (!sthis->port_.is_open()) return;
                uint32_t req_id = sthis->req_id_++;
                io::dispatch(sthis->port_.get_executor(),
                    [sthis, req_id] () {
                        stream::Packet p;
                        p.set_req_id(req_id);
                        p.mutable_poll_sub();
                        sthis->write_packet(std::move(p));
                    }
                );
            };
            auto cancel = [wp, id](io::yield_ctx& yield,
                                        float timeout) -> bool {
                // do the unsubscribe
                auto sthis = wp.lock();
                if (!sthis) return false;
                if (!sthis->port_.is_open()) return true;
                // keep the adapter alive for the duration of this
                // operations
                auto a = sthis->adapters_.at(id);
                sthis->adapters_.erase(id);

                io::deadline_timer timer(sthis->ioc_,
                    boost::posix_time::milliseconds(1000));
                uint32_t req_id = sthis->req_id_++;
                stream::Packet res;

                sthis->reqs_.emplace(req_id, req(&timer, &res));

                // put in the request
                io::dispatch(sthis->port_.get_executor(),
                        [sthis, req_id, id, timeout] () {
                            stream::Packet p;
                            p.set_req_id(req_id);
                            stream::Cancel * c = p.mutable_cancel_sub();
                            c->set_var_id(id);
                            c->set_cancel_timeout((uint32_t) (1000*timeout));
                            sthis->write_packet(std::move(p));
                        });
                // wait for response
                boost::system::error_code ec;
                timer.async_wait(yield.ctx[ec]);
                sthis->reqs_.erase(req_id);
                if (ec != io::error::operation_aborted) {
                    // timed out!
                    return false;
                }
                if (!res.success()) {
                    return false;
                }
                return true;
            };
            auto a = std::make_shared<adapter<decltype(poll), decltype(change), decltype(cancel)>>(
                                ioc_, v->get_type(), poll, change, cancel);
            adapters_.emplace(id, a);
        }
        return adapters_.at(id)->subscribe(yield,
                    min_interval, max_interval, timeout);
    }

    value
    device::call(io::yield_ctx& yield, action* a, value arg, float timeout) {
        io::deadline_timer timer(ioc_,
            boost::posix_time::milliseconds(1000));
        uint32_t req_id = req_id_++;
        stream::Packet res;
        reqs_.emplace(req_id, req(&timer, &res));

        boost::system::error_code ec;
        timer.async_wait(yield.ctx[ec]);
        reqs_.erase(req_id);
        if (ec != io::error::operation_aborted) {
            return value::invalid();
        }
        if (res.event_case() != stream::Packet::kCallCompleted) {
            return value::invalid();
        }
        return value::unpack(res.call_completed());
    }

    void
    device::do_reading(size_t requested) {
        auto shared = shared_device_this();
        std::weak_ptr<device> weak{shared};
        if (requested > 0) {
            io::async_read(port_, read_buf_, boost::asio::transfer_exactly(requested),
                    [weak] (const boost::system::error_code& ec, size_t transferred) {
                        auto s = weak.lock();
                        if (!s) return;
                        s->on_read(ec, transferred);
                    });
        } else {
            // if bytes is 0 we just try and read some
            io::async_read(port_, read_buf_, boost::asio::transfer_at_least(1),
                [weak] (const boost::system::error_code& ec, size_t transferred) {
                    auto s = weak.lock();
                    if (!s) return;
                    s->on_read(ec, transferred);
                });
        }
    }
    void
    device::on_read(const boost::system::error_code& ec, size_t transferred) {
        if (ec) return; // on error cancel the reading loop
        if (!decoding_) {
            // consume bytes from the input sequence until we hit two 'S's
            int c = 0;
            do {
                c = read_buf_.sbumpc();
                if (c == 'S' && !one_start_)  {
                    one_start_ = true;
                } else if (c == 'S' && one_start_) {
                    one_start_ = false;
                    decoding_ = true;
                    break;
                } else {
                    one_start_ = false;
                }
            } while (c != EOF);
        }
        if (decoding_) {
            // we hit a message and are decoding the payload
            decodebuf db(&read_buf_);
            // read from db into the write buffer
            std::ostream os(&decode_buf_);
            os << &db;
            if (db.finished()) {
                decoding_ = false;
                if (decode_buf_.size() < 4) {
                    std::cout << "bad length" << std::endl;
                    decode_buf_.consume(decode_buf_.size());
                    do_reading(0);
                    return;
                }

                // get the crc from the decoded buffer
                auto buf = decode_buf_.data();
                auto payload_start = io::buffers_begin(buf);
                auto payload_end = io::buffers_begin(buf) + decode_buf_.size() - 4;
                uint32_t crc_expected = crc::crc32_buffers(payload_start, payload_end);
                uint32_t crc_actual = 0;
                crc_actual |= (uint32_t) ((uint8_t) *(payload_end));       payload_end++;
                crc_actual |= (uint32_t) ((uint8_t) *(payload_end)) << 8;  payload_end++;
                crc_actual |= (uint32_t) ((uint8_t) *(payload_end)) << 16; payload_end++;
                crc_actual |= (uint32_t) ((uint8_t) *(payload_end)) << 24; payload_end++;
                if (crc_actual != crc_expected) {
                    std::cout << "bad crc" << std::endl;
                    decode_buf_.consume(decode_buf_.size());
                    do_reading(0);
                    return;
                }

                // decode the payload
                std::istream input_stream(&decode_buf_);
                google::protobuf::io::IstreamInputStream iss{&input_stream};
                google::protobuf::io::CodedInputStream input{&iss};
                input.PushLimit(decode_buf_.size() - 4);

                stream::Packet packet;
                packet.ParseFromCodedStream(&input);
                on_read(std::move(packet));

                // consume any leftover bytes
                decode_buf_.consume(decode_buf_.size());
            }
        }
        // read some more
        do_reading(0);
    }

    void
    device::do_write_next() {
        // grab the front of the write queue
        auto p = std::move(write_queue_.front());
        write_queue_.pop_front();
        {
            std::ostream raw(&write_buf_);
            raw.put('S');
            raw.put('S');

            encodebuf encoder(&write_buf_);
            std::ostream encoded_out(&encoder);
            {
                // coded stream must be destructed to flush
                ::google::protobuf::io::OstreamOutputStream s(&encoded_out);
                ::google::protobuf::io::CodedOutputStream cs(&s);
                p.SerializeToCodedStream(&cs);
            }

            // write crc from the payload
            uint32_t crc = encoder.get_crc();
            encoded_out.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
            raw.put('E');
        }
        // write_buf_ now has bytes to be written out in the input sequence

        /*
        std::cout << "writing: " << write_buf_.size() << std::endl;
        std::cout << "data: ";

        auto buf = write_buf_.data();
        auto start = io::buffers_begin(buf);
        auto end = io::buffers_end(buf);
        while (start != end) {
            uint8_t ui = (uint8_t) *start;
            if (ui < 127 && ui > 32) {
                std::cout << (char) ui;
            } else {
                std::ios init(NULL);
                init.copyfmt(std::cout);
                std::cout << "\\x" << std::setfill('0') << std::setw(2)
                        << std::hex << (int) ui;
                std::cout.copyfmt(init);
            }
            start++;
        }
        std::cout << std::endl;
        //*/

        auto shared = shared_device_this();
        io::async_write(port_, write_buf_.data(),
            [shared] (const boost::system::error_code& ec, size_t transferred) {
                if (ec) return;
                shared->write_buf_.consume(transferred);
                // if there are more messages, queue another write
                if (shared->write_queue_.size() > 0)
                    shared->do_write_next();
            });
    }

    void
    device::write_packet(stream::Packet&& p) {
        write_queue_.emplace_back(std::move(p));
        // if there is a write chain active
        if (write_queue_.size() > 1) return;
        do_write_next();
    }

    void
    device::on_read(stream::Packet&& p) {
        if (p.has_update()) {
            // updates have var_id in the req_id
            node::id var_id = (node::id) p.req_id();
            auto it = adapters_.find(var_id);
            if (it == adapters_.end()) return;
            else it->second->update(value::unpack(p.update()));
        } else {
            // look at the req_id
            uint32_t req_id = p.req_id();
            if (reqs_.find(req_id) != reqs_.end()) {
                auto& r = reqs_.at(req_id);
                if (r.timer) r.timer->cancel();
                if (r.packet) *r.packet = std::move(p);
            }
        }
    }

    local_context_ptr
    device::create(io::yield_ctx& yield, io::io_context& ioc,
            const std::string_view& name, const std::string_view& type,
            const params& p) {
        int baud = (int) p.at("baud").get<float>();
        const std::string& port = p.at("port").get<std::string>();
        auto s = std::make_shared<device>(ioc, std::string{name}, port, baud);
        s->init(yield, 500);
        return s;
    }

    // the device scanner task that detects new ports
    static std::vector<std::string> fetch_ports() {
        std::vector<std::string> ports;
#if defined(__linux__) && !defined(__ANDROID__)
        try {
            // go list files
            fs::path dir{"/dev"};
            fs::directory_iterator end;
            for (fs::directory_iterator it{dir}; it != end; it++) {
                const fs::path& p = it->path();
                if (p.filename().string().rfind("ttyACM", 0) == 0) {
                    ports.push_back(p.string());
                }
            }
        } catch (std::exception& e) {
            std::cout << e.what() << std::endl;
        }
#endif
        return ports;
    }

    static params to_params(const std::vector<std::string>& p) {
        std::vector<params> par;
        for (const std::string& s : p) {
            par.push_back(params{s});
        }
        return params{std::move(par)};
    }

    device_scanner::device_scanner(io::io_context& ioc, const std::string_view& name)
            : local_component(ioc, name, "device_scanner", params()),
                    requests_() {
    }

    void
    device_scanner::init() {
        io::io_context& ioc = ioc_;
        // start the device scanner thread...
        auto sp = std::static_pointer_cast<device_scanner>(shared_from_this());
        std::weak_ptr<device_scanner> wp{sp};
        io::spawn(ioc_, [wp, &ioc](io::yield_context yield) {
            io::yield_ctx ctx{yield};
            io::deadline_timer timer{ioc};
            while (true) {
                timer.expires_from_now(boost::posix_time::seconds(2));
                timer.async_wait(yield);
                {
                    auto sp = wp.lock();
                    if (!sp) break;
                    std::vector<std::string> p = fetch_ports();
                    if (p == sp->last_devices_) continue;
                    sp->last_devices_ = p;
                    // broadcast the ports to all parameter streams
                    params ports = to_params(sp->last_devices_);
                    auto it = sp->requests_.begin();
                    while (it != sp->requests_.end()) {
                        auto p = it->second;
                        auto ps = p.lock();
                        if (!ps || ps->is_closed()) {
                            it = sp->requests_.erase(it);
                        } else {
                            ps->write(params{ports});
                            it++;
                        }
                    }
                }
            }
        });

    }

    device_scanner::~device_scanner() {
        for (auto& wp : requests_) {
            auto sp = wp.second.lock();
            if (sp) sp->close();
        }
    }

    params_stream_ptr
    device_scanner::request(io::yield_ctx& yield, const params& p) {
        auto stream = std::make_shared<params_stream>();
        requests_.emplace(stream.get(), std::weak_ptr<params_stream>{stream});

        auto raw = stream.get();
        auto sp = std::static_pointer_cast<device_scanner>(shared_from_this());
        std::weak_ptr<device_scanner> wp{sp};

        // on stream destruction remove stream from
        // our requests if the scanner still exists
        stream->destroyed.add(this, [raw, wp] () {
            auto sp = wp.lock();
            if (!sp) return;
            sp->requests_.erase(raw);
        });

        params ports = to_params(fetch_ports());
        stream->write(std::move(ports));
        return stream;
    }

    local_component_ptr
    device_scanner::create(io::yield_ctx&, io::io_context& ioc,
            const std::string_view& name, const std::string_view& type,
            const params& p) {
        auto sp = std::make_shared<device_scanner>(ioc, name);
        sp->init();
        return sp;
    }
}
