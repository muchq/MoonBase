package com.muchq.yochat;

import io.netty.channel.Channel;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.SimpleChannelInboundHandler;
import io.netty.channel.group.ChannelGroup;
import io.netty.channel.group.ChannelMatchers;
import io.netty.channel.group.DefaultChannelGroup;
import io.netty.util.concurrent.GlobalEventExecutor;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentSkipListSet;

public class ChatHandler extends SimpleChannelInboundHandler<String> {
  private static final Logger LOGGER = LoggerFactory.getLogger(ChatHandler.class);

  private final ChannelGroup channels = new DefaultChannelGroup(GlobalEventExecutor.INSTANCE);
  private final Map<Channel, String> users = new ConcurrentHashMap<>();
  private final Set<String> usernames = new ConcurrentSkipListSet<>();

  private static final String HELLO = "Connected. Enter a username by typing `/name <your name>`.\n";
  private static final String GOODBYE = "Disconnected.\n";
  private static final String SET_NAME_COMMAND = "/name ";
  private static final String HELP_COMMAND = "/help";
  private static final String LIST_USERS_COMMAND = "/who";
  private static final String LURKERS_COMMAND = "/lurkers";
  private static final String KICK_LURKERS_COMMAND = "/kick-lurkers";
  private static final String DISCONNECT_COMMAND = "/quit";

  @Override
  public void channelActive(ChannelHandlerContext context) {
    LOGGER.info("new connection for {}", context.channel().remoteAddress());
    context.writeAndFlush(HELLO);
    channels.add(context.channel());
  }

  @Override
  public void channelInactive(ChannelHandlerContext context) {
    LOGGER.info("connection terminated for {}", context.channel().remoteAddress());
    users.remove(context.channel());
    context.close();
  }

  @Override
  public void exceptionCaught(ChannelHandlerContext context, Throwable cause) {
    LOGGER.error("Unhandled error from {}", idFromContext(context), cause);
    channelInactive(context);
  }

  @Override
  protected void channelRead0(ChannelHandlerContext context, String rawMessage) {
    String msg = rawMessage.trim();
    if (msg.isBlank()) {
      return;
    }

    LOGGER.info("{} ({}) said {}", context, users.get(context.channel()), msg);

    if (DISCONNECT_COMMAND.equalsIgnoreCase(msg)) {
      LOGGER.info("{} ({}) disconnected", context, users.get(context.channel()));
      blast(context, idFromContext(context) + " left chat.");
      sayBye(context);
      return;
    }

    if (LURKERS_COMMAND.equalsIgnoreCase(msg)) {
      LOGGER.info("{} ({}) asked for lurkers", context, users.get(context.channel()));
      context.writeAndFlush("there are " + (channels.size() - users.size()) + " nameless lurkers.\n");
      return;
    }

    if (KICK_LURKERS_COMMAND.equalsIgnoreCase(msg)) {
      LOGGER.info("{} ({}) kicked lurkers", context, users.get(context.channel()));
      for (Channel channel : channels) {
        if (!users.containsKey(channel)) {
          channel.close();
        }
      }
      return;
    }

    if (msg.startsWith(SET_NAME_COMMAND)) {
      LOGGER.info("{} ({}) set name", context, users.get(context.channel()));
      String newName = msg.substring(SET_NAME_COMMAND.length()).trim();
      registerName(context, newName);
      blast(context, idFromContext(context) + " joined chat.");
      return;
    }

    if (HELP_COMMAND.equalsIgnoreCase(msg)) {
      LOGGER.info("{} ({}) asked for help", context, users.get(context.channel()));
      context.writeAndFlush("/name <NAME> to set your username\n/quit to disconnect\n/help prints this message\n");
      return;
    }

    if (LIST_USERS_COMMAND.equalsIgnoreCase(msg)) {
      LOGGER.info("{} ({}) asked for users", context, users.get(context.channel()));
      String users = String.join("\n", usernames);
      context.writeAndFlush("current users:\n" + users + "\n");
      return;
    }

    if (!users.containsKey(context.channel())) {
      context.writeAndFlush("Pick a name before sending messages! Type /help for help.\n");
      return;
    }

    blast(context, idFromContext(context) + ": " + msg);
  }

  private String idFromContext(ChannelHandlerContext context) {
    return users.getOrDefault(context.channel(), context.channel().remoteAddress().toString());
  }

  // FIXME: racy
  private void registerName(ChannelHandlerContext context, String name) {
    if (name == null || name.isBlank()) {
      context.writeAndFlush("Sorry that username is invalid.\n");
      return;
    }
    if (usernames.contains(name)) {
      context.writeAndFlush("Sorry that username is taken.\n");
      return;
    }

    if (users.containsKey(context.channel())) {
      blast(context, users.get(context.channel()) + " is now known as " + name);
    }

    users.put(context.channel(), name);
    usernames.add(name);
    context.writeAndFlush("Name set to " + name + "\n");
  }

  private void sayBye(ChannelHandlerContext context) {
    context.writeAndFlush(GOODBYE);
    String user = users.remove(context.channel());
    if (user != null) {
      usernames.remove(user);
    }
    context.close();
  }

  private void blast(ChannelHandlerContext context, String message) {
    channels.writeAndFlush(message + "\n", ChannelMatchers.isNot(context.channel()));
  }
}

