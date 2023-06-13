# image stuff

```shell
bazel build //go/images
bazel-bin/go/images/images_/images static_content/tippo.png 15
open static_content/tippo.png static_content/tippo.png.grey.Box.png static_content/tippo.png.grey.X.png static_content/tippo.png.grey.Y.png
```

![Box X](../../../../static_content/tippo.png.png)
![Box X](../../../../static_content/tippo.png.grey.X.png)
![Box Y](../../../../static_content/tippo.png.grey.Y.png)
![Box](../../../../static_content/tippo.png.grey.Box.png)
