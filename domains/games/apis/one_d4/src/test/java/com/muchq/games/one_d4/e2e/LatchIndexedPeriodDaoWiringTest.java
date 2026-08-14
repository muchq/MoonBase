package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.db.SqlDialect;
import io.micronaut.context.annotation.Value;
import java.lang.reflect.Constructor;
import java.lang.reflect.Parameter;
import org.jdbi.v3.core.Jdbi;
import org.junit.jupiter.api.Test;

/**
 * {@link LatchIndexedPeriodDao} replaces the production {@code IndexedPeriodStore} bean, so it has
 * to take the same {@link SqlDialect} the module wires — not re-derive a dialect from {@code
 * indexer.db.url} with an H2 default.
 */
public class LatchIndexedPeriodDaoWiringTest {

  @Test
  public void latchIndexedPeriodDaoTakesTheModulesSqlDialect() {
    Constructor<?>[] ctors = LatchIndexedPeriodDao.class.getDeclaredConstructors();
    assertThat(ctors).as("LatchIndexedPeriodDao should have exactly one constructor").hasSize(1);

    Constructor<?> ctor = ctors[0];
    assertThat(ctor.getParameterTypes())
        .as("expected (Jdbi, SqlDialect), matching IndexerModule.indexedPeriodStore")
        .containsExactly(Jdbi.class, SqlDialect.class);

    Parameter dialect = ctor.getParameters()[1];
    if (dialect.isNamePresent()) {
      assertThat(dialect.getName()).isEqualTo("dialect");
    }
    assertThat(dialect.getAnnotation(Value.class))
        .as("a @Value default that named H2 is how the latch drifted from the DataSource dialect")
        .isNull();
  }
}
