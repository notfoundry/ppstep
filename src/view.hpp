#ifndef PPSTEP_VIEW_HPP
#define PPSTEP_VIEW_HPP

#include <cstdlib>

#include <boost/spirit/include/qi.hpp>

#include <linenoise/linenoise.h>

#include "client_fwd.hpp"
#include "utils.hpp"

namespace ppstep {
    using namespace boost::spirit;

    template <class TokenT, class ContainerT>
    struct client_cli {

        client_cli(client<TokenT, ContainerT>& cl) : cl(cl), steps_requested(0) {}

        template <class Attr>
        void step(Attr const& attr) {
            if (attr) {
                steps_requested = boost::fusion::at_c<1>(*attr);
            } else {
                steps_requested = 1;
            }
        }

        template <class Attr>
        void add_breakpoint(Attr const& attr, break_condition cond) {
            cl.add_breakpoint({attr.begin(), attr.end()}, cond);
        }

        template <class Attr>
        void remove_breakpoint(Attr const& attr, break_condition cond) {
            cl.remove_breakpoint({attr.begin(), attr.end()}, cond);
        }

        void step_continue() {
            steps_requested = 1;
            cl.set_mode(stepping_mode::UNTIL_BREAK);
        }

        void quit() {
            throw session_terminate();
        }

        void current_state() {
            print_token_container(std::cout, cl.last_history()) << std::endl;
        }

        template <typename Iterator>
        bool parse(Iterator first, Iterator last) {
            using qi::lit;
            using qi::print;
            using qi::uint_;
            using qi::alpha;
            using qi::lexeme;
            using qi::alnum;
            using qi::eoi;
            using qi::phrase_parse;
            using ascii::space;
            using ascii::space_type;

            auto token = +(print);

#define PPSTEP_ACTION(...) ([this](auto const& attr){ __VA_ARGS__; })

            qi::rule<Iterator, ascii::space_type> grammar =
                lexeme[(lit("step") | lit("s")) >> -(+space >> uint_)][PPSTEP_ACTION(step(attr))]
              | (lit("continue") | lit("c"))[PPSTEP_ACTION(step_continue())]
              | lexeme[
                  (lit("break") | lit("b")) >> *space > (
                        ((lit("call") | lit("c")) >> +space >> token[PPSTEP_ACTION(add_breakpoint(attr, break_condition::CALL))])
                      | ((lit("expand") | lit("e")) >> +space >> token[PPSTEP_ACTION(add_breakpoint(attr, break_condition::EXPANDED))])
                      | ((lit("rescan") | lit("r")) >> +space >> token[PPSTEP_ACTION(add_breakpoint(attr, break_condition::RESCANNED))])
                      | ((lit("lex") | lit("l")) >> +space >> token[PPSTEP_ACTION(add_breakpoint(attr, break_condition::LEXED))])
                )]
              | lexeme[
                  (lit("delete") | lit("d")) >> *space > (
                        ((lit("call") | lit("c")) >> +space >> token[PPSTEP_ACTION(remove_breakpoint(attr, break_condition::CALL))])
                      | ((lit("expand") | lit("e")) >> +space >> token[PPSTEP_ACTION(remove_breakpoint(attr, break_condition::EXPANDED))])
                      | ((lit("rescan") | lit("r")) >> +space >> token[PPSTEP_ACTION(remove_breakpoint(attr, break_condition::RESCANNED))])
                      | ((lit("lex") | lit("l")) >> +space >> token[PPSTEP_ACTION(remove_breakpoint(attr, break_condition::LEXED))])
                )]
              | (lit("quit") | lit("q"))[PPSTEP_ACTION(quit())]
              | eoi[PPSTEP_ACTION(current_state())];

#undef PPSTEP_ACTION

            qi::on_error<qi::fail>(grammar, [](auto const& args, auto const& ctx, auto const&) {
                std::cout << "Found unexpected argument \"" << boost::fusion::at_c<2>(args) << "\" while parsing \"" << boost::fusion::at_c<0>(args) << "\"." << std::endl;
            });

            bool r = phrase_parse(first, last, grammar, space);
            if (first != last)
                return false;
            return r;
        }

        void prompt() {
            if (steps_requested > 0) --steps_requested;
            if (steps_requested) return;

            cl.set_mode(stepping_mode::FREE);

            current_state();

            for (char* raw_line; (raw_line = linenoise("pp> ")) != nullptr;) {
                linenoiseHistoryAdd(raw_line);

                bool valid = parse(raw_line, raw_line + std::strlen(raw_line));
                if (!valid) {
                    std::cout << "Undefined command: \"" << raw_line << "\"." << std::endl;
                }

                std::free(static_cast<void*>(raw_line));

                if (valid) {
                    if (steps_requested) break;
                }
            }
        }

    private:
        client<TokenT, ContainerT>& cl;
        std::size_t steps_requested;
    };
}

#endif // PPSTEP_VIEW_HPP