package com.muchq.indexer.queue;

import java.util.UUID;

public record IndexMessage(UUID requestId, String player, String platform, String startMonth, String endMonth) {
}
