package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import io.micronaut.context.annotation.Value;
import java.lang.reflect.Constructor;
import java.lang.reflect.Parameter;
import org.jdbi.v3.core.Jdbi;
import org.junit.jupiter.api.Test;

/**
 * {@link LatchIndexedPeriodDao} replaces the production {@code IndexedPeriodStore} bean, so it has
 * to take the same dialect signal the rest of the module takes — the {@code useH2} bean — rather
 * than re-deriving the dialect from {@code indexer.db.url} with an H2 default. That default is how
 * a context with {@code INDEXER_DB_URL} set and the Micronaut property unset would get a Postgres
 * {@code DataSource} and an H2 dialect on the latch alone.
 */
public class LatchIndexedPeriodDaoWiringTest {

  @Test
  public void latchIndexedPeriodDaoTakesTheModulesUseH2Bean() {
    Constructor<?>[] ctors = LatchIndexedPeriodDao.class.getDeclaredConstructors();
    assertThat(ctors).as("LatchIndexedPeriodDao should have exactly one constructor").hasSize(1);

    Constructor<?> ctor = ctors[0];
    assertThat(ctor.getParameterTypes())
        .as("expected (Jdbi, Boolean useH2), matching IndexerModule.indexedPeriodStore")
        .containsExactly(Jdbi.class, Boolean.class);

    Parameter useH2 = ctor.getParameters()[1];
    // Micronaut's generated bean def matches the Boolean by the source parameter name
    // (useH2). Reflection only sees that name when javac ran with -parameters; when it does,
    // pin the spelling so a rename cannot silently desync from IndexerModule.
    if (useH2.isNamePresent()) {
      assertThat(useH2.getName()).isEqualTo("useH2");
    }
    assertThat(useH2.getAnnotation(Value.class))
        .as(
            "a @Value default that names H2 is the latent dialect mismatch: the DataSource would"
                + " follow INDEXER_DB_URL while the latch stayed on H2")
        .isNull();
  }
}
