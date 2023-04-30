package net.eggsample;

import jakarta.inject.Singleton;

import java.time.Clock;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Queue;
import java.util.Set;
import java.util.concurrent.ConcurrentLinkedQueue;

@Singleton
public class UserService {
  private final Clock clock = Clock.systemUTC();
  private final Queue<ChangeEvent> changeEvents = new ConcurrentLinkedQueue<>();

  public void addUser(String user) {
    changeEvents.add(new ChangeEvent(user, clock.millis(), Type.START));
  }

  public void removeUser(String user) {
    changeEvents.add(new ChangeEvent(user, clock.millis(), Type.END));
  }

  public List<Cohort> computeCohorts() {
    List<Cohort> cohorts = new ArrayList<>();
    Set<String> users = new HashSet<>();
    long currentTime = 0;
    for (var event : changeEvents) {
      if (event.time > currentTime) {
        if (!users.isEmpty()) {
          cohorts.add(new Cohort(Set.copyOf(users), currentTime, event.time));
        }
        currentTime = event.time;
        if (event.type == Type.START) {
          users.add(event.name);
        } else if (event.type == Type.END) {
          users.remove(event.name);
        }
      }
    }

    return cohorts;
  }

  public enum Type { START, END}
  public record Cohort(Set<String> users, long start, long end){}
  public record ChangeEvent(String name, long time, Type type){}
}
