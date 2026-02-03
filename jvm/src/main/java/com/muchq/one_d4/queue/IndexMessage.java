package com.muchq.one_d4.queue;

import java.util.UUID;

public record IndexMessage(
    UUID requestId, String player, String platform, String startMonth, String endMonth) {}
