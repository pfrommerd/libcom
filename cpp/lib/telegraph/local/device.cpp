#include "device.hpp"

#include "../utils/io.hpp"

#include "crc.hpp"
#include "stream.pb.h"

#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"

#include <boost/asio.hpp>
#include <variant>
#include <iostream>
#include <iomanip>
#include <memory>

namespace telegraph {

    static params make_device_params(const std::string& port, int baud) {
        std::map<std::string, params, std::less<>> i;
        i["port"] = port;
        i["baud"] = baud;
        return params(std::move(i));
    }


    device::device(io::io_context& ioc, const std::string& name, const std::string& port, int baud)
            : local_context(ioc, name, "device", make_device_params(port, baud), nullptr), 
              write_queue_(), write_buf_(), read_buf_(),
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
    device::init(io::yield_ctx& yield) {
        // start reading (we can't do this in the constructor
        // since there shared_from_this() doesn't work)
        auto sthis = shared_device_this();
        io::dispatch(port_.get_executor(), [sthis] () { sthis->do_reading(0); });

        io::deadline_timer timer(ioc_, 
            boost::posix_time::milliseconds(1000));
        uint32_t req_id = sthis->req_id_++;
        stream::Packet res;

        sthis->reqs_.emplace(req_id, req(&timer, &res));

        // put in request
        io::dispatch(port_.get_executor(),
                [sthis, req_id] () {
                    stream::Packet p;
                    p.set_req_id(req_id);
                    p.mutable_fetch_tree();
                    sthis->write_packet(std::move(p));
                });

        // need to handle error code
        boost::system::error_code ec;
        timer.async_wait(yield.ctx[ec]);
        reqs_.erase(req_id);
        // if we timed out
        if (ec != io::error::operation_aborted) {
            throw io_error("timed out when connecting to " + params_.at("port").get<std::string>());
        }
        if (!res.has_tree()) {
            throw io_error("got bad response when connecting to " + params_.at("port").get<std::string>());
        }
        // create shared pointer from unpacked tree
        std::shared_ptr<node> tree(node::unpack(res.tree()));
        tree_->set_owner(shared_device_this());
        tree_ = tree;
    }

