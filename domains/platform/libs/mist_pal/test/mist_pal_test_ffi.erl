-module(mist_pal_test_ffi).

-export([mist_clock_running/0, request/4, with_byte_255/1]).

%% An HTTP request through OTP's client: the status and the response headers,
%% names lowercased.
request(Method, Url, Headers, Body) ->
    {ok, _} = application:ensure_all_started(inets),
    Hs = [{binary_to_list(K), binary_to_list(V)} || {K, V} <- Headers],
    Request =
        case Method of
            get -> {binary_to_list(Url), Hs};
            post -> {binary_to_list(Url), Hs, "application/json", Body}
        end,
    case httpc:request(Method, Request, [{timeout, 5000}], [{body_format, binary}]) of
        {ok, {{_, Status, _}, RespHeaders, RespBody}} ->
            {ok, {Status, [{list_to_binary(K), list_to_binary(V)} || {K, V} <- RespHeaders], RespBody}};
        {error, Reason} ->
            {error, iolist_to_binary(io_lib:format("~0p", [Reason]))}
    end.

%% Text followed by a byte no UTF-8 string contains.
with_byte_255(Text) -> <<Text/binary, 255>>.

%% Whether mist's Date-header cache exists. Its OTP application creates it
%% on start, and without it every response logs a warning.
mist_clock_running() -> ets:whereis(mist_clock) =/= undefined.
