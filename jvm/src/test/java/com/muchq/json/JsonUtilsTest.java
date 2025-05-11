package com.muchq.json;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.core.type.TypeReference;
import com.muchq.json.utils.GuavaWidget;
import com.muchq.json.utils.Java8Widget;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.junit.Test;

public class JsonUtilsTest {

  @Test
  public void itCanReadAndWriteJsonAsTypeReference() {
    var widgets = new ArrayList<Java8Widget>();
    widgets.add(new Java8Widget(1));
    widgets.add(new Java8Widget(2));

    var widgetStrings = JsonUtils.writeAsString(widgets);
    var read = JsonUtils.readAs(widgetStrings, new TypeReference<List<Java8Widget>>() {});

    assertThat(read).isEqualTo(widgets);
  }

  @Test
  public void itCanReadAndWriteClassAsBytes() {
    var widget = new Java8Widget(1);
    var bytes = JsonUtils.writeAsBytes(widget);
    var read = JsonUtils.readAs(bytes, Java8Widget.class);
    assertThat(widget).isEqualTo(read);
  }

  @Test
  public void itCanReadAndWriteTypeReferenceAsBytes() {
    var widgets = new HashSet<Java8Widget>();
    widgets.add(new Java8Widget(1));
    var bytes = JsonUtils.writeAsBytes(widgets);
    var read = JsonUtils.readAs(bytes, new TypeReference<Set<Java8Widget>>() {});
    assertThat(widgets).isEqualTo(read);
  }

  @Test
  public void itCanReadAndWriteGuavaObjects() {
    var w = new GuavaWidget(123);
    var json = JsonUtils.writeAsString(w);
    assertThat(JsonUtils.readAs(json, GuavaWidget.class)).isEqualTo(w);
  }

  @Test
  public void itCanReadAndWriteJava8Objects() {
    var w = new Java8Widget(123);
    var json = JsonUtils.writeAsString(w);
    assertThat(JsonUtils.readAs(json, Java8Widget.class)).isEqualTo(w);
  }
}
