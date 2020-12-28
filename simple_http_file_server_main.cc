#include <cstdlib>
#include <iostream>
#include <csignal>
#include <thread>
#include <mutex>
#include <fstream>
#include <map>
#include <cstdint>

#include "simple_http_file_server_handler.h"
#include <ugorji/util/logging.h>

ugorji::conn::Manager* mgr_;

int main(int argc, char** argv) {
    ugorji::util::Log::getInstance().minLevel_ = ugorji::util::Log::TRACE;
    int port = 9999;
    int workers = -1;
    std::unordered_map<std::string, std::map<std::string,std::string>*> mappings;
    std::map<std::string,std::string>* map;
    std::vector<std::unique_ptr<std::map<std::string,std::string>>> maps;
    std::vector<std::string> domains;
    
    bool doMap(false);
    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if(arg == "-p" || arg == "--port") {
            port = std::stoi(argv[++i]);
        } else if(arg == "-w" || arg == "--workers") {
            workers = std::stoi(argv[++i]);
        } else if(arg == "-d" || arg == "--domain" || arg == "--host") {
            doMap = true;
            domains.push_back(std::string(argv[++i]));
        } else if(arg == "-m" || arg == "--mapping") {
            if(doMap) {
                LOG(TRACE, "<simplehttpfileserver> mapping for some domains", 0);
                auto mm = std::make_unique<std::map<std::string,std::string>>();
                map = mm.get();
                maps.push_back(std::move(mm));
                for(auto& d: domains) mappings[d] = map;
                domains.clear();
                doMap = false;
            }
            auto k = std::string(argv[++i]);
            auto v = std::string(argv[++i]);
            LOG(TRACE, "<simplehttpfileserver> mapping %s => %s", k.data(), v.data());
            (*map)[k] = v;
        } else if(arg == "-h" || arg == "--help") {
            std::cout << "Usage: simplehttpfileserver " << std::endl
                      << "\t[-p|--port portno] Default: 9999"  << std::endl
                      << "\t[-w|--workers numWorkers] Default: -1 (unlimited)"  << std::endl
                      << "\t[-d|--domain domain] ..."  << std::endl
                      << "\t[-m|--mapping prefix substitution] ..." << std::endl
                      << std::endl;
            return 0;
        }
    }
    
    LOG(INFO, "<simplehttpfileserver> port: %d", port);
    for(auto& m: mappings) {
        LOG(TRACE, "<simplehttpfileserver> domain: '%s' (size: %d)", m.first.data(), m.second->size());
        for(auto& m2: *(m.second)) LOG(TRACE, "<simplehttpfileserver> \t%s => %s", m2.first.data(), m2.second.data());
    }

    //always install signal handler in main thread, and before making other threads.
    LOG(INFO, "<simplehttpfileserver> Setup Signal Handler (SIGINT, SIGTERM, SIGUSR1)", 0);
    
    auto sighdlr = [](int sig) {
                       LOG(INFO, "<simplehttpfileserver> receiving signal: %d", sig);
                       if(mgr_ != nullptr) {
                           mgr_->close();
                           mgr_ = nullptr;
                       }
                   };
    auto sighdlr_noop = [](int sig) {};
    std::signal(SIGINT,  sighdlr); // ctrl-c
    std::signal(SIGTERM, sighdlr); // kill <pid>
    std::signal(SIGUSR1, sighdlr_noop); // used to interrupt epoll_wait
    // std::signal(SIGUSR2, sighdlr_noop); // used to interrupt epoll_wait
    
    auto mgr = std::make_unique<ugorji::conn::Manager>(port, workers);
    mgr_ = mgr.get();
    std::string err = "";
    mgr->open(err);
    if(err.size() > 0) {
        LOG(ERROR, "%s", err.data());
        return 1;
    }

    std::vector<std::string> empty;
    std::vector<std::unique_ptr<SimpleHttpFileServerHandler>> hdlrs;
    auto fn = [&]() mutable -> decltype(auto) {
                  auto hh = std::make_unique<SimpleHttpFileServerHandler>(mappings, empty, empty);
                  auto hdlr = hh.get();
                  hdlrs.push_back(std::move(hh));
                  return *hdlr;
              };
    
    mgr->run(fn, true);
    mgr->wait();
    
    int exitcode = (mgr->hasServerErrors() ? 1 : 0);
    
    LOG(INFO, "<main>: shutdown completed", 0);

    return exitcode;
}
