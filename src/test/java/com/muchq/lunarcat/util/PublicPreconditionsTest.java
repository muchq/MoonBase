package com.muchq.lunarcat.util;

import javax.ws.rs.BadRequestException;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;

public class PublicPreconditionsTest {

  @Rule
  public ExpectedException expectedException = ExpectedException.none();

  @Test
  public void itThrowsBadRequestOnFalsePredicate() {
    expectedException.expect(BadRequestException.class);
    PublicPreconditions.checkArgument(false, "should throw");
  }

  @Test
  public void itThrowsBadRequestOnNullArgument() {
    expectedException.expect(BadRequestException.class);
    PublicPreconditions.checkNotNull(null, "should throw");
  }
}
