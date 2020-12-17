#ifndef PPSTEP_CLIENT_HPP
#define PPSTEP_CLIENT_HPP

#include <vector>
#include <stack>
#include <optional>
#include <tuple>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <functional>

#include "server_fwd.hpp"
#include "client_fwd.hpp"
#include "view.hpp"
#include "utils.hpp"

namespace ppstep {
    
    template <class ContainerT>
    struct offset_container {
        using iterator = typename ContainerT::const_iterator;
        
        offset_container(ContainerT&& tokens, iterator&& start) : tokens(std::move(tokens)), start(std::move(start)) {}
        
        offset_container(ContainerT&& tokens) : tokens(std::move(tokens)), start(this->tokens.end()) {}
        
        offset_container(offset_container<ContainerT> const&) = delete;
        
        std::optional<std::pair<iterator, iterator>> find_pattern(ContainerT const& pattern) const {
            return find_sublist(tokens, pattern, start);
        }
        
        ContainerT tokens;
        iterator start;
    };
    
    template <class TokenT, class ContainerT>
    struct client {
        client(server_state<ContainerT>& state, std::string prefix) : state(&state), cli(client_cli<TokenT, ContainerT>(*this, std::move(prefix))), mode(stepping_mode::FREE) {}
        
        client(server_state<ContainerT>& state) : client(state, "") {}

        template <class ContextT>
        void on_lexed(ContextT& ctx, TokenT const& token) {
            if (token_stack.empty()) {
                auto last_tokens = token_history.empty() ? ContainerT() : *newest_history();
                last_tokens.push_back(token);

                lexed_tokens.push_back(token);
                token_history.push_back(last_tokens);

                handle_prompt(ctx, token, break_condition::LEXED);

            } else {
                auto const& last_tokens = *newest_history();

                lex_buffer.push_back(token);
                if (std::equal(std::next(std::begin(last_tokens), lexed_tokens.size()), std::end(last_tokens),
                               std::begin(lex_buffer), std::end(lex_buffer),
                               [](auto const& a, auto const& b) { return a.get_value() == b.get_value(); })) {
                    lexed_tokens.insert(std::end(lexed_tokens), std::begin(lex_buffer), std::end(lex_buffer));
                    lex_buffer.clear();
                    reset_token_stack();
                }
            }
        }

        template <class ContextT>
        void on_expand_function(ContextT& ctx, TokenT const& call, std::vector<ContainerT> const& arguments, ContainerT call_tokens) {
            if (token_stack.empty() || !can_match(call_tokens)) {
                push(std::move(call_tokens));
            }

            handle_prompt(ctx, call, break_condition::CALL);
        }

        template <class ContextT>
        void on_expand_object(ContextT& ctx, TokenT const& call) {
            auto call_tokens = ContainerT{call};
            if (token_stack.empty() || !can_match(call_tokens)) {
                push(std::move(call_tokens));
            }

            handle_prompt(ctx, call, break_condition::CALL);
        }

        template <class ContextT>
        void on_expanded(ContextT& ctx, ContainerT const& initial, ContainerT const& result) {
            try {
                auto const& [tokens, start, end] = match(initial);

                ContainerT new_tokens;
                typename ContainerT::const_iterator new_start;
                splice_between(*tokens, result, start, end, new_tokens, new_start);

                push(std::move(new_tokens), std::move(new_start));
            } catch (std::logic_error const&) {
                push(ContainerT(result));
            }

            handle_prompt(ctx, *(initial.begin()), break_condition::EXPANDED);
        }

        template <class ContextT>
        void on_rescanned(ContextT& ctx, ContainerT const& cause, ContainerT const& initial, ContainerT const& result) {
            if (initial.empty()) return;

            try {
                auto const& [tokens, start, end] = match(initial);

                ContainerT new_tokens;
                typename ContainerT::const_iterator new_start;
                splice_between(*tokens, result, start, end, new_tokens, new_start);
                
                push(std::move(new_tokens), std::move(new_start));
            } catch (std::logic_error const&) {
                push(ContainerT(result));
            }

            handle_prompt(ctx, *(initial.begin()), break_condition::RESCANNED);
        }
        
        template <typename ContextT, typename ExceptionT>
        void on_exception(ContextT& ctx, ExceptionT const& e) {
            std::cout << e.what() << ": " << e.description() << std::endl;
            cli.prompt(ctx);
        }

        template <class ContextT>
        void on_complete(ContextT& ctx) {
            std::cout << "Preprocessing complete." << std::endl;
            cli.prompt(ctx);
        }
        
