# tracer school

Going through [computer graphics from scratch](https://gabrielgambetta.com/computer-graphics-from-scratch/)

## Build
```
bazel build jvm/src/main/scala/com/muchq/scraphics:ray_tracer_deploy.jar
```

## Run
### bazel
```shell
bazel run //jvm/src/main/scala/com/muchq/scraphics:ray_tracer
open bazel-bin/jvm/src/main/scala/com/muchq/scraphics/ray_tracer.runfiles/moon_base/tracer_output.png
```

### java
```shell
java -jar bazel-bin/jvm/src/main/scala/com/muchq/scraphics/ray_tracer_deploy.jar
open tracer_output.png
```

## output

### no light
![image](output/first_rays.png)


### with light sources
![image](output/ray_with_light.png)


### with light and reflections
![image](output/shiny.png)

### with shadows
![image](output/shadows.png)

### with reflections
![image](output/reflections.png)
