# tracer school

Going through [computer graphics from scratch](https://gabrielgambetta.com/computer-graphics-from-scratch/)

## Build
```
bazel build //src/main/scala/com/muchq/scraphics:Tracer
```

## Run
requires scala 3.x

```
scala src/main/scala/com/muchq/scraphics/Tracer.scala
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
