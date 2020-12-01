package com.andyaylward.yochat;

import io.netty.channel.Channel;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.SimpleChannelInboundHandler;
import io.netty.channel.group.ChannelGroup;
import io.netty.channel.group.DefaultChannelGroup;
import io.netty.util.concurrent.GlobalEventExecutor;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

public class ChatHandler extends SimpleChannelInboundHandler<String> {
  private static final Logger LOGGER = LoggerFactory.getLogger(ChatHandler.class);
  private static final ChannelGroup channels = new DefaultChannelGroup(GlobalEventExecutor.INSTANCE);
  private static final Map<Channel, User> users = new HashMap<>();
  private static final Set<String> usernames = new HashSet<>();

  private static final String HELLO = "Connected. Enter a username by typing `/name <your name>`.\n";
  private static final String GOODBYE = "Disconnected.\n";
  private static final String SET_NAME_COMMAND = "/name ";
  private static final String HELP_COMMAND = "/help ";
  private static final String DISCONNECT_COMMAND = "/quit";

  @Override
  public void channelActive(ChannelHandlerContext context) {
    context.writeAndFlush(HELLO);
    channels.add(context.channel());
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
      for (Channel c : channels) {
        if (c != context.channel()) {
          c.writeAndFlush(idFromContext(context) + " left chat.\n");
        }
      }
      sayBye(context);
      return;
    }

    if (msg.startsWith(SET_NAME_COMMAND)) {
      String newName = msg.substring(SET_NAME_COMMAND.length()).trim();
      if (usernames.contains(newName)) {
        context.writeAndFlush("Sorry that username is taken.\n");
        return;
      }

      users.put(context.channel(), new User(newName));
      usernames.add(newName);
      context.writeAndFlush("Name set to " + newName + "\n");
      for (Channel c : channels) {
        if (c != context.channel()) {
          c.writeAndFlush(idFromContext(context) + " joined chat.\n");
        }
      }

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
    channels.remove(context.channel());
    User user = users.remove(context.channel());
    if (user != null) {
      usernames.remove(user.name);
    }
    context.close();
  }
}
