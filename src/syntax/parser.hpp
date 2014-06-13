#ifndef PARSER_HPP_INCLUDED
#define PARSER_HPP_INCLUDED

// #define BOOST_SPIRIT_DEBUG
#define BOOST_SPIRIT_USE_PHOENIX_V3
#include <boost/spirit/include/qi.hpp>

#include <boost/spirit/include/phoenix.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>
#include <boost/spirit/include/support_line_pos_iterator.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_fusion.hpp>

#include <boost/fusion/include/io.hpp>

#include <iostream>
#include <iomanip>
#include <string>

#include "ast.hpp"
#include "../misc.hpp"

// to put line,column info to AST,see following url.
// http://stackoverflow.com/questions/19612657/boostspirit-access-position-iterator-from-semantic-actions
namespace shiranui{
    namespace syntax{
        typedef boost::spirit::line_pos_iterator<std::string::const_iterator>
                                                               pos_iterator_t;


        struct error_handler_f{
            typedef boost::spirit::qi::error_handler_result result_type;
            // does'nt work currently.(may need space skipping?)
            // get_line,get_column is defined in
            //   http://www.boost.org/doc/libs/1_47_0/boost/spirit/home/support/iterators/line_pos_iterator.hpp

            template<typename T1,typename T2,typename T3,typename T4>
            result_type operator()(T1 b,T2 e,T3 where,const T4& what) const{
                std::cerr << "Error expecting " << what << " in line " << get_line(where) << ":" << std::endl
                          << std::string(b,e) << std::endl
                          << std::setw(std::distance(b,where)) << "^---- here" << std::endl;
                return boost::spirit::qi::fail;
            }
        };

        template<typename Iterator>
        struct annotation_f{
            typedef void result_type;
            const Iterator first;
            annotation_f(Iterator first_) : first(first_){}
            template<typename Val,typename First,typename Last>
            void operator()(Val& v,First f,Last l) const{
                do_annotate(v,f,l,first);
            }
        private:
            void static do_annotate(ast::LocationInfo& li,Iterator f,Iterator l,Iterator first){
                li.line = get_line(f);
                li.column = get_column(first,f);
                li.length = std::distance(f,l);
            }
            void static do_annotate(ast::LocationInfo* li,Iterator f,Iterator l,Iterator first){
                li->line = get_line(f);
                li->column = get_column(first,f);
                li->length = std::distance(f,l);
            }

            static void do_annotate(...){
                std::cerr << "(not having LocationInfo)" << std::endl;
            }
        };

