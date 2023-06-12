#include <iostream>
#include <string>

#include "cpp/example_cc_go/example_cc_go.h"
#include "golang/example_lib/example_lib.h"

std::string example_cc_go::MakeGreeting(const std::string& name) {
    std::string greeting(Greet(const_cast<char*>(name.c_str())));
    return greeting;
}
