#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>

#include <ctime>
#include <cstring>

#include <string>
#include <string_view>

#include <ugorji/util/logging.h>

#include "simple_http_file_server_handler.h"

const int CONN_BUF_INCR = 256; // ugorji::conn::CONN_BUF_INCR

// TODO
// - consider sending in chunks of 16Kib each (default socket size)
//   per https://www.nginx.com/blog/thread-pools-boost-performance-9x/
// - cap sendfile to 16MB,
//   per https://serverfault.com/questions/666214/slow-download-big-static-files-from-nginx/812737
//
// const size_t CONN_SENDFILE_CHUNK_SIZE = 1 << 14; // 16Kib

std::string view2cstr(std::string_view& s) {
    // LOG(TRACE, "<view_to_cstr> size: %d", s.size());
    return std::string(s.data(), s.size());
}

std::string bytes2cstr(slice_bytes_t& c) {
    return std::string(c.v, c.len);
}

// retrofitted from https://raw.githubusercontent.com/nginx/nginx/master/conf/mime.types
//
// got by running:
//   cd ~/repos/local/repo/go/src/ugorji.net/scratch/main
//   curl -s https://raw.githubusercontent.com/nginx/nginx/master/conf/mime.types |
//           go run -tags nginx_mime nginx_mime_types_to_cpp_map.go
const std::unordered_map<std::string_view, std::string> MIME_TYPES = 
{
	{"html", "text/html"},
	{"htm", "text/html"},
	{"shtml", "text/html"},
	{"css", "text/css"},
	{"xml", "text/xml"},
	{"gif", "image/gif"},
	{"jpeg", "image/jpeg"},
	{"jpg", "image/jpeg"},
	{"js", "application/javascript"},
	{"atom", "application/atom+xml"},
	{"rss", "application/rss+xml"},

	{"mml", "text/mathml"},
	{"txt", "text/plain"},
	{"jad", "text/vnd.sun.j2me.app-descriptor"},
	{"wml", "text/vnd.wap.wml"},
	{"htc", "text/x-component"},

	{"png", "image/png"},
	{"svg", "image/svg+xml"},
	{"svgz", "image/svg+xml"},
	{"tif", "image/tiff"},
	{"tiff", "image/tiff"},
	{"wbmp", "image/vnd.wap.wbmp"},
	{"webp", "image/webp"},
	{"ico", "image/x-icon"},
	{"jng", "image/x-jng"},
	{"bmp", "image/x-ms-bmp"},

	{"woff", "font/woff"},
	{"woff2", "font/woff2"},

	{"jar", "application/java-archive"},
	{"war", "application/java-archive"},
	{"ear", "application/java-archive"},
	{"json", "application/json"},
	{"hqx", "application/mac-binhex40"},
	{"doc", "application/msword"},
	{"pdf", "application/pdf"},
	{"ps", "application/postscript"},
	{"eps", "application/postscript"},
	{"ai", "application/postscript"},
	{"rtf", "application/rtf"},
	{"m3u8", "application/vnd.apple.mpegurl"},
	{"kml", "application/vnd.google-earth.kml+xml"},
	{"kmz", "application/vnd.google-earth.kmz"},
	{"xls", "application/vnd.ms-excel"},
	{"eot", "application/vnd.ms-fontobject"},
	{"ppt", "application/vnd.ms-powerpoint"},
	{"odg", "application/vnd.oasis.opendocument.graphics"},
	{"odp", "application/vnd.oasis.opendocument.presentation"},
	{"ods", "application/vnd.oasis.opendocument.spreadsheet"},
	{"odt", "application/vnd.oasis.opendocument.text"},

	{"wmlc", "application/vnd.wap.wmlc"},
	{"7z", "application/x-7z-compressed"},
	{"cco", "application/x-cocoa"},
	{"jardiff", "application/x-java-archive-diff"},
	{"jnlp", "application/x-java-jnlp-file"},
	{"run", "application/x-makeself"},
	{"pl", "application/x-perl"},
	{"pm", "application/x-perl"},
	{"prc", "application/x-pilot"},
	{"pdb", "application/x-pilot"},
	{"rar", "application/x-rar-compressed"},
	{"rpm", "application/x-redhat-package-manager"},
	{"sea", "application/x-sea"},
	{"swf", "application/x-shockwave-flash"},
	{"sit", "application/x-stuffit"},
	{"tcl", "application/x-tcl"},
	{"tk", "application/x-tcl"},
	{"der", "application/x-x509-ca-cert"},
	{"pem", "application/x-x509-ca-cert"},
	{"crt", "application/x-x509-ca-cert"},
	{"xpi", "application/x-xpinstall"},
	{"xhtml", "application/xhtml+xml"},
	{"xspf", "application/xspf+xml"},
	{"zip", "application/zip"},

	{"bin", "application/octet-stream"},
	{"exe", "application/octet-stream"},
	{"dll", "application/octet-stream"},
	{"deb", "application/octet-stream"},
	{"dmg", "application/octet-stream"},
	{"iso", "application/octet-stream"},
	{"img", "application/octet-stream"},
	{"msi", "application/octet-stream"},
	{"msp", "application/octet-stream"},
	{"msm", "application/octet-stream"},

	{"mid", "audio/midi"},
	{"midi", "audio/midi"},
	{"kar", "audio/midi"},
	{"mp3", "audio/mpeg"},
	{"ogg", "audio/ogg"},
	{"m4a", "audio/x-m4a"},
	{"ra", "audio/x-realaudio"},

	{"3gpp", "video/3gpp"},
	{"3gp", "video/3gpp"},
	{"ts", "video/mp2t"},
	{"mp4", "video/mp4"},
	{"mpeg", "video/mpeg"},
	{"mpg", "video/mpeg"},
	{"mov", "video/quicktime"},
	{"webm", "video/webm"},
	{"flv", "video/x-flv"},
	{"m4v", "video/x-m4v"},
	{"mng", "video/x-mng"},
	{"asx", "video/x-ms-asf"},
	{"asf", "video/x-ms-asf"},
	{"wmv", "video/x-ms-wmv"},
	{"avi", "video/x-msvideo"},

};

