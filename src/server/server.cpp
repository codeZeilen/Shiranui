#include "server.hpp"
#include "../runtime/value_printer.hpp"
#include "../runtime/dsl/dsl_exception.hpp"
#include <sstream>
#include <chrono>

namespace shiranui{
    namespace server{
        PipeServer::PipeServer(std::istream& i,std::ostream& o)
            : is(i),os(o),flyline_lock(FLYLINE_LOCK_FREE){
        }

        PipeServer::~PipeServer(){
            main_thread.interrupt();
            main_thread.join();
            for(sp<boost::thread> t : flyline_threads){
                t->interrupt();
                t->join();
            }
            current_diver = nullptr;
            flyline_threads.clear();
            program_per_flyline.clear();
            diver_per_flyline.clear();
        }
        void PipeServer::start(){
            boost::thread receive(&PipeServer::receive,this);
            receive.join();
        }

        void PipeServer::send_command(const std::string& command,const std::string& value,
                                      const int loadcount){

            boost::this_thread::interruption_point();
            os_lock.lock();
            if(value != ""){
                os << how_many_lines(value) << " " << loadcount << " " << command << "\n"
                   << value << std::endl;
            }else{
                os << how_many_lines(value) << " " << loadcount << " " << command << std::endl;
            }
            os_lock.unlock();
        }
        void PipeServer::send_command_with_two_points(const std::string& command,
                                                      const int start_point,
                                                      const int end_point,
                                                      const int loadcount){
            std::stringstream ss;
            ss << start_point << " " << end_point;
            return send_command(command,ss.str(),loadcount);
        }
        void PipeServer::send_syntaxerror(const int start_point,const int end_point,
                                          const int loadcount){
            return send_command_with_two_points(COMMAND_SYNTAXEROR,
                                                start_point,end_point,loadcount);
        }
        void PipeServer::send_good_flyline(const int start_point,const int end_point,
                                           const int insert_point,const int remove_length,
                                           const int loadcount){
            std::stringstream ss;
            ss << start_point << " " << end_point << std::endl;
            ss << insert_point << " " << remove_length;
            return send_command(COMMAND_GOOD_FLYLINE, ss.str(), loadcount);
        }
        void PipeServer::send_bad_flyline(const int start_point,const int end_point,
                                          const int insert_point,const int remove_length,
                                          const std::string& error,const int loadcount){
            std::stringstream ss;
            ss << start_point << " " << end_point << std::endl;
            ss << insert_point << " " << remove_length << std::endl;
            ss << error;
            return send_command(COMMAND_BAD_FLYLINE,ss.str(),loadcount);
        }
        void PipeServer::send_runtimeerror(const int start_point,const int end_point,
                                           const int loadcount){
            return send_command_with_two_points(COMMAND_RUNTIMEERROR,
                                                start_point,end_point,loadcount);
        }

        void PipeServer::send_idle_flyline(const int start_point,const int end_point,
                                           const int insert_point,const int remove_length,
                                           const std::string& value,const int loadcount){
            std::stringstream ss;
            ss << start_point << " " << end_point << std::endl
               << insert_point << " " << remove_length << std::endl
               << value;
            return send_command(COMMAND_IDLE_FLYLINE,ss.str(),loadcount);
        }

        void PipeServer::send_lock_flyline(const int start_point,const int end_point,
                                           const int loadcount){
            return send_command_with_two_points(COMMAND_LOCK_FLYLINE,
                                                start_point,end_point,loadcount);
        }

        template<typename T>
        void PipeServer::send_debug_print(const T& value,const int loadcount){
            std::stringstream ss;
            ss << value;
            return send_command(COMMAND_DEBUG_PRINT,ss.str(),loadcount);
        }

