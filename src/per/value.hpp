#ifndef __PER_VALUE_HPP__
#define __PER_VALUE_HPP__

#include <string>
#include <cinttypes>
#include <ostream>

#include "type.hpp"

namespace per {
    class value;

    template<typename T>
        T unwrap(const value& v) {}

    class value {
    public:
        union box {
            bool b;
            uint8_t uint8;
            uint16_t uint16;
            uint32_t uint32;
            uint64_t uint64;
            int8_t int8; // also used for the enum type
            int16_t int16;
            int32_t int32;
            int64_t int64;
            float f;
            double d;
        }; 

        inline value() : type_(type::NONE), value_() {}
        inline value(bool b) : type_(type::BOOL), value_{ .b = b } {}
        inline value(uint8_t v) : type_(type::UINT8), value_{ .uint8  = v } {}
        inline value(uint16_t v) : type_(type::UINT16), value_{ .uint16  = v } {}
        inline value(uint32_t v) : type_(type::UINT32), value_{ .uint32  = v } {}
        inline value(uint64_t v) : type_(type::UINT64), value_{ .uint64  = v } {}

        inline value(int8_t v) : type_(type::INT8), value_{ .int8  = v } {}
        inline value(int16_t v) : type_(type::INT16), value_{ .int16  = v } {}
        inline value(int32_t v) : type_(type::INT32), value_{ .int32  = v } {}
        inline value(int64_t v) : type_(type::INT64), value_{ .int64  = v } {}

        inline value(float v) : type_(type::FLOAT), value_{ .f = v } {}
        inline value(double v) : type_(type::DOUBLE), value_{ .d = v } {}

        inline value(type::type_class t, uint8_t v) : type_(t), value_{ .uint8 = v } {}

        constexpr type::type_class get_type_class() const { return type_; }
        constexpr const box& get_box() const { return value_; }

        template<typename T>
            T get() const {
                return unwrap<T>(*this);
            }
    private:
        type::type_class type_; // only the type class, 
                                // see the stream for the actual type
        uint64_t timestamp_; // nanoseconds UTC timestamp
        box value_;
    };

    template<>
        constexpr uint8_t unwrap<uint8_t>(const value& v) {
            return v.get_box().uint8;
        }

    std::ostream& operator<<(std::ostream& o, const value& v);
}
#endif