    subscription_ptr 
    device::subscribe(io::yield_ctx& yield, const variable* v,
                        float min_interval, float max_interval, float timeout) {
        // get the adapter for the variable
        node::id id = v->get_id();
        auto it = adapters_.find(id);
        if (it == adapters_.end()) {
            auto wp = std::weak_ptr<device>(shared_device_this());
            auto change = [wp, id](io::yield_ctx& yield, float new_min, 
                            float new_max, float timeout) -> bool {
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
                        [sthis, req_id, id, new_min, new_max, timeout] () {
                            stream::Packet p;
                            p.set_req_id(req_id);
                            stream::Subscribe* s = p.mutable_change_sub();
                            s->set_var_id(id);
                            s->set_timeout((uint32_t) (1000*timeout));
                            s->set_min_interval((uint32_t) (1000*new_min));
                            s->set_max_interval((uint32_t) (1000*new_max));
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
            auto cancel = [wp, id](io::yield_ctx& yield, 
                                        float timeout) -> bool {
                // do the subscribe
                auto sthis = wp.lock();
                if (!sthis) return false;

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
                            c->set_timeout((uint32_t) (1000*timeout));
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
            auto a = std::make_shared<adapter<decltype(change), decltype(cancel)>>(
                                ioc_, v->get_type(), change, cancel);
            adapters_.emplace(id, a);
        }
        return nullptr;
    }

    value
    device::call(io::yield_ctx& yield, action* a, value arg, float timeout) {
        return value();
    }

    void
    device::do_reading(size_t requested) {
        auto shared = shared_device_this();
        if (requested > 0) {
            io::async_read(port_, read_buf_, boost::asio::transfer_exactly(requested),
                    [shared] (const boost::system::error_code& ec, size_t transferred) {
                        shared->on_read(ec, transferred);
                    });
        } else {
            // if bytes is 0 we just try and read some
            io::async_read(port_, read_buf_, boost::asio::transfer_at_least(1),
                [shared] (const boost::system::error_code& ec, size_t transferred) {
                    shared->on_read(ec, transferred);
                });
        }
    }
    void 
    device::on_read(const boost::system::error_code& ec, size_t transferred) {
        if (ec) return; // on error cancel the reading loop

        // calculate the header length
        uint32_t length = 0;

        uint8_t byte;
        uint8_t header_pos = 0;
        uint_fast8_t bitpos = 0;
        do {
            // something went very wrong
            if (bitpos >= 32) {
                // clear the read buffer
                read_buf_.consume(read_buf_.size());
                break;
            }
            // next byte
            if (header_pos > read_buf_.size()) break;
            byte = *(io::buffers_begin(read_buf_.data()) + (header_pos++));

            length |= (uint32_t) (byte & 0x7F) << bitpos;
            bitpos = (uint_fast8_t)(bitpos + 7);
        } while (byte & 0x80);

        // something went very wrong
        if (bitpos > 32) {
            // clear the read buffer
            read_buf_.consume(read_buf_.size());
        }

        if (byte & 0x80) {
            // header is not complete, 
            // try reading any number of bytes
            do_reading(0); 
            return;
        } else {
            size_t expected = header_pos + length + 4;
            if (read_buf_.size() < expected) {
                // read length bytes + 4 bytes for checksum
                do_reading(expected - read_buf_.size());
                return;
            }
        }
        // calculate crc
        uint32_t crc_expected = 0;
        uint32_t crc_actual = 0;
        {
            auto buf = read_buf_.data();
            auto checksum_loc = io::buffers_begin(buf) + header_pos + length;
            crc_expected = crc::crc32_buffers(io::buffers_begin(buf), checksum_loc);

            // hacky but we can only read a byte at a time (might be
            // split across buffers)
            crc_actual |= (uint32_t) ((uint8_t) *(checksum_loc)); 
            checksum_loc++;
            crc_actual |= (uint32_t) ((uint8_t) *(checksum_loc)) << 8; 
            checksum_loc++;
            crc_actual |= (uint32_t) ((uint8_t) *(checksum_loc)) << 16; 
            checksum_loc++;
            crc_actual |= (uint32_t) ((uint8_t) *(checksum_loc)) << 24; 
            checksum_loc++;
        }

        // we have the entire message!
        // consume the header
        read_buf_.consume(header_pos); 

        // parse the buffer if crc valid
        if (crc_actual == crc_expected) {
            // parse the message
            std::istream input_stream(&read_buf_);
			google::protobuf::io::IstreamInputStream iss{&input_stream};
			google::protobuf::io::CodedInputStream input{&iss};
			input.PushLimit(length);

            stream::Packet packet;
            packet.ParseFromCodedStream(&input);
            on_read(std::move(packet));
        }

        // consume the message bytes and the checksum
        read_buf_.consume(length);
        read_buf_.consume(4);

        // call this function again to
        // parse the next message if there is one,
        // and queue another read if there is not
        on_read(ec, 0);
    }

    void 
    device::do_write_next() {
        // grab the front of the write queue
        auto p = std::move(write_queue_.front());
        write_queue_.pop_front();
        // write into the write_buf_
        {
            std::ostream output_stream(&write_buf_);
            {
                // coded stream must be destructed to flush
                ::google::protobuf::io::OstreamOutputStream s(&output_stream);
                ::google::protobuf::io::CodedOutputStream cs(&s);
                cs.WriteVarint32((int) p.ByteSizeLong());
                p.SerializeToCodedStream(&cs);
            }
            // write crc
            auto buf = write_buf_.data();
            uint32_t crc = crc::crc32_buffers(io::buffers_begin(buf), io::buffers_end(buf));
            output_stream.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
        }
        // write_buf_ now has bytes to be written out in the input sequence

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
            std::cout << "got updated!" << std::endl;
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
            const params& p, const sources_map& srcs) {
        int baud = (int) p.at("baud").get<float>();
        const std::string& port = p.at("port").get<std::string>();
        auto s = std::make_shared<device>(ioc, std::string{name}, port, baud);
        s->init(yield);
        return s;
    }

    // the device scanner task that detects new ports

    device_scanner::device_scanner(io::io_context& ioc, const std::string_view& name)
                        : local_component(ioc, name, "device_scanner", params()) {}


    params_stream_ptr
    device_scanner::request(io::yield_ctx& yield , const params& p) {
        auto stream = std::make_unique<params_stream>();
        stream->write(params{1});
        stream->close();
        return stream;
    }

    local_component_ptr
    device_scanner::create(io::yield_ctx&, io::io_context& ioc,
            const std::string_view& name, const std::string_view& type,
            const params& p, const sources_map& srcs) {
        return std::make_shared<device_scanner>(ioc, name);
    }
}
