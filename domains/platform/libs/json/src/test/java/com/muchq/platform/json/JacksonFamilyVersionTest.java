package com.muchq.platform.json;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.core.JsonParser;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.List;
import org.junit.jupiter.api.Test;

/**
 * jackson-core, jackson-databind and jackson-annotations have to sit on one minor version.
 *
 * <p>They are three artifacts pinned by two different BOMs — micronaut-core-bom manages annotations
 * and jackson-bom manages core and databind — so a bump to either one can move part of the family
 * and leave the rest behind. Nothing fails at that moment, which is the problem: databind reads
 * annotation classes reflectively, so a split surfaces later as a {@code NoSuchMethodError} on
 * whichever annotation attribute databind reaches for next, in whatever code path happens to touch
 * it first. That is a runtime failure in production code, discovered by a user.
 *
 * <p>It has already happened once here. Micronaut 5 stopped managing Jackson 2 core and databind,
 * so they fell through to jackson-bom and moved to 2.22.0 while annotations stayed pinned at 2.21.
 * Nothing in the build or the suite noticed; it was found by reading the lock file.
 *
 * <p>Read off the loaded classes rather than out of {@code maven_install.json}. The lock file says
 * what was resolved, which is a fact about the build; this says what a JVM actually has on its
 * classpath, which is the thing that breaks.
 */
public class JacksonFamilyVersionTest {

  @Test
  public void theJacksonFamilyIsOnOneMinorVersion() {
    String core = minorOf(JsonParser.class);
    String databind = minorOf(ObjectMapper.class);
    String annotations = minorOf(JsonProperty.class);

    assertThat(List.of(core, databind, annotations))
        .as(
            "jackson core=%s databind=%s annotations=%s. databind introspects annotation classes,"
                + " so a family split across minors fails at runtime rather than here. Pin the"
                + " straggler explicitly in bazel/java.MODULE.bazel — note that jackson-annotations"
                + " dropped its patch component in 2.20, so its coordinate is e.g. \"2.22\".",
            core, databind, annotations)
        .containsOnly(databind);
  }

  /**
   * The {@code major.minor} of the jar a class was loaded from, via its manifest.
   *
   * <p>Fails rather than returns null when the manifest carries no version: a null would make the
   * comparison above pass by comparing nothing, which is exactly the shape of test this file exists
   * to argue against.
   */
  private static String minorOf(Class<?> fromArtifact) {
    Package pkg = fromArtifact.getPackage();
    String version = pkg == null ? null : pkg.getImplementationVersion();

    assertThat(version)
        .as(
            "%s reports no Implementation-Version, so this test can compare nothing. Its jar was"
                + " repackaged or its manifest stripped; find another version signal before"
                + " trusting a green run here.",
            fromArtifact.getName())
        .isNotNull();

    String[] parts = version.split("\\.");
    assertThat(parts.length)
        .as("%s reports version %q, which has no minor component", fromArtifact.getName(), version)
        .isGreaterThanOrEqualTo(2);
    return parts[0] + "." + parts[1];
  }
}
