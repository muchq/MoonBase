-module(mithril_v2_ffi).

-export([
    array_from_list/1,
    array_get/2,
    only_letters/1,
    read_bits/1,
    read_text/1,
    store_graph/1,
    stored_graph/0
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

%% Letters and letter numbers, the nearest Unicode classes to Rust's
%% char::is_alphabetic that PCRE names. \z, because $ also matches before a
%% final newline.
only_letters(Word) ->
    re:run(Word, <<"^[\\p{L}\\p{Nl}]+\\z">>, [unicode, ucp, {capture, none}]) =:= match.

store_graph(Graph) ->
    persistent_term:put({mithril_v2, graph}, Graph),
    nil.

stored_graph() -> persistent_term:get({mithril_v2, graph}).

describe(Path, Reason) ->
    iolist_to_binary([Path, ": ", file:format_error(Reason)]).