        void PipeServer::send_dive_strike(const int start_point,const int end_point,
                                          const int loadcount){
            return send_command_with_two_points(COMMAND_DIVE_STRIKE,
                                                start_point,end_point,loadcount);
        }
        void PipeServer::send_dive_highlight(const int start_point,const int end_point,
                                             const int loadcount){
            return send_command_with_two_points(COMMAND_DIVE_HIGHLIGHT,
                                                start_point,end_point,loadcount);
        }
        void PipeServer::send_dive_explore(const int start_point,const int end_point,
                                           const std::string& what,const int loadcount){
            std::stringstream ss;
            ss << start_point << " " << end_point << std::endl
               << what;
            return send_command(COMMAND_DIVE_EXPLORE,ss.str(),loadcount);
        }
        void PipeServer::send_dive_flymark_result(const int start_point,const int end_point,
                                      const int insert_point,const int remove_length,
                                      const std::string& value,const int loadcount){
            std::stringstream ss;
            ss << start_point << " " << end_point << std::endl
               << insert_point << " " << remove_length << std::endl
               << value;
            return send_command(COMMAND_FLYMARK,ss.str(),loadcount);
        }
        void PipeServer::send_dive_flymark_index(const int start_point,const int end_point,
                                                 const int index,const int loadcount){
            std::stringstream ss;
            ss << start_point << " " << end_point << std::endl
               << index;
            return send_command(COMMAND_FLYMARK_INDEX,ss.str(),loadcount);
        }
        void PipeServer::send_dive_clear(const int loadcount){
            return send_command(COMMAND_DIVE_CLEAR,"",loadcount);
        }

        void PipeServer::send_dive_lift_result(const std::string what,const int loadcount){
            return send_command(COMMAND_LIFT_RESULT,what,loadcount);
        }

        // receive
        void PipeServer::receive(){
            while(true){
                int line,loadcount;
                std::string command;
                while(is >> line >> loadcount >> command){
                    is.ignore(); // ignore newline.
                    std::string value;
                    for(int i=0;i<line;i++){
                        std::string s;
                        // getline doesn't have newline.
                        std::getline(is,s);
                        value += s;
                        if(i != line-1){
                            value += "\n";
                        }
                    }
                    receive_command(command,value,loadcount);
                }
                is.clear(); // remove eof flag
            }
        }
        void PipeServer::receive_command(const std::string& command,
                                         const std::string& value,
                                         const int loadcount){
            // loadcount can be ignored by dive or surface
            if(command == COMMAND_CHANGE){
                return on_change_command(value,loadcount);
            }else if(command == COMMAND_DIVE){
                return on_dive_command(value,loadcount);
            }else if(command == COMMAND_SURFACE){
                return on_surface_command(value,loadcount);
            }else if(command == COMMAND_LIFT){
                return on_lift_command(value,loadcount);
            }else if(command == COMMAND_FLYMARK_JUMP){
                return on_jump_command(value,loadcount);
            }else if(command == COMMAND_MOVE_TO_CALLER){
                return on_move_to_caller_command(value,loadcount);
            }else{
                send_debug_print("Unknown command:" + command,loadcount);
            }
        }

        void PipeServer::on_change_command(const std::string& value,
                                           const int loadcount){
            main_thread.interrupt();
            main_thread.join();
            for(sp<boost::thread> t : flyline_threads){
                t->interrupt();
                t->join();
            }
            flyline_threads.clear();
            std::stringstream ss(value);
            while(not ss.eof()){
                int point,remove_length,line_size;
                std::string insert_value;
                ss >> point >> remove_length >> line_size;
                point--;
                ss.ignore(); // remove newline.
                for(int i=0;i<line_size;i++){
                    std::string s;
                    getline(ss,s);
                    insert_value += s;
                    if(i != line_size - 1){
                        insert_value += '\n';
                    }
                }
                source.erase(point,remove_length);
                source.insert(point,insert_value);
            }

            main_thread = boost::thread(boost::bind(&PipeServer::exec,this,source,loadcount));
        }

