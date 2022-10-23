#usr/bin/env bash

killall java
ggpull --rebase
mvn clean package
nohup java -jar target/YoChat-1.0-SNAPSHOT.jar &

