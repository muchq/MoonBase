package com.muchq.yochat;

import com.muchq.yochat.lib.YoServer;

public class App {

  public static void main(String[] args) throws Exception {
    YoServer.builder()
        .setChannelHandler(new ChatHandler())
        .setPort(Integer.parseInt(System.getenv("PORT")))
        .buildAndRun();
  }
}