simpleHttpFileServerConnFdStateMach& SimpleHttpFileServerHandler::stateFor(int fd) {
    std::lock_guard<std::mutex> lk(mu_);
    simpleHttpFileServerConnFdStateMach* raw;
    auto it = clientfds_.find(fd);
    if(it == clientfds_.end()) {
        auto xx = std::make_unique<simpleHttpFileServerConnFdStateMach>(fd);
        raw = xx.get();
        clientfds_.emplace(fd, std::move(xx));
        // clientfds_.insert({fd, std::move(xx)});
        // clientfds_[fd] = xx;
        // *x = xx;
        LOG(INFO, "Adding Connection Socket fd: %d", fd);
    } else {
        raw = it->second.get();
    }
    return *raw;
    // auto itb = clientfds_.insert({fd, nullptr});
    // // auto itb = clientfds_.emplace(fd, nullptr);
    // if(!itb.second) {
    //     LOG(INFO, "Adding Connection Socket fd: %d", fd);
    //     itb.first->second = new simpleHttpFileServerConnFdStateMach(fd);
    // }
    // *x = itb.first->second;
}

void SimpleHttpFileServerHandler::unregisterFd(int fd, std::string& err) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = clientfds_.find(fd);
    if(it != clientfds_.end()) {
        LOG(INFO, "Removing socket fd: %d", fd);
        clientfds_.erase(it);
    }
}

void SimpleHttpFileServerHandler::handleFd(int fd, std::string& err) {
    // simpleHttpFileServerConnFdStateMach* h;
    auto& h = stateFor(fd);
    // h is either waiting, reading, processing, writing
    using namespace ugorji::conn;
    
    std::lock_guard<std::mutex> lk(h.mu_);
    switch(h.state_) {
    case ugorji::conn::CONN_READY:
        h.reinit();
        h.state_ = ugorji::conn::CONN_READING;
        readFd(h, err);
        break;
    case ugorji::conn::CONN_READING:
        readFd(h, err);
        break;
    case ugorji::conn::CONN_PROCESSING:
        processFd(h, err);
        break;
    case ugorji::conn::CONN_WRITING:
        writeFd(h, err);
    }
}


