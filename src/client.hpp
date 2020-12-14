#ifndef PPSTEP_CLIENT_HPP
#define PPSTEP_CLIENT_HPP

#include <vector>
#include <stack>
#include <tuple>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <functional>

#include "client_fwd.hpp"
#include "view.hpp"
#include "utils.hpp"

namespace ppstep {
    template <class TokenT, class ContainerT>
    struct client {
        client() : cli(client_cli<TokenT, ContainerT>(*this)), mode(stepping_mode::FREE) {}

        void on_lexed(TokenT const& token) {
            if (token_stack.empty()) {
                auto last_tokens = token_history.empty() ? ContainerT() : last_history();
                last_tokens.push_back(token);

                lexed_tokens.push_back(token);
                token_history.push_back(last_tokens);

                handle_prompt(token, break_condition::LEXED);

            } else {
                auto const& last_tokens = last_history();

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

        void on_expand_function(TokenT const& call, std::vector<ContainerT> const& arguments, ContainerT call_tokens) {
            if (token_stack.empty()) {
                push(std::move(call_tokens));
            }

            handle_prompt(call, break_condition::CALL);
        }

        void on_expand_object(TokenT const& call) {
            if (token_stack.empty()) {
                push({call});
            }

            handle_prompt(call, break_condition::CALL);
        }

        void on_expanded(ContainerT const& initial, ContainerT const& result) {
            auto const& [tokens, start, end] = match(initial);

            auto&& [new_tokens, new_start] = splice_between(*tokens, result, start, end);

            push(std::move(new_tokens), std::move(new_start));

            handle_prompt(*(initial.begin()), break_condition::EXPANDED);
        }

        void on_rescanned(ContainerT const& cause, ContainerT const& initial, ContainerT const& result) {
            if (initial.empty()) return;

            auto const& [tokens, start, end] = match(initial);

            auto&& [new_tokens, new_start] = splice_between(*tokens, result, start, end);

            push(std::move(new_tokens), std::move(new_start));

            handle_prompt(*(initial.begin()), break_condition::RESCANNED);
        }

        void on_complete() {
            std::cout << "Expansion complete." << std::endl;
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

        void set_mode(stepping_mode m) {
            mode = m;
        }

        ContainerT const& last_history() {
            return *(token_history.rbegin());
        }

    private:
        using container_iterator = typename ContainerT::const_iterator;

        void push(ContainerT&& tokens) {
            push(std::move(tokens), std::begin(tokens));
        }

        void push(ContainerT&& tokens, container_iterator&& head) {
            auto historical_tokens = ContainerT(std::begin(lexed_tokens), std::end(lexed_tokens));
            historical_tokens.insert(std::end(historical_tokens), std::begin(tokens), std::end(tokens));
            token_history.push_back(historical_tokens);

            token_stack.emplace(std::move(tokens), std::move(head));
        }

        std::tuple<ContainerT const*, container_iterator, container_iterator> match(ContainerT const& pattern) {
            while (!token_stack.empty()) {
                auto const& [tokens, head] = token_stack.top();

                auto sublist = find_sublist(tokens, pattern, head);

                if (sublist) {
                    auto [start, end] = *sublist;

                    return std::make_tuple(&tokens, start, end);
                } else {
                    token_stack.pop();
                }
            }
            throw std::logic_error("could not find pattern in token stack");
        }

        std::pair<ContainerT, container_iterator> splice_between(ContainerT const& tokens, ContainerT const& result, container_iterator start, container_iterator end) {
            auto new_tokens = ContainerT();

            new_tokens.insert(new_tokens.end(), tokens.begin(), start);
            auto index = new_tokens.size();

            new_tokens.insert(new_tokens.end(), result.begin(), result.end());
            new_tokens.insert(new_tokens.end(), end, tokens.end());

            return std::pair<ContainerT, container_iterator>(std::move(new_tokens), std::next(new_tokens.begin(), index));
        }

        void reset_token_stack() {
            while (!token_stack.empty()) token_stack.pop();
        }

        void handle_prompt(TokenT const& token, break_condition type) {
            switch (mode) {
                case stepping_mode::FREE: {
                    cli.prompt();
                    break;
                }
                case stepping_mode::UNTIL_BREAK: {
                    switch (type) {
                        case break_condition::CALL: {
                            if (expansion_breakpoints.find(token.get_value()) != expansion_breakpoints.end()) {
                                cli.prompt();
                            }
                            break;
                        }
                        case break_condition::EXPANDED: {
                            if (expanded_breakpoints.find(token.get_value()) != expanded_breakpoints.end()) {
                                cli.prompt();
                            }
                            break;
                        }
                    }
                    break;
                }
            }
        }

        client_cli<TokenT, ContainerT> cli;
        std::set<typename TokenT::string_type> expansion_breakpoints;
        std::set<typename TokenT::string_type> expanded_breakpoints;
        stepping_mode mode;

        std::stack<std::pair<ContainerT, container_iterator>> token_stack;
        std::vector<ContainerT> token_history;
        std::vector<TokenT> lexed_tokens;
        std::vector<TokenT> lex_buffer;
    };
}

#endif // PPSTEP_CLIENT_HPP