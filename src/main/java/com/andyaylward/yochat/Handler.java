package com.andyaylward.yochat;

import io.netty.channel.Channel;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.SimpleChannelInboundHandler;
import io.netty.channel.group.ChannelGroup;
import io.netty.channel.group.DefaultChannelGroup;
import io.netty.handler.ssl.SslHandler;
import io.netty.util.concurrent.GlobalEventExecutor;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.HashMap;
import java.util.Map;

public class Handler extends SimpleChannelInboundHandler<String> {
  private static final Logger LOGGER = LoggerFactory.getLogger(Handler.class);
  private static final ChannelGroup channels = new DefaultChannelGroup(GlobalEventExecutor.INSTANCE);
  private static final Map<Channel, User> users = new HashMap<>();

  private static final String HELLO = "Connected. Enter a username by typing `/name <your name>`.\n";
  private static final String GOODBYE = "Disconnected.\n";
  private static final String SET_NAME_COMMAND = "/name ";
  private static final String HELP_COMMAND = "/help ";
  private static final String DISCONNECT_COMMAND = "/quit";

  @Override
  public void channelActive(ChannelHandlerContext context) {
    context.pipeline().get(SslHandler.class).handshakeFuture().addListener(
        future -> {
          context.writeAndFlush(HELLO);
         channels.add(context.channel());
        });
  }

  @Override
  public void exceptionCaught(ChannelHandlerContext context, Throwable cause) {
    LOGGER.error("Unhandled error from {}", idFromContext(context), cause);
    users.remove(context.channel());
    channels.remove(context.channel());
    context.close();
  }

  @Override
  protected void channelRead0(ChannelHandlerContext context, String msg) {
    if (DISCONNECT_COMMAND.equals(msg.toLowerCase())) {
      sayBye(context);
      return;
    }

    if (msg.startsWith(SET_NAME_COMMAND)) {
      String newName = msg.substring(SET_NAME_COMMAND.length()).trim();
      users.put(context.channel(), new User(newName));
      context.writeAndFlush("Name set to " + newName + "\n");
      return;
    }

    if (msg.startsWith(HELP_COMMAND)) {
      context.writeAndFlush("/name <NAME> to set your username\n/quit to disconnect\n/help prints this message\n");
      return;
    }

    if (!users.containsKey(context.channel())) {
      context.writeAndFlush("Pick a name before sending messages! Type /help for help.\n");
      return;
    }

    if (msg.trim().isBlank()) {
      return;
    }

    for (Channel c : channels) {
      if (c != context.channel()) {
        c.writeAndFlush(idFromContext(context) + ": " + msg + "\n");
      }
    }

  }

  private String idFromContext(ChannelHandlerContext context) {
    return users.getOrDefault(context.channel(),
                              new User(context.channel().remoteAddress().toString()))
        .name;
  }

  private void sayBye(ChannelHandlerContext context) {
    context.writeAndFlush(GOODBYE);
    context.close();
    channels.remove(context.channel());
    users.remove(context.channel());
  }
}
