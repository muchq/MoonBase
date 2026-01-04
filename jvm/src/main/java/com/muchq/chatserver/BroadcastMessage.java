package com.muchq.chatserver;

public record BroadcastMessage(String username, String text, int userCount) {}
