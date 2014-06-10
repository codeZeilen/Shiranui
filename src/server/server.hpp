#ifndef SERVER_HPP_INCLUDED
#define SERVER_HPP_INCLUDED

#include <iostream>
#include <mutex>
#include <thread>

namespace shiranui{
    namespace server{
        int how_many_lines(const std::string&);
        struct PipeServer{
            std::istream& is;
            std::ostream& os;
            std::mutex send_lock;
            PipeServer(std::istream&,std::ostream&);
            void start();
            void send_command(const std::string&,const std::string&);
            void receive_command();
            virtual void on_receive_command(const std::string&,const std::string&);
        };
    }
}
#endif