        void PipeServer::on_dive_command(const std::string& value,const int loadcount){
            std::stringstream ss(value);
            int point;ss >> point;
            // FIXME:temporary
            auto programs = program_per_flyline;
            auto divers = diver_per_flyline;
            // TODO: check main_thread,flyline_threads condition.
            for(int i=0;i<static_cast<int>(programs.size());i++){
                int start_point = programs[i]->flylines[i]->point;
                int end_point = start_point + programs[i]->flylines[i]->length;
                if(start_point <= point and point <= end_point){
                    // currently running
                    if(divers[i] == nullptr){
                        std::stringstream out;
                        out << "dive_start -> " << i << std::endl
                            << " but currently running.";
                        // send_debug_print(out.str(),loadcount);
                    }else{
                        std::stringstream out;
                        out << "dive_start -> " << i;
                        // send_debug_print(out.str(),loadcount);
                        dive_start(divers[i],
                                   programs[i]->flylines[i],
                                   programs[i],loadcount);
                        flyline_lock = i;
                    }
                    return;
                }
            }
            // maybe dive to inner function
            if(current_diver != nullptr){
                dive(current_diver,point,loadcount);
            }else{
                std::stringstream out;
                out << "cannot find where to dive." << std::endl;
                send_debug_print(out.str(),loadcount);
            }
        }

        void PipeServer::on_jump_command(const std::string& value,const int loadcount){
            std::stringstream ss(value);
            int point,index;
            ss >> point >> index;
            if(current_diver != nullptr){
                auto message = current_diver->jump(point,index);
                send_diving_message(message,loadcount);
            }else{
                send_debug_print("Not diving now",loadcount);
            }
        }
        // TODO: should treat point.(if left is list...)
        void PipeServer::dive_start(sp<runtime::diver::Diver> diver,
                                    sp<syntax::ast::FlyLine> sf,
                                    sp<syntax::ast::SourceCode> source_ast,
                                    const int loadcount){
            using namespace shiranui::runtime::diver;
            using namespace shiranui::syntax::ast;
            current_diver = diver;
            diver->clear();
            send_dive_clear(loadcount);
            {
                sp<TestFlyLine> l = std::dynamic_pointer_cast<TestFlyLine>(sf);
                if(l != nullptr){
                    DivingMessage ms = diver->scan_flymark(source_ast);
                    ms = ms + diver->dive(l->left);
                    send_diving_message(ms,loadcount);
                }
            }
            {
                sp<IdleFlyLine> l = std::dynamic_pointer_cast<IdleFlyLine>(sf);
                if(l != nullptr){
                    DivingMessage ms = diver->scan_flymark(source_ast);
                    ms = ms + diver->dive(l->left);
                    send_diving_message(ms,loadcount);
                }
            }
            {
                int start_point = sf->point;
                int end_point = start_point + sf->length;
                send_lock_flyline(start_point,end_point,loadcount);
            }
        }

        void PipeServer::dive(sp<runtime::diver::Diver> diver,int point,const int loadcount){
            using namespace shiranui::runtime::diver;
            DivingMessage ms = diver->dive(point);
            send_diving_message(ms,loadcount);
        }

        void PipeServer::on_surface_command(const std::string&,const int loadcount){
            if(current_diver != nullptr){
                surface(current_diver,loadcount);
            }else{
            }
        }
        void PipeServer::on_move_to_caller_command(const std::string&,const int loadcount){
            if(current_diver != nullptr){
                move_to_caller(current_diver,loadcount);
            }else{
            }
        }
        // undo for diver.
        void PipeServer::surface(sp<runtime::diver::Diver> diver,int loadcount){
            using namespace shiranui::runtime::diver;
            DivingMessage ms = diver->surface();
            send_diving_message(ms,loadcount);
        }
        void PipeServer::move_to_caller(sp<runtime::diver::Diver> diver,int loadcount){
            using namespace shiranui::runtime::diver;
            DivingMessage ms = diver->move_to_caller();
            send_diving_message(ms,loadcount);
        }

