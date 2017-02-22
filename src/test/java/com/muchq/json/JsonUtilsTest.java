package com.muchq.json;

import com.muchq.json.utils.GuavaWidget;
import com.muchq.json.utils.Java8Widget;
import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThat;

public class JsonUtilsTest {

  @Test
  public void itCanReadAndWriteGuavaObjects() {
    GuavaWidget w = new GuavaWidget(123);
    String json = JsonUtils.writeAsString(w);
    assertThat(JsonUtils.readAs(json, GuavaWidget.class)).isEqualTo(w);
  }

  @Test
  public void itCanReadAndWriteJava8Objects() {
    Java8Widget w = new Java8Widget(123);
    String json = JsonUtils.writeAsString(w);
    assertThat(JsonUtils.readAs(json, Java8Widget.class)).isEqualTo(w);
  }
}
