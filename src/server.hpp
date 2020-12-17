#ifndef PPSTEP_SERVER_HPP
#define PPSTEP_SERVER_HPP

#include <vector>

#include "server_fwd.hpp"
#include "client.hpp"

namespace ppstep {
    template <class ContainerT>
    struct server_state {
        server_state() : expanding(), rescanning() {}

        std::vector<ContainerT> expanding;
        std::vector<std::pair<ContainerT, ContainerT>> rescanning;
    };

    template <typename TokenT, typename ContainerT>
    struct server : boost::wave::context_policies::eat_whitespace<TokenT> {
        using base_type = boost::wave::context_policies::eat_whitespace<TokenT>;

        server(server_state<ContainerT>& state, client<TokenT, ContainerT>& sink, bool debug = false) : state(&state), sink(&sink), debug(debug), evaluating_conditional(false)  {}

        ~server() {}

        inline bool should_skip_token(TokenT const& token) {
            return IS_CATEGORY(token, boost::wave::WhiteSpaceTokenType)
                    || IS_CATEGORY(token, boost::wave::EOFTokenType)
                    || (boost::wave::token_id(token) == boost::wave::T_PLACEMARKER)
                    || !token.is_valid();
        }

        inline ContainerT sanitize(ContainerT const& tokens) {
            auto acc = ContainerT();
            for (auto const& token : tokens) {
                if (should_skip_token(token)) continue;
                acc.push_back(token);
            }
            return acc;
        }

        template <typename ContextT, typename IteratorT>
        bool expanding_function_like_macro(
                ContextT& ctx,
                TokenT const& macrodef, std::vector<TokenT> const& formal_args,
                ContainerT const& definition,
                TokenT const& macrocall, std::vector<ContainerT> const& arguments,
                IteratorT const& seqstart, IteratorT const& seqend) {
            if (evaluating_conditional) return false;

            auto sanitized_arguments = std::vector<ContainerT>();
            for (auto const& arg_container : arguments) {
                sanitized_arguments.push_back(sanitize(arg_container));
            }

            auto full_call = ContainerT(seqstart, seqend);
            {
                full_call.push_front(macrocall);
                full_call.push_back(*seqend);
                full_call = sanitize(full_call);
            }
            
            if (!debug) {
                sink->on_expand_function(ctx, macrodef, sanitized_arguments, full_call);
            } else {
                std::cout << "F: ";
                print_token_container(std::cout, full_call) << std::endl;
            }

            state->expanding.push_back(full_call);

            return false;
        }

        template <typename ContextT>
        bool expanding_object_like_macro(
                ContextT& ctx, TokenT const& macrodef,
                ContainerT const& definition, TokenT const& macrocall) {
            if (evaluating_conditional) return false;
            
            if (!debug) {
                sink->on_expand_object(ctx, macrocall);
            } else {
                std::cout << "O: ";
                print_token(std::cout, macrocall) << std::endl;
            }

            state->expanding.push_back({macrocall});
            return false;
        }

        template <typename ContextT>
        void expanded_macro(ContextT& ctx, ContainerT const& result) {
            if (evaluating_conditional) return;

            auto const& initial = *(state->expanding.rbegin());
            
            if (!debug) {
                 sink->on_expanded(ctx, sanitize(initial), sanitize(result));
            } else {
                std::cout << "E: ";
                print_token_container(std::cout, sanitize(initial)) << " -> ";
                print_token_container(std::cout, sanitize(result)) << std::endl;
            }

            state->rescanning.push_back({initial, result});

            state->expanding.pop_back();
        }

        template <typename ContextT>
        void rescanned_macro(ContextT& ctx, ContainerT const& result) {
            if (evaluating_conditional) return;

            auto const& [cause, initial] = *(state->rescanning.rbegin());

            if (!debug) {
                sink->on_rescanned(ctx, sanitize(cause), sanitize(initial), sanitize(result));
            } else {
                std::cout << "R: ";
                print_token_container(std::cout, sanitize(initial)) << " -> ";
                print_token_container(std::cout, sanitize(result)) << std::endl;
            }

            state->rescanning.pop_back();
        }
        
        template <typename ContextT>
        bool found_directive(ContextT const& ctx, TokenT const& directive) {
            auto directive_id = boost::wave::token_id(directive);
            switch (directive_id) {
                case boost::wave::T_PP_IF:
                case boost::wave::T_PP_ELIF:
                case boost::wave::T_PP_IFDEF:
                case boost::wave::T_PP_IFNDEF: {
                    evaluating_conditional = true;
                    break;
                }
                default:
                    break;
            }
            return false;
        }
        
        template <typename ContextT>
        bool evaluated_conditional_expression(ContextT const& ctx, TokenT const& directive, ContainerT const& expression, bool expression_value) {
            evaluating_conditional = false;

            return false;
        }
        
        template <typename ContextT, typename ParametersT, typename DefinitionT>
        void defined_macro(ContextT const& ctx, TokenT const& macro_name, bool is_functionlike, ParametersT const& parameters,
                           DefinitionT const& definition, bool is_predefined) {
            
        }
        
        template <typename ContextT>
        void undefined_macro(ContextT const& ctx, TokenT const& macro_name) {
            
        }

        template <typename ContextT>
        void lexed_token(ContextT& ctx, TokenT const& result) {
            if (should_skip_token(result)) return;

            if (!debug) {
                sink->on_lexed(ctx, result);
            } else {
                std::cout << "L: ";
                print_token(std::cout, result) << std::endl;
            }
        }
        
        template <typename ContextT, typename ExceptionT>
        void throw_exception(ContextT& ctx, ExceptionT const& e) {
            sink->on_exception(ctx, e);
            boost::throw_exception(e);
        }

        template <typename ContextT>
        void start(ContextT& ctx) {
            if (debug) return;

            sink->on_start(ctx);
        }

        template <typename ContextT>
        void complete(ContextT& ctx) {
            if (debug) return;

            sink->on_complete(ctx);
        }

        server_state<ContainerT>* state;
        client<TokenT, ContainerT>* sink;
        bool debug;

        unsigned int conditional_nesting;
        bool evaluating_conditional;
    };
}

#endif // PPSTEP_SERVER_HPP