        void PipeServer::on_lift_command(const std::string& value,const int loadcount){
            std::stringstream ss(value);
            int from,to;
            ss >> from >> to;
            if(current_diver != nullptr){
                auto ms = current_diver->lift(from,to);
                send_diving_message(ms,loadcount);
            }else{
                send_debug_print("not diving",loadcount);
            }
        }

        void PipeServer::exec(std::string source,const int loadcount){
            using namespace shiranui;
            using namespace shiranui::syntax;
            using namespace shiranui::syntax::ast;
            using namespace shiranui::runtime;
            using namespace shiranui::runtime::DSL;
            using namespace shiranui::runtime::diver;

            const auto start_time = std::chrono::system_clock::now();
            // send_debug_print(source,loadcount);
            pos_iterator_t first(source.begin()),last(source.end());
            pos_iterator_t iter = first;
            bool ok = false;
            Parser<pos_iterator_t> resolver;
            sp<SourceCode> program;
            try{
                ok = parse(iter,last,resolver,program);
            }catch (boost::spirit::qi::expectation_failure<pos_iterator_t> const& x){
                send_syntaxerror(std::distance(first,x.first),
                                 std::distance(first,x.last),loadcount);
                return;
            }
            if(ok and iter == last){
                Runner r(true);
                try{
                    program->accept(r);
                }catch(NoSuchVariableException e){
                    int start_point = e.where->point;
                    int end_point = start_point + e.where->length;
                    send_runtimeerror(start_point,end_point,loadcount);
                    return;
                }catch(ConvertException e){
                    int start_point = e.where->point;
                    int end_point = start_point + e.where->length;
                    send_runtimeerror(start_point,end_point,loadcount);
                    return;
                }catch(RuntimeException e){
                    int start_point = e.where->point;
                    int end_point = start_point + e.where->length;
                    send_runtimeerror(start_point,end_point,loadcount);
                    return;
                }catch(DSLUnknownVariable e){
                    int start_point = e.where->point;
                    int end_point = start_point + e.where->length;
                    send_runtimeerror(start_point,end_point,loadcount);
                    return;
                }catch(DSLAlreadyUsedVariable e){
                    int start_point = e.where->point;
                    int end_point = start_point + e.where->length;
                    send_runtimeerror(start_point,end_point,loadcount);
                    return;
                }
                program_per_flyline = std::vector<sp<SourceCode>>(program->flylines.size(),nullptr);
                diver_per_flyline = std::vector<sp<Diver>>(program->flylines.size(),nullptr);
                // TODO:should kill diver process
                current_diver = nullptr;

                for(int i=0;i<static_cast<int>(program->flylines.size());i++){
                    flyline_threads.push_back(std::make_shared<boost::thread>(
                             boost::bind(&PipeServer::run_flyline,this,source,i,loadcount)));
                }
            }else{
                send_syntaxerror(std::distance(first,iter),std::distance(first,last),loadcount);
            }
            const auto end_time = std::chrono::system_clock::now();
            const auto time_span = end_time - start_time;
            std::stringstream ts;

            ts << "First:" << std::chrono::duration_cast<std::chrono::milliseconds>(time_span).count() << "[ms]";
            send_debug_print(ts.str(),loadcount);
        }