void SimpleHttpFileServerHandler::readFd(simpleHttpFileServerConnFdStateMach& h, std::string& err) {
    if(h.state_ == ugorji::conn::CONN_READING) {
        int n2 = CONN_BUF_INCR;
        while(true) {
            // always read up to the full expected # bytes, or half if you read less last time.
            // this ensures that we don't expand the array unnecessarily.
            n2 = (n2 < CONN_BUF_INCR/2) ? CONN_BUF_INCR/2 : CONN_BUF_INCR;
            ::slice_bytes_expand(&h.in_, n2);
            n2 = ::read(h.fd_, &h.in_.bytes.v[h.in_.bytes.len], n2);
            LOG(TRACE, "<handle_fd> read %d bytes, into alloc'ed cap: %d", n2, h.in_.cap);
            if(n2 > 0) { // TODO what if n2 == 0?
                h.in_.bytes.len += n2;
                continue;
            } 
            if(n2 == 0) return; // EOF
            if(errno == EINTR) continue;
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                // check if a http end sequence, and if so, continue to processing, else return
                auto len=h.in_.bytes.len;
                if(len > 4 && memcmp(&h.in_.bytes.v[len-4], "\r\n\r\n", 4) == 0) {
                    // ... && h.in_.bytes.v[len-3] == '\n' && h.in_.bytes.v[len-2] == '\r' && ...
                    break;
                }
                return;
            }
            snprintf(errbuf_, 128, "read returned %d, with errno: %s", n2, ugorji::conn::errnoStr().data());
            err = errbuf_;
            h.reinit();
            return;
        }     
        h.state_ = ugorji::conn::CONN_PROCESSING;
        h.in_cursor_ = 0;
        processFd(h, err);
    }
}

