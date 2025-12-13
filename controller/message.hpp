#pragma once

#include "debug.hpp"

#include <cstdint>
#include <cstring>

class Message {
public:
    Message(int cap): cap_(cap), len_(0) {
        buffer_ = malloc(cap_);
        LOG("Message() malloc-ed %d bytes at 0x%lx\n", cap_, (uint64_t)buffer_);
        dbg_assert(buffer_ != nullptr, "malloc failed in Message(%d)", cap_);
    }
    ~Message() {
        free(buffer_);
    }
    void *data() {
        return buffer_;
    }
    int len() {
        return len_;
    }
    int cap() {
        return cap_;
    }
    void extend(int new_cap) {
        if (cap_ >= new_cap) {
            return;
        }
        buffer_ = realloc(buffer_, new_cap);
        cap_ = new_cap;
    }
    void *alloc_tail(int nbytes) {
        extend(len_ + nbytes);
        void *ret = (char *)buffer_ + len_;
        len_ += nbytes;
        return ret;
    }
private:
    int cap_;
    int len_;
    void *buffer_;
};
