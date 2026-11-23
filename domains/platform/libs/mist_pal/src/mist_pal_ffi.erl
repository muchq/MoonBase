-module(mist_pal_ffi).

-export([
    address_to_string/1,
    getenv/1,
    monotonic_micros/0,
    parse_address/1,
    post_json/2,
    start_mist_clock/0,
    system_time_nanos/0
]).

getenv(Name) ->
    case os:getenv(binary_to_list(Name)) of
        false -> {error, nil};
        Value -> {ok, unicode:characters_to_binary(Value)}
    end.

%% A bare IPv4 or IPv6 address as its integer value. inet reads a v6 zone
%% ("%eth0") as part of the address, so the caller refuses '%' first.
parse_address(Text) ->
    case unicode:characters_to_list(Text) of
        Chars when is_list(Chars) ->
            case inet:parse_strict_address(Chars) of
                {ok, {A, B, C, D}} ->
                    {ok, {family4, (A bsl 24) bor (B bsl 16) bor (C bsl 8) bor D}};
                {ok, {_, _, _, _, _, _, _, _} = Groups} ->
                    {ok, {family6, lists:foldl(fun(G, Acc) -> (Acc bsl 16) bor G end, 0, tuple_to_list(Groups))}};
                {error, _} ->
                    {error, nil}
            end;
        _ ->
            {error, nil}
    end.

address_to_string({v4, Value}) ->
    <<A, B, C, D>> = <<Value:32>>,
    list_to_binary(inet:ntoa({A, B, C, D}));
address_to_string({v6, Value}) ->
    Groups = list_to_tuple([G || <<G:16>> <= <<Value:128>>]),
    list_to_binary(inet:ntoa(Groups)).

%% Plain HTTP through OTP's own client; the collector is on the app network.
post_json(Url, Body) ->
    case application:ensure_all_started(inets) of
        {ok, _} ->
            Request = {binary_to_list(Url), [], "application/json", Body},
            Options = [{timeout, 5000}, {connect_timeout, 5000}],
            case httpc:request(post, Request, Options, [{body_format, binary}]) of
                {ok, {{_, Status, _}, _, _}} -> {ok, Status};
                {error, Reason} -> {error, inspect(Reason)}
            end;
        {error, Reason} ->
            {error, inspect(Reason)}
    end.

inspect(Term) -> iolist_to_binary(io_lib:format("~0p", [Term])).

%% mist keeps its Date header in an ETS table that its OTP application's start
%% callback creates. A release that only runs the entry module starts no
%% applications, so start the clock here, once.
start_mist_clock() ->
    case ets:whereis(mist_clock) of
        undefined ->
            {ok, _} = 'mist@internal@clock':start(normal, []),
            nil;
        _ ->
            nil
    end.

system_time_nanos() -> erlang:system_time(nanosecond).

monotonic_micros() -> erlang:monotonic_time(microsecond).
