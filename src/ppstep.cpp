#include <string>
#include <iostream>
#include <list>

#include <boost/wave.hpp>
#include <boost/wave/cpplexer/cpp_lex_token.hpp>
#include <boost/wave/cpplexer/cpp_lex_iterator.hpp>
#include <boost/wave/cpplexer/re2clex/cpp_re2c_lexer.hpp>

#include "client.hpp"
#include "server.hpp"

using token_type = boost::wave::cpplexer::lex_token<>;

using token_sequence_type = std::list<token_type, boost::fast_pool_allocator<token_type>>;

using lex_iterator_type = boost::wave::cpplexer::lex_iterator<token_type>;

using context_type =
    boost::wave::context<
        std::string::iterator,
        lex_iterator_type,
        boost::wave::iteration_context_policies::load_file_to_string,
        ppstep::server<token_type, token_sequence_type>
    >;


static std::string read_entire_file(std::istream&& instream) {
    instream.unsetf(std::ios::skipws);

    return std::string(std::istreambuf_iterator<char>(instream.rdbuf()), std::istreambuf_iterator<char>());
}

int main(int argc, char const** argv) {
    if (argc != 2) {
        std::cerr << "usage: pp <in_file>" << std::endl;
        return 1;
    }

    char const* input_file = argv[1];
    auto instring = read_entire_file(std::ifstream(input_file));

    auto client = ppstep::client<token_type, token_sequence_type>();
    auto hooks = ppstep::server<token_type, token_sequence_type>(client);
    context_type ctx(instring.begin(), instring.end(), input_file, hooks);

    static_assert(std::is_same_v<token_sequence_type, typename context_type::token_sequence_type>,
                  "wave context token container type not same as expansion tracer token container type");

    ctx.set_language(boost::wave::language_support(
        boost::wave::support_cpp2a
        | boost::wave::support_option_va_opt
        | boost::wave::support_option_convert_trigraphs
        | boost::wave::support_option_long_long
        | boost::wave::support_option_include_guard_detection
        | boost::wave::support_option_emit_pragma_directives
        | boost::wave::support_option_insert_whitespace));

    auto first = ctx.begin();
    auto last = ctx.end();
    try {
        while (first != last) {
            hooks.lexed_token(ctx, *first);
            ++first;
        }
        hooks.complete();
    } catch (ppstep::session_terminate const& e) {
        ;
    } catch (boost::wave::cpp_exception const& e) {
        std::cerr << e.what() << ": " << e.description() << std::endl;
    } catch (boost::wave::cpplexer::lexing_exception const& e) {
        std::cerr << e.what() << ": " << e.description() << std::endl;
    }

    return 0;
}