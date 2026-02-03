package com.muchq.yochat.lib;

import io.netty.bootstrap.ServerBootstrap;
import io.netty.channel.ChannelHandler;
import io.netty.channel.EventLoopGroup;
import io.netty.channel.nio.NioEventLoopGroup;
import io.netty.channel.socket.nio.NioServerSocketChannel;
import io.netty.handler.logging.LogLevel;
import io.netty.handler.logging.LoggingHandler;
import java.util.Objects;

public class YoServer {

  private final ChannelHandler channelHandler;
  private final int port;

  private YoServer(ChannelHandler channelHandler, int port) {
    this.channelHandler = channelHandler;
    this.port = port;
  }

  public void run() throws Exception {
    EventLoopGroup bossGroup = new NioEventLoopGroup(1);
    EventLoopGroup workerGroup = new NioEventLoopGroup();
    try {
      ServerBootstrap b = new ServerBootstrap();
      b.group(bossGroup, workerGroup)
          .channel(NioServerSocketChannel.class)
          .handler(new LoggingHandler(LogLevel.INFO))
          .childHandler(channelHandler);

      b.bind(port).sync().channel().closeFuture().sync();
    } finally {
      bossGroup.shutdownGracefully();
      workerGroup.shutdownGracefully();
    }
  }

  public static Builder builder() {
    return new Builder();
  }

  public static class Builder {

    private ChannelHandler channelHandler;
    private Integer port;

    public Builder setChannelHandler(ChannelHandler channelHandler) {
      this.channelHandler = new ChannelHandlerInitializer(channelHandler);
      return this;
    }

    public Builder setPort(Integer port) {
      this.port = port;
      return this;
    }

    public YoServer build() {
      Objects.requireNonNull(channelHandler, "channelHandler");
      Objects.requireNonNull(port, "port");
      return new YoServer(channelHandler, port);
    }

    public void buildAndRun() throws Exception {
      build().run();
    }
  }
}
