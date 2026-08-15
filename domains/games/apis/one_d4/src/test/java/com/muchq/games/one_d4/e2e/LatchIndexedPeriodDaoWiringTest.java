package com.muchq.games.one_d4.e2e;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.db.PostgresSqlDialect;
import com.muchq.games.one_d4.db.SqlDialect;
import io.micronaut.context.ApplicationContext;
import java.util.Map;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The latch replaces the production {@code IndexedPeriodStore}, so it has to hold the same {@link
 * SqlDialect} instance the module wired — not a dialect re-derived from a {@code @Value} default.
 * Booting the context is what makes that property observable.
 */
public class LatchIndexedPeriodDaoWiringTest {

  private ApplicationContext ctx;

  @BeforeEach
  public void setUp() {
    ctx =
        ApplicationContext.builder()
            .properties(
                Map.of(
                    "indexer.db.url",
                    "jdbc:h2:mem:latch_wiring_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                    "micronaut.server.port",
                    "-1"))
            .build()
            .start();
  }

  @AfterEach
  public void tearDown() {
    if (ctx != null) {
      ctx.stop();
    }
  }

  @Test
  public void latchSharesTheModulesSqlDialect() {
    SqlDialect dialect = ctx.getBean(SqlDialect.class);
    LatchIndexedPeriodDao latch = ctx.getBean(LatchIndexedPeriodDao.class);

    // Named by class rather than importing H2SqlDialect: that type lives on :test_db, and this
    // suite already has H2 on the classpath without pulling the whole test_db library in.
    assertThat(dialect.getClass().getSimpleName())
        .as("module-boot against an H2 URL must select the H2 dialect via TestSqlDialectFactory")
        .isEqualTo("H2SqlDialect");
    assertThat(dialect)
        .as("control: the production Postgres dialect must not win on an H2 URL")
        .isNotInstanceOf(PostgresSqlDialect.class);
    assertThat(latch.dialect())
        .as("the latch must hold the same SqlDialect bean the rest of the module got")
        .isSameAs(dialect);
  }
}
