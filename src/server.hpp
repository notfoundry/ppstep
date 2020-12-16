#ifndef PPSTEP_SERVER_HPP
#define PPSTEP_SERVER_HPP

#include <stack>
#include <vector>

#include "server_fwd.hpp"
#include "client.hpp"

namespace ppstep {
    template <typename TokenT, typename ContainerT>
    struct server : boost::wave::context_policies::eat_whitespace<TokenT> {
        using base_type = boost::wave::context_policies::eat_whitespace<TokenT>;

        server(ppstep::client<TokenT, ContainerT>& sink) : sink(&sink), expanding(), rescanning(), conditional_nesting(0), evaluating_conditional(false)  {}

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

            sink->on_expand_function(ctx, macrodef, sanitized_arguments, full_call);

            expanding.push(full_call);

            return false;
        }

        template <typename ContextT>
        bool expanding_object_like_macro(
                ContextT& ctx, TokenT const& macrodef,
                ContainerT const& definition, TokenT const& macrocall) {
            if (evaluating_conditional) return false;
            
            sink->on_expand_object(ctx, macrocall);

            expanding.push({macrocall});
            return false;
        }

        template <typename ContextT>
        void expanded_macro(ContextT& ctx, ContainerT const& result) {
            if (evaluating_conditional) return;

            auto const& initial = expanding.top();
            sink->on_expanded(ctx, sanitize(initial), sanitize(result));

            rescanning.push({initial, result});

            expanding.pop();
        }

        template <typename ContextT>
        void rescanned_macro(ContextT& ctx, ContainerT const& result) {
            if (evaluating_conditional) return;

            auto const& [cause, initial] = rescanning.top();
            sink->on_rescanned(ctx, sanitize(cause), sanitize(initial), sanitize(result));

            rescanning.pop();
        }
        
        template <typename ContextT>
        bool found_directive(ContextT const& ctx, TokenT const& directive) {
            auto directive_id = boost::wave::token_id(directive);
            switch (directive_id) {
                case boost::wave::T_PP_IF:
                case boost::wave::T_PP_ELIF:
                case boost::wave::T_PP_IFDEF:
                case boost::wave::T_PP_IFNDEF: {
                    ++conditional_nesting;
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
            --conditional_nesting;
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

            sink->on_lexed(ctx, result);
        }
        
        template <typename ContextT>
        void start(ContextT& ctx) {
            sink->on_start(ctx);
        }

        template <typename ContextT>
        void complete(ContextT& ctx) {
            sink->on_complete(ctx);
        }

        ppstep::client<TokenT, ContainerT>* sink;
        std::stack<ContainerT> expanding;
        std::stack<std::pair<ContainerT, ContainerT>> rescanning;
        unsigned int conditional_nesting;
        bool evaluating_conditional;
    };
}

#endif // PPSTEP_SERVER_HPP