        template<typename Iterator=pos_iterator_t,typename Skipper=boost::spirit::qi::space_type>
        struct Parser : public boost::spirit::qi::grammar<Iterator,ast::SourceCode*(),Skipper>{
            // change const_definement to sourcecode.
            Parser(Iterator first) : Parser::base_type(source),
                                     annotate(first){
                namespace ph = boost::phoenix;
                namespace qi = boost::spirit::qi;
                using namespace qi::ascii;
                using qi::lit;
                auto set_location_info = annotate(qi::_val,qi::_1,qi::_3);
                {
                    identifier.name("identifier");
                    identifier = boost::spirit::as_string[(alpha >> *(alnum | char_('_')))];
                    on_success(identifier,set_location_info);
                }

                {
                    integer.name("integer");
                    integer    = qi::int_ [qi::_val = ph::new_<ast::Number>(qi::_1)];
                    on_success(integer,set_location_info);
                }

                {
                    variable.name("variable");
                    variable   = identifier [qi::_val = ph::new_<ast::Variable>(qi::_1)];
                    on_success(variable,set_location_info);
                }
                {
                    function.name("function");
                    function   = (lit("\\") >> "(" >> (identifier % ",") >> ")" >> block)
                                  [qi::_val = ph::new_<ast::Function>(qi::_1,qi::_2)]
                               | (lit("\\") > "(" > ")" > block)
                                  [qi::_val = ph::new_<ast::Function>(
                                              std::vector<ast::Identifier>(),qi::_1)]
                               ;
                    on_success(function,set_location_info);

                }

                {
                    definement.name("definement");
                    definement = ("let" > identifier > "=" > expression)
                                  [qi::_val = ph::new_<ast::Definement>(qi::_1,qi::_2,true)]
                               | ("mut" > identifier > "=" > expression)
                                  [qi::_val = ph::new_<ast::Definement>(qi::_1,qi::_2,false)]
                               ;
                    on_success(definement,set_location_info);
                }
                {
                    ifelse_stmt.name("if_else_statement");
                    ifelse_stmt= ("if" >> expression >> "then" >> block >> "else" >> block)
                                  [qi::_val = ph::new_<ast::IfElseStatement>(qi::_1,qi::_2,qi::_3)]
                               | ("if" >> expression >> "then" >> block)
                                  [qi::_val = ph::new_<ast::IfElseStatement>(qi::_1,qi::_2)]
                               ;
                    on_success(ifelse_stmt,set_location_info);
                }
                {
                    return_stmt.name("return_statement");
                    return_stmt = ("return" > expression)
                                 [qi::_val = ph::new_<ast::ReturnStatement>(qi::_1)]
                                ;
                    on_success(return_stmt,set_location_info);
                }
                {
                    block.name("block");
                    block = ("{" > *statement > "}")
                             [qi::_val = ph::new_<ast::Block>(qi::_1)]
                          ;
                    on_success(block,set_location_info);
                }
                {
                    statement.name("statement");
                    statement  = (definement > ";")
                               | ifelse_stmt
                               | block
                               | (return_stmt > ";")
                               ;
                    on_success(statement,set_location_info);
                }

                {
                    expression.name("expression");
                    expression = ifelse_expr [qi::_val = qi::_1]
                               | test [qi::_val = qi::_1]
                               ;
                    on_success(expression,set_location_info);
                }

                {
                    ifelse_expr.name("if_else_expression");
                    ifelse_expr= ("if" >> expression >> "then" 
                                       >> expression >> "else" >> expression)
                                 [qi::_val = ph::new_<ast::IfElseExpression>(qi::_1,qi::_2,qi::_3)];
                    on_success(ifelse_expr,set_location_info);
                }
                {
                    test.name("test");
                    test       = or_test [qi::_val = qi::_1];
                    on_success(test,set_location_info);
                }
                {
                    or_test.name("or_test");
                    or_test    = and_test [qi::_val = qi::_1] >>
                                  *(("or" >> and_test)
                                   [qi::_val = ph::new_<ast::BinaryOperator>("or",qi::_val,qi::_1)])
                               ;
                    on_success(or_test,set_location_info);
                }
                {
                    and_test.name("and_test");
                    and_test   = not_test [qi::_val = qi::_1] >> 
                                 *(("and" >> not_test)
                                   [qi::_val = ph::new_<ast::BinaryOperator>("and",qi::_val,qi::_1)])
                               ;
                    on_success(and_test,set_location_info);
                }
                {
                    not_test.name("not_test");
                    not_test   = ("not" >> not_test)
                                  [qi::_val = ph::new_<ast::UnaryOperator>("not",qi::_1)]
                               | comparison [qi::_val = qi::_1]
                               ;
                    on_success(not_test,set_location_info);
                }

                {
                    comparison.name("comparison");
                    // TODO add >,<,<=,>=,...
                    comparison = addi [qi::_val = qi::_1] >> 
                                 *(("=" >> addi)
                                   [qi::_val = ph::new_<ast::BinaryOperator>("=",qi::_val,qi::_1)]
                                 | ("/=" >> multi)
                                   [qi::_val = ph::new_<ast::BinaryOperator>("/=",qi::_val,qi::_1)]
                                  )
                               ;
                    on_success(comparison,set_location_info);
                }

                {
                    addi.name("addi");
                    addi       = multi [qi::_val = qi::_1] >> 
                                 *(('+' >> addi)
                                   [qi::_val = ph::new_<ast::BinaryOperator>("+",qi::_val,qi::_1)]
                                 | ('-' >> multi)
                                   [qi::_val = ph::new_<ast::BinaryOperator>("-",qi::_val,qi::_1)]
                                  )
                               ;
                    on_success(addi,set_location_info);
                }

                {
                    multi.name("multi");
                    multi      = unary [qi::_val = qi::_1] >>
                                 *(('*' >> unary)
                                   [qi::_val = ph::new_<ast::BinaryOperator>("*",qi::_val,qi::_1)]
                                 | ('/' >> unary)
                                   [qi::_val = ph::new_<ast::BinaryOperator>("/",qi::_val,qi::_1)]
                                 | ('%' >> unary)
                                   [qi::_val = ph::new_<ast::BinaryOperator>("%",qi::_val,qi::_1)]

                                  )
                               ;
                    on_success(multi,set_location_info);
                }
                {
                    unary.name("unary");
                    unary      = power [qi::_val = qi::_1]
                               | ("+" >> unary)
                                  [qi::_val = ph::new_<ast::UnaryOperator>("+",qi::_1)]
                               | ("-" >> unary)
                                  [qi::_val = ph::new_<ast::UnaryOperator>("-",qi::_1)]
                               ;
                    on_success(unary,set_location_info);
                }
                {
                    power.name("power");
                    power      = atom [qi::_val = qi::_1] >>
                                 *(('^' >> atom)
                                   [qi::_val = ph::new_<ast::BinaryOperator>("^",qi::_val,qi::_1)]
                                 | ('(' >> (expression % ',') >> ')')
                                   [qi::_val = ph::new_<ast::FunctionCall>(qi::_val,qi::_1)]
                                 | (lit('(') >> ')')
                                   [qi::_val = ph::new_<ast::FunctionCall>(
                                                        qi::_val,std::vector<ast::Expression*>())]
                                  )
                               ;
                    on_success(power,set_location_info);
                }

                {
                    atom.name("atom");
                    atom       = ("(" > expression > ")")
                               | integer
                               | variable
                               | function
                               ;
                    on_success(atom,set_location_info);
                }
                {
                    flyline.name("flyline");
                    // change expression to expression or error.
                    // add eol or something like that.
                    // do not use no_skip contains expression
                    flyline = ("#-" >> expression >> "->" >> expression >> ";")
                               [qi::_val = ph::new_<ast::FlyLine>(qi::_1,qi::_2)]
                            | ("#-" > expression > "->" > ";")
                               [qi::_val = ph::new_<ast::FlyLine>(qi::_1)]
                            ;
                    on_success(flyline,set_location_info);
                }
                {
                    source.name("source");
                    source = qi::eps [qi::_val = ph::new_<ast::SourceCode>()]
                          >> *(statement 
                                [ph::bind(&ast::SourceCode::add_statement,qi::_val,qi::_1)]
                              |flyline
                                [ph::bind(&ast::SourceCode::add_flyline,qi::_val,qi::_1)]
                              )
                           ;
                    on_success(source,set_location_info);
                }
            }
            boost::phoenix::function<error_handler_f> handler;
            boost::phoenix::function<annotation_f<Iterator>> annotate;

            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       expression;
            boost::spirit::qi::rule<Iterator,ast::Identifier()>                identifier;
            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       test;
            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       or_test;
            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       and_test;
            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       not_test;
            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       comparison;
            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       multi;
            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       addi;
            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       unary;
            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       power;
            boost::spirit::qi::rule<Iterator,ast::Expression*(),Skipper>       atom;
            boost::spirit::qi::rule<Iterator,ast::SourceCode*(),Skipper>       source;
            boost::spirit::qi::rule<Iterator,ast::Number*()>                   integer;
            boost::spirit::qi::rule<Iterator,ast::Function*(),Skipper>         function;
            boost::spirit::qi::rule<Iterator,ast::Variable*()>                 variable;
            boost::spirit::qi::rule<Iterator,ast::Definement*(),Skipper>       definement;
            boost::spirit::qi::rule<Iterator,ast::IfElseStatement*(),Skipper>  ifelse_stmt;
            boost::spirit::qi::rule<Iterator,ast::ReturnStatement*(),Skipper>  return_stmt;
            boost::spirit::qi::rule<Iterator,ast::IfElseExpression*(),Skipper> ifelse_expr;
            boost::spirit::qi::rule<Iterator,ast::Block*(),Skipper>            block;
            boost::spirit::qi::rule<Iterator,ast::FlyLine*(),Skipper>          flyline;
            boost::spirit::qi::rule<Iterator,ast::Statement*(),Skipper>        statement;
        };
    }
}

#endif
