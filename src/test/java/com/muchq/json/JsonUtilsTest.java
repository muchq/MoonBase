package com.muchq.json;

import com.fasterxml.jackson.core.type.TypeReference;
import com.muchq.json.utils.GuavaWidget;
import com.muchq.json.utils.Java8Widget;
import org.junit.Test;

import java.util.ArrayList;
import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;

public class JsonUtilsTest {

  @Test
  public void itCanReadAndWriteJsonAsTypeReference() {
    List<Java8Widget> widgets = new ArrayList<>();
    widgets.add(new Java8Widget(1));
    widgets.add(new Java8Widget(2));

    String widgetStrings = JsonUtils.writeAsString(widgets);
    List<Java8Widget> read = JsonUtils.readAs(widgetStrings, new TypeReference<List<Java8Widget>>() {});

    assertThat(read).isEqualTo(widgets);
  }

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