        void PipeServer::run_flyline(std::string source,const int flyline_index,
                                     const int loadcount){
            using namespace shiranui;
            using namespace shiranui::syntax;
            using namespace shiranui::syntax::ast;
            using namespace shiranui::runtime;
            using namespace shiranui::runtime::diver;

            const auto start_time = std::chrono::system_clock::now();
            pos_iterator_t first(source.begin()),last(source.end());
            pos_iterator_t iter = first;
            Parser<pos_iterator_t> resolver;
            sp<SourceCode> program;
            parse(iter,last,resolver,program);

            boost::this_thread::interruption_point();
            program_per_flyline[flyline_index] = program; // TODO:use better way.
            Runner r(true);
            sp<FlyLine> sf = program->flylines[flyline_index];
            program->accept(r); // do not cause exception.

            {
                sp<TestFlyLine> l = std::dynamic_pointer_cast<TestFlyLine>(sf);
                if(l != nullptr) run_testflyline(r,l,loadcount);
            }
            {
                sp<IdleFlyLine> l = std::dynamic_pointer_cast<IdleFlyLine>(sf);
                if(l != nullptr) run_idleflyline(r,l,program,loadcount);
            }
            diver_per_flyline[flyline_index] = std::make_shared<Diver>(program);
            if(flyline_index == flyline_lock){
                dive_start(diver_per_flyline[flyline_index],sf,
                           program,loadcount);
            }
            const auto end_time = std::chrono::system_clock::now();
            const auto time_span = end_time - start_time;
            std::stringstream ts;
            ts << "Exec [" << flyline_index << "]: " << std::chrono::duration_cast<std::chrono::milliseconds>(time_span).count() << "[ms]";
            //send_debug_print(ts.str(),loadcount);
        }

        void PipeServer::run_testflyline(runtime::Runner& r,
                                         sp<syntax::ast::TestFlyLine> sf,
                                         const int loadcount){
            using namespace syntax::ast;
            using namespace runtime::value;
            using namespace shiranui::runtime;
            using namespace shiranui::runtime::DSL;

            int start_point = sf->point;
            int end_point = start_point + sf->length;

            auto bin = std::make_shared<BinaryOperator>("=",sf->left,sf->right);
            int remove_start = sf->right->point + sf->right->length;
            int remove_length = sf->error != nullptr ?
                                  sf->error->point + sf->error->length - remove_start
                                : 0;
            sp<Value> left,right;
            try {
                sf->left->accept(r);
                left = r.cur_v;
                sf->right->accept(r);
                right = r.cur_v;
            }catch (NoSuchVariableException e){
                return send_bad_flyline(start_point, end_point, remove_start, remove_length, " || " + e.str(), loadcount);
            }catch (ConvertException e){
                return send_bad_flyline(start_point, end_point, remove_start, remove_length, " || " + e.str(), loadcount);
            }catch (AssertException e){
                return send_bad_flyline(start_point, end_point, remove_start, remove_length, " || " + e.str(), loadcount);
            }catch (ZeroDivException e){
                return send_bad_flyline(start_point, end_point, remove_start, remove_length, " || " + e.str(), loadcount);
            }catch (MaxDepthExceededException e){
                return send_bad_flyline(start_point, end_point, remove_start, remove_length, " || " + e.str(), loadcount);
            }catch (RuntimeException e){
                return send_bad_flyline(start_point, end_point, remove_start, remove_length, " || " + e.str(), loadcount);
            }catch(DSLUnknownVariable e){
                return send_bad_flyline(start_point, end_point, remove_start, remove_length, " || " + e.str(), loadcount);
            }catch(DSLAlreadyUsedVariable e){
                return send_bad_flyline(start_point, end_point, remove_start, remove_length, " || " + e.str(), loadcount);
            }

            if(check_equality(left,right)){
                send_good_flyline(start_point, end_point, remove_start,remove_length,loadcount);
            }else{
                send_bad_flyline(start_point, end_point, remove_start, remove_length, " || " + to_reproductive(left),
                                 loadcount);
            }
        }

