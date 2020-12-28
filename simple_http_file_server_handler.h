#pragma once

#include <ugorji/conn/conn.h>

class simpleHttpFileServerConnFdStateMach {
public:
    std::mutex mu_;
    slice_bytes in_;
    slice_bytes resp_; // this is the header and body, except
    int fd_;
    int out_cursor_;
    int in_cursor_;
    int path_fd_;
    int resp_size_;
    off_t sendfile_offset_;
    std::time_t resp_mod_time_;
    std::string_view path_;
    slice_bytes_t host_;
    slice_bytes_t if_mod_since_;
    slice_bytes_t if_unmod_since_;
    ugorji::conn::ConnState state_;
    bool resp_ok_;
    bool resp_head_only_;
    
    explicit simpleHttpFileServerConnFdStateMach(int fd) :
        in_({}),
        resp_({}),
        fd_(fd)
        {
        // slice_bytes_zero(&in_);
        // slice_bytes_zero(&resp_);
        reinit();
    }
    ~simpleHttpFileServerConnFdStateMach() {
        slice_bytes_free(in_);
        slice_bytes_free(resp_);
    }
    // previously, we use std::string.append(...) but std::string involves a lot
    // of copying. Instead, we put append directly on the State machine.
    // This just appends some bytes onto the response.
    //
    // Note that a std::span would have been a good alternative.
    simpleHttpFileServerConnFdStateMach& append(const std::string_view& s) {
        slice_bytes_append(&resp_, s.data(), s.size());
        return *this;
    }
    void writeDone() {
        out_cursor_ = 0;
        resp_.bytes.len = 0;
        resp_size_ = 0;
        resp_ok_ = false;
        resp_head_only_ = false;
        host_ = {};
        if_mod_since_ = {};
        if_unmod_since_ = {};
        resp_mod_time_ = 0;
        state_ = ugorji::conn::CONN_READY;
        path_fd_ = -1;
        sendfile_offset_ = 0;
        path_ = ""; // path_.clear();
    }        
    void reinit() {
        in_.bytes.len = 0;
        in_cursor_ = 0;
        writeDone();
    }
};

// SimpleHttpFileServerHandler
//
// parse the http 1.1 request, get the path, do the path mapping translation,
// find the file, send it out with headers including lastmod and content-length,
// or send out an appropriate 4XX error code (if not exist, cannot read, etc).
//
// The only HTTP methods supported are GET and HEAD.
//
// Expects requests with an absolute path, and a Host header specified.
// Honors If-Modified-ince and If-Unmodified-Since flags.
//
// This server also supports pipelining of requests, so multiple requests can be
// sent on a persistent connection before any response is sent (in order).
//
// all gzip, parsing, etc is done by reverse proxy.
//
// There is only support for regular files, not sym links or directories.
//
// This also supports multiple Hosts, with distinct mappings for each one.
// e.g.
//    ugorji.net:
//        /s --> /.../
//        /  --> /.../
//    nwoke.net:
//        /s --> /.../
//        /  --> /.../
// This way, multiple hosts can have their files served from a single instance.
class SimpleHttpFileServerHandler  : public ugorji::conn::Handler {
private:
    std::mutex mu_;
    char errbuf_[128] {};
    // ugorji::conn::ConnState state_;
    std::unordered_map<int,std::unique_ptr<simpleHttpFileServerConnFdStateMach>> clientfds_;
    std::unordered_map<std::string,std::map<std::string,std::string>*>& path_mappings_;
    std::vector<std::string>& deny_starts_with_;
    std::vector<std::string>& deny_ends_with_;
    simpleHttpFileServerConnFdStateMach& stateFor(int fd);
    void readFd(simpleHttpFileServerConnFdStateMach& h, std::string& err);
    void processFd(simpleHttpFileServerConnFdStateMach& h, std::string& err);
    void writeFd(simpleHttpFileServerConnFdStateMach& h, std::string& err);
public:
    SimpleHttpFileServerHandler(std::unordered_map<std::string,std::map<std::string,std::string>*>& path_mappings,
                                std::vector<std::string>& deny_starts_with,
                                std::vector<std::string>& deny_ends_with) :
        Handler(),
        path_mappings_(path_mappings),
        deny_starts_with_(deny_starts_with),
        deny_ends_with_(deny_ends_with) {}
    ~SimpleHttpFileServerHandler() {}
    void handleFd(int fd, std::string& err) override;
    void unregisterFd(int fd, std::string& err) override;
};

