package net.eggsample;

import io.micronaut.http.MediaType;
import io.micronaut.http.annotation.Controller;
import io.micronaut.http.annotation.Delete;
import io.micronaut.http.annotation.Get;
import io.micronaut.http.annotation.Post;
import io.micronaut.http.annotation.Produces;
import jakarta.inject.Inject;
import net.eggsample.UserService.Cohort;

import javax.ws.rs.PathParam;
import java.util.List;

@Controller("/users")
@Produces(MediaType.APPLICATION_JSON)
public class UserController {
  private final UserService userService;

  @Inject
  public UserController(UserService userService) {
    this.userService = userService;
  }

  @Post("/{name}")
  public void addUser(@PathParam("name") String name) {
    userService.addUser(name);
  }

  @Delete("/{name}")
  public void removeUser(@PathParam("name") String name) {
    userService.removeUser(name);
  }

  @Get("/cohorts")
  public List<Cohort> getCohorts() {
    return userService.computeCohorts();
  }
}