        void PipeServer::run_idleflyline(runtime::Runner& r,
                                         sp<syntax::ast::IdleFlyLine> sf,
                                         sp<syntax::ast::SourceCode> program,
                                         const int loadcount){
            using namespace syntax::ast;
            using namespace runtime::value;
            using namespace shiranui::runtime;
            using namespace shiranui::runtime::DSL;
            // TODO:save infomation.
            auto run_idleflyline_sub = [this,sf,loadcount](std::string left_str){
                int start_point = sf->point;
                int end_point = start_point + sf->length;
                if(sf->right != nullptr){
                    int remove_start = sf->right->point;
                    int remove_end = remove_start + sf->right->length;
                    int remove_length = remove_end - remove_start;
                    send_idle_flyline(start_point,end_point,remove_start,remove_length,
                                      left_str,loadcount);
                }else{
                    send_idle_flyline(start_point,end_point,end_point-1,0,left_str,loadcount);
                }
            };
            try{
                sf->left->accept(r);
            }catch(NoSuchVariableException e){
                return run_idleflyline_sub(e.str());
            }catch(ConvertException e){
                return run_idleflyline_sub(e.str());
            }catch(AssertException e){
                return run_idleflyline_sub(e.str());
            }catch(ZeroDivException e){
                return run_idleflyline_sub(e.str());
            }catch(MaxDepthExceededException e){
                return run_idleflyline_sub(e.str());
            }catch(RuntimeException e){
                return run_idleflyline_sub(e.str());
            }catch(DSLUnknownVariable e){
                return run_idleflyline_sub(e.str());
            }catch(DSLAlreadyUsedVariable e){
                return run_idleflyline_sub(e.str());
            }

            sp<Value> left = r.cur_v;
            std::string left_str = to_reproductive(left,program);
            run_idleflyline_sub(left_str);
        }

        void PipeServer::send_diving_message(runtime::diver::DivingMessage message,
                                             int loadcount){
            using namespace runtime::diver;
            std::stringstream ss(message.str());
            // send_dive_clear(-1);
            if(message.need_refresh){
                send_dive_clear(-1);
            }

            std::string command;
            while(ss >> command){
                if(command == STRIKE){
                    int start_point,length;
                    ss >> start_point >> length;
                    int end_point = start_point + length;
                    send_dive_strike(start_point,end_point,loadcount);
                }else if(command == HIGHLIGHT){
                    int start_point,length;
                    ss >> start_point >> length;
                    int end_point = start_point + length;
                    send_dive_highlight(start_point,end_point,loadcount);
                }else if(command == EXPLORE){
                    int start_point,length;
                    ss >> start_point >> length;
                    int end_point = start_point + length;
                    std::string value;
                    ss.ignore();
                    std::getline(ss,value);
                    send_dive_explore(start_point,end_point,value,loadcount);
                }else if(command == ERROR){
                    int start_point,length;
                    std::string what;
                    ss >> start_point >> length;
                    ss.ignore();
                    std::getline(ss,what);

                    std::stringstream out;
                    out << "ERROR: " << what << " (" << start_point 
                                     << "," << start_point + length << ")";
                    send_debug_print(out.str(),loadcount);
                }else if(command == FLYMARK_RESULT){
                    int start_point,end_point,length;
                    int insert_point,remove_length;
                    std::string what;
                    ss >> start_point >> length;
                    end_point = start_point + length;
                    ss.ignore();
                    ss >> insert_point >> remove_length;
                    ss.ignore();
                    std::getline(ss,what);
                    //TODO: Add loadcount here.
                    send_dive_flymark_result(start_point,end_point,insert_point,
                                             remove_length,what,loadcount);
                }else if(command == LIFT_RESULT){
                    std::string what;
                    ss.ignore();
                    std::getline(ss,what);
                    send_dive_lift_result(what,loadcount);
                }else if(command == FLYMARK_INDEX){
                    int start_point,end_point,length,index;
                    ss >> start_point >> length >> index;
                    end_point = start_point + length;
                    send_dive_flymark_index(start_point,end_point,index,loadcount);
                }else{
                    send_debug_print(command + " ????",loadcount);
                }
            }
        }

        int how_many_lines(const std::string& s){
            if(s == "") return 0;
            return count(s.begin(),s.end(),'\n')+1;
        }
    }
}
