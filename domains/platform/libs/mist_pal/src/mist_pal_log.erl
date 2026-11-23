%% A logger formatter for one flat JSON object per line, and the entry point
%% mist_pal/log writes through.
-module(mist_pal_log).

-export([configure/0, format/2, log/2]).

%% Logs at info and up, every line through this formatter, OTP's progress
%% reports aside. Once per VM.
configure() ->
    case persistent_term:get({?MODULE, configured}, false) of
        true ->
            nil;
        false ->
            ok = logger:set_primary_config(level, info),
            %% At info, OTP would also report every supervised process it
            %% starts.
            _ = logger:add_primary_filter(mist_pal_no_progress,
                                          {fun logger_filters:progress/2, stop}),
            _ = logger:update_handler_config(default, formatter, {?MODULE, #{}}),
            persistent_term:put({?MODULE, configured}, true),
            nil
    end.

%% Members is the JSON object's members, encoded, without the braces.
log(Level, Members) ->
    configure(),
    logger:log(Level, #{?MODULE => Members}),
    nil.

format(#{level := Level, msg := Msg, meta := Meta}, _Config) ->
    Time = maps:get(time, Meta, logger:timestamp()),
    Timestamp = calendar:system_time_to_rfc3339(Time, [{unit, microsecond}, {offset, "Z"}]),
    Members =
        case Msg of
            {report, #{?MODULE := Encoded}} -> Encoded;
            _ -> ["\"message\":", json:encode(text(Msg, Meta))]
        end,
    ["{\"timestamp\":\"", Timestamp, "\",\"level\":\"", label(Level), "\",", Members, "}\n"].

label(Level) when Level =:= emergency; Level =:= alert; Level =:= critical; Level =:= error ->
    "ERROR";
label(warning) -> "WARN";
label(debug) -> "DEBUG";
label(_) -> "INFO".

%% A message from mist or OTP, rendered as text.
text(Msg, Meta) ->
    Chars =
        try
            case Msg of
                {string, String} ->
                    String;
                {report, Report} ->
                    case maps:get(report_cb, Meta, undefined) of
                        Cb when is_function(Cb, 1) ->
                            {Format, Args} = Cb(Report),
                            io_lib:format(Format, Args);
                        Cb when is_function(Cb, 2) ->
                            Cb(Report, #{depth => unlimited, chars_limit => unlimited,
                                         single_line => true});
                        _ ->
                            io_lib:format("~0p", [Report])
                    end;
                {Format, Args} ->
                    io_lib:format(Format, Args)
            end
        catch
            _:_ -> io_lib:format("~0p", [Msg])
        end,
    case unicode:characters_to_binary(Chars) of
        Binary when is_binary(Binary) -> string:trim(Binary, trailing, "\n");
        _ -> iolist_to_binary(io_lib:format("~0p", [Msg]))
    end.
