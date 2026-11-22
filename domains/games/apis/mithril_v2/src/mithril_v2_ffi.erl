-module(mithril_v2_ffi).

-export([
    array_from_list/1,
    array_get/2,
    getenv/1,
    monotonic_micros/0,
    only_letters/1,
    post_json/2,
    read_bits/1,
    read_text/1,
    store_graph/1,
    stored_graph/0,
    system_time_nanos/0
]).

array_from_list(Items) -> list_to_tuple(Items).

array_get(Array, Index) -> element(Index + 1, Array).

read_text(Path) ->
    case read_bits(Path) of
        {ok, Bytes} ->
            case unicode:characters_to_binary(Bytes) of
                Text when is_binary(Text) -> {ok, Text};
                _ -> {error, <<Path/binary, " is not UTF-8">>}
            end;
        Error ->
            Error
    end.

read_bits(Path) ->
    case file:read_file(Path) of
        {ok, Bytes} -> {ok, Bytes};
        {error, Reason} -> {error, describe(Path, Reason)}
    end.

getenv(Name) ->
    case os:getenv(binary_to_list(Name)) of
        false -> {error, nil};
        Value -> {ok, unicode:characters_to_binary(Value)}
    end.

%% Letters and letter numbers, the nearest Unicode classes to Rust's
%% char::is_alphabetic that PCRE names. \z, because $ also matches before a
%% final newline.
only_letters(Word) ->
    re:run(Word, <<"^[\\p{L}\\p{Nl}]+\\z">>, [unicode, ucp, {capture, none}]) =:= match.

%% Plain HTTP through OTP's own client; the collector is on the app network.
post_json(Url, Body) ->
    {ok, _} = application:ensure_all_started(inets),
    Request = {binary_to_list(Url), [], "application/json", Body},
    Options = [{timeout, 5000}, {connect_timeout, 5000}],
    case httpc:request(post, Request, Options, [{body_format, binary}]) of
        {ok, {{_, Status, _}, _, _}} -> {ok, Status};
        {error, Reason} -> {error, iolist_to_binary(io_lib:format("~0p", [Reason]))}
    end.

store_graph(Graph) ->
    persistent_term:put({mithril_v2, graph}, Graph),
    nil.

stored_graph() -> persistent_term:get({mithril_v2, graph}).

system_time_nanos() -> erlang:system_time(nanosecond).

monotonic_micros() -> erlang:monotonic_time(microsecond).

describe(Path, Reason) ->
    iolist_to_binary([Path, ": ", file:format_error(Reason)]).