        template <class ContextT>
        void on_start(ContextT& ctx) {
            std::cout << "Preprocessing " << ctx.get_main_pos() << '.' << std::endl;
            cli.prompt(ctx, false);
        }

        void add_breakpoint(typename TokenT::string_type const& macro, break_condition cond) {
            switch (cond) {
                case break_condition::CALL: {
                    expansion_breakpoints.insert(macro);
                    break;
                }
                case break_condition::EXPANDED: {
                    expanded_breakpoints.insert(macro);
                    break;
                }
            }
        }

        void remove_breakpoint(typename TokenT::string_type const& macro, break_condition cond) {
            switch (cond) {
                case break_condition::CALL: {
                    expansion_breakpoints.erase(macro);
                    break;
                }
                case break_condition::EXPANDED: {
                    expanded_breakpoints.erase(macro);
                    break;
                }
            }
        }
        
        server_state<ContainerT> const& get_state() {
            return *state;
        }

        void set_mode(stepping_mode m) {
            mode = m;
        }

        auto newest_history() {
            return token_history.rbegin();
        }
        
        auto oldest_history() {
            return token_history.rend();
        }

    private:
        using container_iterator = typename ContainerT::const_iterator;
        
        using range_container = std::tuple<ContainerT const*, container_iterator, container_iterator>;

        void push(ContainerT&& tokens) {
            push(std::move(tokens), std::begin(tokens));
        }

        void push(ContainerT&& tokens, container_iterator&& head) {
            auto historical_tokens = ContainerT(std::begin(lexed_tokens), std::end(lexed_tokens));
            historical_tokens.insert(std::end(historical_tokens), std::begin(tokens), std::end(tokens));
            token_history.push_back(historical_tokens);

            if (head != tokens.end()) {
                token_stack.emplace_back(std::move(tokens), std::move(head));
            } else {
                token_stack.emplace_back(std::move(tokens));
            }
        }

        range_container match(ContainerT const& pattern) {
            while (!token_stack.empty()) {
                auto const& top = token_stack.back();

                auto sublist = top.find_pattern(pattern);

                if (sublist) {
                    auto [start, end] = *sublist;

                    return std::make_tuple(&(top.tokens), start, end);
                } else {
                    token_stack.pop_back();
                }
            }
            
            std::stringstream ss;
            print_token_container(ss, pattern);
            throw std::logic_error("could not find pattern \"" + ss.str() + "\" in token stack");
        }
        
        bool can_match(ContainerT const& pattern) {
            return std::any_of(token_stack.rbegin(), token_stack.rend(), [&pattern](auto const& c){ return c.find_pattern(pattern).has_value(); });
        }

        void splice_between(ContainerT const& tokens, ContainerT const& result, container_iterator start, container_iterator end,
                                                       ContainerT& new_tokens, container_iterator& new_start) {
            new_tokens.insert(new_tokens.end(), tokens.begin(), start);
            auto index = new_tokens.size();

            new_tokens.insert(new_tokens.end(), result.begin(), result.end());
            new_tokens.insert(new_tokens.end(), end, tokens.end());
            
            new_start = new_tokens.begin();
            std::advance(new_start, index);
        }

        void reset_token_stack() {
            token_stack.clear();
        }

        template <class ContextT>
        void handle_prompt(ContextT& ctx, TokenT const& token, break_condition type) {
            bool do_prompt = false;

            switch (mode) {
                case stepping_mode::FREE: {
                    do_prompt = true;
                    break;
                }
                case stepping_mode::UNTIL_BREAK: {
                    switch (type) {
                        case break_condition::CALL: {
                            if (expansion_breakpoints.find(token.get_value()) != expansion_breakpoints.end()) {
                                do_prompt = true;
                            }
                            break;
                        }
                        case break_condition::EXPANDED: {
                            if (expanded_breakpoints.find(token.get_value()) != expanded_breakpoints.end()) {
                                do_prompt = true;
                            }
                            break;
                        }
                    }
                    break;
                }
            }

            if (do_prompt) {
                cli.prompt(ctx);
            }
        }

        server_state<ContainerT>* state;
        client_cli<TokenT, ContainerT> cli;
        std::set<typename TokenT::string_type> expansion_breakpoints;
        std::set<typename TokenT::string_type> expanded_breakpoints;
        stepping_mode mode;

        std::list<offset_container<ContainerT>> token_stack;
        std::vector<ContainerT> token_history;
        std::vector<TokenT> lexed_tokens;
        std::vector<TokenT> lex_buffer;
    };
}

#endif // PPSTEP_CLIENT_HPP