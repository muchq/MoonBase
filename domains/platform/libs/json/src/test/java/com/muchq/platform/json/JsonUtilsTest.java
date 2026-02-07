package com.muchq.platform.json;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.core.type.TypeReference;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.junit.Test;

public class JsonUtilsTest {

  @Test
  public void itCanReadAndWriteJsonAsTypeReference() {
    List<Java8Widget> widgets = new ArrayList<>();
    widgets.add(new Java8Widget(1));
    widgets.add(new Java8Widget(2));

    String widgetStrings = JsonUtils.writeAsString(widgets);
    List<Java8Widget> read =
        JsonUtils.readAs(widgetStrings, new TypeReference<List<Java8Widget>>() {});

    assertThat(read).isEqualTo(widgets);
  }

  @Test
  public void itCanReadAndWriteClassAsBytes() {
    Java8Widget widget = new Java8Widget(1);
    byte[] bytes = JsonUtils.writeAsBytes(widget);
    Java8Widget read = JsonUtils.readAs(bytes, Java8Widget.class);
    assertThat(widget).isEqualTo(read);
  }

  @Test
  public void itCanReadAndWriteTypeReferenceAsBytes() {
    Set<Java8Widget> widgets = new HashSet<>();
    widgets.add(new Java8Widget(1));
    byte[] bytes = JsonUtils.writeAsBytes(widgets);
    Set<Java8Widget> read = JsonUtils.readAs(bytes, new TypeReference<Set<Java8Widget>>() {});
    assertThat(widgets).isEqualTo(read);
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