void SimpleHttpFileServerHandler::processFd(simpleHttpFileServerConnFdStateMach& h, std::string& err) {
    // parse the request path from the headers, transform it, and handle sending the file
    // first line should look like:
    //    GET /pub/WWW/TheProject.html HTTP/1.1
    //    HEAD /pub/WWW/TheProject.html HTTP/1.1
    //
    // extract the path, transform it to a path name using simple string manipulation
    if(h.state_ == ugorji::conn::CONN_PROCESSING) {
        auto in = h.in_.bytes;
        size_t m1(h.in_cursor_), m2(0);
        if(m1 > in.len-2) {
            LOG(TRACE, "<handle_fd> done pipeline processing: in_cursor: %d, for bytes read: %d", m1, in.len);
            h.reinit();
            return;
        }
        size_t p1(0), p2(0), h1(0), h2(0);
        size_t x1(0), x2(0), x3(0), x4(0);
        char st(0);

        LOG(TRACE, "<handle_fd> PROCESSING ... ", 0);
        size_t i = m1;
        // char c = 0;
        for(; i < in.len; i++) {
            char c = in.v[i];
            if(c == '\n') {
                ++i;
                break;
            }
            switch(st) {
            case 0:
                if(i > 0 && c == ' ') {
                    m2 = i;
                    st = 1;
                }
                break;
            case 1:
                if(p1 == 0) {
                    if(c != ' ') p1 = i;
                } else if(c == ' ') {
                    if(p2 == 0) p2 = i;
                    st = 2;
                } else if(c == '?' || c == '#') {
                    p2 = i;
                }
                break;
            case 2:
                if(h1 == 0) {
                    if(c != ' ') h1 = i;
                } else if(c == ' ' || c == '\r' ) {
                    h2 = i;
                    st = 3;
                }
                break;
            case 3:
                break;
            }
        }
        
        LOG(TRACE, "<handle_fd> m1: %d, m2: %d, p1: %d, p2: %d, h1: %d, h2: %d", m1, m2, p1, p2, h1, h2);
        
        if(m2 <= m1 || p2 <= p1 || h2 <= h1) {
            err = "unexpected error parsing request";
            h.reinit();
            return;
        }
        
        if(h2 > m1) LOG(TRACE, "<handle_fd> request line: %s", std::string(&in.v[m1], h2-m1).data());

        LOG(TRACE, "<handle_fd> looking for headers", 0);
        x1 = i;
        x2 = 0;
        x3 = 0;
        x4 = 0;
        for(; i < in.len; i++) {
            char c = in.v[i];
            if(c == '\n') {
                // parse the header and store it
                LOG(TRACE, "<handle_fd> parsing header line: %s", std::string(&in.v[x1], i-x1).data());
                if(x2-x1 == 4 && strncasecmp(&in.v[x1], "Host", 4) == 0) {
                    h.host_ = slice_bytes_t{&in.v[x3], x4-x3};
                    LOG(TRACE, "<handle_fd> found host: %s", std::string(h.host_.v, h.host_.len).data());
                } else if(x2-x1 == 17 && strncasecmp(&in.v[x1], "If-Modified-Since", 17) == 0) {
                    h.if_mod_since_ = slice_bytes_t{&in.v[x3], x4-x3};
                } else if(x2-x1 == 19 && strncasecmp(&in.v[x1], "If-Unmodified-Since", 19) == 0) {
                    h.if_unmod_since_ = slice_bytes_t{&in.v[x3], x4-x3};
                }
                ++i;
                x1 = i;
                x2 = 0;
                x3 = 0;
                x4 = 0;
                if(i < in.len-1 && in.v[i] == '\r' && in.v[i+1] == '\n') {
                    i += 2;
                    break;
                }
                continue;
            }
            if(c == ':') {
                if(x2 == 0) x2 = i;
            } else if(x2 > 0) {
                if(c == '\r') {
                    x4 = i;
                } else if(x3 == 0 && c != ' ') {
                    x3 = i;
                }
            }
        }

        if(h.host_.len > 0) LOG(TRACE, "<handle_fd> header: Host: %s", bytes2cstr(h.host_).data());
        if(h.if_mod_since_.len > 0) LOG(TRACE, "<handle_fd> header: If-Modified-Since: %s", bytes2cstr(h.if_mod_since_).data());
        if(h.if_unmod_since_.len > 0) LOG(TRACE, "<handle_fd> header: If-Unmodified-Since: %s", bytes2cstr(h.if_unmod_since_).data());
        
        std::string httpErrBody = "ERROR SERVING FILE\n";
        std::string xPoweredByHdrLine =
            "X-Powered-By: simplehttpfileserver/ugorji\r\n"
            "Server: simplehttpfileserver/ugorji";

        bool pathOK(true), errAdded(false);
        if(h.host_.len == 0) {
            h.append("HTTP/1.1 400 Bad Request").append("\r\n");
            httpErrBody = "No Host Header\n";
            
            pathOK = false;
            errAdded = false;
        }
        
        if(m2-m1 == 3 && memcmp(&in.v[m1], "GET", 3) == 0) h.resp_head_only_ = false;
        else if(m2-m1 == 4 && memcmp(&in.v[m1], "HEAD", 4) == 0) h.resp_head_only_ = true;
        else {
            h.append("HTTP/1.1 501 Not Implemented").append("\r\n");
            httpErrBody = std::string(&in.v[m1], m2-m1) + " Method not implemented\n";
            errAdded = true;
            pathOK = false;
        }

        LOG(TRACE, "<handle_fd> before checking path: pathOK = %d", pathOK);
        
        if(pathOK) {
            if(p1 > 0 && p2 > p1 && in.v[p1] == '/') {
                h.path_ = std::string_view(&in.v[p1], p2-p1);
                LOG(TRACE, "<handle_fd> found path: %s", view2cstr(h.path_).data());
            }  else pathOK = false;
        }

        LOG(TRACE, "<handle_fd> before checking http version: pathOK = %d", pathOK);
        if(pathOK) {
            if(h1 > 0 && h2 > h1 && memcmp(&in.v[h1], "HTTP/1.1", h2-h1) == 0) ;
            else pathOK = false;
        }

        h.in_cursor_ = i;
        
        // std::string_view vv(in.v, in.len);
        // int a, b, c, d;
        // a = vv.find("GET");
        // if(a == 0) {
        //     b = vv.find(" /", a, 2);
        //     if(b != std::string_view::npos) {
        //         b++;
        //         c = vv.find(' ', b);
        //         if(c != std::string_view::npos) {
        //             d = vv.find("HTTP/1.1", c, 8);
        //             if(d != std::string_view::npos) {
        //                 h.path_ = std::string(vv, b, c-b);
        //                 LOG(TRACE, "<handle_fd> found path: %s", view2cstr(h.path_).data());
        //             }
        //         }
        //     }
        // }
        // if(h.path_.size() == 0) {
        //     pathOK = false;
        // }

        // parse headers
        // e.g. Host:     www.ugorji.net\r\n
        //      1   2     3             4        

        LOG(TRACE, "<handle_fd> before checking deny: pathOK = %d", pathOK);
        if(pathOK) {
            for(auto it = deny_starts_with_.begin(); it != deny_starts_with_.end(); it++) {
                if(h.path_.rfind(*it, 0) == 0) {
                    pathOK = false;
                    break;
                }
            }
        }
        if(pathOK) {
            for(auto it = deny_ends_with_.begin(); it != deny_ends_with_.end(); it++) {
                auto vv = *it;
                auto cmp = h.path_.size() - vv.size();
                if(cmp > 0 && h.path_.find(vv, cmp) == 0) {
                    pathOK = false;
                    break;
                }
            }
        }
        
        std::string filepath("");
        
        LOG(TRACE, "<handle_fd> before checking mappings: pathOK = %d", pathOK);
        if(pathOK) {
            pathOK = false;
            auto host = std::string(h.host_.v, h.host_.len);
            LOG(TRACE, "<handle_fd> checking mappings for host: '%s'", host.data());
            auto iter = path_mappings_.find(host);
            if(iter != path_mappings_.end()) {
                LOG(TRACE, "<handle_fd> found mappings for host: %s", host.data());
                auto m3 = *(iter->second);
                for(auto it = m3.begin(); it != m3.end(); it++) {
                    LOG(TRACE, "<handle_fd> checking path: %s against mapping: %s", view2cstr(h.path_).data(), it->first.data());
                    if(h.path_.rfind(it->first, 0) == 0) {
                        pathOK=true;
                        filepath = h.path_;
                        filepath.replace(0, it->first.size(), it->second, 0, it->second.size());
                        LOG(TRACE, "<handle_fd> mapped path: %s to filepath: %s", view2cstr(h.path_).data(), filepath.data());
                        break;
                    }
                }
            }
        }

        auto ttnow = std::time(nullptr);
        bool useTnow(false);
        char tnow[32];
        if(std::strftime(tnow, sizeof(tnow), "%a, %e %b %Y %H:%M:%S GMT", std::gmtime(&ttnow))) useTnow = true;
        
        if(pathOK) {
            h.path_fd_ = open(filepath.data(), O_RDONLY);
            if(h.path_fd_ == -1) {
                if(errno == EACCES) {
                    h.append("HTTP/1.1 403 Forbidden").append("\r\n");
                    httpErrBody = "Wrong permissions to access file\n";
                } else {
                    h.append("HTTP/1.1 400 Bad Request").append("\r\n");
                    httpErrBody = "Cannot Open File\n";
                }
                pathOK = false;
                // errAdded = true;
            } else {
                struct stat statv;
                if(fstat(h.path_fd_, &statv) == -1) {
                    h.append("HTTP/1.1 404 Not Found").append("\r\n");
                    httpErrBody = "Missing File\n";
                    // errAdded = true;
                    pathOK = false;
                } else if(!S_ISREG(statv.st_mode)) {
                    h.append("HTTP/1.1 404 Not Found").append("\r\n");
                    httpErrBody = "Not a regular file\n";
                    // errAdded = true;
                    pathOK = false;
                } else {
                    // check if_modified_since and if_unmodified_since, and
                    // appropriately set 304 or 412 respectively.
                    ::time_t ifModSince(0), ifUnmodSince(0);
                    char tbuf[32] = {};
                    struct ::tm tm = {};
                    if(h.if_mod_since_.len > 0) {
                        // memset(&tm, 0, sizeof(tm));
                        ::memmove(tbuf, h.if_mod_since_.v, h.if_mod_since_.len);
                        ::strptime(tbuf, "%a, %e %b %Y %H:%M:%S GMT", &tm);
                        ifModSince = ::timegm(&tm);
                        LOG(TRACE, "<handle_fd> checking if-mod-since: %d >= file-last-mod: %d, : %d",
                            ifModSince, statv.st_mtime, ifModSince >= statv.st_mtime);
                        if(ifModSince >= statv.st_mtime) {
                            h.append("HTTP/1.1 304 Not Modified").append("\r\n");
                            httpErrBody = "Not Modified since\n";
                            // errAdded = true;
                            pathOK = false;
                        }
                    } else if(h.if_unmod_since_.len > 0) {
                        // memset(&tm, 0, sizeof(tm));
                        ::memmove(tbuf, h.if_unmod_since_.v, h.if_unmod_since_.len);
                        ::strptime(tbuf, "%a, %e %b %Y %H:%M:%S GMT", &tm);
                        ifUnmodSince = ::timegm(&tm);
                        LOG(TRACE, "<handle_fd> checking if-unmod-since: %d < file-last-mod: %d, : %d",
                            ifUnmodSince, statv.st_mtime, ifUnmodSince < statv.st_mtime);
                        if(ifUnmodSince < statv.st_mtime) {
                            h.append("HTTP/1.1 412 Precondition Failed").append("\r\n");
                            httpErrBody = "Precondition Failed\n";
                            // errAdded = true;
                            pathOK = false;
                        }
                    }
                    if(pathOK) {
                        h.resp_ok_ = true;
                        h.resp_size_ = statv.st_size;
                        h.resp_mod_time_ = statv.st_mtime;
                        h.append("HTTP/1.1 200 OK").append("\r\n");
                        std::string mimeType = "application/octet-stream";

                        size_t lastdot = h.path_.rfind('.');
                        if(lastdot != std::string_view::npos) {
                            auto extension = h.path_.substr(lastdot+1);
                            auto extlen = extension.size();
                            if(extlen < 5) {
                                char nn[5] = {};
                                // char c(0);
                                bool upperCaseFound(false);
                                for(size_t j = 0; j < extlen; j++) {
                                    char k = extension[j];
                                    if(k >= 'A' && k <= 'Z') {
                                        upperCaseFound = true;
                                        nn[j] = k + 'a' - 'A';
                                    } else nn[j] = k;
                                }
                                if(upperCaseFound) extension = std::string_view(nn, extlen);
                            }
                            auto it = MIME_TYPES.find(extension);
                            if(it != MIME_TYPES.end()) mimeType = it->second;
                            LOG(TRACE, "<handle_fd> extension: %s, mimeType: %s", view2cstr(extension).data(), mimeType.data());
                        }
                        if(std::strftime(tbuf, sizeof(tbuf), "%a, %e %b %Y %H:%M:%S GMT", std::gmtime(&h.resp_mod_time_))) {
                            h.append("Last-Modified: ").append(tbuf).append("\r\n");
                        }
                        h.append("Content-Type: ").append(mimeType).append("\r\n")
                            .append("Connection: keep-alive").append("\r\n")
                            .append("Content-Length: ").append(std::to_string(h.resp_size_)).append("\r\n");
                        if(useTnow) h.append("Date: ").append(tnow).append("\r\n");
                        h.append(xPoweredByHdrLine).append("\r\n")
                            .append("\r\n");
                    }
                }
            }
        } else if(!errAdded) {
            h.append("HTTP/1.1 400 Bad Request").append("\r\n");
            httpErrBody = "Invalid Path\n";
            //errAdded = true;
        }

        if(!h.resp_ok_) {
            h.resp_size_ = httpErrBody.size();
            h.append("Content-Type: text/plain; charset=UTF-8").append("\r\n")
                .append("Connection: keep-alive").append("\r\n")
                .append("Content-Length: ").append(std::to_string(h.resp_size_)).append("\r\n");
            if(useTnow) h.append("Date: ").append(tnow).append("\r\n");
            h.append(xPoweredByHdrLine).append("\r\n")
                .append("\r\n");
            h.append(httpErrBody);
        }
        h.state_ = ugorji::conn::CONN_WRITING;
        writeFd(h, err);
    }
}

