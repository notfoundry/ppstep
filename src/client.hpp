#ifndef PPSTEP_CLIENT_HPP
#define PPSTEP_CLIENT_HPP

#include <vector>
#include <stack>
#include <optional>
#include <variant>
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
    namespace ansi {
        constexpr auto black_fg = "\u001b[30m";
        constexpr auto white_fg = "\u001b[37;1m";

        constexpr auto yellow_bg = "\u001b[43m";
        constexpr auto blue_bg = "\u001b[44;1m";
        constexpr auto white_bg = "\u001b[47m";

        constexpr auto reset = "\u001b[0m";
    }

    namespace events {
        template <class ContainerT, class DerivedT>
        struct formatting_event {
            formatting_event(std::size_t start, std::size_t end) : start(start), end(end) {}

            void print(std::ostream& os, ContainerT const& tokens) const {
                auto sub_start = std::next(tokens.begin(), start);
                auto sub_end = std::next(tokens.begin(), end);

                auto it = tokens.begin();
                auto end = tokens.end();
                
                print_token_range(os, it, sub_start);
                if (it != tokens.begin())
                    os << ' ';

                os << ansi::white_bg << ansi::black_fg;
                static_cast<DerivedT const*>(this)->format(os);
                if (sub_start != sub_end) {
                    print_token_range(os, sub_start, sub_end) << ansi::reset;
                } else {
                    os << ' ' << ansi::reset;
                }
                if (sub_end != end)
                    os << ' ';

                print_token_range(os, sub_end, tokens.end()) << std::endl;
            }
            
            std::size_t start, end;
        };

        template <class ContainerT>
        struct call : formatting_event<ContainerT, call<ContainerT>> {
            call(std::size_t start, std::size_t end) : formatting_event<ContainerT, call<ContainerT>>(start, end) {}

            void format(std::ostream& os) const {
                os << ansi::white_bg << ansi::black_fg;
            }
        };
        
        template <class ContainerT>
        struct expanded : formatting_event<ContainerT, expanded<ContainerT>> {
            expanded(std::size_t start, std::size_t end) : formatting_event<ContainerT, expanded<ContainerT>>(start, end) {}

            void format(std::ostream& os) const {
                os << ansi::yellow_bg << ansi::black_fg;
            }
        };
        
        template <class ContainerT>
        struct rescanned : formatting_event<ContainerT, rescanned<ContainerT>> {
            rescanned(std::size_t start, std::size_t end) : formatting_event<ContainerT, rescanned<ContainerT>>(start, end) {}

            void format(std::ostream& os) const {
                os << ansi::blue_bg << ansi::white_fg;
            }
        };
        
        template <class ContainerT>
        struct lexed {
            void print(std::ostream& os, ContainerT const& tokens) const {
                print_token_container(std::cout, tokens) << std::endl;
            }
        };
    }
    
    template <class ContainerT>
    using preprocessing_event =
        std::variant<
            events::call<ContainerT>,
            events::expanded<ContainerT>,
            events::rescanned<ContainerT>,
            events::lexed<ContainerT>>;
    
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
    
    template <class ContainerT>
    struct historical_event {
        historical_event(ContainerT tokens, preprocessing_event<ContainerT>&& event) : tokens(std::move(tokens)), event(std::move(event)) {}

        ContainerT tokens;
        preprocessing_event<ContainerT> event;
    };
    
    template <class TokenT, class ContainerT>
    struct client {
        client(server_state<ContainerT>& state, std::string prefix) : state(&state), cli(client_cli<TokenT, ContainerT>(*this, std::move(prefix))), mode(stepping_mode::FREE) {}
        
        client(server_state<ContainerT>& state) : client(state, "") {}

        template <class ContextT>
        void on_lexed(ContextT& ctx, TokenT const& token) {
            if (token_stack.empty()) {
                auto last_tokens = token_history.empty() ? ContainerT() : newest_history()->tokens;
                last_tokens.push_back(token);

                lexed_tokens.push_back(token);
                token_history.push_back(historical_event<ContainerT>(last_tokens, events::lexed<ContainerT>()));

                handle_prompt(ctx, token, preprocessing_event_type::LEXED);

            } else {
                auto const& last_tokens = newest_history()->tokens;

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
            if (token_stack.empty()) {
                push(std::move(call_tokens), events::call<ContainerT>(lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
            } else {
                auto lookup = find_match_indices(token_stack.back(), call_tokens);
                if (lookup) {
                    auto [start, end] = *lookup;
                    token_history.push_back(historical_event<ContainerT>(
                        prepend_lexed(token_stack.back().tokens),
                        events::call<ContainerT>(lexed_tokens.size() + start, lexed_tokens.size() + end)));
                } else {
                    reset_token_stack();
                    push(std::move(call_tokens), events::call<ContainerT>(lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
                }
            }
            
            handle_prompt(ctx, call, preprocessing_event_type::CALL);
        }

        template <class ContextT>
        void on_expand_object(ContextT& ctx, TokenT const& call) {
            auto call_tokens = ContainerT{call};
            
            if (token_stack.empty()) {
                push(std::move(call_tokens), events::call<ContainerT>(lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
            } else {
                auto lookup = find_match_indices(token_stack.back(), call_tokens);
                if (lookup) {
                    auto [start, end] = *lookup;
                    token_history.push_back(historical_event<ContainerT>(
                        prepend_lexed(token_stack.back().tokens),
                        events::call<ContainerT>(lexed_tokens.size() + start, lexed_tokens.size() + end)));
                } else {
                    reset_token_stack();
                    push(std::move(call_tokens), events::call<ContainerT>(lexed_tokens.size() + 0, lexed_tokens.size() + call_tokens.size()));
                }
            }

            handle_prompt(ctx, call, preprocessing_event_type::CALL);
        }

        template <class ContextT>
        void on_expanded(ContextT& ctx, ContainerT const& initial, ContainerT const& result) {
            try {
                auto const& [tokens, start, end] = match(initial);

                ContainerT new_tokens;
                std::size_t new_start, new_end;
                splice_between(*tokens, result, start, end, new_tokens, new_start, new_end);

                push(std::move(new_tokens),
                     std::next(new_tokens.begin(), new_start),
                     events::expanded<ContainerT>(lexed_tokens.size() + new_start, lexed_tokens.size() + new_end));

            } catch (std::logic_error const&) {
                push(ContainerT(result), events::expanded<ContainerT>(lexed_tokens.size() + 0, lexed_tokens.size() + result.size()));
            }

            handle_prompt(ctx, *(initial.begin()), preprocessing_event_type::EXPANDED);
        }

        template <class ContextT>
        void on_rescanned(ContextT& ctx, ContainerT const& cause, ContainerT const& initial, ContainerT const& result) {
            if (initial.empty()) return;

            try {
                auto const& [tokens, start, end] = match(initial);

                ContainerT new_tokens;
                std::size_t new_start, new_end;
                splice_between(*tokens, result, start, end, new_tokens, new_start, new_end);
                
                push(std::move(new_tokens),
                     std::next(new_tokens.begin(), new_start),
                     events::rescanned<ContainerT>(lexed_tokens.size() + new_start, lexed_tokens.size() + new_end));

            } catch (std::logic_error const&) {
                push(ContainerT(result), events::rescanned<ContainerT>(lexed_tokens.size() + 0, lexed_tokens.size() + result.size()));
            }

            handle_prompt(ctx, *(initial.begin()), preprocessing_event_type::RESCANNED);
        }
        
        template <typename ContextT, typename ExceptionT>
        void on_exception(ContextT& ctx, ExceptionT const& e) {
            std::cout << e.what() << ": " << e.description() << std::endl;
            cli.prompt(ctx, "exception");
        }

        template <class ContextT>
        void on_complete(ContextT& ctx) {
            std::cout << "Preprocessing complete." << std::endl;
            cli.prompt(ctx, "complete");
        }
        
        template <class ContextT>
        void on_start(ContextT& ctx) {
            std::cout << "Preprocessing " << ctx.get_main_pos() << '.' << std::endl;
            cli.prompt(ctx, "started", false);
        }

        void add_breakpoint(typename TokenT::string_type const& macro, preprocessing_event_type cond) {
            switch (cond) {
                case preprocessing_event_type::CALL: {
                    expansion_breakpoints.insert(macro);
                    break;
                }
                case preprocessing_event_type::EXPANDED: {
                    expanded_breakpoints.insert(macro);
                    break;
                }
            }
        }

        void remove_breakpoint(typename TokenT::string_type const& macro, preprocessing_event_type cond) {
            switch (cond) {
                case preprocessing_event_type::CALL: {
                    expansion_breakpoints.erase(macro);
                    break;
                }
                case preprocessing_event_type::EXPANDED: {
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

        ContainerT prepend_lexed(ContainerT const& tokens) {
            auto acc = ContainerT(std::begin(lexed_tokens), std::end(lexed_tokens));
            acc.insert(std::end(acc), std::begin(tokens), std::end(tokens));
            return acc;
        }

        void push(ContainerT&& tokens, preprocessing_event<ContainerT>&& event) {
            push(std::move(tokens), std::begin(tokens), std::move(event));
        }

        void push(ContainerT&& tokens, container_iterator&& head, preprocessing_event<ContainerT>&& event) {
            auto historical_tokens = prepend_lexed(tokens);
            token_history.push_back(historical_event<ContainerT>(historical_tokens, std::move(event)));

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
        
        std::optional<std::pair<std::size_t, std::size_t>> find_match_indices(offset_container<ContainerT> const& oc, ContainerT const& pattern) {
            auto sublist = oc.find_pattern(pattern);
            if (sublist) {
                auto [start, end] = *sublist;

                auto begin_to_start = std::distance(oc.tokens.begin(), start);
                auto begin_to_end = begin_to_start + std::distance(start, end);
                return {{begin_to_start, begin_to_end}};
            } else {
                return {};
            }
        }

        void splice_between(ContainerT const& tokens, ContainerT const& result, container_iterator start, container_iterator end,
                                                       ContainerT& new_tokens, std::size_t& new_start, std::size_t& new_end) {
            new_tokens.insert(new_tokens.end(), tokens.begin(), start);
            new_start = new_tokens.size();

            new_tokens.insert(new_tokens.end(), result.begin(), result.end());
            new_end = new_tokens.size();

            new_tokens.insert(new_tokens.end(), end, tokens.end());
        }

        void reset_token_stack() {
            token_stack.clear();
        }
        
        char const* get_preprocessing_event_type_name(preprocessing_event_type type) {
            switch (type) {
                case preprocessing_event_type::CALL: return "call";
                case preprocessing_event_type::EXPANDED: return "expanded";
                case preprocessing_event_type::RESCANNED: return "rescanned";
                case preprocessing_event_type::LEXED: return "lexed";
                default: return "";
            }
        }

        template <class ContextT>
        void handle_prompt(ContextT& ctx, TokenT const& token, preprocessing_event_type type) {
            bool do_prompt = false;

            switch (mode) {
                case stepping_mode::FREE: {
                    do_prompt = true;
                    break;
                }
                case stepping_mode::UNTIL_BREAK: {
                    switch (type) {
                        case preprocessing_event_type::CALL: {
                            if (expansion_breakpoints.find(token.get_value()) != expansion_breakpoints.end()) {
                                do_prompt = true;
                            }
                            break;
                        }
                        case preprocessing_event_type::EXPANDED: {
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
                cli.prompt(ctx, get_preprocessing_event_type_name(type));
            }
        }

        server_state<ContainerT>* state;
        client_cli<TokenT, ContainerT> cli;
        std::set<typename TokenT::string_type> expansion_breakpoints;
        std::set<typename TokenT::string_type> expanded_breakpoints;
        stepping_mode mode;

        std::list<offset_container<ContainerT>> token_stack;
        std::vector<historical_event<ContainerT>> token_history;
        std::vector<TokenT> lexed_tokens;
        std::vector<TokenT> lex_buffer;
    };
}

#endif // PPSTEP_CLIENT_HPP