void SimpleHttpFileServerHandler::writeFd(simpleHttpFileServerConnFdStateMach& h, std::string& err) {
    if(h.state_ == ugorji::conn::CONN_WRITING) {
        LOG(TRACE, "<http-fileserver>: writing output to fd: %d", h.fd_);
        LOG(TRACE, "<http-fileserver>: writing fd: %d, path: %s", h.fd_, view2cstr(h.path_).data());
        int z = h.resp_.bytes.len;
        while(h.out_cursor_ < z) {
            int n2 = ::write(h.fd_, &h.resp_.bytes.v[h.out_cursor_], z-h.out_cursor_);
            if(n2 < 0) {
                if(errno == EINTR) continue;
                if(errno == EAGAIN || errno == EWOULDBLOCK) return;
                snprintf(errbuf_, 128, "write returned %d, with errno: %s", n2, ugorji::conn::errnoStr().data());
                err = errbuf_;
                h.reinit();
                return;
            }
            h.out_cursor_ += n2;
        }

        if(h.resp_ok_ && !h.resp_head_only_) {
            // sendfile(h.fd_, h.path_fd_, &off, h.resp_size_); // blocking call
            // sendfile in chunks of 128K
            while(h.sendfile_offset_ < h.resp_size_) {
                auto n2 = sendfile(h.fd_, h.path_fd_, &h.sendfile_offset_, h.resp_size_ - h.sendfile_offset_);
                if(n2 < 0) {
                    if(errno == EINTR) continue;
                    if(errno == EAGAIN || errno == EWOULDBLOCK) return;
                    snprintf(errbuf_, 128, "sendfile returned %ld, with errno: %s", n2, ugorji::conn::errnoStr().data());
                    // err = std::string(errbuf_);
                    err = errbuf_;
                    h.reinit();
                    return;
                }
            }
        }
        
        if(h.path_fd_ > 2) close(h.path_fd_); // file dscriptors 0, 1, 2 are stdin/out/err always
        h.writeDone();
        h.state_ = ugorji::conn::CONN_PROCESSING;
        processFd(h, err);
    }
}

